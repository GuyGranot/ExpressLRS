#pragma once

/*
 * Wire format for the spectrum analyzer: TX module -> handset, or receiver ->
 * flight controller / host. Pure layout and codec with no Arduino, driver or
 * CRSF dependencies, so it can be tested natively and reused by the handset
 * decoder and by offline tooling.
 *
 * Carried in a CRSF_FRAMETYPE_ELRS_VENDOR extended frame (that type is shared
 * and provisional, hence the sub-type byte -- see include/crsf_protocol.h).
 *
 * Payload:
 *   [0]     ELRS sub-type, SPECTRUM_SUBTYPE here
 *   [1]     protocol version (of everything from [2] on)
 *   [2]     flags, see SPECTRUM_FLAG_*
 *   [3]     sweep sequence (wraps; constant across one sweep of one trace)
 *   [4]     bin offset: absolute index of the first bin in this frame
 *   [5]     bin count N in this frame
 *   [6]     total bins in the full sweep
 *   [7:10]  centre frequency of bin 0, kHz, big endian
 *   [11:12] spacing between bin centres, kHz, big endian
 *   [13:14] sensing bandwidth, kHz, big endian; 0 when unknown. Traces taken
 *           at different bandwidths are not comparable, so a trace carries its own.
 *   [15:]   N int8 RSSI values in dBm, bins binOffset..binOffset+N-1, corrected
 *           for the target's receive-path gain (power_lna_gain) so they are
 *           antenna-referred. Uncalibrated: the absolute level still depends
 *           on the receive chain and antenna, so readings are relative --
 *           comparable within one target, not across targets or against a lab
 *           instrument. SPECTRUM_RSSI_INVALID = not measured.
 *
 * Big endian to match CRSF and elrs.lua's existing fieldGetValue() reader. A
 * sweep spans ceil(totalBins / SPECTRUM_MAX_BINS_PER_FRAME) frames, the last
 * carrying SPECTRUM_FLAG_SWEEP_END. The axis repeats in every frame, so a
 * decoder joining mid-sweep is never guessing and a dropped frame cannot shift it.
 *
 * Size budget: an extended frame spends 5 header bytes and a CRC out of
 * CRSF_MAX_PACKET_LEN's 64, so the payload must stay <= 58.
 * SPECTRUM_MAX_PAYLOAD_BYTES is 55; test_payload_fits_crsf_frame asserts it.
 */

#include <stdint.h>

// ELRS sub-type of this payload, payload[0]. Registry lives in the
// CRSF_FRAMETYPE_ELRS_VENDOR comment; the value is defined here so this codec
// stays free of CRSF includes.
#define SPECTRUM_SUBTYPE 0x01

#define SPECTRUM_PROTO_VERSION 1

// Sub-type 0x02: the receiver's answer to a scan trigger, sent on every
// trigger whether accepted or not, so a refusal is never silent.
//   [0] sub-type   [1] version   [2] one of SPECTRUM_STATUS_*
#define SPECTRUM_SUBTYPE_STATUS 0x02
#define SPECTRUM_STATUS_PAYLOAD_BYTES 3

#define SPECTRUM_STATUS_ACCEPTED 0       // sweeping, or restarted if already sweeping
#define SPECTRUM_STATUS_REFUSED_LINKED 1 // RC link is up; this is a bench diagnostic
#define SPECTRUM_STATUS_RADIO_FAILED 2   // radio init failed; nothing was measured

// 40 bins/frame makes an 80 bin 2.4GHz sweep exactly two frames, and a 40
// channel FCC915 sweep exactly one.
#define SPECTRUM_MAX_BINS_PER_FRAME 40
#define SPECTRUM_HEADER_BYTES 15
#define SPECTRUM_MAX_PAYLOAD_BYTES (SPECTRUM_HEADER_BYTES + SPECTRUM_MAX_BINS_PER_FRAME)

// Bins in one sweep == FHSS channels in the domain; 80 is the largest
// freq_count in any regulatory domain table (FHSS.cpp). binOffset/totalBins
// are uint8 on the wire, so this can never exceed 255.
#define SPECTRUM_MAX_BINS 80

// Sentinel for a bin that was not measured (e.g. a sweep aborted part-way).
// INT8_MIN, so it can never collide with a real reading: producers clamp to
// >= SPECTRUM_RSSI_INVALID + 1.
#define SPECTRUM_RSSI_INVALID ((int8_t)-128)

enum
{
    SPECTRUM_FLAG_TRACE_MASK = 0x03, // trace id, see SPECTRUM_TRACE_*
    SPECTRUM_FLAG_SWEEP_END = 0x04,  // last frame of this trace's sweep; set by the encoder

    // Antenna compare: both radios sweep the same band, one trace each. The two
    // traces share an axis, so a consumer must treat trace identity as
    // (axis, RADIO_2) rather than axis alone, or it will blend them into one.
    SPECTRUM_FLAG_MODE_COMPARE = 0x08, // 1 = the pair below is an antenna compare
    SPECTRUM_FLAG_RADIO_2 = 0x10,      // compare mode: this trace is radio 2's
};

enum
{
    SPECTRUM_TRACE_LIVE = 0,    // most recent sweep
    SPECTRUM_TRACE_MAXHOLD = 1, // running per-bin maximum since the last reset
};

/**
 * Describes one frame. The same struct drives both directions, so encode and
 * decode are mirror images and same-typed fields cannot be transposed by
 * accident at the call site.
 *
 * Encoding: caller sets flags (trace id), sweepSeq, binOffset, totalBins,
 * startFreqKhz, stepKhz and rbwKhz; the encoder sets version, binCount, and
 * the SWEEP_END bit of flags.
 *
 * Decoding: every field is an output.
 */
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
    uint16_t rbwKhz;  // sensing bandwidth, 0 if unknown
} spectrumFrameInfo_t;

/**
 * Encode one frame's worth of bins, starting at info->binOffset.
 *
 * SPECTRUM_FLAG_SWEEP_END is set by this function, not by the caller: it is
 * derived from whether this frame reaches totalBins, so it cannot disagree
 * with the data. Any SWEEP_END bit in info->flags on entry is ignored.
 *
 * @param payload destination, >= SPECTRUM_MAX_PAYLOAD_BYTES. Untouched when
 *                this returns 0, so it is safe to encode straight into a frame
 *                buffer past the header.
 * @param bins    the full sweep, info->totalBins entries; reads the window
 *                [binOffset, binOffset + info->binCount)
 * @param info    in/out, see above. info->binCount is the number of bins
 *                encoded (0 on error).
 * @return payload length in bytes, or 0 if info is invalid
 */
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

/**
 * Decode a frame payload.
 *
 * Bins are written to bins[0 .. info->binCount), i.e. indexed relative to this
 * frame, not to the sweep. Use info->binOffset to place them.
 *
 * @return true if the payload is well formed and the bins fit in the caller's
 *         buffer
 */
static inline bool SpectrumDecodeFrame(const uint8_t *payload, const uint8_t payloadLen,
                                       spectrumFrameInfo_t *info,
                                       int8_t *bins, const uint8_t maxBins)
{
    if (payloadLen < SPECTRUM_HEADER_BYTES)
    {
        return false;
    }
    // Sub-type first: this frame type is shared, so a payload that is not ours
    // is a routine occurrence, not a malformed frame.
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
    // The window must lie inside the sweep. Guard in 16 bit so the sum cannot
    // wrap back into range.
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

/**
 * Centre frequency of an absolute bin index (0 .. totalBins-1) in kHz.
 */
static inline uint32_t SpectrumBinFreqKhz(const spectrumFrameInfo_t *info, const uint8_t absoluteBin)
{
    return info->startFreqKhz + (uint32_t)absoluteBin * (uint32_t)info->stepKhz;
}
