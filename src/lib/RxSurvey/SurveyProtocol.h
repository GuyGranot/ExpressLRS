#pragma once

/*
 * Wire format for the Phase 0 in-flight-survey bench experiment: receiver ->
 * flight controller / host. Pure layout and codec with no Arduino, driver or
 * CRSF dependencies, so it can be tested natively and reused by offline tooling.
 *
 * Carried in a CRSF_FRAMETYPE_ELRS_VENDOR extended frame, sub-type 0x03 (the
 * frame type is shared -- see include/crsf_protocol.h and SpectrumProtocol.h,
 * which owns 0x01 and 0x02).
 *
 * WHAT THIS MEASURES, AND WHY IT IS SEPARATE FROM THE SWEEP
 *
 * lib/SpectrumSweep measures a band with the link DOWN: it drops to STDBY_RC per
 * bin and waits 0.5-4 ms for the AGC to re-acquire. Neither is available on a
 * live link. This sub-protocol carries instantaneous RSSI read a controlled,
 * MEASURED offset after the end of a received packet, so the two can be compared
 * per channel and the AGC carry-over from the wanted signal can be quantified
 * rather than assumed. It answers one question and is not a shipping feature.
 *
 * Levels carry the same caveats as the sweep -- antenna-referred via
 * power_lna_gain, but uncalibrated in absolute terms -- and are directly
 * comparable to a sweep of the same band on the same target only because the
 * survey is restricted to LoRa air rates, whose bandwidth equals the sweep's
 * default (wide) RBW. bwKhz carries the link's own bandwidth so a consumer can
 * check that rather than trust it.
 *
 * Frame payload:
 *   [0]      sub-type, SURVEY_SUBTYPE
 *   [1]      protocol version (of everything from [2] on)
 *   [2]      flags, see SURVEY_FLAG_*. Reports the gates, so a frame carrying
 *            zero samples still says WHY none were taken.
 *   [3]      frame sequence (wraps; a gap means frames were lost on the wire)
 *   [4]      requested sample offset, us/4 -- what the host asked for
 *   [5]      enum_rate of the live link (comparable across targets; index is not)
 *   [6:7]    link sensing bandwidth, kHz, big endian
 *   [8]      power_lna_gain in dB (int8), already subtracted from every RSSI below
 *   [9]      channel count of the active FHSS domain
 *   [10:13]  centre frequency of channel 0, kHz, big endian
 *   [14:15]  spacing between channel centres, kHz, big endian
 *   [16:17]  samples dropped since the last frame (ring overrun), big endian
 *   [18]     sample count N
 *   [19:]    N records of SURVEY_SAMPLE_BYTES, see below
 *
 * Sample record:
 *   [0] FHSS channel index radio 1 was tuned to
 *   [1] same for radio 2, or SURVEY_CHAN_INVALID when there is no second radio
 *   [2] radio 1 instantaneous RSSI, int8 dBm, or SURVEY_RSSI_INVALID
 *   [3] radio 2 instantaneous RSSI, int8 dBm, or SURVEY_RSSI_INVALID
 *   [4] ACHIEVED offset from packet end, us/4 (clamped at 255 = 1020us)
 *   [5] RSSI of the packet that just ended, int8 dBm -- the wanted-signal
 *       strength this sample's AGC state was set by. AGC carry-over should scale
 *       with this; a fixed delta that does not is a calibration difference.
 *   [6] per-sample flags, see SURVEY_SFLAG_*
 *
 * The achieved offset is measured from ProcessRFPacket() entry, not from the
 * last chip on the air: it excludes the DIO-to-ISR latency, which is a small
 * constant. That constant shifts the whole curve and cannot change its shape,
 * which is what the experiment reads.
 *
 * Big endian to match CRSF. Size budget: an extended frame spends 5 header bytes
 * and a CRC out of CRSF_MAX_PACKET_LEN's 64, so the payload must stay <= 58.
 * SURVEY_MAX_PAYLOAD_BYTES is 54; test_payload_fits_crsf_frame asserts it.
 */

#include <stdint.h>

// Sub-type of this payload, payload[0]. Registry lives in the
// CRSF_FRAMETYPE_ELRS_VENDOR comment; the value is defined here so this codec
// stays free of CRSF includes. 0x01/0x02 belong to SpectrumProtocol.h.
#define SURVEY_SUBTYPE 0x03

#define SURVEY_PROTO_VERSION 1

// Sub-type 0x04: the receiver's answer to an arm/disarm command, sent on every
// command so a command that never landed is distinguishable from one that did.
//   [0] sub-type  [1] version  [2] SURVEY_STATUS_*  [3] offset us/4 in force
//   [4] sample period in ms in force
#define SURVEY_SUBTYPE_STATUS 0x04
#define SURVEY_STATUS_PAYLOAD_BYTES 5

#define SURVEY_STATUS_ARMED 0    // sampling whenever the gates allow it
#define SURVEY_STATUS_DISARMED 1 // hook is a single predicated branch again

#define SURVEY_SAMPLE_BYTES 7
#define SURVEY_HEADER_BYTES 19
// 5 samples keeps the payload at 54 of the 58 available.
#define SURVEY_MAX_SAMPLES_PER_FRAME 5
#define SURVEY_MAX_PAYLOAD_BYTES (SURVEY_HEADER_BYTES + \
                                  SURVEY_SAMPLE_BYTES * SURVEY_MAX_SAMPLES_PER_FRAME)

// Sentinels. INT8_MIN so neither can collide with a real reading; producers
// clamp real values to >= SURVEY_RSSI_INVALID + 1.
#define SURVEY_RSSI_INVALID ((int8_t)-128)
#define SURVEY_CHAN_INVALID 0xFF

// Offsets are carried as us/4, so this is the largest representable one.
#define SURVEY_OFFSET_MAX_US 1020
#define SURVEY_OFFSET_QUANTUM_US 4

enum
{
    SURVEY_FLAG_ARMED = 0x01,       // the runtime enable is on
    SURVEY_FLAG_DUAL_RADIO = 0x02,  // two radios, so rssi2/chan2 are meaningful
    // Why no samples were taken. Any of these set with N == 0 is the receiver
    // reporting a blocked gate rather than going silent.
    SURVEY_FLAG_GATED_LINK = 0x04,    // connectionState != connected
    SURVEY_FLAG_GATED_TIMER = 0x08,   // RXtimerState != tim_locked
    SURVEY_FLAG_GATED_RATE = 0x10,    // not a LoRa air rate (see the header note)
    SURVEY_FLAG_GATED_BINDING = 0x20, // InBindingMode
    // The radio is not running at all: WiFi/BLE mode, or the link-down sweep.
    // Distinct from GATED_LINK because "no RC link" invites you to check the
    // transmitter, and in this state the transmitter is not the problem.
    SURVEY_FLAG_GATED_RADIO = 0x40,
};

enum
{
    SURVEY_SFLAG_PACKET_ON_RADIO2 = 0x01, // the packet arrived on radio 2
    SURVEY_SFLAG_GEMINI = 0x02,           // radios were on different channels
};

/**
 * Describes one frame. The same struct drives both directions, so encode and
 * decode are mirror images and same-typed fields cannot be transposed by
 * accident at the call site.
 */
typedef struct surveyFrameInfo_s
{
    uint8_t version;
    uint8_t flags;
    uint8_t seq;
    uint8_t reqOffsetQus; // requested offset, us/4
    uint8_t enumRate;
    uint16_t bwKhz;
    int8_t lnaGainDb;
    uint8_t channelCount;
    uint32_t startFreqKhz;
    uint16_t stepKhz;
    uint16_t dropped;
    uint8_t sampleCount;
} surveyFrameInfo_t;

typedef struct surveySample_s
{
    uint8_t chan1;
    uint8_t chan2;
    int8_t rssi1;
    int8_t rssi2;
    uint8_t offsetQus; // achieved offset, us/4
    int8_t packetRssi;
    uint8_t flags;
} surveySample_t;

/**
 * Encode a frame.
 *
 * @param payload destination, >= SURVEY_MAX_PAYLOAD_BYTES. Untouched when this
 *                returns 0, so it is safe to encode straight into a frame buffer
 *                past the header.
 * @param samples info->sampleCount records; may be null when sampleCount is 0,
 *                which is the gate-report heartbeat.
 * @param info    in/out; the encoder sets version.
 * @return payload length in bytes, or 0 if info is invalid
 */
static inline uint8_t SurveyEncodeFrame(uint8_t *payload, const surveySample_t *samples,
                                        surveyFrameInfo_t *info)
{
    if (info->sampleCount > SURVEY_MAX_SAMPLES_PER_FRAME)
    {
        return 0;
    }
    if (info->sampleCount != 0 && samples == 0)
    {
        return 0;
    }

    info->version = SURVEY_PROTO_VERSION;

    payload[0] = SURVEY_SUBTYPE;
    payload[1] = info->version;
    payload[2] = info->flags;
    payload[3] = info->seq;
    payload[4] = info->reqOffsetQus;
    payload[5] = info->enumRate;
    payload[6] = (info->bwKhz >> 8) & 0xFF;
    payload[7] = info->bwKhz & 0xFF;
    payload[8] = (uint8_t)info->lnaGainDb;
    payload[9] = info->channelCount;
    payload[10] = (info->startFreqKhz >> 24) & 0xFF;
    payload[11] = (info->startFreqKhz >> 16) & 0xFF;
    payload[12] = (info->startFreqKhz >> 8) & 0xFF;
    payload[13] = info->startFreqKhz & 0xFF;
    payload[14] = (info->stepKhz >> 8) & 0xFF;
    payload[15] = info->stepKhz & 0xFF;
    payload[16] = (info->dropped >> 8) & 0xFF;
    payload[17] = info->dropped & 0xFF;
    payload[18] = info->sampleCount;

    uint8_t *out = &payload[SURVEY_HEADER_BYTES];
    for (uint8_t i = 0; i < info->sampleCount; i++)
    {
        *out++ = samples[i].chan1;
        *out++ = samples[i].chan2;
        *out++ = (uint8_t)samples[i].rssi1;
        *out++ = (uint8_t)samples[i].rssi2;
        *out++ = samples[i].offsetQus;
        *out++ = (uint8_t)samples[i].packetRssi;
        *out++ = samples[i].flags;
    }

    return SURVEY_HEADER_BYTES + SURVEY_SAMPLE_BYTES * info->sampleCount;
}

/**
 * Decode a frame payload.
 *
 * @param samples written with info->sampleCount records; may be null only when
 *                maxSamples is 0 and the frame carries none.
 * @return true if the payload is well formed and the samples fit
 */
static inline bool SurveyDecodeFrame(const uint8_t *payload, const uint8_t payloadLen,
                                     surveyFrameInfo_t *info,
                                     surveySample_t *samples, const uint8_t maxSamples)
{
    if (payloadLen < SURVEY_HEADER_BYTES)
    {
        return false;
    }
    // Sub-type first: this frame type is shared, so a payload that is not ours
    // is a routine occurrence, not a malformed frame.
    if (payload[0] != SURVEY_SUBTYPE)
    {
        return false;
    }
    if (payload[1] != SURVEY_PROTO_VERSION)
    {
        return false;
    }

    const uint8_t count = payload[18];
    if (count > SURVEY_MAX_SAMPLES_PER_FRAME || count > maxSamples)
    {
        return false;
    }
    if (payloadLen != SURVEY_HEADER_BYTES + SURVEY_SAMPLE_BYTES * count)
    {
        return false;
    }

    info->version = payload[1];
    info->flags = payload[2];
    info->seq = payload[3];
    info->reqOffsetQus = payload[4];
    info->enumRate = payload[5];
    info->bwKhz = ((uint16_t)payload[6] << 8) | (uint16_t)payload[7];
    info->lnaGainDb = (int8_t)payload[8];
    info->channelCount = payload[9];
    info->startFreqKhz = ((uint32_t)payload[10] << 24) | ((uint32_t)payload[11] << 16) |
                         ((uint32_t)payload[12] << 8) | (uint32_t)payload[13];
    info->stepKhz = ((uint16_t)payload[14] << 8) | (uint16_t)payload[15];
    info->dropped = ((uint16_t)payload[16] << 8) | (uint16_t)payload[17];
    info->sampleCount = count;

    const uint8_t *in = &payload[SURVEY_HEADER_BYTES];
    for (uint8_t i = 0; i < count; i++)
    {
        samples[i].chan1 = *in++;
        samples[i].chan2 = *in++;
        samples[i].rssi1 = (int8_t)*in++;
        samples[i].rssi2 = (int8_t)*in++;
        samples[i].offsetQus = *in++;
        samples[i].packetRssi = (int8_t)*in++;
        samples[i].flags = *in++;
    }
    return true;
}

/**
 * Centre frequency of an FHSS channel index in kHz. Deliberately identical in
 * form to SpectrumBinFreqKhz(): the survey and the sweep must land on the same
 * axis or the per-channel comparison is meaningless.
 */
static inline uint32_t SurveyChanFreqKhz(const surveyFrameInfo_t *info, const uint8_t chan)
{
    return info->startFreqKhz + (uint32_t)chan * (uint32_t)info->stepKhz;
}
