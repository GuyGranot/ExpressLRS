#include "RxSpectrum.h"

#if defined(RX_SPECTRUM_SCAN)

#include "RxSpectrumProtocol.h"

#include "common.h"
#include "device.h"
#include "logging.h"
#include "CRSFRouter.h"
#include "crsf_protocol.h"
#include "RXOTAConnector.h"
#include "FHSS.h"
#include "POWERMGNT.h"
#include "hwTimer.h"
// NB: no #include "hardware.h" -- it has no include guard and arrives via
// common.h -> targets.h. Including it directly is a redefinition error.

#include <string.h>

/*
 * Receive-only spectrum sweep for an ELRS RECEIVER. Mirrors lib/TxSpectrum (read
 * its DESIGN.md -- every safety constraint here is reasoned through there), with
 * three RX-specific changes:
 *   - the trigger is a CRSF command over the FC/host UART (see RXEndpoint), not a
 *     handset Lua parameter;
 *   - frames are streamed out the serial (FC) connector, not to a handset;
 *   - the band/antenna-port is chosen by the trigger argument (there is no page
 *     button), and "both" dwells on each port in turn.
 *
 * HARDWARE SAFETY INVARIANT (DESIGN.md R3, mirrored for RX): this path is
 * receive-only and must never key the telemetry-uplink PA. The only radio calls
 * below are SetFrequencyReg(..., doRx) (-> RFAMP.RXenable()), the per-bin RSSI
 * reads, and the LR1121 STDBY_RC reset. The three PA openers -- TXnb(),
 * startCWTest(), SetMode(TX) -- are never on this path, and the telemetry TX is
 * additionally suppressed by keeping hwTimer stopped (R3.1a) and by detaching the
 * RXdone DIO1 ISR (I3) so nothing re-arms it.
 */

extern RXOTAConnector otaConnector; // the RF-side connector, used only as the
                                    // "source" to exclude when routing our frames
                                    // to the serial (FC/host) connector.

// Bins sampled per timeout() call. Bounds the BLOCKING TIME, not the bin count:
// see TxSpectrum DESIGN.md invariant S1. hwTimer is stopped and the inbound UART
// is idle during a scan, so ~1.3ms flat per call is safe.
#define RX_SPECTRUM_CHUNK_BUDGET_US 1300
#define RX_SPECTRUM_MAX_POSITIONS_PER_CALL 16

// One frame per interval, traces alternating (live / max-hold). 20 frames/s is
// ~1.2KB/s against a 420000-baud CRSF UART -- pure headroom -- but pacing keeps
// the 256-byte serial FIFO from bursting.
#define RX_SPECTRUM_EMIT_INTERVAL_MS 50

// In "both" mode, dwell on each band long enough to emit several complete
// live+max-hold traces (and let max-hold accumulate) before switching, so the
// host never sees a trace torn across bands.
#define RX_SPECTRUM_BAND_DWELL_MS 500

static int8_t liveBins[TX_SPECTRUM_MAX_BINS];
static int8_t maxHoldBins[TX_SPECTRUM_MAX_BINS];

// Scan-invariant, latched by BeginScan().
static const fhss_config_t *scanCfg;
static uint32_t scanSpread;
static uint32_t settleUs;
static int lnaGainDb;
static bool scanDual;      // split one band across two radios (same-band Gemini RX)
static uint8_t positions;  // radio-1 dwell positions per sweep
static uint8_t positionsPerCall;
static SX12XX_Radio_Number_t activeRadio; // band-matched radio, latched by BeginScan()

// Port/band selection. requestedBand is the RX_SPECTRUM_PORT_* the host asked
// for; wantSubGHz is the band currently being swept (flips per dwell in "both").
static uint8_t requestedBand;
static bool wantSubGHz;
static uint32_t bandDwellStartMs;

static uint8_t sweepCursor; // next dwell position to sample
static uint8_t sweepSeq;    // increments per completed sweep
static bool running;

static txSpectrumFrameInfo_t emitInfo;
static uint32_t lastEmitMs;

/*
 * Instantaneous RSSI of the given radio; SX127x names this GetCurrRSSI, the
 * others GetRssiInst. Kept local (mirrors TxSpectrum's shim).
 */
static int8_t ReadRssiInst(const SX12XX_Radio_Number_t radio)
{
#if defined(RADIO_SX127X)
    return Radio.GetCurrRSSI(radio);
#else
    return Radio.GetRssiInst(radio);
#endif
}

/*
 * RX-entry RSSI settling time. Do NOT replace the LR1121 branch with a generic
 * table: the sweep drops to STDBY_RC and re-enters RX on every bin (SweepChunk),
 * so the delay must cover a PLL relock plus a full AGC re-acquisition -- the flat
 * 1000us (Semtech SWSD003). See TxSpectrum DESIGN.md 2.3.3 / invariant M1.
 */
static uint32_t RssiSettleUs(const uint8_t SF, const uint8_t radio_type)
{
#if defined(RADIO_SX127X)
    (void)SF;
    (void)radio_type;
    return 240; // unmeasured
#elif defined(RADIO_LR1121)
    (void)SF;
    (void)radio_type;
    return 1000;
#else // RADIO_SX128X
    if (radio_type == RadioBandMod::Combined::LORA_2G4)
    {
        switch ((SX1280_RadioLoRaSpreadingFactors_t)SF)
        {
        case SX1280_LORA_SF5:
            return 100;
        case SX1280_LORA_SF6:
            return 141;
        case SX1280_LORA_SF7:
            return 218;
        default:
            return 480;
        }
    }
    if (radio_type == RadioBandMod::Combined::FLRC_2G4)
    {
        return 80;
    }
    return 480;
#endif
}

/*
 * Radio-domain frequency of a bin. The bin grid IS the FHSS channel grid.
 * FreqCorrection is deliberately not applied: we want the true grid.
 */
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

/*
 * True when this device can scan a second (cross-band) band: a dual-band FHSS
 * config is present. On a single-band RX this is false and only the primary band
 * is scannable, so the 900/2.4 selector collapses to "the configured band".
 */
static bool RxSpectrumHasSecondBand()
{
    return FHSSconfigDualBand != nullptr && FHSSconfigDualBand->freq_count != 0;
}

/*
 * Map the requested sub-GHz/2.4 port to the matching FHSS band config and the
 * band-matched radio. On a cross-band dual-radio LR1121 the primary band lives on
 * radio 1 and the dual band on radio 2; each is wired to its own antenna port, so
 * this selection IS the antenna-port selection. The axis (ComputeAxis) then
 * reports the true frequency, so 900 vs 2.4 is unambiguous regardless of which is
 * the link's "primary".
 */
static void SelectBand(const bool subGHz)
{
    const bool primaryIsSubGHz = (FHSSconfig->freq_start < 1000000000UL);
    if (RxSpectrumHasSecondBand() && (subGHz != primaryIsSubGHz))
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

/*
 * Max-hold is accumulated HERE and must stay here: the sweep runs faster than the
 * emit rate, so a host-side max-hold would miss the sweeps in between (the whole
 * point on bursty bands). See TxSpectrum DESIGN.md 2.5.
 */
static void StoreBin(const uint8_t bin, const int8_t raw)
{
    // Back out the front-end gain so the value is antenna-referred. Clamp to
    // >= INVALID+1 so the sentinel stays unambiguous and the max-hold is a plain
    // compare.
    const int8_t v = (int8_t)constrain((int16_t)raw - (int16_t)lnaGainDb,
                                       TX_SPECTRUM_RSSI_INVALID + 1, INT8_MAX);
    liveBins[bin] = v;
    if (v > maxHoldBins[bin])
    {
        maxHoldBins[bin] = v;
    }
}

/*
 * Sample up to positionsPerCall dwell positions. On a same-band dual radio the
 * band is split (radio 1 low half, radio 2 high half). On a cross-band device
 * scanDual is false, so only the band-matched radio is driven.
 */
static void SweepChunk()
{
    for (uint8_t n = 0; n < positionsPerCall && sweepCursor < positions; n++, sweepCursor++)
    {
        const uint8_t bin1 = sweepCursor;
        const uint8_t bin2 = (uint8_t)(sweepCursor + positions);
        const bool haveBin2 = scanDual && (bin2 < emitInfo.totalBins);

#if defined(RADIO_LR1121)
        // Drop to STDBY_RC before every retune so the RX entry below re-runs gain
        // acquisition. Load-bearing on LR1121: RX_CONT never re-acquires gain on a
        // bare retune, so a gain step carries into every later bin (the ~20dB 915
        // floor lift). Costs a PLL relock -- hence the 1000us settle (M1).
        Radio.SpectrumResetRx(haveBin2 ? SX12XX_Radio_All : activeRadio);
#endif

        if (haveBin2)
        {
            Radio.SetFrequencyReg(BinToRadioFreq(bin2), SX12XX_Radio_2, false);
        }

        // doRx=true is mandatory: on a command/state-machine radio a bare
        // frequency write only takes effect on the next RX entry. Works only
        // because SX1280/LR1121 SetMode's early-return guard is commented out;
        // re-adding it silently reports bin 0's RSSI across every bin.
        Radio.SetFrequencyReg(BinToRadioFreq(bin1), activeRadio, true);

        delayMicroseconds(settleUs);

#if defined(RADIO_LR1121)
        Radio.StartRssiInst(haveBin2 ? SX12XX_Radio_All : activeRadio);
#endif
        StoreBin(bin1, ReadRssiInst(activeRadio));
        if (haveBin2)
        {
            StoreBin(bin2, ReadRssiInst(SX12XX_Radio_2));
        }
    }

    if (sweepCursor >= positions)
    {
        sweepCursor = 0;
        sweepSeq++;
    }
}

static void EmitNextFrame()
{
    // Latch the sequence at the start of a trace so both frames of one trace agree.
    if (emitInfo.binOffset == 0)
    {
        emitInfo.sweepSeq = sweepSeq;
    }

    const uint8_t trace = emitInfo.flags & TX_SPECTRUM_FLAG_TRACE_MASK;
    const int8_t *src = (trace == TX_SPECTRUM_TRACE_MAXHOLD) ? maxHoldBins : liveBins;

    // Encode straight into the frame buffer past the CRSF header -- the encoder
    // does not touch payload on its error path.
    uint8_t buf[sizeof(crsf_ext_header_t) + TX_SPECTRUM_MAX_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE];
    const uint8_t len = TxSpectrumEncodeFrame(buf + sizeof(crsf_ext_header_t), src, &emitInfo);
    if (len == 0)
    {
        emitInfo.binOffset = 0;
        return;
    }

    // RX -> flight controller / host. deliverMessage(&otaConnector, ...) forwards
    // to every OTHER connector (i.e. the serial connector to the FC/host),
    // mirroring how the RX already emits link statistics (rx_main link-stats path).
    crsfRouter.SetExtendedHeaderAndCrc((crsf_ext_header_t *)buf,
                                       CRSF_FRAMETYPE_ELRS_VENDOR,
                                       CRSF_EXT_FRAME_SIZE(len),
                                       CRSF_ADDRESS_FLIGHT_CONTROLLER,
                                       CRSF_ADDRESS_CRSF_RECEIVER);
    crsfRouter.deliverMessage(&otaConnector, (crsf_header_t *)buf);

    emitInfo.binOffset += emitInfo.binCount;
    if (emitInfo.binOffset >= emitInfo.totalBins)
    {
        emitInfo.binOffset = 0;
        emitInfo.flags = (trace == TX_SPECTRUM_TRACE_LIVE) ? TX_SPECTRUM_TRACE_MAXHOLD
                                                           : TX_SPECTRUM_TRACE_LIVE;
    }
}

static void BeginScan()
{
    SelectBand(wantSubGHz);

    uint8_t bins = (uint8_t)scanCfg->freq_count;
    if (bins > TX_SPECTRUM_MAX_BINS)
    {
        bins = TX_SPECTRUM_MAX_BINS;
    }

    memset(&emitInfo, 0, sizeof(emitInfo));
    emitInfo.totalBins = bins;
    emitInfo.flags = TX_SPECTRUM_TRACE_LIVE;
    ComputeAxis();

#if defined(RADIO_LR1121)
    // Reconfigure the band-matched radio for the selected band. Crossing 900<->2.4
    // on an LR1121 is not a bare frequency write -- Config() selects the sub-GHz vs
    // 2.4 PA and RF switch (SetPaConfig, inferred from freq < 1GHz) and leaves the
    // radio in STDBY_RC. Proven receive-only (no SET_TX-class opcode on this path;
    // hwTimer stays stopped). bw/sf/cr are band-agnostic, so reusing the current
    // air rate tracks a valid sensing config on either band -- only the frequency
    // (hence the PA/RF-switch path, hence the antenna port) is band-specific.
    const expresslrs_mod_settings_s *const mp = ExpressLRS_currAirRate_Modparams;
    Radio.Config(mp->bw, mp->sf, mp->cr, scanCfg->freq_start, mp->PreambleLen,
                 false /*InvertIQ: sensing, not a link*/, mp->PayloadLength,
                 mp->radio_type, 0, 0, activeRadio);
#endif

    settleUs = RssiSettleUs(ExpressLRS_currAirRate_Modparams->sf,
                            ExpressLRS_currAirRate_Modparams->radio_type);
    lnaGainDb = hardware_int(HARDWARE_power_lna_gain);
    // Same-band split (2x rate) only on a same-band dual radio. On a cross-band
    // device the two radios own different bands, so sweep the band-matched radio.
    scanDual = isDualRadio() && !RxSpectrumHasSecondBand();
    positions = scanDual ? (uint8_t)((bins + 1) / 2) : bins;
    positionsPerCall = (uint8_t)constrain((uint32_t)(RX_SPECTRUM_CHUNK_BUDGET_US / settleUs),
                                          1u, (uint32_t)RX_SPECTRUM_MAX_POSITIONS_PER_CALL);

    memset(liveBins, (uint8_t)TX_SPECTRUM_RSSI_INVALID, sizeof(liveBins));
    memset(maxHoldBins, (uint8_t)TX_SPECTRUM_RSSI_INVALID, sizeof(maxHoldBins));

    sweepCursor = 0;
    sweepSeq = 0;
    lastEmitMs = millis();
    bandDwellStartMs = millis();

    DBGLN("RxSpectrum: subGHz %u radio %u, %u bins, start %u kHz, step %u kHz, settle %u us, lna %d dB, %u pos/call",
          wantSubGHz, (unsigned)activeRadio, emitInfo.totalBins, emitInfo.startFreqKhz,
          emitInfo.stepKhz, settleUs, lnaGainDb, positionsPerCall);
}

/*
 * Isolate the radio from the live OTA RXdone interrupt for the whole scan. On the
 * RX this is more important than on the TX: DIO1 RXdone is the primary packet
 * path, and its ISR would otherwise fire asynchronously mid-sweep, contend on SPI
 * with the sweep, and re-arm the telemetry-TX (tock) timer. Detaching it gives the
 * sweep exclusive ownership. Exit is by reset, so the re-init restores the ISR --
 * there is no teardown path to get wrong. See TxSpectrum DESIGN.md 2.8 (I3).
 */
static void SweepIsolateRadio()
{
    detachInterrupt(GPIO_PIN_DIO1);
    if (GPIO_PIN_DIO1_2 != UNDEF_PIN)
    {
        detachInterrupt(GPIO_PIN_DIO1_2);
    }
}

void RxSpectrumStart(uint8_t band)
{
    // Bench / passthrough diagnostic only: refuse to tear down a live RC link.
    if (connectionState == connected)
    {
        return;
    }

    if (band > RX_SPECTRUM_PORT_BOTH)
    {
        band = RX_SPECTRUM_PORT_900;
    }
    // A second-band request on a single-band device collapses to the one band.
    if (!RxSpectrumHasSecondBand())
    {
        band = RX_SPECTRUM_PORT_900;
    }
    requestedBand = band;
    // "900" and "both" open on the sub-GHz port; "2.4" opens on the 2.4 port.
    wantSubGHz = (band != RX_SPECTRUM_PORT_2G4);

    // Re-trigger while scanning = restart the measurement (also clears max-hold).
    // Runs on the loop core (the CRSF command path), the same single-threaded core
    // that runs SweepChunk in the device timeout(), so there is no race.
    if (connectionState == spectrumScan)
    {
        BeginScan();
        return;
    }

    // R3.1: drop to minimum power before entering (the WiFi/BLE idiom). This is a
    // gesture, NOT the interlock -- what actually guarantees no TX is that hwTimer
    // stays stopped (R3.1a) and the RXdone ISR is detached.
    POWERMGNT::setPower(MinPower);
    setConnectionState(spectrumScan); // event() does the one-shot hardware init
}

static int event()
{
    if (connectionState != spectrumScan)
    {
        running = false;
        return DURATION_NEVER;
    }

    if (!running)
    {
        // Keep the radio alive -- it is the instrument -- but stop the telemetry-TX
        // timer (R3.1a) and take exclusive ownership of the radio (I3).
        hwTimer::stop();
        SweepIsolateRadio();
        BeginScan();
        running = true;
    }
    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    if (connectionState != spectrumScan)
    {
        return DURATION_NEVER;
    }

    SweepChunk();

    const uint32_t now = millis();

    // "Both" mode: after dwelling long enough on the current port, flip to the
    // other one at a sweep boundary. Re-running BeginScan reconfigures the radio
    // for the new band (PA path / RF switch / antenna port) and resets the axis.
    if (requestedBand == RX_SPECTRUM_PORT_BOTH && sweepCursor == 0 &&
        (uint32_t)(now - bandDwellStartMs) >= RX_SPECTRUM_BAND_DWELL_MS)
    {
        wantSubGHz = !wantSubGHz;
        BeginScan();
        return DURATION_IMMEDIATELY;
    }

    if ((uint32_t)(now - lastEmitMs) >= RX_SPECTRUM_EMIT_INTERVAL_MS)
    {
        EmitNextFrame();
        lastEmitMs = now;
    }

    return DURATION_IMMEDIATELY;
}

device_t RxSpectrum_device = {
    .initialize = nullptr,
    .start = nullptr,
    .event = event,
    .timeout = timeout,
    .subscribe = EVENT_CONNECTION_CHANGED,
};

#endif // RX_SPECTRUM_SCAN
