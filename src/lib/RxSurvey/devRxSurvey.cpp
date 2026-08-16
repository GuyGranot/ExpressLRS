#include "devRxSurvey.h"

#if defined(DEBUG_RF_SURVEY)

#include "SurveyProtocol.h"

#include "common.h"
#include "logging.h"
#include "crsf_protocol.h"
#include "CRSFRouter.h"
#include "FHSS.h"
#include "LBT.h"
#include "LQCALC.h"
#include "OTA.h"
#include "RXOTAConnector.h"
// no "hardware.h": it has no include guard and arrives via common.h -> targets.h

#include <string.h>

// Owned by rx_main.cpp. Read directly: the hook runs inside the tock ISR, which
// on ESP32 cannot be preempted by the RXdone ISR that writes some of these.
extern RXtimerState_e RXtimerState;
extern uint8_t uplinkLQ;
extern uint8_t antenna;
extern uint8_t geminiMode;
extern LQCALC<100> LQCalc;
extern RXOTAConnector otaConnector; // the source to exclude when routing to serial

// fires on every firmware build against the real CRSF constants
static_assert(sizeof(crsf_ext_header_t) + SURVEY_MAX_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE
                  <= CRSF_MAX_PACKET_LEN,
              "survey frame exceeds CRSF_MAX_PACKET_LEN");
static_assert(SURVEY_MAX_PAYLOAD_BYTES <= CRSF_PAYLOAD_SIZE_MAX,
              "survey payload exceeds CRSF_PAYLOAD_SIZE_MAX");

#define RX_SURVEY_MIN_LQ 70 // below this the link is already struggling

// One constant drives the sampling window, the armed link-stats interval and
// the status frame.
#define RX_SURVEY_EXPORT_INTERVAL_MS 40 // 25 Hz
#define RX_SURVEY_WINDOW_US (RX_SURVEY_EXPORT_INTERVAL_MS * 1000)
#define RX_SURVEY_GRACE_US (2 * RX_SURVEY_WINDOW_US) // covered-dwell deferral bound
#define RX_SURVEY_LOOKAHEAD 16 // sequence entries scanned when deferring

// Bench-measured: after a wanted packet stronger than this, SX128x AGC still
// reads tens of dB high at the tock. The LR1121 measured clean and needs no gate.
#define RX_SURVEY_PACKET_RSSI_GATE (-60)

static volatile uint8_t surveyMode; // RX_SURVEY_*; static init = OFF at boot
static int8_t lnaGainDb;

// Coverage bitmaps, one per band axis; chanCovered treats anything past the end
// as covered. ISR-only while armed; SetMode stops the ISR before touching them.
#define RX_SURVEY_COVERAGE_BITS 96
static uint32_t covered1[RX_SURVEY_COVERAGE_BITS / 32];
static uint32_t covered2[RX_SURVEY_COVERAGE_BITS / 32];
static uint8_t coveredCount1, coveredCount2;
static uint8_t chanCount1, chanCount2;
static volatile uint8_t coverageGen;
static uint8_t lastRadioType = 0xFF; // forces a coverage reset on first entry

// The staged sample as one 32-bit word: an aligned word access is atomic on
// ESP32, so the tock-ISR store and Publish()'s loop-context load cannot tear.
#define RX_SURVEY_STAGED_INVALID ((uint32_t)SURVEY_DBG_MAG_INVALID |            \
                                  ((uint32_t)SURVEY_DBG_MAG_INVALID << 8) |     \
                                  ((uint32_t)SURVEY_DBG_CHAN_NONE << 16))
static volatile uint32_t stagedWord = RX_SURVEY_STAGED_INVALID;
static bool toggle; // freshness bit; its parity doubles as the band alternation
static uint32_t lastStageUs;

// Worst-case time added to a tock, judged against PACKET_TO_TOCK_SLACK (200us).
static volatile uint16_t worstTockUs;

// The bench stream ('sf' serial command): every staged sample also goes into
// this SPSC ring in full fidelity, and timeout() drains it into 0x83 vendor
// frames. A flight never asks, so a flight never spends UART on it.
#define RX_SURVEY_RING_SLOTS 8 // power of two; 25 Hz in, 20 ms drain out
#define RX_SURVEY_RING_MASK (RX_SURVEY_RING_SLOTS - 1)
#define RX_SURVEY_DRAIN_INTERVAL_MS 20
#define RX_SURVEY_HEARTBEAT_INTERVAL_MS 500
#define RX_SURVEY_STATUS_INTERVAL_MS 1000
#define RX_SURVEY_IDLE_POLL_MS 500

static surveySample_t ring[RX_SURVEY_RING_SLOTS];
static volatile uint8_t ringHead; // written by the tock ISR only
static volatile uint8_t ringTail; // written by loop() only
static volatile uint16_t droppedSamples;
static volatile bool benchStream; // off at boot; cleared by SetMode(OFF)
static uint8_t frameSeq;
static uint32_t lastEmitMs;
static uint32_t lastStatusMs;

static int8_t ICACHE_RAM_ATTR readRssiInst(const SX12XX_Radio_Number_t radio)
{
#if defined(RADIO_SX127X)
    return Radio.GetCurrRSSI(radio);
#else
    return Radio.GetRssiInst(radio);
#endif
}

// Antenna-referred (raw minus the receive path's power_lna_gain), clamped clear of the sentinel.
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
    ringTail = ringHead; // drop samples measured under the previous mode
    droppedSamples = 0;

    if (mode == RX_SURVEY_OFF)
    {
        benchStream = false;
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

// One gate encoding for the tock's gate stack and the heartbeat frame both.
static uint8_t ICACHE_RAM_ATTR evaluateGates()
{
    uint8_t gates = 0;
    // torn down for WiFi/BLE, or (CE) owned by LBT's CCA reads
    bool radioDown = LbtIsEnabled ||
                     connectionState == wifiUpdate || connectionState == bleJoystick;
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

    // cheapest rejector first: most armed tocks fail only the window check
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
        // first armed tock or a rate change: re-derive the maps from the live FHSS
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
        // the link fixes the band: Both and the link's own band mean On
        if (mode == (RadioBandMod::isB900(rt) ? RX_SURVEY_2G4 : RX_SURVEY_900))
        {
            return;
        }
        sample1 = true;
        sample2 = isDualRadio();
    }

    // HandleFHSS has not run yet this tock, so FHSSptr still names the current dwell
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
        const uint8_t partner = FHSSgeminiPartnerChannel(chanP);
        // the same expression HandleFHSS used when it tuned this dwell
        parity = ((OtaNonce / ExpressLRS_currAirRate_Modparams->FHSShopInterval) % 2) == 0;
        chanR1 = parity ? chanP : partner;
        chanR2 = parity ? partner : chanP;
    }

    // Coverage-driven scheduling: defer a covered dwell (bounded by the grace
    // window) when an uncovered one is reachable in the known sequence.
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
    // the Last* values hold stale readings when no packet arrived this period
    const bool packetOnRadio2 = packetThisPeriod &&
                                (Radio.GetProcessingPacketRadio() == SX12XX_Radio_2);
#if defined(RADIO_SX128X)
    if (packetThisPeriod)
    {
        const int8_t pktRssi = packetOnRadio2 ? Radio.LastPacketRSSI2 : Radio.LastPacketRSSI;
        if (pktRssi > RX_SURVEY_PACKET_RSSI_GATE)
        {
            return; // AGC still holds the packet's gain; the window stays open
        }
    }
#endif

    // Safe here and only here: on ESP32 the whole tock runs inside the hwTimer
    // critical section, so the RXdone ISR cannot preempt these SPI transfers.
#if defined(RADIO_LR1121)
    // two-step on this family: the reads below return what this command latched
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

    uint8_t chanByte;
    bool bit7;
    if (dualBandRate)
    {
        // Alternate the band debug[2] carries, bit 7 naming it; the freshness
        // toggle's pre-flip value is the alternation phase for free.
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

    if (benchStream)
    {
        // full-fidelity copy so the host can check the two transports agree
        const uint8_t head = ringHead;
        const uint8_t next = (uint8_t)((head + 1) & RX_SURVEY_RING_MASK);
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
            s->flags = (uint8_t)((packetOnRadio2 ? SURVEY_SFLAG_PACKET_ON_RADIO2 : 0) |
                                 (packetThisPeriod ? 0 : SURVEY_SFLAG_CLEAN));
            ringHead = next;
        }
    }

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

// Loop half: the bench stream's drain and the status/heartbeat frames, idling
// at a slow poll unless a host has asked for the stream.

// One band's config -> one axis; LR1121 registers hold Hz, the other families
// register units.
static void axisFromConfig(const fhss_config_t *const cfg, const uint32_t spread,
                           uint32_t *const startKhz, uint16_t *const stepKhz)
{
#if defined(RADIO_LR1121)
    *startKhz = cfg->freq_start / 1000;
    *stepKhz = (uint16_t)((spread / FREQ_SPREAD_SCALE) / 1000);
#else
    *startKhz = (uint32_t)(((double)cfg->freq_start * FREQ_STEP / 1000.0) + 0.5);
    *stepKhz = (uint16_t)((((double)spread / FREQ_SPREAD_SCALE) * FREQ_STEP / 1000.0) + 0.5);
#endif
}

// The channel axes in kHz, derived from the live FHSS configs so tooling joins
// samples to frequencies; the second axis stays 0 unless dual-band.
static void fillAxis(surveyFrameInfo_t *const info)
{
    const bool primary = FHSSusePrimaryFreqBand;
    const fhss_config_t *const cfg = primary ? FHSSconfig : FHSSconfigDualBand;
    axisFromConfig(cfg, primary ? freq_spread : freq_spread_DualBand,
                   &info->startFreqKhz, &info->stepKhz);
    info->channelCount = (uint8_t)cfg->freq_count;

    if (FHSSuseDualBand)
    {
        // radio 1 hops the primary grid, radio 2 this one, off one shared pointer
        axisFromConfig(FHSSconfigDualBand, freq_spread_DualBand,
                       &info->startFreqKhz2, &info->stepKhz2);
        info->channelCount2 = (uint8_t)FHSSconfigDualBand->freq_count;
    }
}

// The live link's sensing bandwidth in kHz -- on a LoRa rate every rate in a
// band shares the band's widest bandwidth.
static uint16_t linkBandwidthKhz()
{
#if defined(RADIO_SX127X)
    return 500;
#elif defined(RADIO_LR1121)
    // DualBand reports the primary (sub-GHz) chain's bandwidth; the second
    // band's 812 kHz is implied by the second axis being present.
    const uint8_t rt = ExpressLRS_currAirRate_Modparams->radio_type;
    return (RadioBandMod::isB900(rt) || RadioBandMod::isBDUAL(rt)) ? 500 : 812;
#else // RADIO_SX128X
    return 812;
#endif
}

// Stamp the vendor-frame header and CRC and route it out the FC UART; loop context.
static void sendVendorFrame(uint8_t *const buf, const uint8_t payloadLen)
{
    crsfRouter.SetExtendedHeaderAndCrc((crsf_ext_header_t *)buf,
                                       CRSF_FRAMETYPE_ELRS_VENDOR,
                                       CRSF_EXT_FRAME_SIZE(payloadLen),
                                       CRSF_ADDRESS_FLIGHT_CONTROLLER,
                                       CRSF_ADDRESS_CRSF_RECEIVER);
    crsfRouter.deliverMessage(&otaConnector, (crsf_header_t *)buf);
}

// Build and send one data (or sample-less heartbeat) frame from the live link.
static void emitDataFrame(const uint8_t flags, const surveySample_t *const samples,
                          const uint8_t count, const uint16_t dropped)
{
    uint8_t buf[sizeof(crsf_ext_header_t) + SURVEY_MAX_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE];

    surveyFrameInfo_t info;
    memset(&info, 0, sizeof(info));
    info.flags = flags;
    info.seq = frameSeq++;
    info.enumRate = ExpressLRS_currAirRate_Modparams->enum_rate;
    info.bwKhz = linkBandwidthKhz();
    info.lnaGainDb = lnaGainDb;
    info.dropped = dropped;
    info.sampleCount = count;
    fillAxis(&info);

    const uint8_t len = SurveyEncodeFrame(buf + sizeof(crsf_ext_header_t), samples, &info);
    if (len != 0)
    {
        sendVendorFrame(buf, len);
    }
}

void RxSurveySendStatus()
{
    uint8_t buf[sizeof(crsf_ext_header_t) + SURVEY_STATUS_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE];
    uint8_t *const payload = buf + sizeof(crsf_ext_header_t);
    const uint16_t worst = worstTockUs; // aligned 16-bit read, atomic on ESP32
    payload[0] = SURVEY_SUBTYPE_STATUS;
    payload[1] = SURVEY_PROTO_VERSION;
    payload[2] = surveyMode; // 0 = disarmed
    payload[3] = (surveyMode != RX_SURVEY_OFF) ? RX_SURVEY_EXPORT_INTERVAL_MS : 0;
    payload[4] = (uint8_t)(worst >> 8);
    payload[5] = (uint8_t)worst;
    payload[6] = coverageGen;
    sendVendorFrame(buf, SURVEY_STATUS_PAYLOAD_BYTES);
}

void RxSurveyBenchStream(const bool on)
{
    ringTail = ringHead; // both stream edges start from an empty ring
    droppedSamples = 0;
    lastEmitMs = 0;
    benchStream = on;
    DBGLN("Survey: bench stream %s", on ? "on" : "off");
}

static int start()
{
    return RX_SURVEY_IDLE_POLL_MS;
}

static int timeout()
{
    if (!benchStream || surveyMode == RX_SURVEY_OFF)
    {
        return RX_SURVEY_IDLE_POLL_MS;
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
        tail = (uint8_t)((tail + 1) & RX_SURVEY_RING_MASK);
    }
    ringTail = tail;
    dropped = droppedSamples;
    droppedSamples = 0;
    interrupts();

    const uint32_t now = millis();
    // dropped != 0 implies count > 0 (drops only happen against a full ring),
    // so a nonzero drop count always rides out on a data frame
    if (count > 0 || (uint32_t)(now - lastEmitMs) >= RX_SURVEY_HEARTBEAT_INTERVAL_MS)
    {
        emitDataFrame((uint8_t)(evaluateGates() |
                                (isDualRadio() ? SURVEY_FLAG_DUAL_RADIO : 0)),
                      count > 0 ? batch : nullptr, count, dropped);
        lastEmitMs = now;
    }

    if ((uint32_t)(now - lastStatusMs) >= RX_SURVEY_STATUS_INTERVAL_MS)
    {
        RxSurveySendStatus();
        lastStatusMs = now;
    }

    return RX_SURVEY_DRAIN_INTERVAL_MS;
}

device_t RxSurvey_device = {
    .initialize = nullptr,
    .start = start,
    .event = nullptr,
    .timeout = timeout,
    .subscribe = EVENT_NONE,
};

#endif // DEBUG_RF_SURVEY
