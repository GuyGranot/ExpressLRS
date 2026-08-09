#pragma once

/*
 * Wire formats for the in-flight passive RF survey (DEBUG_RF_SURVEY): receiver
 * -> flight controller / host. Pure layout and codec with no Arduino, driver or
 * CRSF dependencies, so it can be tested natively and reused by offline tooling.
 *
 * THE 3-BYTE IN-FLIGHT TRANSPORT
 *
 * The survey exports one sample per CRSF link-statistics frame through the
 * three downlink bytes that are dead on a receiver (downlink_RSSI_1,
 * downlink_Link_quality, downlink_SNR), which Betaflight has logged untouched
 * as debug[0..2] under debug_mode = CRSF_LINK_STATISTICS_DOWN since 4.2.
 *
 *   debug[0]  bits 6..0: source A magnitude, 0-126 = -dBm antenna-referred,
 *             127 = unavailable. bit 7: freshness toggle -- flips once per
 *             staged sample. Frames can repeat back to back (forced sends), so
 *             "new sample" is "the toggle changed", not "a frame arrived".
 *   debug[1]  bits 6..0: source B magnitude, same encoding, so the two chains
 *             quantise identically and their delta carries no packing artefact.
 *             bit 7: clean-sample bit -- no wanted packet arrived in the period
 *             this sample was taken (the in-band AGC cross-check).
 *   debug[2]  bits 6..0: FHSS channel index, 127 = no sample yet.
 *             bit 7: on a single-band link, the assignment bit (Gemini
 *             radio-swap parity, or the active antenna on switched diversity);
 *             on a dual-band link, the band bit -- 0 = the index is into the
 *             sub-GHz grid, 1 = into the 2.4 grid. Which grid alternates per
 *             sample, so each band joins at half the export cadence.
 *
 * Source A is radio 1 (the sub-GHz radio on a dual-band link), source B is
 * radio 2. Both magnitudes are present in every sample regardless of which
 * band's index debug[2] carries.
 *
 * THE 0x83 BENCH FRAMES
 *
 * For bench validation the same samples can also be streamed in full fidelity
 * over the FC UART, carried in a CRSF_FRAMETYPE_ELRS_VENDOR extended frame,
 * sub-type 0x03 (data) and 0x04 (status). The frame type is shared -- see
 * include/crsf_protocol.h and SpectrumProtocol.h, which owns 0x01 and 0x02.
 *
 * Levels everywhere are antenna-referred via power_lna_gain but uncalibrated in
 * absolute terms, and are comparable to a default (wide RBW) spectrum sweep of
 * the same band because sampling is gated to LoRa air rates, whose bandwidth
 * equals that RBW. bwKhz carries the link's own bandwidth so a consumer can
 * check that rather than trust it.
 *
 * Data frame payload:
 *   [0]      sub-type, SURVEY_SUBTYPE
 *   [1]      protocol version (of everything from [2] on)
 *   [2]      flags, see SURVEY_FLAG_*. Reports the gates, so a frame carrying
 *            zero samples still says WHY none were taken.
 *   [3]      frame sequence (wraps; a gap means frames were lost on the wire)
 *   [4]      enum_rate of the live link (comparable across targets; index is not)
 *   [5:6]    link sensing bandwidth, kHz, big endian
 *   [7]      power_lna_gain in dB (int8), already subtracted from every RSSI below
 *   [8]      channel count of the effective FHSS domain (a configured band
 *            subset shifts channel 0 and shrinks this count)
 *   [9:12]   centre frequency of channel 0, kHz, big endian
 *   [13:14]  spacing between channel centres, kHz, big endian
 *   [15:18]  centre frequency of channel 0 in the SECOND band, kHz, big endian,
 *            or 0 when the link occupies one band. Only a dual-band (cross-band
 *            LR1121) link has two: radio 1 hops the sub-GHz grid above while
 *            radio 2 hops this one, both driven by one shared sequence pointer.
 *   [19:20]  spacing of the second band's channel centres, kHz, big endian
 *   [21]     channel count of the second band, 0 when there is none
 *   [22:23]  samples dropped since the last frame (ring overrun), big endian
 *   [24]     sample count N
 *   [25:]    N records of SURVEY_SAMPLE_BYTES, see below
 *
 * Sample record:
 *   [0] FHSS channel index radio 1 was tuned to
 *   [1] same for radio 2: an index into the second-band axis when the frame
 *       carries one, the primary axis otherwise, or SURVEY_CHAN_INVALID when
 *       there is no second radio or nothing valid to report for it
 *   [2] radio 1 instantaneous RSSI, int8 dBm, or SURVEY_RSSI_INVALID
 *   [3] radio 2 instantaneous RSSI, int8 dBm, or SURVEY_RSSI_INVALID
 *   [4] per-sample flags, see SURVEY_SFLAG_*
 *
 * Big endian to match CRSF. Size budget: an extended frame spends 5 header bytes
 * and a CRC out of CRSF_MAX_PACKET_LEN's 64, so the payload must stay <= 58.
 * SURVEY_MAX_PAYLOAD_BYTES is 50; test_payload_fits_crsf_frame asserts it.
 */

#include <stdint.h>

// Sub-type of this payload, payload[0]. Registry lives in the
// CRSF_FRAMETYPE_ELRS_VENDOR comment; the value is defined here so this codec
// stays free of CRSF includes. 0x01/0x02 belong to SpectrumProtocol.h.
#define SURVEY_SUBTYPE 0x03

#define SURVEY_PROTO_VERSION 1

// Sub-type 0x04: sent in answer to every bench command (so a command that never
// landed is distinguishable from one that did) and periodically while streaming.
//   [0] sub-type  [1] version  [2] survey mode in force, 0 = disarmed
//   [3] export interval in ms, 0 while disarmed
//   [4:5] worst-case us the sampler has added to a tock, big endian
//   [6] coverage generation counter
#define SURVEY_SUBTYPE_STATUS 0x04
#define SURVEY_STATUS_PAYLOAD_BYTES 7

#define SURVEY_SAMPLE_BYTES 5
#define SURVEY_HEADER_BYTES 25
// 5 samples keeps the payload at 50 of the 58 available.
#define SURVEY_MAX_SAMPLES_PER_FRAME 5
#define SURVEY_MAX_PAYLOAD_BYTES (SURVEY_HEADER_BYTES + \
                                  SURVEY_SAMPLE_BYTES * SURVEY_MAX_SAMPLES_PER_FRAME)

// Sentinels. INT8_MIN so neither can collide with a real reading; producers
// clamp real values to >= SURVEY_RSSI_INVALID + 1.
#define SURVEY_RSSI_INVALID ((int8_t)-128)
#define SURVEY_CHAN_INVALID 0xFF

enum
{
    SURVEY_FLAG_DUAL_RADIO = 0x01, // two radios, so rssi2/chan2 are meaningful
    // Why no samples were taken. Any of these set with N == 0 is the receiver
    // reporting a blocked gate rather than going silent.
    SURVEY_FLAG_GATED_LINK = 0x02,    // connectionState != connected
    SURVEY_FLAG_GATED_TIMER = 0x04,   // RXtimerState != tim_locked
    SURVEY_FLAG_GATED_RATE = 0x08,    // not a LoRa air rate (see the header note)
    SURVEY_FLAG_GATED_BINDING = 0x10, // InBindingMode
    // The radio is not running at all: WiFi/BLE mode, or a link-down sweep.
    // Distinct from GATED_LINK because "no RC link" invites you to check the
    // transmitter, and in this state the transmitter is not the problem.
    SURVEY_FLAG_GATED_RADIO = 0x20,
    // Uplink LQ is below the sampling threshold -- the link works but is
    // struggling, and the survey stands aside.
    SURVEY_FLAG_GATED_LQ = 0x40,
};

enum
{
    SURVEY_SFLAG_PACKET_ON_RADIO2 = 0x01, // the packet arrived on radio 2
    // No wanted packet arrived in the period this sample was taken, so it is
    // free of the TX's own energy and of any AGC step it caused. The in-flight
    // cross-check: clean and post-packet populations agreeing per channel means
    // no AGC bias on this airframe at this rate.
    SURVEY_SFLAG_CLEAN = 0x02,
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
    uint8_t enumRate;
    uint16_t bwKhz;
    int8_t lnaGainDb;
    uint8_t channelCount;
    uint32_t startFreqKhz;
    uint16_t stepKhz;
    uint32_t startFreqKhz2; // 0 when the link occupies one band
    uint16_t stepKhz2;
    uint8_t channelCount2; // 0 when the link occupies one band
    uint16_t dropped;
    uint8_t sampleCount;
} surveyFrameInfo_t;

typedef struct surveySample_s
{
    uint8_t chan1;
    uint8_t chan2;
    int8_t rssi1;
    int8_t rssi2;
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
    if (info->sampleCount != 0 && samples == nullptr)
    {
        return 0;
    }

    info->version = SURVEY_PROTO_VERSION;

    payload[0] = SURVEY_SUBTYPE;
    payload[1] = info->version;
    payload[2] = info->flags;
    payload[3] = info->seq;
    payload[4] = info->enumRate;
    payload[5] = (info->bwKhz >> 8) & 0xFF;
    payload[6] = info->bwKhz & 0xFF;
    payload[7] = (uint8_t)info->lnaGainDb;
    payload[8] = info->channelCount;
    payload[9] = (info->startFreqKhz >> 24) & 0xFF;
    payload[10] = (info->startFreqKhz >> 16) & 0xFF;
    payload[11] = (info->startFreqKhz >> 8) & 0xFF;
    payload[12] = info->startFreqKhz & 0xFF;
    payload[13] = (info->stepKhz >> 8) & 0xFF;
    payload[14] = info->stepKhz & 0xFF;
    payload[15] = (info->startFreqKhz2 >> 24) & 0xFF;
    payload[16] = (info->startFreqKhz2 >> 16) & 0xFF;
    payload[17] = (info->startFreqKhz2 >> 8) & 0xFF;
    payload[18] = info->startFreqKhz2 & 0xFF;
    payload[19] = (info->stepKhz2 >> 8) & 0xFF;
    payload[20] = info->stepKhz2 & 0xFF;
    payload[21] = info->channelCount2;
    payload[22] = (info->dropped >> 8) & 0xFF;
    payload[23] = info->dropped & 0xFF;
    payload[24] = info->sampleCount;

    uint8_t *out = &payload[SURVEY_HEADER_BYTES];
    for (uint8_t i = 0; i < info->sampleCount; i++)
    {
        *out++ = samples[i].chan1;
        *out++ = samples[i].chan2;
        *out++ = (uint8_t)samples[i].rssi1;
        *out++ = (uint8_t)samples[i].rssi2;
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

    const uint8_t count = payload[24];
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
    info->enumRate = payload[4];
    info->bwKhz = ((uint16_t)payload[5] << 8) | (uint16_t)payload[6];
    info->lnaGainDb = (int8_t)payload[7];
    info->channelCount = payload[8];
    info->startFreqKhz = ((uint32_t)payload[9] << 24) | ((uint32_t)payload[10] << 16) |
                         ((uint32_t)payload[11] << 8) | (uint32_t)payload[12];
    info->stepKhz = ((uint16_t)payload[13] << 8) | (uint16_t)payload[14];
    info->startFreqKhz2 = ((uint32_t)payload[15] << 24) | ((uint32_t)payload[16] << 16) |
                          ((uint32_t)payload[17] << 8) | (uint32_t)payload[18];
    info->stepKhz2 = ((uint16_t)payload[19] << 8) | (uint16_t)payload[20];
    info->channelCount2 = payload[21];
    info->dropped = ((uint16_t)payload[22] << 8) | (uint16_t)payload[23];
    info->sampleCount = count;

    const uint8_t *in = &payload[SURVEY_HEADER_BYTES];
    for (uint8_t i = 0; i < count; i++)
    {
        samples[i].chan1 = *in++;
        samples[i].chan2 = *in++;
        samples[i].rssi1 = (int8_t)*in++;
        samples[i].rssi2 = (int8_t)*in++;
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

/** Same, on the second band's axis. Meaningless when startFreqKhz2 is 0. */
static inline uint32_t SurveyChan2FreqKhz(const surveyFrameInfo_t *info, const uint8_t chan)
{
    return info->startFreqKhz2 + (uint32_t)chan * (uint32_t)info->stepKhz2;
}

// Sentinels of the 3-byte transport (layout in the header comment above).
#define SURVEY_DBG_MAG_INVALID 127
#define SURVEY_DBG_CHAN_NONE 127

/** One decoded sample of the 3-byte transport; drives both pack and unpack. */
typedef struct surveyDebug_s
{
    uint8_t magA; // 0-126, or SURVEY_DBG_MAG_INVALID
    uint8_t magB;
    uint8_t chan; // 0-126, or SURVEY_DBG_CHAN_NONE
    bool toggle;
    bool clean;
    bool bit7; // assignment bit or band bit, see the header comment
} surveyDebug_t;

/** Antenna-referred dBm -> transport magnitude. -100 dBm packs as 100. */
static inline uint8_t SurveyDbgMagFromDbm(int16_t dbm)
{
    if (dbm > 0)
    {
        dbm = 0;
    }
    if (dbm < -126)
    {
        dbm = -126;
    }
    return (uint8_t)-dbm;
}

static inline void SurveyPackDebug(uint8_t out[3], const surveyDebug_t *in)
{
    out[0] = (uint8_t)((in->magA & 0x7F) | (in->toggle ? 0x80 : 0));
    out[1] = (uint8_t)((in->magB & 0x7F) | (in->clean ? 0x80 : 0));
    out[2] = (uint8_t)((in->chan & 0x7F) | (in->bit7 ? 0x80 : 0));
}

static inline void SurveyUnpackDebug(const uint8_t in[3], surveyDebug_t *out)
{
    out->magA = in[0] & 0x7F;
    out->toggle = (in[0] & 0x80) != 0;
    out->magB = in[1] & 0x7F;
    out->clean = (in[1] & 0x80) != 0;
    out->chan = in[2] & 0x7F;
    out->bit7 = (in[2] & 0x80) != 0;
}
