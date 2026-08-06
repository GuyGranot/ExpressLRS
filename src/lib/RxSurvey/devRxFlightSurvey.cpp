#include "devRxFlightSurvey.h"

#if defined(RX_FLIGHT_SURVEY)

#include "SurveyProtocol.h"
#include "SurveyShared.h"

#include "common.h"
#include "device.h"
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

#define FLIGHT_SURVEY_MIN_LQ 70 // below this the link is already struggling

// The export cadence. One constant drives the sampling window, the link-stats
// interval while armed, and the figure the status frame reports, so the three
// cannot drift apart.
#define FLIGHT_SURVEY_EXPORT_INTERVAL_MS 40 // 25 Hz
#define FLIGHT_SURVEY_WINDOW_US (FLIGHT_SURVEY_EXPORT_INTERVAL_MS * 1000)
#define FLIGHT_SURVEY_GRACE_US (2 * FLIGHT_SURVEY_WINDOW_US) // covered-dwell deferral bound
#define FLIGHT_SURVEY_LOOKAHEAD 16 // sequence entries scanned when deferring

// Phase 0's one hard number: after a stronger-than-this wanted packet, SX128x
// AGC still reads tens of dB high at the tock (recovery needs ~600 us close-in,
// which the tock cannot wait out). Discarding those samples is the defence; the
// LR1121 measured clean by 200 us at every strength and needs none.
#define FLIGHT_SURVEY_PACKET_RSSI_GATE (-60)

static volatile uint8_t surveyMode; // FLIGHT_SURVEY_*; static init = OFF at boot
static int8_t lnaGainDb;

// Coverage bitmaps, one per band axis. 96 bits covers the 80-channel grid;
// ChanCovered treats anything past the end as covered, so a hypothetical wider
// domain degrades to unscheduled sampling instead of indexing out of bounds.
// ISR-only while armed; SetMode stops the ISR before touching them.
#define FLIGHT_SURVEY_COVERAGE_BITS 96
static uint32_t covered1[FLIGHT_SURVEY_COVERAGE_BITS / 32];
static uint32_t covered2[FLIGHT_SURVEY_COVERAGE_BITS / 32];
static uint8_t coveredCount1, coveredCount2;
static uint8_t chanCount1, chanCount2;
static volatile uint8_t coverageGen;
static uint8_t lastRadioType = 0xFF; // forces a coverage reset on first entry

// The staged sample: packed debug bytes. Written by the tock ISR, copied into
// linkStats only by Publish() (loop) under noInterrupts() -- the three bytes
// are one logical record and must never tear. {127,127,127} = "no sample yet".
static volatile uint8_t stagedBytes[3] = {SURVEY_DBG_MAG_INVALID, SURVEY_DBG_MAG_INVALID,
                                          SURVEY_DBG_CHAN_NONE};
static bool toggle; // freshness bit; its parity doubles as the band alternation
static uint32_t lastStageUs;

// Worst-case time the hook has added to a tock, for the link A/B soak. The
// budget it is judged against is PACKET_TO_TOCK_SLACK (200 us).
static volatile uint16_t worstTockUs;

/*
 * The bench stream. While a host asks for it (the 'sf' serial command), every
 * staged sample also goes into this SPSC ring in full fidelity, and the device
 * timeout() drains it into v2 0x83 vendor frames -- the same transport and
 * tooling as Phase 0, so rxsurvey.py can validate the shipping sampler against
 * the 3-byte transport byte for byte. A flight never asks, so a flight never
 * spends UART on it.
 */
#define FLIGHT_SURVEY_RING_SLOTS 8 // power of two; 25 Hz in, 20 ms drain out
#define FLIGHT_SURVEY_RING_MASK (FLIGHT_SURVEY_RING_SLOTS - 1)
#define FLIGHT_SURVEY_DRAIN_INTERVAL_MS 20
#define FLIGHT_SURVEY_HEARTBEAT_INTERVAL_MS 500
#define FLIGHT_SURVEY_STATUS_INTERVAL_MS 1000
#define FLIGHT_SURVEY_IDLE_POLL_MS 500

static surveySample_t ring[FLIGHT_SURVEY_RING_SLOTS];
static volatile uint8_t ringHead; // written by the tock ISR only
static volatile uint8_t ringTail; // written by loop() only
static volatile uint16_t droppedSamples;
static volatile bool benchStream; // off at boot; cleared by SetMode(OFF)
static uint8_t frameSeq;
static uint32_t lastEmitMs;
static uint32_t lastStatusMs;

static bool ICACHE_RAM_ATTR ChanCovered(const uint32_t *const map, const uint8_t chan)
{
    if (chan >= FLIGHT_SURVEY_COVERAGE_BITS)
    {
        return true; // out of tracking range: never interesting, never marked
    }
    return (map[chan >> 5] >> (chan & 31)) & 1;
}

static void ICACHE_RAM_ATTR MarkChan(uint32_t *const map, uint8_t *const count, const uint8_t chan)
{
    if (!ChanCovered(map, chan))
    {
        map[chan >> 5] |= 1u << (chan & 31);
        (*count)++;
    }
}

static void ICACHE_RAM_ATTR ResetCoverage()
{
    for (uint8_t i = 0; i < FLIGHT_SURVEY_COVERAGE_BITS / 32; i++)
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

void RxFlightSurveySetMode(uint8_t mode)
{
    if (mode > FLIGHT_SURVEY_2G4)
    {
        mode = FLIGHT_SURVEY_OFF;
    }
    surveyMode = FLIGHT_SURVEY_OFF; // stop the ISR before touching its state

    stagedBytes[0] = SURVEY_DBG_MAG_INVALID;
    stagedBytes[1] = SURVEY_DBG_MAG_INVALID;
    stagedBytes[2] = SURVEY_DBG_CHAN_NONE;
    toggle = false;
    coverageGen = 0;
    lastRadioType = 0xFF; // the hook re-derives the maps on its first entry
    worstTockUs = 0;
    ringTail = ringHead; // drop samples measured under the previous mode
    droppedSamples = 0;

    if (mode == FLIGHT_SURVEY_OFF)
    {
        benchStream = false;
        // Restore the stock frame content (constant zeros on a receiver).
        linkStats.downlink_RSSI_1 = 0;
        linkStats.downlink_Link_quality = 0;
        linkStats.downlink_SNR = 0;
        DBGLN("FlightSurvey: off");
        return;
    }

    // Read once here, not per sample: hardware_int() walks the layout.
    lnaGainDb = (int8_t)hardware_int(HARDWARE_power_lna_gain);
    lastStageUs = micros() - FLIGHT_SURVEY_WINDOW_US; // first sample immediately
    surveyMode = mode; // last, so the ISR only ever sees armed with fresh state

    DBGLN("FlightSurvey: mode %u, lna %d dB", mode, lnaGainDb);
}

uint32_t RxFlightSurveyLinkStatsInterval(const uint32_t defaultIntervalMs)
{
    return (surveyMode != FLIGHT_SURVEY_OFF) ? FLIGHT_SURVEY_EXPORT_INTERVAL_MS
                                             : defaultIntervalMs;
}

void RxFlightSurveyPublish()
{
    if (surveyMode == FLIGHT_SURVEY_OFF)
    {
        return;
    }
    noInterrupts();
    linkStats.downlink_RSSI_1 = stagedBytes[0];
    linkStats.downlink_Link_quality = stagedBytes[1];
    linkStats.downlink_SNR = (int8_t)stagedBytes[2];
    interrupts();
}

// Why sampling is not happening: one encoding for the tock's gate stack and the
// loop-context heartbeat frame both, so the two cannot drift apart and a blocked
// gate is always visible on the wire.
static uint8_t ICACHE_RAM_ATTR EvaluateGates()
{
    uint8_t gates = 0;
    // The radio is unavailable to the survey: torn down for WiFi AP / BLE
    // joystick / the link-down sweep, or (CE builds) owned by LBT's own CCA
    // RSSI reads. Constant-false LbtIsEnabled folds out of non-CE builds.
    if (LbtIsEnabled ||
        connectionState == wifiUpdate || connectionState == bleJoystick ||
        connectionState == spectrumScan)
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
    if (uplinkLQ < FLIGHT_SURVEY_MIN_LQ)
    {
        gates |= SURVEY_FLAG_GATED_LQ;
    }
    return gates;
}

void ICACHE_RAM_ATTR RxFlightSurveyTock()
{
    const uint8_t mode = surveyMode;
    if (mode == FLIGHT_SURVEY_OFF)
    {
        return; // the whole disarmed cost: one RAM read and this branch
    }

    // Cheapest rejector first: on a healthy link the vast majority of armed
    // tocks fail only this window check, so they never pay for the gates.
    const uint32_t t0 = micros();
    if ((uint32_t)(t0 - lastStageUs) < FLIGHT_SURVEY_WINDOW_US)
    {
        return;
    }
    if (EvaluateGates() != 0)
    {
        return;
    }
    const uint8_t rt = ExpressLRS_currAirRate_Modparams->radio_type;
    // This hook runs before OtaNonce++, HandleSendDataDl tests the incremented
    // nonce, so +1 asks "is this tock about to key up telemetry" -- skip it.
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
        ResetCoverage();
    }

    // Which radios this mode samples on this link.
    const bool dualBandRate = FHSSuseDualBand;
    bool sample1, sample2;
    if (dualBandRate)
    {
        sample1 = (mode != FLIGHT_SURVEY_2G4); // radio 1 is the sub-GHz chain
        sample2 = (mode != FLIGHT_SURVEY_900); // radio 2 is the 2.4 chain
    }
    else
    {
        // Single band: the link fixes it, so Both and the link's own band mean
        // On, and naming the absent band means no samples.
        if (mode == (RadioBandMod::isB900(rt) ? FLIGHT_SURVEY_2G4 : FLIGHT_SURVEY_900))
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
    const bool interesting = (sample1 && !ChanCovered(covered1, chanR1)) ||
                             (geminiSplit && sample2 && !ChanCovered(covered1, chanR2)) ||
                             (dualBandRate && sample2 && !ChanCovered(covered2, chanS));
    if (!interesting)
    {
        const uint32_t sinceStage = t0 - lastStageUs;
        const uint32_t hopUs = (uint32_t)ExpressLRS_currAirRate_Modparams->interval *
                               ExpressLRS_currAirRate_Modparams->FHSShopInterval;
        if (sinceStage < FLIGHT_SURVEY_GRACE_US && hopUs != 0)
        {
            const uint16_t seqCount = FHSSgetSequenceCount();
            uint32_t reach = (FLIGHT_SURVEY_GRACE_US - sinceStage) / hopUs;
            if (reach > FLIGHT_SURVEY_LOOKAHEAD)
            {
                reach = FLIGHT_SURVEY_LOOKAHEAD;
            }
            uint16_t n = idx;
            for (uint32_t i = 1; i <= reach; i++)
            {
                if (++n >= seqCount)
                {
                    n = 0;
                }
                if (sample1 && !ChanCovered(covered1, seq[n]))
                {
                    return; // an uncovered dwell is reachable; wait for it
                }
                if (dualBandRate && sample2 && !ChanCovered(covered2, FHSSsequence_DualBand[n]))
                {
                    return;
                }
            }
        }
        // Nothing better within reach, or the grace expired: sample this one.
    }

    const bool packetThisPeriod = LQCalc.currentIsSet();
    bool packetOnRadio2 = false;
    int8_t pktRssi = SURVEY_RSSI_INVALID;
    if (packetThisPeriod)
    {
        // The raw per-packet value, valid only when a packet actually arrived
        // this period -- LastPacketRSSI holds a stale reading otherwise.
        packetOnRadio2 = (Radio.GetProcessingPacketRadio() == SX12XX_Radio_2);
        pktRssi = packetOnRadio2 ? Radio.LastPacketRSSI2 : Radio.LastPacketRSSI;
    }
#if defined(RADIO_SX128X)
    if (packetThisPeriod && pktRssi > FLIGHT_SURVEY_PACKET_RSSI_GATE)
    {
        return; // AGC still holds the packet's gain; the window stays open
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
        rssi1 = SurveyReferred(SurveyReadRssiInst(SX12XX_Radio_1), lnaGainDb);
    }
    if (sample2)
    {
        rssi2 = SurveyReferred(SurveyReadRssiInst(SX12XX_Radio_2), lnaGainDb);
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
    stagedBytes[0] = bytes[0];
    stagedBytes[1] = bytes[1];
    stagedBytes[2] = bytes[2];
    lastStageUs = t0;

    if (benchStream)
    {
        // Full-fidelity copy of the same sample for the 0x83 bench stream, so
        // the host can check the two transports agree byte for byte.
        const uint8_t head = ringHead;
        const uint8_t next = (uint8_t)((head + 1) & FLIGHT_SURVEY_RING_MASK);
        if (next == ringTail)
        {
            droppedSamples++;
        }
        else
        {
            surveySample_t *const s = &ring[head];
            s->chan1 = sample1 ? chanR1 : SURVEY_CHAN_INVALID;
            s->chan2 = !sample2 ? SURVEY_CHAN_INVALID : (dualBandRate ? chanS : chanR2);
            s->rssi1 = rssi1;
            s->rssi2 = rssi2;
            s->offsetQus = 0; // read at the tock, not at a controlled offset
            s->packetRssi = pktRssi;
            s->flags = (uint8_t)((packetOnRadio2 ? SURVEY_SFLAG_PACKET_ON_RADIO2 : 0) |
                                 (geminiSplit ? SURVEY_SFLAG_GEMINI : 0) |
                                 (packetThisPeriod ? 0 : SURVEY_SFLAG_CLEAN));
            ringHead = next;
        }
    }

    if (sample1)
    {
        MarkChan(covered1, &coveredCount1, chanR1);
        if (geminiSplit && sample2)
        {
            MarkChan(covered1, &coveredCount1, chanR2);
        }
    }
    if (dualBandRate && sample2)
    {
        MarkChan(covered2, &coveredCount2, chanS);
    }
    bool done = !sample1 || (coveredCount1 >= chanCount1);
    if (dualBandRate && sample2)
    {
        done = done && (coveredCount2 >= chanCount2);
    }
    if (done)
    {
        ResetCoverage();
        coverageGen++;
    }

    const uint32_t dt = micros() - t0;
    if (dt > worstTockUs)
    {
        worstTockUs = (uint16_t)dt;
    }
}

/*
 * Loop half: the bench stream's drain and the status/heartbeat frames. All of
 * it idles at a slow poll unless a host has asked for the stream.
 */

void RxFlightSurveySendStatus()
{
    uint8_t buf[sizeof(crsf_ext_header_t) + SURVEY_STATUS_EXT_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE];
    uint8_t *const payload = buf + sizeof(crsf_ext_header_t);
    const uint16_t worst = worstTockUs; // aligned 16-bit read, atomic on ESP32
    payload[0] = SURVEY_SUBTYPE_STATUS;
    payload[1] = SURVEY_PROTO_VERSION;
    payload[2] = (surveyMode != FLIGHT_SURVEY_OFF) ? SURVEY_STATUS_ARMED : SURVEY_STATUS_DISARMED;
    payload[3] = 0; // no controlled offset: the read site is the tock
    payload[4] = (surveyMode != FLIGHT_SURVEY_OFF) ? FLIGHT_SURVEY_EXPORT_INTERVAL_MS : 0;
    payload[5] = (uint8_t)(worst >> 8);
    payload[6] = (uint8_t)worst;
    payload[7] = surveyMode;
    payload[8] = coverageGen;
    SurveySendVendorFrame(buf, SURVEY_STATUS_EXT_PAYLOAD_BYTES);
}

void RxFlightSurveyBenchStream(const bool on)
{
    ringTail = ringHead; // both stream edges start from an empty ring
    droppedSamples = 0;
    lastEmitMs = 0;
    benchStream = on;
    DBGLN("FlightSurvey: bench stream %s", on ? "on" : "off");
}

static int start()
{
    return FLIGHT_SURVEY_IDLE_POLL_MS;
}

static int timeout()
{
    if (!benchStream || surveyMode == FLIGHT_SURVEY_OFF)
    {
        return FLIGHT_SURVEY_IDLE_POLL_MS;
    }

    // Copy out under a brief critical section: droppedSamples is
    // read-and-cleared, and the sample block must match the tail advance.
    surveySample_t batch[SURVEY_MAX_SAMPLES_PER_FRAME];
    uint8_t count = 0;
    uint16_t dropped;

    noInterrupts();
    uint8_t tail = ringTail;
    while (count < SURVEY_MAX_SAMPLES_PER_FRAME && tail != ringHead)
    {
        batch[count++] = ring[tail];
        tail = (uint8_t)((tail + 1) & FLIGHT_SURVEY_RING_MASK);
    }
    ringTail = tail;
    dropped = droppedSamples;
    droppedSamples = 0;
    interrupts();

    const uint32_t now = millis();
    // dropped != 0 implies count > 0: samples are only dropped against a full
    // ring, and only this drain empties it -- so a nonzero drop count always
    // rides out on a data frame and never needs folding back.
    if (count > 0 || (uint32_t)(now - lastEmitMs) >= FLIGHT_SURVEY_HEARTBEAT_INTERVAL_MS)
    {
        SurveyEmitDataFrame((uint8_t)(EvaluateGates() | SURVEY_FLAG_ARMED |
                                      (isDualRadio() ? SURVEY_FLAG_DUAL_RADIO : 0)),
                            0 /* no controlled offset: the read site is the tock */,
                            lnaGainDb, frameSeq++,
                            count > 0 ? batch : nullptr, count, dropped);
        lastEmitMs = now;
    }

    if ((uint32_t)(now - lastStatusMs) >= FLIGHT_SURVEY_STATUS_INTERVAL_MS)
    {
        RxFlightSurveySendStatus();
        lastStatusMs = now;
    }

    return FLIGHT_SURVEY_DRAIN_INTERVAL_MS;
}

device_t RxFlightSurvey_device = {
    .initialize = nullptr,
    .start = start,
    .event = nullptr,
    .timeout = timeout,
    .subscribe = EVENT_NONE,
};

#endif // RX_FLIGHT_SURVEY
