#include "devRxSurvey.h"

#if defined(RX_SURVEY_PHASE0)

#include "SurveyProtocol.h"
#include "SurveyShared.h"

#include "common.h"
#include "device.h"
#include "logging.h"
#include "crsf_protocol.h"
// NB: no #include "hardware.h" -- it has no include guard and arrives via
// common.h -> targets.h. Including it directly is a redefinition error.

/*
 * Phase 0 bench instrumentation. See devRxSurvey.h for what it measures and why
 * none of it belongs in a shipping build.
 *
 * Structure: the RXdone ISR takes at most one sample per sample period and drops
 * it in a single-producer/single-consumer ring; loop() drains the ring and emits
 * CRSF_FRAMETYPE_ELRS_VENDOR frames out the serial (FC/host) port, the same
 * transport lib/RxSpectrum already uses. In and out over Betaflight serial
 * passthrough -- but unlike the sweep this one REQUIRES a live RC link, so the
 * transmitter stays on and the flight controller stays in passthrough.
 */

// Power of two so the wrap is a mask. 16 slots is ~1.6 s of headroom at the
// default 100 ms sample period, far more than the 20 ms drain interval needs;
// the surplus is what makes an overrun mean "something is wrong" rather than
// "the host was busy".
#define SURVEY_RING_SLOTS 16
#define SURVEY_RING_MASK (SURVEY_RING_SLOTS - 1)

// How often loop() drains the ring, and how often a sample-less frame goes out
// so a blocked gate is reported rather than looking like a dead receiver.
#define SURVEY_DRAIN_INTERVAL_MS 20
#define SURVEY_HEARTBEAT_INTERVAL_MS 500
#define SURVEY_IDLE_POLL_MS 500

static surveySample_t ring[SURVEY_RING_SLOTS];
static volatile uint8_t ringHead; // written by the ISR only
static volatile uint8_t ringTail; // written by loop() only
static volatile uint16_t droppedSamples;

// Runtime enable and its parameters. Volatile, RAM only, off at every boot.
static volatile bool armed;
static volatile uint16_t reqOffsetUs;
static volatile uint32_t samplePeriodUs = 100000;
static volatile uint32_t lastSampleUs;

// Owned by rx_main.cpp. Read rather than passed in so the gates can be evaluated
// from loop() too -- see EvaluateGates().
extern RXtimerState_e RXtimerState;

// Latched by RxSurveyNoteHop() from inside HandleFHSS.
static volatile uint8_t hopChan1 = SURVEY_CHAN_INVALID;
static volatile uint8_t hopChan2 = SURVEY_CHAN_INVALID;
static volatile bool hopGemini;

static int8_t lnaGainDb;
static uint8_t frameSeq;
static uint32_t lastEmitMs;

/*
 * Why sampling is not happening, evaluated from whatever context asks.
 *
 * This MUST NOT live in the sample hook alone. That hook only runs when a packet
 * is received, so a gate list built there can only ever be populated when the
 * link is already working -- and the first thing anyone needs to know is why it
 * is not. Evaluated here, the loop-context heartbeat frame carries a live answer
 * even when the radio is switched off entirely.
 */
static ICACHE_RAM_ATTR uint8_t EvaluateGates()
{
    uint8_t gates = 0;
    // Radio torn down: WiFi AP / BLE joystick, or the link-down spectrum sweep.
    // Checked first because it explains the others rather than compounding them.
    if (connectionState == wifiUpdate || connectionState == bleJoystick ||
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
    // LoRa only. Every LoRa air rate in a band uses that band's widest bandwidth,
    // which is also the sweep's default RBW, so the two measurements pass through
    // the same filter and need no correction. FLRC's 0.6 MHz does not, and FLRC is
    // also the tightest timing case.
    if (!RadioBandMod::isLoRa(ExpressLRS_currAirRate_Modparams->radio_type))
    {
        gates |= SURVEY_FLAG_GATED_RATE;
    }
    if (InBindingMode)
    {
        gates |= SURVEY_FLAG_GATED_BINDING;
    }
    return gates;
}

void RxSurveyArm(const uint16_t offsetUs, const uint8_t periodMs, const bool on)
{
    reqOffsetUs = (offsetUs > SURVEY_OFFSET_MAX_US) ? SURVEY_OFFSET_MAX_US : offsetUs;
    samplePeriodUs = (uint32_t)(periodMs == 0 ? 100 : periodMs) * 1000;

    // Read once here, not per sample: hardware_int() walks the layout and the
    // sample path runs in an ISR.
    lnaGainDb = (int8_t)hardware_int(HARDWARE_power_lna_gain);

    // Drop anything measured under the previous settings rather than let it
    // arrive labelled with the new ones.
    ringTail = ringHead;
    droppedSamples = 0;
    lastSampleUs = micros();
    armed = on;

    DBGLN("RxSurvey: %s, offset %u us, period %u ms, lna %d dB",
          on ? "armed" : "disarmed", (unsigned)reqOffsetUs, (unsigned)periodMs, lnaGainDb);
}

void ICACHE_RAM_ATTR RxSurveyNoteHop(const uint8_t chan1, const uint8_t chan2, const bool gemini)
{
    hopChan1 = chan1;
    hopChan2 = chan2;
    hopGemini = gemini;
}

void ICACHE_RAM_ATTR RxSurveySamplePostPacket(const uint32_t packetEndUs,
                                              const int8_t packetRssi, const bool packetOnRadio2)
{
    if (!armed || EvaluateGates() != 0)
    {
        return;
    }

    const uint32_t now = micros();
    if ((uint32_t)(now - lastSampleUs) < samplePeriodUs)
    {
        return;
    }
    lastSampleUs = now;

    // Busy-wait to the requested offset.
    //
    // This is the part that is bench-only. It holds the RXdone ISR, which on
    // ESP32 also delays the tock timer ISR behind it -- and tock is where the
    // hop happens. At LoRa 500Hz the next packet starts on air roughly 490 us
    // after this one ends, so an offset near or past that WILL cost the packet
    // that follows. That is deliberate and bounded: one packet per sample period,
    // i.e. 1 in 50 at 500Hz with the default 100 ms. The cost shows up as LQ in
    // the same capture, so it is measured rather than argued about.
    //
    // Never waits backwards. If ProcessRFPacket() already ran past the request,
    // the sample is taken now and reports the offset it actually got, which is
    // why the host must plot against the achieved offset and not the requested one.
    const uint32_t deadline = packetEndUs + reqOffsetUs;
    while ((int32_t)(micros() - deadline) < 0)
    {
        // Belt and braces against a corrupted deadline: reqOffsetUs is clamped
        // at arm time, so this can only fire if something else went wrong.
        if ((uint32_t)(micros() - now) > SURVEY_OFFSET_MAX_US)
        {
            break;
        }
    }

    const bool dual = isDualRadio();

    // SPI from here on. Safe only because this is the same ISR that already
    // drives the bus for GetLastPacketStats() -- SPIEx has no locking, so the
    // identical reads from loop() context could resume with this ISR's chip
    // select and transfer length.
#if defined(RADIO_LR1121)
    // Two-step on this family: the read below returns what this command latched.
    Radio.StartRssiInst(dual ? SX12XX_Radio_All : SX12XX_Radio_1);
#endif
    const uint32_t readAt = micros();
    const int8_t rssi1 = SurveyReferred(SurveyReadRssiInst(SX12XX_Radio_1), lnaGainDb);
    const int8_t rssi2 = dual ? SurveyReferred(SurveyReadRssiInst(SX12XX_Radio_2), lnaGainDb)
                              : SURVEY_RSSI_INVALID;

    const uint8_t head = ringHead;
    const uint8_t next = (uint8_t)((head + 1) & SURVEY_RING_MASK);
    if (next == ringTail)
    {
        droppedSamples++; // consumer is not keeping up; say so on the wire
        return;
    }

    uint32_t achieved = readAt - packetEndUs;
    if (achieved > SURVEY_OFFSET_MAX_US)
    {
        achieved = SURVEY_OFFSET_MAX_US;
    }

    surveySample_t *const s = &ring[head];
    s->chan1 = hopChan1;
    s->chan2 = dual ? hopChan2 : SURVEY_CHAN_INVALID;
    s->rssi1 = rssi1;
    s->rssi2 = rssi2;
    s->offsetQus = (uint8_t)(achieved / SURVEY_OFFSET_QUANTUM_US);
    s->packetRssi = packetRssi;
    s->flags = (uint8_t)((packetOnRadio2 ? SURVEY_SFLAG_PACKET_ON_RADIO2 : 0) |
                         (hopGemini ? SURVEY_SFLAG_GEMINI : 0));
    ringHead = next;
}

void RxSurveySendStatus()
{
    uint8_t buf[sizeof(crsf_ext_header_t) + SURVEY_STATUS_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE];
    uint8_t *const payload = buf + sizeof(crsf_ext_header_t);
    payload[0] = SURVEY_SUBTYPE_STATUS;
    payload[1] = SURVEY_PROTO_VERSION;
    payload[2] = armed ? SURVEY_STATUS_ARMED : SURVEY_STATUS_DISARMED;
    payload[3] = (uint8_t)(reqOffsetUs / SURVEY_OFFSET_QUANTUM_US);
    payload[4] = (uint8_t)(samplePeriodUs / 1000);
    SurveySendVendorFrame(buf, SURVEY_STATUS_PAYLOAD_BYTES);
}

static int start()
{
    return SURVEY_IDLE_POLL_MS;
}

static int timeout()
{
    if (!armed)
    {
        return SURVEY_IDLE_POLL_MS;
    }

    // Copy out under a brief critical section. Three reasons it has to be one
    // rather than a lock-free read: droppedSamples is read-and-cleared, the
    // sample block must match the tail advance, and the same pattern already
    // guards the FC UART write in SerialIO.cpp.
    surveySample_t batch[SURVEY_MAX_SAMPLES_PER_FRAME];
    uint8_t count = 0;
    uint16_t dropped;

    noInterrupts();
    uint8_t tail = ringTail;
    while (count < SURVEY_MAX_SAMPLES_PER_FRAME && tail != ringHead)
    {
        batch[count++] = ring[tail];
        tail = (uint8_t)((tail + 1) & SURVEY_RING_MASK);
    }
    ringTail = tail;
    dropped = droppedSamples;
    droppedSamples = 0;
    interrupts();

    const uint32_t now = millis();
    // A sample-less frame every SURVEY_HEARTBEAT_INTERVAL_MS carries the gate
    // flags, so "no samples" always comes with a reason attached.
    if (count > 0 || (uint32_t)(now - lastEmitMs) >= SURVEY_HEARTBEAT_INTERVAL_MS)
    {
        SurveyEmitDataFrame((uint8_t)(EvaluateGates() |
                                      (armed ? SURVEY_FLAG_ARMED : 0) |
                                      (isDualRadio() ? SURVEY_FLAG_DUAL_RADIO : 0)),
                            (uint8_t)(reqOffsetUs / SURVEY_OFFSET_QUANTUM_US),
                            lnaGainDb, frameSeq++,
                            count > 0 ? batch : nullptr, count, dropped);
        lastEmitMs = now;
    }
    else if (dropped != 0)
    {
        // Nothing to carry it on; fold it back so the count is not lost.
        noInterrupts();
        droppedSamples += dropped;
        interrupts();
    }

    return SURVEY_DRAIN_INTERVAL_MS;
}

device_t RxSurvey_device = {
    .initialize = nullptr,
    .start = start,
    .event = nullptr,
    .timeout = timeout,
    .subscribe = EVENT_NONE,
};

#endif // RX_SURVEY_PHASE0
