#include "devRxSurvey.h"

#if defined(DEBUG_RF_SURVEY)

#include "SurveyProtocol.h"

#include "common.h"
#include "logging.h"
#include "crsf_protocol.h"
#include "FHSS.h"
#include "LBT.h"
#include "LQCALC.h"
#include "OTA.h"
// NB: no #include "hardware.h" -- it has no include guard and arrives via
// common.h -> targets.h. Including it directly is a redefinition error.

// Owned by rx_main.cpp. Read directly: the hook runs inside the tock ISR, which
// on ESP32 cannot be preempted by the RXdone ISR that writes some of these.
extern RXtimerState_e RXtimerState;
extern uint8_t uplinkLQ;
extern uint8_t antenna;
extern uint8_t geminiMode;
extern LQCALC<100> LQCalc;

#define RX_SURVEY_MIN_LQ 70 // below this the link is already struggling

// The export cadence. One constant drives the sampling window, the link-stats
// interval while armed, and the figure the status frame reports.
#define RX_SURVEY_EXPORT_INTERVAL_MS 40 // 25 Hz
#define RX_SURVEY_WINDOW_US (RX_SURVEY_EXPORT_INTERVAL_MS * 1000)
#define RX_SURVEY_GRACE_US (2 * RX_SURVEY_WINDOW_US) // covered-dwell deferral bound
#define RX_SURVEY_LOOKAHEAD 16 // sequence entries scanned when deferring

// Bench-measured: after a stronger-than-this wanted packet, SX128x AGC still
// reads tens of dB high at the tock (recovery needs ~600us close-in, which the
// tock cannot wait out), so those samples are discarded. The LR1121 measured
// clean by 200us at every strength and needs no gate.
#define RX_SURVEY_PACKET_RSSI_GATE (-60)

static volatile uint8_t surveyMode; // RX_SURVEY_*; static init = OFF at boot
static int8_t lnaGainDb;

// Coverage bitmaps, one per band axis. 96 bits covers the 80-channel grid;
// chanCovered treats anything past the end as covered, so a hypothetical wider
// domain degrades to unscheduled sampling instead of indexing out of bounds.
// ISR-only while armed; SetMode stops the ISR before touching them.
#define RX_SURVEY_COVERAGE_BITS 96
static uint32_t covered1[RX_SURVEY_COVERAGE_BITS / 32];
static uint32_t covered2[RX_SURVEY_COVERAGE_BITS / 32];
static uint8_t coveredCount1, coveredCount2;
static uint8_t chanCount1, chanCount2;
static volatile uint8_t coverageGen;
static uint8_t lastRadioType = 0xFF; // forces a coverage reset on first entry

// The staged sample: the three packed debug bytes in one 32-bit word, stored by
// the tock ISR and loaded by Publish() (loop) -- an aligned word access is
// atomic on ESP32, so the record cannot tear and needs no critical section.
#define RX_SURVEY_STAGED_INVALID ((uint32_t)SURVEY_DBG_MAG_INVALID |            \
                                  ((uint32_t)SURVEY_DBG_MAG_INVALID << 8) |     \
                                  ((uint32_t)SURVEY_DBG_CHAN_NONE << 16))
static volatile uint32_t stagedWord = RX_SURVEY_STAGED_INVALID;
static bool toggle; // freshness bit; its parity doubles as the band alternation
static uint32_t lastStageUs;

// Worst-case time the hook has added to a tock, for the link A/B soak. The
// budget it is judged against is PACKET_TO_TOCK_SLACK (200us).
static volatile uint16_t worstTockUs;

static int8_t ICACHE_RAM_ATTR readRssiInst(const SX12XX_Radio_Number_t radio)
{
#if defined(RADIO_SX127X)
    return Radio.GetCurrRSSI(radio);
#else
    return Radio.GetRssiInst(radio);
#endif
}

// Antenna-referred, exactly as SpectrumSweep::StoreBin does it, and clamped so
// a real value can never collide with SURVEY_RSSI_INVALID.
static int8_t ICACHE_RAM_ATTR referred(const int8_t raw, const int8_t gainDb)
{
    const int16_t v = (int16_t)raw - (int16_t)gainDb;
    if (v < SURVEY_RSSI_INVALID + 1)
    {
        return SURVEY_RSSI_INVALID + 1;
    }
    if (v > INT8_MAX)
    {
        return INT8_MAX;
    }
    return (int8_t)v;
}

static bool ICACHE_RAM_ATTR chanCovered(const uint32_t *const map, const uint8_t chan)
{
    if (chan >= RX_SURVEY_COVERAGE_BITS)
    {
        return true; // out of tracking range: never interesting, never marked
    }
    return (map[chan >> 5] >> (chan & 31)) & 1;
}

static void ICACHE_RAM_ATTR markChan(uint32_t *const map, uint8_t *const count, const uint8_t chan)
{
    if (!chanCovered(map, chan))
    {
        map[chan >> 5] |= 1u << (chan & 31);
        (*count)++;
    }
}

static void ICACHE_RAM_ATTR resetCoverage()
{
    for (uint8_t i = 0; i < RX_SURVEY_COVERAGE_BITS / 32; i++)
    {
        covered1[i] = 0;
        covered2[i] = 0;
    }
    coveredCount1 = 0;
    coveredCount2 = 0;
    const fhss_config_t *const cfg = FHSSusePrimaryFreqBand ? FHSSconfig : FHSSconfigDualBand;
    chanCount1 = (uint8_t)cfg->freq_count;
    chanCount2 = FHSSuseDualBand ? (uint8_t)FHSSconfigDualBand->freq_count : 0;
}

void RxSurveySetMode(uint8_t mode)
{
    if (mode > RX_SURVEY_2G4)
    {
        mode = RX_SURVEY_OFF;
    }
    surveyMode = RX_SURVEY_OFF; // stop the ISR before touching its state

    stagedWord = RX_SURVEY_STAGED_INVALID;
    toggle = false;
    coverageGen = 0;
    lastRadioType = 0xFF; // the hook re-derives the maps on its first entry
    worstTockUs = 0;

    if (mode == RX_SURVEY_OFF)
    {
        // Restore the stock frame content (constant zeros on a receiver).
        linkStats.downlink_RSSI_1 = 0;
        linkStats.downlink_Link_quality = 0;
        linkStats.downlink_SNR = 0;
        DBGLN("Survey: off");
        return;
    }

    // Read once here, not per sample: hardware_int() walks the layout.
    lnaGainDb = (int8_t)hardware_int(HARDWARE_power_lna_gain);
    lastStageUs = micros() - RX_SURVEY_WINDOW_US; // first sample immediately
    surveyMode = mode; // last, so the ISR only ever sees armed with fresh state

    DBGLN("Survey: mode %u, lna %d dB", mode, lnaGainDb);
}

uint32_t RxSurveyLinkStatsInterval(const uint32_t defaultIntervalMs)
{
    return (surveyMode != RX_SURVEY_OFF) ? RX_SURVEY_EXPORT_INTERVAL_MS
                                         : defaultIntervalMs;
}

void RxSurveyPublish()
{
    if (surveyMode == RX_SURVEY_OFF)
    {
        return;
    }
    const uint32_t staged = stagedWord; // one aligned load; see stagedWord above
    linkStats.downlink_RSSI_1 = (uint8_t)staged;
    linkStats.downlink_Link_quality = (uint8_t)(staged >> 8);
    linkStats.downlink_SNR = (int8_t)(uint8_t)(staged >> 16);
}

// Why sampling is not happening: one encoding for the tock's gate stack and the
// loop-context heartbeat frame both, so a blocked gate is visible on the wire.
static uint8_t ICACHE_RAM_ATTR evaluateGates()
{
    uint8_t gates = 0;
    // The radio is unavailable to the survey: torn down for WiFi AP / BLE
    // joystick or a link-down sweep, or (CE builds) owned by LBT's own CCA
    // RSSI reads. Constant-false LbtIsEnabled folds out of non-CE builds.
    bool radioDown = LbtIsEnabled ||
                     connectionState == wifiUpdate || connectionState == bleJoystick;
#if defined(RX_SPECTRUM_SCAN)
    radioDown |= connectionState == spectrumScan;
#endif
    if (radioDown)
    {
        gates |= SURVEY_FLAG_GATED_RADIO;
    }
    if (connectionState != connected)
    {
        gates |= SURVEY_FLAG_GATED_LINK;
    }
    if (RXtimerState != tim_locked)
    {
        gates |= SURVEY_FLAG_GATED_TIMER;
    }
    if (!RadioBandMod::isLoRa(ExpressLRS_currAirRate_Modparams->radio_type))
    {
        gates |= SURVEY_FLAG_GATED_RATE;
    }
    if (InBindingMode)
    {
        gates |= SURVEY_FLAG_GATED_BINDING;
    }
    if (uplinkLQ < RX_SURVEY_MIN_LQ)
    {
        gates |= SURVEY_FLAG_GATED_LQ;
    }
    return gates;
}

void ICACHE_RAM_ATTR RxSurveyTock()
{
    const uint8_t mode = surveyMode;
    if (mode == RX_SURVEY_OFF)
    {
        return; // the whole disarmed cost: one RAM read and this branch
    }

    // Cheapest rejector first: on a healthy link the vast majority of armed
    // tocks fail only this window check, so they never pay for the gates.
    const uint32_t t0 = micros();
    if ((uint32_t)(t0 - lastStageUs) < RX_SURVEY_WINDOW_US)
    {
        return;
    }
    if (evaluateGates() != 0)
    {
        return;
    }
    const uint8_t rt = ExpressLRS_currAirRate_Modparams->radio_type;
    // This hook runs before OtaNonce++ and HandleSendDataDl tests the
    // incremented nonce, so +1 asks "is this tock about to key up telemetry".
    if (ExpressLRS_currTlmDenom > 1 &&
        ((uint8_t)(OtaNonce + 1) % ExpressLRS_currTlmDenom) == 0)
    {
        return;
    }

    if (rt != lastRadioType)
    {
        // First armed tock, or a mid-flight rate change (which reconnects, and on
        // an LR1121 can move between bands): re-derive the maps from the live FHSS.
        lastRadioType = rt;
        resetCoverage();
    }

    // Which radios this mode samples on this link.
    const bool dualBandRate = FHSSuseDualBand;
    bool sample1, sample2;
    if (dualBandRate)
    {
        sample1 = (mode != RX_SURVEY_2G4); // radio 1 is the sub-GHz chain
        sample2 = (mode != RX_SURVEY_900); // radio 2 is the 2.4 chain
    }
    else
    {
        // Single band: the link fixes it, so Both and the link's own band mean
        // On, and naming the absent band means no samples.
        if (mode == (RadioBandMod::isB900(rt) ? RX_SURVEY_2G4 : RX_SURVEY_900))
        {
            return;
        }
        sample1 = true;
        sample2 = isDualRadio();
    }

    // Pre-hop channels: HandleFHSS has not run yet this tock, so FHSSptr still
    // names the dwell the radio is sitting on.
    const uint8_t *const seq = FHSSusePrimaryFreqBand ? FHSSsequence : FHSSsequence_DualBand;
    const uint8_t idx = FHSSptr;
    const uint8_t chanP = seq[idx];
    const uint8_t chanS = dualBandRate ? FHSSsequence_DualBand[idx] : SURVEY_CHAN_INVALID;

    // Gemini: the radios wear cur/partner alternately; parity says which.
    const bool geminiSplit = geminiMode && !dualBandRate;
    bool parity = false;
    uint8_t chanR1 = chanP;
    uint8_t chanR2 = chanP;
    if (geminiSplit)
    {
        const uint8_t numfhss = (uint8_t)FHSSgetChannelCount();
        const uint8_t partner = (uint8_t)((chanP + numfhss / 2) % numfhss);
        // The same expression HandleFHSS used when it tuned this dwell: OtaNonce
        // has advanced within the dwell but not past it, so the quotient holds.
        parity = ((OtaNonce / ExpressLRS_currAirRate_Modparams->FHSShopInterval) % 2) == 0;
        chanR1 = parity ? chanP : partner;
        chanR2 = parity ? partner : chanP;
    }

    // Coverage-driven scheduling: prefer dwells that fill the bitmap, deferring
    // a covered one (bounded by the grace window) when an uncovered one is
    // coming -- the sequence is fully known, so this is a table walk, no PRNG.
    const bool interesting = (sample1 && !chanCovered(covered1, chanR1)) ||
                             (geminiSplit && sample2 && !chanCovered(covered1, chanR2)) ||
                             (dualBandRate && sample2 && !chanCovered(covered2, chanS));
    if (!interesting)
    {
        const uint32_t sinceStage = t0 - lastStageUs;
        const uint32_t hopUs = (uint32_t)ExpressLRS_currAirRate_Modparams->interval *
                               ExpressLRS_currAirRate_Modparams->FHSShopInterval;
        if (sinceStage < RX_SURVEY_GRACE_US && hopUs != 0)
        {
            const uint16_t seqCount = FHSSgetSequenceCount();
            uint32_t reach = (RX_SURVEY_GRACE_US - sinceStage) / hopUs;
            if (reach > RX_SURVEY_LOOKAHEAD)
            {
                reach = RX_SURVEY_LOOKAHEAD;
            }
            uint16_t n = idx;
            for (uint32_t i = 1; i <= reach; i++)
            {
                if (++n >= seqCount)
                {
                    n = 0;
                }
                if (sample1 && !chanCovered(covered1, seq[n]))
                {
                    return; // an uncovered dwell is reachable; wait for it
                }
                if (dualBandRate && sample2 && !chanCovered(covered2, FHSSsequence_DualBand[n]))
                {
                    return;
                }
            }
        }
        // Nothing better within reach, or the grace expired: sample this one.
    }

    const bool packetThisPeriod = LQCalc.currentIsSet();
#if defined(RADIO_SX128X)
    if (packetThisPeriod)
    {
        // The raw per-packet value, valid only when a packet actually arrived
        // this period -- LastPacketRSSI holds a stale reading otherwise.
        const int8_t pktRssi = (Radio.GetProcessingPacketRadio() == SX12XX_Radio_2)
                                   ? Radio.LastPacketRSSI2 : Radio.LastPacketRSSI;
        if (pktRssi > RX_SURVEY_PACKET_RSSI_GATE)
        {
            return; // AGC still holds the packet's gain; the window stays open
        }
    }
#endif

    // The reads. Safe here and only here: on ESP32 the whole tock runs inside
    // the hwTimer critical section, so the RXdone ISR -- which drives the same
    // unlocked SPI bus -- cannot preempt these transfers.
#if defined(RADIO_LR1121)
    // Two-step on this family: the reads below return what this command latched.
    Radio.StartRssiInst((SX12XX_Radio_Number_t)((sample1 ? SX12XX_Radio_1 : 0) |
                                                (sample2 ? SX12XX_Radio_2 : 0)));
#endif
    int8_t rssi1 = SURVEY_RSSI_INVALID;
    int8_t rssi2 = SURVEY_RSSI_INVALID;
    if (sample1)
    {
        rssi1 = referred(readRssiInst(SX12XX_Radio_1), lnaGainDb);
    }
    if (sample2)
    {
        rssi2 = referred(readRssiInst(SX12XX_Radio_2), lnaGainDb);
    }
    const uint8_t magA = sample1 ? SurveyDbgMagFromDbm(rssi1) : SURVEY_DBG_MAG_INVALID;
    const uint8_t magB = sample2 ? SurveyDbgMagFromDbm(rssi2) : SURVEY_DBG_MAG_INVALID;

    // debug[2]: which channel index rides this sample, and what bit 7 means.
    uint8_t chanByte;
    bool bit7;
    if (dualBandRate)
    {
        // Alternate the band whose index debug[2] carries; bit 7 names it. The
        // freshness toggle flips once per sample, so its pre-flip value is the
        // alternation phase for free. With one band deselected the other rides
        // every sample at full cadence.
        const bool use2G4 = sample2 && (!sample1 || toggle);
        chanByte = use2G4 ? chanS : chanP;
        bit7 = use2G4;
    }
    else
    {
        chanByte = chanP;
        bit7 = geminiSplit ? parity : (antenna != 0);
    }

    toggle = !toggle;
    surveyDebug_t d;
    d.magA = magA;
    d.magB = magB;
    d.chan = chanByte;
    d.toggle = toggle;
    d.clean = !packetThisPeriod; // no wanted packet this period: the AGC cross-check
    d.bit7 = bit7;
    uint8_t bytes[3];
    SurveyPackDebug(bytes, &d);
    stagedWord = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16);
    lastStageUs = t0;

    if (sample1)
    {
        markChan(covered1, &coveredCount1, chanR1);
        if (geminiSplit && sample2)
        {
            markChan(covered1, &coveredCount1, chanR2);
        }
    }
    if (dualBandRate && sample2)
    {
        markChan(covered2, &coveredCount2, chanS);
    }
    bool done = !sample1 || (coveredCount1 >= chanCount1);
    if (dualBandRate && sample2)
    {
        done = done && (coveredCount2 >= chanCount2);
    }
    if (done)
    {
        resetCoverage();
        coverageGen++;
    }

    const uint32_t dt = micros() - t0;
    if (dt > worstTockUs)
    {
        worstTockUs = (uint16_t)dt;
    }
}

#endif // DEBUG_RF_SURVEY
