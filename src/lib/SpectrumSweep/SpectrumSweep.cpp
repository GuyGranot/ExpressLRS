#include "SpectrumSweep.h"

#if defined(TX_SPECTRUM_SCAN)

#include "common.h"
#include "logging.h"
#include "crsf_protocol.h"
#include "FHSS.h"
// NB: no #include "hardware.h" -- it has no include guard and arrives via
// common.h -> targets.h. Including it directly is a redefinition error.

#include <string.h>

// Checked here: SpectrumProtocol.h has no CRSF includes to check against.
static_assert(sizeof(crsf_ext_header_t) + SPECTRUM_MAX_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE
                  <= CRSF_MAX_PACKET_LEN,
              "spectrum frame exceeds CRSF_MAX_PACKET_LEN");
static_assert(SPECTRUM_MAX_PAYLOAD_BYTES <= CRSF_PAYLOAD_SIZE_MAX,
              "spectrum payload exceeds CRSF_PAYLOAD_SIZE_MAX");

// Budget blocking time, not bin count: settleUs spans 80us (FLRC) to 1000us
// (LR1121). Blocking is safe here -- hwTimer is stopped.
#define CHUNK_BUDGET_US 1300
#define MAX_POSITIONS_PER_CALL 16

// One trace pair per radio. Only index 0 is used unless comparing, but the
// arrays cannot be conditional: isDualRadio() reads a pin from the hardware
// layout, which a Unified build only knows at runtime.
static int8_t liveBins[2][SPECTRUM_MAX_BINS];
static int8_t maxHoldBins[2][SPECTRUM_MAX_BINS];

// Driver bandwidth constant and its value in kHz, wide first. Wide is what
// every air rate in the band uses, so a default sweep senses through the same
// filter the link does.
typedef struct
{
    uint8_t code;
    uint16_t khz;
} spectrumRbwOption_t;

#if defined(RADIO_SX127X)
static const spectrumRbwOption_t rbwOptions[rbwCount] = {
    {SX127x_BW_500_00_KHZ, 500}, {SX127x_BW_250_00_KHZ, 250}, {SX127x_BW_125_00_KHZ, 125}};
#elif defined(RADIO_LR1121)
static const spectrumRbwOption_t rbwOptionsSubGHz[rbwCount] = {
    {LR11XX_RADIO_LORA_BW_500, 500}, {LR11XX_RADIO_LORA_BW_250, 250}, {LR11XX_RADIO_LORA_BW_125, 125}};
// 200/400/800 are the 2G4-only codes; the sub-GHz set above is not interchangeable.
static const spectrumRbwOption_t rbwOptions2G4[rbwCount] = {
    {LR11XX_RADIO_LORA_BW_800, 812}, {LR11XX_RADIO_LORA_BW_400, 406}, {LR11XX_RADIO_LORA_BW_200, 203}};
#else // RADIO_SX128X
static const spectrumRbwOption_t rbwOptions[rbwCount] = {
    {SX1280_LORA_BW_0800, 812}, {SX1280_LORA_BW_0400, 406}, {SX1280_LORA_BW_0200, 203}};
#endif

static uint8_t rbwSel = rbwWide;
static bool compareRequested;

// Scan-invariant, all latched once by SpectrumSweepBegin().
static const fhss_config_t *scanCfg;
static uint32_t scanSpread;
static uint32_t settleUs;
static int lnaGainDb;
static bool sweepCompare;  // latched at Begin: both radios on every bin
static uint8_t positionsPerCall;
static SX12XX_Radio_Number_t activeRadio; // band-matched radio

static uint8_t sweepCursor; // next dwell position to sample
static uint8_t sweepSeq;    // free-running, increments per completed sweep

static int8_t streamSource = 0; // -1 emits every source in turn
static uint8_t emitSource;      // source the emit cursor is working through

// Doubles as the encoder's argument block: no second copy of totalBins/axis.
static spectrumFrameInfo_t emitInfo;

// The SX127x driver names this GetCurrRSSI, the others GetRssiInst.
static int8_t ReadRssiInst(const SX12XX_Radio_Number_t radio)
{
#if defined(RADIO_SX127X)
    return Radio.GetCurrRSSI(radio);
#else
    return Radio.GetRssiInst(radio);
#endif
}

/*
 * RX-entry settling. Read too early and RSSI is garbage silently -- bins come
 * back low with nothing to say so. Indexed by selection rather than by kHz
 * because each step halves the bandwidth, and settling scales with 1/bandwidth.
 *
 * Only the LR1121 wide figure is measured: 1000 us is Semtech's own
 * DELAY_BETWEEN_SET_RX_AND_VALID_RSSI_MS, and this sweep re-enters RX on every
 * bin, so each read also covers a PLL relock and a full AGC re-acquisition.
 * Shortening it while the per-bin reset stays brings back the 915 whole-band
 * floor lift. The rest are deliberately generous pending a CW-rig measurement.
 */
static uint32_t RssiSettleUs(const uint8_t rbw)
{
#if defined(RADIO_SX127X)
    static const uint32_t settleUsByRbw[rbwCount] = {500, 1000, 2000}; // unmeasured
#elif defined(RADIO_LR1121)
    static const uint32_t settleUsByRbw[rbwCount] = {1000, 2000, 4000};
#else // RADIO_SX128X
    // Replaces a table keyed on spreading factor, taken from LBT.cpp: those
    // figures cover a mode switch, not the per-bin retune this does.
    static const uint32_t settleUsByRbw[rbwCount] = {500, 1000, 2000};
#endif
    return settleUsByRbw[rbw];
}

// The bin grid is the FHSS channel grid. FreqCorrection is deliberately not
// applied: the true grid, not the link's tracking offset.
static uint32_t BinToRadioFreq(const uint8_t bin)
{
    return scanCfg->freq_start + ((uint32_t)scanSpread * bin / FREQ_SPREAD_SCALE);
}

static void ComputeAxis()
{
#if defined(RADIO_LR1121)
    // FREQ_HZ_TO_REG_VAL is the identity on LR1121: these are already Hz.
    emitInfo.startFreqKhz = scanCfg->freq_start / 1000;
    emitInfo.stepKhz = (uint16_t)((scanSpread / FREQ_SPREAD_SCALE) / 1000);
#else
    emitInfo.startFreqKhz = (uint32_t)(((double)scanCfg->freq_start * FREQ_STEP / 1000.0) + 0.5);
    emitInfo.stepKhz = (uint16_t)((((double)scanSpread / FREQ_SPREAD_SCALE) * FREQ_STEP / 1000.0) + 0.5);
#endif
}

bool SpectrumSweepHasSecondBand()
{
    return FHSSconfigDualBand != nullptr && FHSSconfigDualBand->freq_count != 0;
}

// freq_center, not freq_start: the FHSS tables wrap start/stop in
// FREQ_HZ_TO_REG_VAL but leave centre in Hz, and that macro is the identity
// only on LR1121. Comparing freq_start against a Hz threshold reports an
// SX128x as sub-GHz.
static bool ConfigIsSubGHz(const fhss_config_t *cfg)
{
    return cfg->freq_center < 1000000000UL;
}

bool SpectrumSweepPrimaryIsSubGHz()
{
    return ConfigIsSubGHz(FHSSconfig);
}

// Each cross-band chip is wired to its own band-matched front end and antenna
// port, so choosing the band is also choosing the port. The axis reports the
// true frequency either way.
static void SelectBand(const bool useSecondBand)
{
    if (useSecondBand && SpectrumSweepHasSecondBand())
    {
        scanCfg = FHSSconfigDualBand;
        scanSpread = freq_spread_DualBand;
        // Radio 2 owns the dual band only on a two-radio device; a single-radio
        // dual-band chip switches band via Config() on radio 1.
        activeRadio = isDualRadio() ? SX12XX_Radio_2 : SX12XX_Radio_1;
    }
    else
    {
        scanCfg = FHSSconfig;
        scanSpread = freq_spread;
        activeRadio = SX12XX_Radio_1;
    }
}

// Max-hold has to accumulate here: the sweep outruns the emit rate, so a
// consumer-side max-hold would miss the sweeps in between.
static void StoreBin(const uint8_t source, const uint8_t bin, const int8_t raw)
{
    // Antenna-referred; clamped to >= INVALID+1 to keep the sentinel unambiguous.
    const int8_t v = (int8_t)constrain((int16_t)raw - (int16_t)lnaGainDb,
                                       SPECTRUM_RSSI_INVALID + 1, INT8_MAX);
    liveBins[source][bin] = v;
    if (v > maxHoldBins[source][bin])
    {
        maxHoldBins[source][bin] = v;
    }
}

void SpectrumSweepChunk()
{
    for (uint8_t n = 0; n < positionsPerCall && sweepCursor < emitInfo.totalBins; n++, sweepCursor++)
    {
        const uint8_t bin = sweepCursor;
        // Comparing means both radios on the SAME bin in the same dwell, so the
        // two traces see the same air. Sweeping them one after the other would
        // compare different moments of a band that changes between them.
        const SX12XX_Radio_Number_t radios = sweepCompare ? SX12XX_Radio_All : activeRadio;

#if defined(RADIO_LR1121)
        // RX_CONT never re-acquires gain on a bare retune, so without this drop
        // to STDBY_RC one bin's gain step carries into every bin after it and
        // the trace reads that step's floor. Costs a PLL relock per bin, which
        // is what RssiSettleUs()'s 1 ms covers -- do not shorten one alone.
        Radio.SpectrumResetRx(radios);
#endif

        // One write covers the pair when comparing: both radios sit on the same
        // bin, so they need the identical frequency by definition, and a write
        // addressed to SX12XX_Radio_All reaches both chips at once.
        //
        // doRx is mandatory, for a different reason per family. On the LR1121
        // the frequency and RX entry are a single command (SET_FREQ_SET_RX)
        // addressed to exactly the radios named here -- so naming one leaves the
        // other configured but never listening. On the SX128x it calls RXnb(),
        // and the write only takes effect on RX entry at all; that path also
        // relies on the early-return in SX1280::SetMode staying commented out,
        // which otherwise makes every bin report bin 0's RSSI.
        Radio.SetFrequencyReg(BinToRadioFreq(bin), radios, true);

        delayMicroseconds(settleUs);

#if defined(RADIO_LR1121)
        Radio.StartRssiInst(radios);
#endif
        if (sweepCompare)
        {
            StoreBin(0, bin, ReadRssiInst(SX12XX_Radio_1));
            StoreBin(1, bin, ReadRssiInst(SX12XX_Radio_2));
        }
        else
        {
            StoreBin(0, bin, ReadRssiInst(activeRadio));
        }
    }

    if (sweepCursor >= emitInfo.totalBins)
    {
        sweepCursor = 0;
        sweepSeq++;
    }
}

bool SpectrumSweepAtSweepStart()
{
    return sweepCursor == 0;
}

void SpectrumSweepIsolateRadio()
{
    // The same pins each family's Hal::end() detaches, minus the SPI teardown --
    // the sweep still needs the bus. SX127x signals RXdone on DIO0, the others
    // on DIO1, so the pin is per family and not a shared constant.
#if defined(RADIO_SX127X)
    const int dio = GPIO_PIN_DIO0;
    const int dio2 = GPIO_PIN_DIO0_2;
#else
    const int dio = GPIO_PIN_DIO1;
    const int dio2 = GPIO_PIN_DIO1_2;
#endif
    detachInterrupt(dio);
    if (dio2 != UNDEF_PIN)
    {
        detachInterrupt(dio2);
    }
}

void SpectrumSweepSetRbw(const uint8_t rbw)
{
    rbwSel = (rbw < rbwCount) ? rbw : rbwWide;
}

void SpectrumSweepResetMaxHold()
{
    // INVALID is -128 == 0x80, so memset writes it exactly.
    memset(maxHoldBins, (uint8_t)SPECTRUM_RSSI_INVALID, sizeof(maxHoldBins));
}

void SpectrumSweepSetCompare(const bool on)
{
    compareRequested = on;
}

// Traces available to stream: 2 while comparing, otherwise 1.
static uint8_t SourceCount()
{
    return sweepCompare ? 2 : 1;
}

void SpectrumSweepStreamSource(const int8_t source)
{
    streamSource = source;
    emitSource = (source > 0) ? (uint8_t)source : 0;
}

uint8_t SpectrumSweepEncodeFrame(uint8_t *payload)
{
    // Latch so both frames of a trace agree. The sweep runs underneath, so a
    // trace can still tear -- harmless, max-hold is monotonic.
    if (emitInfo.binOffset == 0)
    {
        emitInfo.sweepSeq = sweepSeq;
    }

    const uint8_t trace = emitInfo.flags & SPECTRUM_FLAG_TRACE_MASK;
    const int8_t *src = (trace == SPECTRUM_TRACE_MAXHOLD) ? maxHoldBins[emitSource]
                                                             : liveBins[emitSource];

    // A comparing sweep produces two traces on one axis, so the axis alone no
    // longer identifies them: the consumer needs RADIO_2 to tell the pair apart.
    // MODE_COMPARE is latched in Begin() -- it cannot change mid-sweep.
    emitInfo.flags = (emitInfo.flags & (uint8_t)~SPECTRUM_FLAG_RADIO_2) |
                     ((emitSource == 1) ? SPECTRUM_FLAG_RADIO_2 : 0);

    const uint8_t len = SpectrumEncodeFrame(payload, src, &emitInfo);
    if (len == 0)
    {
        emitInfo.binOffset = 0;
        return 0;
    }

    emitInfo.binOffset += emitInfo.binCount;
    if (emitInfo.binOffset >= emitInfo.totalBins)
    {
        emitInfo.binOffset = 0;
        const bool wasLive = (trace == SPECTRUM_TRACE_LIVE);
        emitInfo.flags = (emitInfo.flags & (uint8_t)~SPECTRUM_FLAG_TRACE_MASK) |
                         (wasLive ? SPECTRUM_TRACE_MAXHOLD : SPECTRUM_TRACE_LIVE);

        // Both traces of this source have gone out; move to the next one when
        // the caller asked for all of them.
        if (!wasLive && streamSource < 0)
        {
            emitSource = (uint8_t)((emitSource + 1) % SourceCount());
        }
    }
    return len;
}

void SpectrumSweepBegin(const bool useSecondBand)
{
    SelectBand(useSecondBand);

    // Latched for the whole sweep so a mid-sweep toggle cannot leave half a
    // trace measured on one radio and half on two.
    sweepCompare = compareRequested && isDualRadio();
    if (!sweepCompare)
    {
        streamSource = 0;
        emitSource = 0;
    }

    // Truncate rather than overrun liveBins[] if a future table grows.
    uint8_t bins = (uint8_t)scanCfg->freq_count;
    if (bins > SPECTRUM_MAX_BINS)
    {
        bins = SPECTRUM_MAX_BINS;
    }

    memset(&emitInfo, 0, sizeof(emitInfo));
    emitInfo.totalBins = bins;
    emitInfo.flags = SPECTRUM_TRACE_LIVE |
                     (sweepCompare ? SPECTRUM_FLAG_MODE_COMPARE : 0);
    ComputeAxis();

    // Configure for sensing rather than inheriting whatever the link left: the
    // bandwidth a trace was measured through is then this code's to state, and
    // the same every time. SF and CR satisfy the signature only -- RSSI is read
    // ahead of the despreader and nothing here demodulates.
#if defined(RADIO_LR1121)
    // Reconfiguring is also what makes a band flip work: crossing 900<->2.4
    // picks a different PA and RF switch, not just a frequency.
    //
    // NB: image calibration spans the primary band only, so a radio retuned to
    // the other band may read slightly hot or cold.
    const bool subGHz = ConfigIsSubGHz(scanCfg);
    const spectrumRbwOption_t rbw = subGHz ? rbwOptionsSubGHz[rbwSel] : rbwOptions2G4[rbwSel];
    // Comparing sweeps both radios, so both must be configured for sensing --
    // Config() takes a radio number here, and passing activeRadio would leave
    // the other one on whatever the link left it, reading a band it is not on.
    Radio.Config(rbw.code, LR11XX_RADIO_LORA_SF5, LR11XX_RADIO_LORA_CR_4_8,
                 scanCfg->freq_start, 8 /*preamble*/, false /*InvertIQ*/, 8 /*payload*/,
                 subGHz ? RadioBandMod::Combined::LORA_900 : RadioBandMod::Combined::LORA_2G4,
                 0, 0, sweepCompare ? SX12XX_Radio_All : activeRadio);
#elif defined(RADIO_SX128X)
    // LoRa packet type whatever the link was on: an FLRC session otherwise
    // senses through its own 0.6 MHz filter and a settle measured for a mode
    // switch. Both radios of a Gemini pair -- Config() takes no radio number.
    const spectrumRbwOption_t rbw = rbwOptions[rbwSel];
    Radio.Config(rbw.code, SX1280_LORA_SF5, SX1280_LORA_CR_4_8, scanCfg->freq_start,
                 8 /*preamble*/, false /*InvertIQ*/, 8 /*payload*/,
                 0 /*flrcSyncWord*/, 0 /*flrcCrcSeed*/, RadioBandMod::Combined::LORA_2G4);
#else // RADIO_SX127X
    const spectrumRbwOption_t rbw = rbwOptions[rbwSel];
    Radio.Config(rbw.code, SX127x_SF_6, SX127x_CR_4_8, scanCfg->freq_start,
                 8 /*preamble*/, false /*InvertIQ*/, 8 /*payload*/);
#endif

    emitInfo.rbwKhz = rbw.khz;
    settleUs = RssiSettleUs(rbwSel);
    lnaGainDb = hardware_int(HARDWARE_power_lna_gain);
    positionsPerCall = (uint8_t)constrain((uint32_t)(CHUNK_BUDGET_US / settleUs),
                                          1u, (uint32_t)MAX_POSITIONS_PER_CALL);

    memset(liveBins, (uint8_t)SPECTRUM_RSSI_INVALID, sizeof(liveBins));
    SpectrumSweepResetMaxHold();

    sweepCursor = 0;
    sweepSeq = 0;

    DBGLN("SpectrumSweep: band %u radio %u, %u bins, start %u kHz, step %u kHz, settle %u us, lna %d dB, %u pos/call",
          (unsigned)useSecondBand, (unsigned)activeRadio, emitInfo.totalBins, emitInfo.startFreqKhz,
          emitInfo.stepKhz, settleUs, lnaGainDb, positionsPerCall);
}

#endif
