#pragma once

/*
 * Wire formats for the in-flight passive RF survey (DEBUG_RF_SURVEY): receiver
 * -> flight controller / host. No Arduino, driver or CRSF dependencies, so the
 * codec runs natively and in offline tooling.
 *
 * In flight, one sample rides each CRSF link-statistics frame in the three
 * unused downlink bytes, logged by Betaflight as debug[0..2] under
 * debug_mode = CRSF_LINK_STATISTICS_DOWN; surveyDebug_t below is the layout.
 * On the bench the same samples stream in full fidelity over the FC UART as
 * CRSF_FRAMETYPE_ELRS_VENDOR sub-types 0x03 (data) and 0x04 (status);
 * 0x01/0x02 are reserved. Multi-byte fields are big endian to match
 * CRSF; the data frame layout is SurveyEncodeFrame() itself. Levels are
 * antenna-referred via power_lna_gain and uncalibrated in absolute terms.
 */

#include <stdint.h>

#define SURVEY_SUBTYPE 0x03 // payload[0] of the shared vendor frame

#define SURVEY_PROTO_VERSION 1

// Status, sent in answer to every bench command and periodically while
// streaming: [0] sub-type, [1] version, [2] mode (0 = disarmed), [3] export
// interval ms, [4:5] worst-case tock us big endian, [6] coverage generation.
#define SURVEY_SUBTYPE_STATUS 0x04
#define SURVEY_STATUS_PAYLOAD_BYTES 7

#define SURVEY_SAMPLE_BYTES 5
#define SURVEY_HEADER_BYTES 25
// 5 samples keeps the payload at 50 of the 58 a CRSF extended frame allows.
#define SURVEY_MAX_SAMPLES_PER_FRAME 5
#define SURVEY_MAX_PAYLOAD_BYTES (SURVEY_HEADER_BYTES + \
                                  SURVEY_SAMPLE_BYTES * SURVEY_MAX_SAMPLES_PER_FRAME)

#define SURVEY_RSSI_INVALID ((int8_t)-128) // producers clamp real readings above it
#define SURVEY_CHAN_INVALID 0xFF

enum
{
    SURVEY_FLAG_DUAL_RADIO = 0x01, // two radios, so rssi2/chan2 are meaningful
    // Gate flags: any of these set with N == 0 reports why no samples were taken.
    SURVEY_FLAG_GATED_LINK = 0x02,    // connectionState != connected
    SURVEY_FLAG_GATED_TIMER = 0x04,   // RXtimerState != tim_locked
    SURVEY_FLAG_GATED_RATE = 0x08,    // not a LoRa air rate
    SURVEY_FLAG_GATED_BINDING = 0x10, // InBindingMode
    SURVEY_FLAG_GATED_RADIO = 0x20,   // radio not running: WiFi/BLE mode
    SURVEY_FLAG_GATED_LQ = 0x40,      // uplink LQ below the sampling threshold
};

enum
{
    SURVEY_SFLAG_PACKET_ON_RADIO2 = 0x01, // the packet arrived on radio 2
    // no wanted packet in the sample period, so free of TX energy and AGC carry-over
    SURVEY_SFLAG_CLEAN = 0x02,
};

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

// Returns the payload length, or 0 (payload untouched) if info is invalid.
// samples may be null when sampleCount is 0 -- the gate-report heartbeat.
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

// Returns true if the payload is a well-formed survey frame and the samples fit.
static inline bool SurveyDecodeFrame(const uint8_t *payload, const uint8_t payloadLen,
                                     surveyFrameInfo_t *info,
                                     surveySample_t *samples, const uint8_t maxSamples)
{
    if (payloadLen < SURVEY_HEADER_BYTES)
    {
        return false;
    }
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

// Channel centre in kHz from the frame's start/step axis.
static inline uint32_t SurveyChanFreqKhz(const surveyFrameInfo_t *info, const uint8_t chan)
{
    return info->startFreqKhz + (uint32_t)chan * (uint32_t)info->stepKhz;
}

// Same, on the second band's axis; meaningless when startFreqKhz2 is 0.
static inline uint32_t SurveyChan2FreqKhz(const surveyFrameInfo_t *info, const uint8_t chan)
{
    return info->startFreqKhz2 + (uint32_t)chan * (uint32_t)info->stepKhz2;
}

#define SURVEY_DBG_MAG_INVALID 127
#define SURVEY_DBG_CHAN_NONE 127

// One sample of the 3-byte transport; drives both pack and unpack.
typedef struct surveyDebug_s
{
    uint8_t magA; // source A (radio 1) magnitude, 0-126 = -dBm, 127 = invalid
    uint8_t magB; // source B (radio 2), same encoding
    uint8_t chan; // FHSS channel index, 127 = none
    bool toggle;  // freshness, flipped once per staged sample
    bool clean;   // no wanted packet in the sample period
    bool bit7;    // assignment bit; the band bit on a dual-band link
} surveyDebug_t;

// Antenna-referred dBm -> transport magnitude; -100 dBm packs as 100.
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
