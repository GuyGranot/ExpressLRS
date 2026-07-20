#include "TxSpectrum.h"

#if defined(TX_SPECTRUM_SCAN)

#include "TxSpectrumProtocol.h"

#include "common.h"
#include "device.h"
#include "logging.h"
#include "CRSFRouter.h"
#include "crsf_protocol.h"
#include "FHSS.h"
#include "POWERMGNT.h"
#include "OTA.h" // provides the global `isArmed` -- the arm-guard input
#include "hwTimer.h"
// NB: no #include "hardware.h" -- it has no include guard and arrives via
// common.h -> targets.h. Including it directly is a redefinition error.

#include <string.h>

/*
 * Sweep engine for the TX-side spectrum analyzer. See DESIGN.md.
 *
 * HARDWARE SAFETY INVARIANT (DESIGN.md R3): this path is receive-only and must
 * never enable the transmit front end. RFAMP.TXenable() has exactly two callers
 * in the SX1280 driver -- startCWTest() (SX1280.cpp:142) and TXnb()
 * (SX1280.cpp:490) -- and nothing here may reach either. The only radio calls
 * below are SetFrequencyReg(..., doRx) and the RSSI reads.
 */

#include "rxtx_intf.h" // scheduleRebootTime()

// The wire-format header keeps itself free of CRSF/Arduino deps so it can be
// unit tested natively, which means it cannot check its own size budget against
// the real CRSF constants. Do it here instead: this file has them in scope and
// is compiled for every target, so a crsf_ext_header_t that grows -- or a
// TX_SPECTRUM_MAX_BINS_PER_FRAME someone raises -- fails the build rather than
// silently emitting frames the handset drops.
static_assert(sizeof(crsf_ext_header_t) + TX_SPECTRUM_MAX_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE
                  <= CRSF_MAX_PACKET_LEN,
              "TX spectrum frame exceeds CRSF_MAX_PACKET_LEN");
static_assert(TX_SPECTRUM_MAX_PAYLOAD_BYTES <= CRSF_PAYLOAD_SIZE_MAX,
              "TX spectrum payload exceeds CRSF_PAYLOAD_SIZE_MAX");

// Bins sampled per timeout() call. This bounds the BLOCKING TIME, not the bin
// count, because settleUs spans 80us (FLRC) to 1000us (LR1121, see RssiSettleUs)
// -- a 12x range, so a fixed count would block anywhere from sub-ms to well over
// 10ms depending on radio family and air rate. Budgeting the time keeps each call
// flat at ~1.3ms regardless, which is what let the LR1121 settle rise 4x without
// re-tuning anything else (DESIGN.md invariant S1).
//
// Blocking here is normal for this framework (devBackpack.cpp does delay(100),
// devADC.cpp sustains DURATION_IMMEDIATELY) and safe: hwTimer is stopped, and
// the only thing waiting is HandleUARTin(), whose ~6.5KB/s of RC data accrues
// ~8 bytes in 1.3ms against a 256-byte hardware ring filled by an ISR.
#define TX_SPECTRUM_CHUNK_BUDGET_US 1300
#define TX_SPECTRUM_MAX_POSITIONS_PER_CALL 16

// DESIGN.md 5: pace one frame per interval, never burst a sweep. Two independent
// reasons, and the tighter one is the handset's heap:
//
//  - EdgeTX's Lua telemetry queue is 255 usable bytes and, on B&W handsets, is
//    shared with ELRS's own parameter traffic. Our frames are 56 bytes on the
//    wire, so a 4-frame burst would nearly fill it in one go.
//  - Every frame we send makes EdgeTX mint a fresh ~54-entry Lua table
//    (luaCrossfireTelemetryPop does lua_newtable + a settable per byte). At
//    50ms that is ~12-22 KB/s of garbage on a script that already forces a full
//    collectgarbage() per string parse. At 25ms it was double that.
//
// One frame per 50ms, traces alternating, so a full live+max-hold update lands at
// 10Hz for a 40-bin band (2 frames) or 5Hz for an 80-bin band (4 frames) -- at or
// under what the ~20-30ms Lua tick can consume. Max-hold quality is unaffected by
// the emit rate: it accumulates from the sweep, which still outruns its own display
// update ~2x on LR1121 (per-bin AGC reset, see SweepChunk) and ~30x on SX1280.
#define TX_SPECTRUM_EMIT_INTERVAL_MS 50

static int8_t liveBins[TX_SPECTRUM_MAX_BINS];
static int8_t maxHoldBins[TX_SPECTRUM_MAX_BINS];

// Scan-invariant, all latched once by BeginScan().
static const fhss_config_t *scanCfg;
static uint32_t scanSpread;
static uint32_t settleUs;
static int lnaGainDb;
static bool scanDual;          // split one band across two radios (same-band Gemini)
static uint8_t positions;      // radio-1 dwell positions per sweep
static uint8_t positionsPerCall;

// Cross-band selection (LR1121 Nomad). 0 = primary band (sub-GHz, radio 1),
// 1 = secondary band (2.4GHz, radio 2). Single-band devices only ever use 0.
// The handset flips this with the page button; see TxSpectrumSwitchBand().
static uint8_t scanBand;
static SX12XX_Radio_Number_t activeRadio; // band-matched radio, latched by BeginScan()

static uint8_t sweepCursor; // next dwell position to sample
static uint8_t sweepSeq;    // free-running, increments per completed sweep
static bool running;

// Emission cursor. Doubles as the encoder's argument block, so the fields
// cannot be transposed and there is no second copy of totalBins/axis to drift.
// emitInfo.sweepSeq is latched from sweepSeq at the start of each trace.
static txSpectrumFrameInfo_t emitInfo;
static uint32_t lastEmitMs;

/*
 * Instantaneous RSSI of the given radio; the SX127x driver names this
 * GetCurrRSSI, the others GetRssiInst.
 *
 * Kept local rather than shared. The natural home is next to isDualRadio() in
 * common.h, but that is a flight header and DESIGN.md R1.2 keeps this diff
 * additive. Recorded in DESIGN.md 9.2. If you add a radio family, this shim is
 * the one place the sweep knows which RSSI call that family wants.
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
 * RX-entry RSSI settling times. RSSI is not valid immediately after RX entry --
 * reading early returns garbage.
 *
 * Origin is LBT.cpp's SpreadingFactorToRSSIvalidDelayUs (empirical bench data,
 * documented at LBT.cpp:37-52). Not calling it directly is justified and load
 * bearing: it is static, lives inside #if defined(Regulatory_Domain_EU_CE_2400),
 * and LBT is in the native env's lib_ignore -- so on a non-CE build it does not
 * exist to call.
 *
 * DO NOT replace the LR1121 branch with a generic settle table. Generic tables --
 * LBT's included -- answer "how long until RSSI is valid on an ALREADY-RUNNING
 * receiver", which is 22us/240us territory. This sweep asks a strictly larger
 * question: it drops to STDBY_RC and re-enters RX on EVERY bin (SweepChunk), so
 * the delay must also cover a PLL relock and a full AGC re-acquisition. That is
 * the flat 1000us. Shorten it while the per-bin reset stays and the 915 whole-band
 * floor lift comes straight back (DESIGN.md 3.3.3, invariant M1).
 *
 * Returning a real delay on the unknown-radio_type fallthrough rather than LBT's 0
 * is also deliberate and is the safer direction: 0 samples before the AGC settles
 * and silently produces garbage rather than failing loudly.
 */
static uint32_t RssiSettleUs(const uint8_t SF, const uint8_t radio_type)
{
#if defined(RADIO_SX127X)
    (void)SF;
    (void)radio_type;
    return 240; // unmeasured
#elif defined(RADIO_LR1121)
    // Flat 1 ms, deliberately ignoring SF/modulation. The LBT-derived 22/40/240 us
    // figures above measure only how long RSSI takes to become valid after RX entry
    // on an already-running receiver. The LR1121 sweep does more than that: it drops
    // to STDBY_RC and re-enters RX on every bin (SweepChunk), so each read must also
    // cover a PLL relock plus a full AGC re-acquisition -- for which none of those
    // numbers is characterised.
    //
    // 1000 us is Semtech's own figure: DELAY_BETWEEN_SET_RX_AND_VALID_RSSI_MS = 1 in
    // the LR11xx spectral_scan example (Lora-net/SWSD003), the closest thing to a
    // reference implementation of exactly this measurement.
    //
    // Cost is sweep rate, not correctness: positionsPerCall scales inversely, so a
    // 40-bin sub-GHz sweep lands near 20 Hz and an 80-bin 2.4 sweep near 10 Hz --
    // both at or above the 10 Hz emit rate, so the live trace stays fed. Shortening
    // this without also removing the per-bin reset reintroduces the 915 floor lift.
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
        return 80; // 60us TX->RX switch + 20us settling
    }
    return 480;
#endif
}

/*
 * Radio-domain frequency of a bin. The bin grid IS the FHSS channel grid, so a
 * future sub-band picker selects over the same axis this plots.
 *
 * scanCfg/scanSpread are latched together with totalBins in BeginScan() from the
 * same FHSSusePrimaryFreqBand predicate FHSSgetChannelCount() uses, so the count
 * and the frequencies can never come from different bands.
 *
 * FreqCorrection is deliberately not applied: we want the true grid, not the
 * link's tracking offset.
 *
 * FHSS.h open-codes this expression in eight places and has no helper to call;
 * extracting one is a flight-critical refactor out of scope here (DESIGN.md 9.1).
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
 * Max-hold is accumulated HERE, on the TX, and must stay here. It looks like
 * removable wire traffic -- the handset could derive it from the live trace and
 * halve both the frames and its own GC load -- but the TX sweeps at ~40-50Hz
 * while the handset only ever sees a live trace at 10Hz. A handset-side max-hold
 * would miss roughly three quarters of all sweeps, and so would fail at exactly
 * the bursty-traffic job it exists for (DESIGN.md 3.5).
 */
static void StoreBin(const uint8_t bin, const int8_t raw)
{
    // Back out the front-end gain so the value is antenna-referred rather than
    // raw. ELRS mandates power_lna_gain for every SX1280 target
    // (hardware.py:372-374); LBT uses the same figure (Unified_ESP32_TX.h:35).
    // Clamping to >= INVALID+1 keeps the sentinel unambiguous, which is what
    // lets the max-hold below be a plain compare.
    const int8_t v = (int8_t)constrain((int16_t)raw - (int16_t)lnaGainDb,
                                       TX_SPECTRUM_RSSI_INVALID + 1, INT8_MAX);
    liveBins[bin] = v;
    if (v > maxHoldBins[bin])
    {
        maxHoldBins[bin] = v;
    }
}

/*
 * Sample up to positionsPerCall dwell positions.
 *
 * On a dual radio (Gemini) the band is split: radio 1 takes the low half and
 * radio 2 the high half, giving two bins per dwell. This mirrors normal Gemini
 * operation, where the two radios already sit on different frequencies
 * (tx_main.cpp:871-882).
 */
static void SweepChunk()
{
    for (uint8_t n = 0; n < positionsPerCall && sweepCursor < positions; n++, sweepCursor++)
    {
        const uint8_t bin1 = sweepCursor;
        const uint8_t bin2 = (uint8_t)(sweepCursor + positions);
        // The scanDual test is redundant today -- on a single radio
        // positions == totalBins, so the bounds check alone would always fail --
        // but relying on that would make this correct only via a non-local
        // invariant. Keep it explicit.
        const bool haveBin2 = scanDual && (bin2 < emitInfo.totalBins);

#if defined(RADIO_LR1121)
        // Drop to STDBY_RC before every retune so the RX entry below restarts the
        // receiver and re-runs gain acquisition. This is load bearing, not tidiness:
        // RX_CONT never re-acquires gain on a bare retune, so a gain step the AGC
        // picks for one bin persists into every bin after it -- and, once it steps
        // down, the whole trace reads that step's gain-limited floor (~-83..-85 dBm,
        // the top gain steps of LR1121 UM 7.2.15) as a flat, peakless, multi-second
        // lift ~20 dB above the true floor. Hardware-confirmed on the Nomad
        // 2026-07-20: with this reset the 915 lift is gone; without it, it returns.
        //
        // The sibling SX126x has the same signature as published errata (RadioLib),
        // likewise cured by restarting the AGC. Costs a PLL relock per bin, which is
        // why RssiSettleUs() returns Semtech's 1 ms for LR1121 -- do not shorten one
        // without the other.
        //
        // LR1121 only: the SX1280 SuperG/Boxer paths are P0-P5 validated as-is and
        // have never shown this, so they keep their measured settle and no reset.
        Radio.SpectrumResetRx(haveBin2 ? SX12XX_Radio_All : activeRadio);
#endif

        if (haveBin2)
        {
            // Radio 2's frequency first, WITHOUT doRx: RXnb() takes no radio
            // argument and puts both radios into RX_CONT (SX1280.cpp:531-533),
            // so radio 1's doRx=true below arms the pair together.
            Radio.SetFrequencyReg(BinToRadioFreq(bin2), SX12XX_Radio_2, false);
        }

        // doRx=true is mandatory, not an optimisation. Writing the frequency
        // alone does not retune a command/state-machine radio: it takes effect
        // when RX is next entered. This is exactly why the upstream
        // adds-noisefloor-measure-feature spike ended on "works on 900 but not
        // on 2.4" -- it hoisted SetMode(RX) out of its loop.
        //
        // This works only because the early-return guard in SX1280::SetMode is
        // commented out (SX1280.cpp:209-216). If that guard is ever restored as
        // an "optimisation", FHSS hopping and this sweep both break -- and this
        // sweep breaks SILENTLY, reporting bin 0's RSSI across every bin. The
        // P2 "distinct per-bin RSSI" check is the regression test for that.
        //
        // activeRadio is the band-matched radio (radio 1 sub-GHz, radio 2 2.4).
        // On a same-band Gemini it is always radio 1, so this is bit-identical to
        // the original single-radio path; only a cross-band scan drives radio 2.
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
    // Latch the sequence at the start of a trace so both frames of one trace
    // agree. The sweep keeps running underneath, so a 2-frame live trace can
    // still tear across sweeps; harmless for a moving display, and max-hold is
    // monotonic so tearing cannot corrupt it.
    if (emitInfo.binOffset == 0)
    {
        emitInfo.sweepSeq = sweepSeq;
    }

    const uint8_t trace = emitInfo.flags & TX_SPECTRUM_FLAG_TRACE_MASK;
    const int8_t *src = (trace == TX_SPECTRUM_TRACE_MAXHOLD) ? maxHoldBins : liveBins;

    // Encode straight into the frame buffer past the header -- the encoder does
    // not touch payload on its error path. Matches rx_main.cpp:1948 and
    // sendELRSstatus (TXModuleParameters.cpp:407).
    uint8_t buf[sizeof(crsf_ext_header_t) + TX_SPECTRUM_MAX_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE];
    const uint8_t len = TxSpectrumEncodeFrame(buf + sizeof(crsf_ext_header_t), src, &emitInfo);
    if (len == 0)
    {
        emitInfo.binOffset = 0;
        return;
    }

    crsfRouter.SetExtendedHeaderAndCrc((crsf_ext_header_t *)buf,
                                       CRSF_FRAMETYPE_ELRS_TX_SPECTRUM,
                                       CRSF_EXT_FRAME_SIZE(len),
                                       CRSF_ADDRESS_RADIO_TRANSMITTER,
                                       CRSF_ADDRESS_CRSF_TRANSMITTER);
    crsfRouter.deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, (crsf_header_t *)buf);

    emitInfo.binOffset += emitInfo.binCount;
    if (emitInfo.binOffset >= emitInfo.totalBins)
    {
        emitInfo.binOffset = 0;
        emitInfo.flags = (trace == TX_SPECTRUM_TRACE_LIVE) ? TX_SPECTRUM_TRACE_MAXHOLD
                                                           : TX_SPECTRUM_TRACE_LIVE;
    }
}

void TxSpectrumResetMaxHold()
{
    // INVALID is -128 == 0x80, so memset writes it exactly.
    memset(maxHoldBins, (uint8_t)TX_SPECTRUM_RSSI_INVALID, sizeof(maxHoldBins));
}

/*
 * True when this device can scan two bands (cross-band LR1121, e.g. Nomad):
 * both a sub-GHz and a 2.4GHz power table are present. This is the same signal
 * the web UI reports as has_low_band / has_high_band (devWIFI.cpp:420-421). On
 * SX128x/SX127x it is false, so the band-flip is inert and the single-band path
 * is preserved bit-for-bit.
 */
static bool TxSpectrumHasSecondBand()
{
#if defined(RADIO_LR1121)
    return POWER_OUTPUT_VALUES_COUNT != 0 && POWER_OUTPUT_VALUES_DUAL_COUNT != 0;
#else
    return false;
#endif
}

static void BeginScan()
{
    // scanBand selects the band explicitly (not via the link's active-band
    // globals), so the page button can scan a band the link is not using. Band 0
    // is the primary/sub-GHz config on radio 1; band 1 is the 2.4GHz dual-band
    // config on radio 2. The assignment is forced by the hardware: each cross-band
    // chip is wired to its own band-matched front end.
    if (scanBand == 0)
    {
        scanCfg = FHSSconfig;
        scanSpread = freq_spread;
        activeRadio = SX12XX_Radio_1;
    }
    else
    {
        scanCfg = FHSSconfigDualBand;
        scanSpread = freq_spread_DualBand;
        // Radio 2 owns 2.4 only on a two-radio device (Nomad). A single-LR1121
        // device can still advertise both bands (TxSpectrumHasSecondBand, matching
        // the band selector at TXModuleParameters.cpp:874, which gates the second
        // *radio* option separately on GPIO_PIN_NSS_2) -- there both bands run on
        // radio 1, switching band via the Config() below. Targeting a non-existent
        // radio 2 would sweep nothing.
        activeRadio = isDualRadio() ? SX12XX_Radio_2 : SX12XX_Radio_1;
    }

    // The band's own channel count -- never FHSSgetChannelCount(), which reads
    // the link's active-band global and would disagree with scanBand.
    uint8_t bins = (uint8_t)scanCfg->freq_count;
    // Defensive: no shipping domain exceeds TX_SPECTRUM_MAX_BINS today, but a
    // future table with more channels must truncate, not overrun liveBins[].
    //
    // 80 is the right constant *because* this scans one band at a time.
    // FHSSgetChannelCount() returns a single band's count -- 2.4GHz is 80 and
    // every sub-GHz domain is smaller (FHSS.cpp:14-23) -- so 80 covers the
    // widest view that can exist. A *combined* dual-band plot would need 120
    // (FCC915 40 + ISM2G4 80) and would silently lose 40 bins right here.
    // DESIGN.md §10 covers why per-band screens avoid that.
    if (bins > TX_SPECTRUM_MAX_BINS)
    {
        bins = TX_SPECTRUM_MAX_BINS;
    }

    memset(&emitInfo, 0, sizeof(emitInfo));
    emitInfo.totalBins = bins;
    emitInfo.flags = TX_SPECTRUM_TRACE_LIVE;
    ComputeAxis();

#if defined(RADIO_LR1121)
    // Reconfigure the band-matched radio for the selected band. This is what makes
    // the page-flip work: crossing 900<->2.4 on an LR1121 is not a bare frequency
    // write -- Config() selects the sub-GHz vs 2.4 PA and RF-switch via SetPaConfig
    // (inferred from freq < 1GHz, LR1121.cpp:175) and leaves the radio in STDBY_RC.
    // It is proven receive-only: no SET_TX-class opcode is on this path (the three
    // TX openers -- TXnb, startCWTest, SetMode(TX) -- are never called here), and
    // hwTimer stays stopped. bw/sf/cr are band-agnostic LoRa/GFSK enums, so reusing
    // the current air rate's tracks a valid sensing config on either band -- only
    // the frequency (hence the PA path) is band-specific. Sync words are irrelevant:
    // we read instantaneous RSSI, never demodulate a packet.
    //
    // On the primary band the link already left this radio configured, so the call
    // is redundant-but-harmless; on the other band (e.g. link on 900, scanning 2.4)
    // it is required, because that radio was never brought up for this band.
    const expresslrs_mod_settings_s *const mp = ExpressLRS_currAirRate_Modparams;
    // Config() consults only the modulation half of radio_type; the band is set
    // by regfreq, so the link's type gives correct LoRa/GFSK sensing on either band.
    Radio.Config(mp->bw, mp->sf, mp->cr, scanCfg->freq_start, mp->PreambleLen,
                 false /*InvertIQ: sensing, not a link*/, mp->PayloadLength,
                 mp->radio_type, 0, 0, activeRadio);
    // NB: image calibration (CalibImage in Begin(), LR1121.cpp:151) spans only the
    // primary band's range, so a radio retuned to the other band may read slightly
    // hot/cold. Correctness, not safety -- verify on the bench (DESIGN §10).
#endif

    settleUs = RssiSettleUs(ExpressLRS_currAirRate_Modparams->sf,
                            ExpressLRS_currAirRate_Modparams->radio_type);
    lnaGainDb = hardware_int(HARDWARE_power_lna_gain);
    // Split one band across both radios (2x rate) only on a same-band Gemini. On a
    // cross-band device the two radios own different bands, so we sweep the single
    // band-matched radio and leave the other alone.
    scanDual = isDualRadio() && !TxSpectrumHasSecondBand();
    positions = scanDual ? (uint8_t)((bins + 1) / 2) : bins;
    positionsPerCall = (uint8_t)constrain((uint32_t)(TX_SPECTRUM_CHUNK_BUDGET_US / settleUs),
                                          1u, (uint32_t)TX_SPECTRUM_MAX_POSITIONS_PER_CALL);

    memset(liveBins, (uint8_t)TX_SPECTRUM_RSSI_INVALID, sizeof(liveBins));
    TxSpectrumResetMaxHold();

    sweepCursor = 0;
    sweepSeq = 0;
    lastEmitMs = millis();

    DBGLN("TxSpectrum: band %u radio %u, %u bins, start %u kHz, step %u kHz, settle %u us, lna %d dB, %u pos/call",
          scanBand, (unsigned)activeRadio, emitInfo.totalBins, emitInfo.startFreqKhz,
          emitInfo.stepKhz, settleUs, lnaGainDb, positionsPerCall);
}

void TxSpectrumStart()
{
    // Re-invoking the running command means "restart the measurement", which is
    // exactly a max-hold reset. Folding it in here is why there is no separate
    // Reset Max-Hold parameter: such a parameter is unreachable from the field
    // list while scanning (the plot owns the screen), and a no-op when not
    // scanning (BeginScan clears max-hold anyway) -- i.e. useless in both states.
    // The handset binds this to a key instead, reusing the Start Scan field id.
    if (connectionState == spectrumScan)
    {
        TxSpectrumResetMaxHold();
        return;
    }

    // Open on the band the link is actually using, so the scan starts on what the
    // user flies and the page button reveals the other. Band 1 only when the device
    // truly has two bands (else FHSSusePrimaryFreqBand is meaningless / always the
    // primary), so single-band devices always take band 0.
    scanBand = (TxSpectrumHasSecondBand() && !FHSSusePrimaryFreqBand) ? 1 : 0;

    // R3.1: drop to minimum power before entering, matching the WiFi/BLE
    // teardown idiom (devWIFI.cpp:967).
    //
    // Do NOT read this as "a latent bug would transmit at minimum power" -- it
    // would not. SetOutputPower only queues pwrPending (SX1280.cpp:185-194);
    // the register write is CommitOutputPower(), reached from TXnbISR -- the
    // TX-*done* ISR (SX1280.cpp:441, "radio goes to FS after TX"). The queued
    // change therefore lands *after* the next packet, so a stray TX goes out at
    // the pre-scan power. devWIFI gets away with the same idiom only because
    // Radio.End() follows it immediately; we keep the radio alive (it is the
    // instrument), so we inherit the idiom without its guard.
    //
    // What actually guarantees no TX is that hwTimer stays stopped: see the
    // TX_SPECTRUM_SCAN guards on the three hwTimer::resume() sites in
    // tx_main.cpp. This call is kept because it is free and points the right
    // way, not because it is load-bearing.
    POWERMGNT::setPower(MinPower);
    setConnectionState(spectrumScan);
}

void TxSpectrumSwitchBand()
{
    // Page-button band flip. No-op unless a scan is running on a device that has a
    // second band, so single-band handsets/devices never see an effect. Re-runs the
    // per-band setup on the same running device_t: hwTimer is already stopped and we
    // stay in spectrumScan, so the sweep just retargets the other band's radio via
    // the Config() in BeginScan(). The new band's max-hold starts cold -- the
    // accepted cost of the single-radio, one-band-at-a-time design (DESIGN §10). The
    // axis flips with the band and the handset re-latches it from the next frame.
    //
    // Runs on the loop core (the CRSF param-write path), the same core that runs
    // SweepChunk in the device timeout(); the loop is single-threaded, so there is
    // no race with an in-flight sweep.
    if (connectionState != spectrumScan || !TxSpectrumHasSecondBand())
    {
        return;
    }
    scanBand ^= 1;
    BeginScan();
}

/*
 * Isolate the radio from the live OTA RX interrupt for the whole scan.
 *
 * The sweep drives the radio from the loop core, but the DIO1 RXdone ISR (attached
 * in LR1121_hal init, never detached) keeps firing asynchronously: LoRa CRC is off
 * during the link, so any matching ambient 915 energy raises RX_DONE, and the ISR
 * then issues its own SPI (GetIrqStatus + GetPacket) on the same radio the sweep is
 * mid-read on, with no mutual exclusion. Detaching DIO1 gives the sweep exclusive
 * ownership; RX_CONT means the radio keeps receiving with no self-fallback, so the
 * measurement itself is unchanged. Exit is by reboot, so the re-init restores the
 * interrupt -- there is no teardown path to get wrong.
 *
 * Honest scope note: this closes a real unsynchronised-SPI race (same class as the
 * cross-core hazard in STATUS.md), but it fixes NO observed symptom. It was written
 * for the 2026-07-19 diagnosis of the 915 floor lift, and testing refuted that -- the
 * lift was AGC gain carryover inside the chip (see SweepChunk) and survived this
 * change untouched. Kept because an async ISR transacting on a radio mid-read is a
 * defect on its own terms, not because it earned its keep on the bench.
 *
 * LR1121 only: the SX1280 SuperG path is hardware-validated (P0-P5) and is left
 * exactly as validated.
 */
static void SweepIsolateRadio()
{
#if defined(RADIO_LR1121)
    detachInterrupt(GPIO_PIN_DIO1);
    if (GPIO_PIN_DIO1_2 != UNDEF_PIN)
    {
        detachInterrupt(GPIO_PIN_DIO1_2);
    }
#endif
}

static int event()
{
    if (connectionState != spectrumScan)
    {
        running = false;
        return DURATION_NEVER;
    }

    // R1.4: never sweep while armed. Abort via the same reboot the mode uses to
    // exit normally, so there is no restore path to get wrong -- arming during a
    // scan reboots straight back into a working link.
    if (isArmed)
    {
        DBGLN("TxSpectrum: armed, aborting scan");
        scheduleRebootTime(400);
        return DURATION_NEVER;
    }

    if (!running)
    {
        // Unlike WiFi/BLE we keep the radio alive -- it is the instrument.
        hwTimer::stop();
        SweepIsolateRadio();
        BeginScan();
        running = true;
    }
    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    // R1.4 is enforced here as a LEVEL check, not only on the arm event above:
    // this is the function that actually sweeps, so the invariant must not
    // depend on EVENT_ARM_FLAG_CHANGED being delivered (it is ESP32-only, see
    // TXModuleEndpoint.cpp:151). The event subscription is the fast path; this
    // is the guarantee.
    if (connectionState != spectrumScan || isArmed)
    {
        return DURATION_NEVER;
    }

    SweepChunk();

    const uint32_t now = millis();
    if ((uint32_t)(now - lastEmitMs) >= TX_SPECTRUM_EMIT_INTERVAL_MS)
    {
        EmitNextFrame();
        lastEmitMs = now;
    }

    return DURATION_IMMEDIATELY;
}

device_t TxSpectrum_device = {
    .initialize = nullptr,
    .start = nullptr,
    .event = event,
    .timeout = timeout,
    .subscribe = (uint32_t)(EVENT_CONNECTION_CHANGED | EVENT_ARM_FLAG_CHANGED),
};

#endif // TX_SPECTRUM_SCAN
