#pragma once

/*
 * Wire protocol for the experimental TX-side spectrum analyzer
 * (TX_SPECTRUM_SCAN). Pure data layout and codec: no Arduino, driver or CRSF
 * dependencies so it can be unit tested natively and reused by offline
 * analysis tooling and by the handset-side decoder.
 *
 * Direction is TX module -> handset, carried in a
 * CRSF_FRAMETYPE_ELRS_TX_SPECTRUM (0x30) extended frame
 * (dest=CRSF_ADDRESS_RADIO_TRANSMITTER, orig=CRSF_ADDRESS_CRSF_TRANSMITTER).
 *
 * See ../TxSpectrum/DESIGN.md.
 *
 * A frame payload:
 *   [0]     protocol version
 *   [1]     flags, see TX_SPECTRUM_FLAG_*
 *   [2]     sweep sequence (wraps, for loss detection; constant across all
 *           frames belonging to one sweep of one trace)
 *   [3]     bin offset: absolute index of the first bin in this frame
 *   [4]     bin count N in this frame
 *   [5]     total bins in the full sweep
 *   [6:9]   centre frequency of bin 0, kHz, big endian
 *   [10:11] spacing between bin centres, kHz, big endian
 *
 * Big endian, matching CRSF convention (cf. the sibling 0x2E, which uses
 * htobe16). This is not cosmetic: the handset decoder is elrs.lua, which already
 * ships a big-endian reader -- fieldGetValue() -- so BE lets it call that helper
 * instead of hand-rolling a decode that would silently disagree with it.
 *   then N bytes:
 *   [i]     int8 RSSI in dBm for bin (binOffset + i), already corrected for
 *           the target's receive-path gain (power_lna_gain) so the value is
 *           antenna-referred. TX_SPECTRUM_RSSI_INVALID means "not measured".
 *
 * A sweep of totalBins is split across ceil(totalBins / TX_SPECTRUM_MAX_BINS_PER_FRAME)
 * frames; the last one carries TX_SPECTRUM_FLAG_SWEEP_END. The frequency axis
 * is repeated in every frame so a decoder that joins mid-sweep is never
 * left guessing, and so a dropped frame cannot silently shift the axis.
 *
 * Size budget: CRSF_MAX_PACKET_LEN is 64. An extended frame costs
 * sizeof(crsf_ext_header_t)=5 plus a 1 byte CRC, so the payload must stay
 * <= 58 bytes. TX_SPECTRUM_MAX_PAYLOAD_BYTES is 52, leaving margin.
 * test_payload_fits_crsf_frame asserts this against the real CRSF constants.
 */

#include <stdint.h>

#define TX_SPECTRUM_PROTO_VERSION 1

// 40 bins/frame makes an 80 bin 2.4GHz sweep exactly two frames, and a 40
// channel FCC915 sweep exactly one.
#define TX_SPECTRUM_MAX_BINS_PER_FRAME 40
#define TX_SPECTRUM_HEADER_BYTES 12
#define TX_SPECTRUM_MAX_PAYLOAD_BYTES (TX_SPECTRUM_HEADER_BYTES + TX_SPECTRUM_MAX_BINS_PER_FRAME)

// Bins in one sweep == FHSS channels in the domain, and 80 is the largest
// freq_count in any regulatory domain table (FHSS.cpp: ISM2G4/CE_LBT 80,
// FCC915 40, AU915/US433_WIDE 20). Sized to the real maximum rather than a
// speculative one; a finer-than-FHSS step would bump TX_SPECTRUM_PROTO_VERSION
// anyway. binOffset/totalBins are uint8 on the wire, so this can never exceed 255.
#define TX_SPECTRUM_MAX_BINS 80

// Sentinel for a bin that was not measured (e.g. a sweep aborted part-way).
// INT8_MIN, so it can never collide with a real reading: producers clamp to
// >= TX_SPECTRUM_RSSI_INVALID + 1.
#define TX_SPECTRUM_RSSI_INVALID ((int8_t)-128)

enum
{
    TX_SPECTRUM_FLAG_TRACE_MASK = 0x03, // trace id, see TX_SPECTRUM_TRACE_*
    TX_SPECTRUM_FLAG_SWEEP_END = 0x04,  // last frame of this trace's sweep; set by the encoder

    // Reserved for antenna-compare mode (DESIGN.md 2.4). The bit positions are
    // claimed now so the wire format need not change later, but NOTHING EMITS
    // THEM YET -- do not infer from these that compare mode exists.
    TX_SPECTRUM_FLAG_MODE_COMPARE = 0x08, // 0 = split-band sweep, 1 = antenna compare
    TX_SPECTRUM_FLAG_RADIO_2 = 0x10,      // compare mode: this trace is radio 2's
};

enum
{
    TX_SPECTRUM_TRACE_LIVE = 0,    // most recent sweep
    TX_SPECTRUM_TRACE_MAXHOLD = 1, // running per-bin maximum since the last reset
};

/**
 * Describes one frame. The same struct drives both directions, so encode and
 * decode are mirror images and same-typed fields cannot be transposed by
 * accident at the call site.
 *
 * Encoding: caller sets flags (trace id), sweepSeq, binOffset, totalBins,
 * startFreqKhz and stepKhz; the encoder sets version, binCount, and the
 * SWEEP_END bit of flags.
 *
 * Decoding: every field is an output.
 */
typedef struct txSpectrumFrameInfo_s
{
    uint8_t version;
    uint8_t flags;
    uint8_t sweepSeq;
    uint8_t binOffset;
    uint8_t binCount;
    uint8_t totalBins;
    uint32_t startFreqKhz;
    uint16_t stepKhz;
} txSpectrumFrameInfo_t;

/**
 * Encode one frame's worth of bins, starting at info->binOffset.
 *
 * TX_SPECTRUM_FLAG_SWEEP_END is set by this function, not by the caller: it is
 * derived from whether this frame reaches totalBins, so it cannot disagree with
 * the data. Any SWEEP_END bit in info->flags on entry is ignored.
 *
 * @param payload destination, >= TX_SPECTRUM_MAX_PAYLOAD_BYTES. Untouched when
 *                this returns 0, so it is safe to encode straight into a frame
 *                buffer past the header.
 * @param bins    the full sweep, info->totalBins entries; reads the window
 *                [binOffset, binOffset + info->binCount)
 * @param info    in/out, see above. info->binCount is the number of bins
 *                encoded (0 on error).
 * @return payload length in bytes, or 0 if info is invalid
 */
static inline uint8_t TxSpectrumEncodeFrame(uint8_t *payload, const int8_t *bins,
                                            txSpectrumFrameInfo_t *info)
{
    info->binCount = 0;
    if (info->totalBins == 0 || info->totalBins > TX_SPECTRUM_MAX_BINS)
    {
        return 0;
    }
    if (info->binOffset >= info->totalBins)
    {
        return 0;
    }

    uint8_t count = info->totalBins - info->binOffset;
    if (count > TX_SPECTRUM_MAX_BINS_PER_FRAME)
    {
        count = TX_SPECTRUM_MAX_BINS_PER_FRAME;
    }

    info->version = TX_SPECTRUM_PROTO_VERSION;
    info->binCount = count;
    info->flags &= (uint8_t)~TX_SPECTRUM_FLAG_SWEEP_END;
    if ((uint16_t)info->binOffset + count >= info->totalBins)
    {
        info->flags |= TX_SPECTRUM_FLAG_SWEEP_END;
    }

    payload[0] = info->version;
    payload[1] = info->flags;
    payload[2] = info->sweepSeq;
    payload[3] = info->binOffset;
    payload[4] = info->binCount;
    payload[5] = info->totalBins;
    payload[6] = (info->startFreqKhz >> 24) & 0xFF;
    payload[7] = (info->startFreqKhz >> 16) & 0xFF;
    payload[8] = (info->startFreqKhz >> 8) & 0xFF;
    payload[9] = info->startFreqKhz & 0xFF;
    payload[10] = (info->stepKhz >> 8) & 0xFF;
    payload[11] = info->stepKhz & 0xFF;

    uint8_t *out = &payload[TX_SPECTRUM_HEADER_BYTES];
    for (uint8_t i = 0; i < count; i++)
    {
        *out++ = (uint8_t)bins[info->binOffset + i];
    }

    return TX_SPECTRUM_HEADER_BYTES + count;
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
static inline bool TxSpectrumDecodeFrame(const uint8_t *payload, const uint8_t payloadLen,
                                         txSpectrumFrameInfo_t *info,
                                         int8_t *bins, const uint8_t maxBins)
{
    if (payloadLen < TX_SPECTRUM_HEADER_BYTES)
    {
        return false;
    }
    if (payload[0] != TX_SPECTRUM_PROTO_VERSION)
    {
        return false;
    }

    const uint8_t binOffset = payload[3];
    const uint8_t count = payload[4];
    const uint8_t totalBins = payload[5];

    if (count == 0 || count > TX_SPECTRUM_MAX_BINS_PER_FRAME || count > maxBins)
    {
        return false;
    }
    if (totalBins == 0 || totalBins > TX_SPECTRUM_MAX_BINS)
    {
        return false;
    }
    // The window must lie inside the sweep. Guard in 16 bit so the sum cannot
    // wrap back into range.
    if ((uint16_t)binOffset + (uint16_t)count > (uint16_t)totalBins)
    {
        return false;
    }
    if (payloadLen != TX_SPECTRUM_HEADER_BYTES + count)
    {
        return false;
    }

    info->version = payload[0];
    info->flags = payload[1];
    info->sweepSeq = payload[2];
    info->binOffset = binOffset;
    info->binCount = count;
    info->totalBins = totalBins;
    info->startFreqKhz = ((uint32_t)payload[6] << 24) | ((uint32_t)payload[7] << 16) |
                         ((uint32_t)payload[8] << 8) | (uint32_t)payload[9];
    info->stepKhz = ((uint16_t)payload[10] << 8) | (uint16_t)payload[11];

    const uint8_t *in = &payload[TX_SPECTRUM_HEADER_BYTES];
    for (uint8_t i = 0; i < count; i++)
    {
        bins[i] = (int8_t)*in++;
    }
    return true;
}

/**
 * Centre frequency of an absolute bin index (0 .. totalBins-1) in kHz.
 */
static inline uint32_t TxSpectrumBinFreqKhz(const txSpectrumFrameInfo_t *info, const uint8_t absoluteBin)
{
    return info->startFreqKhz + (uint32_t)absoluteBin * (uint32_t)info->stepKhz;
}
