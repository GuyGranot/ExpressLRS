#pragma once

/*
 * Spectrum analyzer wire format, TX module -> handset or receiver -> host,
 * carried in a CRSF_FRAMETYPE_ELRS_VENDOR frame; multi-byte fields big endian.
 * No Arduino, driver or CRSF dependencies, so the codec runs in native tests
 * and tooling.
 */

#include <stdint.h>

#define SPECTRUM_SUBTYPE 0x01

#define SPECTRUM_PROTO_VERSION 1

// Sub-type 0x02: the receiver's answer to a scan trigger, sent on every
// trigger whether accepted or not, so a refusal is never silent
#define SPECTRUM_SUBTYPE_STATUS 0x02
#define SPECTRUM_STATUS_PAYLOAD_BYTES 3

#define SPECTRUM_STATUS_ACCEPTED 0       // sweeping, or restarted if already sweeping
#define SPECTRUM_STATUS_REFUSED_LINKED 1 // RC link is up; this is a bench diagnostic
#define SPECTRUM_STATUS_RADIO_FAILED 2   // radio init failed; nothing was measured

// 40 bins/frame: an 80 bin 2.4GHz sweep is exactly two frames, a 40 channel FCC915 sweep one
#define SPECTRUM_MAX_BINS_PER_FRAME 40
#define SPECTRUM_HEADER_BYTES 15
#define SPECTRUM_MAX_PAYLOAD_BYTES (SPECTRUM_HEADER_BYTES + SPECTRUM_MAX_BINS_PER_FRAME)

// Largest freq_count in any regulatory domain table (FHSS.cpp)
#define SPECTRUM_MAX_BINS 80

// Not-measured sentinel; producers clamp real readings to >= -127
#define SPECTRUM_RSSI_INVALID ((int8_t)-128)

enum
{
    SPECTRUM_FLAG_TRACE_MASK = 0x03, // trace id, see SPECTRUM_TRACE_*
    SPECTRUM_FLAG_SWEEP_END = 0x04,  // last frame of this trace's sweep; set by the encoder

    // Compare mode shares one axis across two traces, so trace identity is
    // (axis, RADIO_2) rather than axis alone
    SPECTRUM_FLAG_MODE_COMPARE = 0x08,
    SPECTRUM_FLAG_RADIO_2 = 0x10,
};

enum
{
    SPECTRUM_TRACE_LIVE = 0,
    SPECTRUM_TRACE_MAXHOLD = 1, // running per-bin maximum since the last reset
};

// Drives both encode and decode. When encoding, the encoder owns version,
// binCount and the SWEEP_END flag; the caller sets everything else.
typedef struct spectrumFrameInfo_s
{
    uint8_t version;
    uint8_t flags;
    uint8_t sweepSeq;
    uint8_t binOffset;
    uint8_t binCount;
    uint8_t totalBins;
    uint32_t startFreqKhz;
    uint16_t stepKhz;
    uint16_t rbwKhz; // sensing bandwidth, 0 if unknown
} spectrumFrameInfo_t;

// Encode one frame of bins starting at info->binOffset. SWEEP_END is derived,
// never taken from the caller. Returns 0 (payload untouched) when info is invalid.
static inline uint8_t SpectrumEncodeFrame(uint8_t *payload, const int8_t *bins,
                                          spectrumFrameInfo_t *info)
{
    info->binCount = 0;
    if (info->totalBins == 0 || info->totalBins > SPECTRUM_MAX_BINS)
    {
        return 0;
    }
    if (info->binOffset >= info->totalBins)
    {
        return 0;
    }

    uint8_t count = info->totalBins - info->binOffset;
    if (count > SPECTRUM_MAX_BINS_PER_FRAME)
    {
        count = SPECTRUM_MAX_BINS_PER_FRAME;
    }

    info->version = SPECTRUM_PROTO_VERSION;
    info->binCount = count;
    info->flags &= (uint8_t)~SPECTRUM_FLAG_SWEEP_END;
    if ((uint16_t)info->binOffset + count >= info->totalBins)
    {
        info->flags |= SPECTRUM_FLAG_SWEEP_END;
    }

    payload[0] = SPECTRUM_SUBTYPE;
    payload[1] = info->version;
    payload[2] = info->flags;
    payload[3] = info->sweepSeq;
    payload[4] = info->binOffset;
    payload[5] = info->binCount;
    payload[6] = info->totalBins;
    payload[7] = (info->startFreqKhz >> 24) & 0xFF;
    payload[8] = (info->startFreqKhz >> 16) & 0xFF;
    payload[9] = (info->startFreqKhz >> 8) & 0xFF;
    payload[10] = info->startFreqKhz & 0xFF;
    payload[11] = (info->stepKhz >> 8) & 0xFF;
    payload[12] = info->stepKhz & 0xFF;
    payload[13] = (info->rbwKhz >> 8) & 0xFF;
    payload[14] = info->rbwKhz & 0xFF;

    uint8_t *out = &payload[SPECTRUM_HEADER_BYTES];
    for (uint8_t i = 0; i < count; i++)
    {
        *out++ = (uint8_t)bins[info->binOffset + i];
    }

    return SPECTRUM_HEADER_BYTES + count;
}

// Decode a frame payload. Bins land frame-relative in bins[0 .. binCount);
// use info->binOffset to place them in the sweep.
static inline bool SpectrumDecodeFrame(const uint8_t *payload, const uint8_t payloadLen,
                                       spectrumFrameInfo_t *info,
                                       int8_t *bins, const uint8_t maxBins)
{
    if (payloadLen < SPECTRUM_HEADER_BYTES)
    {
        return false;
    }
    // A foreign sub-type is routine, not malformed: the frame type is shared
    if (payload[0] != SPECTRUM_SUBTYPE)
    {
        return false;
    }
    if (payload[1] != SPECTRUM_PROTO_VERSION)
    {
        return false;
    }

    const uint8_t binOffset = payload[4];
    const uint8_t count = payload[5];
    const uint8_t totalBins = payload[6];

    if (count == 0 || count > SPECTRUM_MAX_BINS_PER_FRAME || count > maxBins)
    {
        return false;
    }
    if (totalBins == 0 || totalBins > SPECTRUM_MAX_BINS)
    {
        return false;
    }
    // 16 bit so the sum cannot wrap back into range
    if ((uint16_t)binOffset + (uint16_t)count > (uint16_t)totalBins)
    {
        return false;
    }
    if (payloadLen != SPECTRUM_HEADER_BYTES + count)
    {
        return false;
    }

    info->version = payload[1];
    info->flags = payload[2];
    info->sweepSeq = payload[3];
    info->binOffset = binOffset;
    info->binCount = count;
    info->totalBins = totalBins;
    info->startFreqKhz = ((uint32_t)payload[7] << 24) | ((uint32_t)payload[8] << 16) |
                         ((uint32_t)payload[9] << 8) | (uint32_t)payload[10];
    info->stepKhz = ((uint16_t)payload[11] << 8) | (uint16_t)payload[12];
    info->rbwKhz = ((uint16_t)payload[13] << 8) | (uint16_t)payload[14];

    const uint8_t *in = &payload[SPECTRUM_HEADER_BYTES];
    for (uint8_t i = 0; i < count; i++)
    {
        bins[i] = (int8_t)*in++;
    }
    return true;
}

// Centre frequency of an absolute bin index in kHz
static inline uint32_t SpectrumBinFreqKhz(const spectrumFrameInfo_t *info, const uint8_t absoluteBin)
{
    return info->startFreqKhz + (uint32_t)absoluteBin * (uint32_t)info->stepKhz;
}
