#include <cstdint>
#include <cstring>
#include <unity.h>

#include "TxSpectrumProtocol.h"

// Deliberately NOT #include <crsf_protocol.h>: it drags lib/CrsfProtocol into
// the native link, and 4.0.1's CRSFEndpoint.cpp uses strlcpy/stpcpy, which the
// Windows (mingw) stdlib lacks -- master added include/native.h for that after
// 4.0.1 was cut. Including it here breaks `pio test -e native` on this branch.
//
// The size budget is therefore asserted where the real constants are already in
// scope and get compiled for every target: a static_assert in TxSpectrum.cpp.
// That check is strictly stronger than one here -- it fires on every firmware
// build, not just when someone runs the native suite.

// The real 2.4GHz FHSS grid (FHSS.cpp:33,47): 2400.4MHz .. 2479.4MHz, 80 channels.
#define G2G4_START_KHZ 2400400u
#define G2G4_STEP_KHZ 1000u
#define G2G4_BINS 80

// The real FCC915 grid (FHSS.cpp:16): 903.5MHz .. 926.9MHz, 40 channels.
#define G915_START_KHZ 903500u
#define G915_STEP_KHZ 600u
#define G915_BINS 40

static void fillRamp(int8_t *bins, const uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        // -100 .. -21 across 80 bins, all plausible dBm and all negative
        bins[i] = (int8_t)(-100 + i);
    }
}

// Build an encoder input block. Only the fields a caller must set are listed;
// the encoder fills version/binCount/SWEEP_END.
static txSpectrumFrameInfo_t mkInfo(const uint8_t totalBins, const uint8_t binOffset,
                                    const uint8_t flags, const uint8_t sweepSeq,
                                    const uint32_t startFreqKhz, const uint16_t stepKhz)
{
    txSpectrumFrameInfo_t info;
    memset(&info, 0, sizeof(info));
    info.totalBins = totalBins;
    info.binOffset = binOffset;
    info.flags = flags;
    info.sweepSeq = sweepSeq;
    info.startFreqKhz = startFreqKhz;
    info.stepKhz = stepKhz;
    return info;
}

/////////////// frame codec ///////////////

void test_frame_roundtrip(void)
{
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);

    uint8_t payload[TX_SPECTRUM_MAX_PAYLOAD_BYTES];
    txSpectrumFrameInfo_t enc = mkInfo(G2G4_BINS, 0, TX_SPECTRUM_TRACE_LIVE, 42,
                                       G2G4_START_KHZ, G2G4_STEP_KHZ);
    const uint8_t len = TxSpectrumEncodeFrame(payload, in, &enc);
    TEST_ASSERT_EQUAL(TX_SPECTRUM_MAX_BINS_PER_FRAME, enc.binCount);
    TEST_ASSERT_EQUAL(TX_SPECTRUM_HEADER_BYTES + TX_SPECTRUM_MAX_BINS_PER_FRAME, len);
    TEST_ASSERT_EQUAL(TX_SPECTRUM_PROTO_VERSION, enc.version); // encoder stamps it

    txSpectrumFrameInfo_t info;
    int8_t out[TX_SPECTRUM_MAX_BINS_PER_FRAME];
    TEST_ASSERT_TRUE(TxSpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));

    TEST_ASSERT_EQUAL(TX_SPECTRUM_PROTO_VERSION, info.version);
    TEST_ASSERT_EQUAL(42, info.sweepSeq);
    TEST_ASSERT_EQUAL(0, info.binOffset);
    TEST_ASSERT_EQUAL(TX_SPECTRUM_MAX_BINS_PER_FRAME, info.binCount);
    TEST_ASSERT_EQUAL(G2G4_BINS, info.totalBins);
    TEST_ASSERT_EQUAL_UINT32(G2G4_START_KHZ, info.startFreqKhz);
    TEST_ASSERT_EQUAL_UINT16(G2G4_STEP_KHZ, info.stepKhz);
    TEST_ASSERT_EQUAL(TX_SPECTRUM_TRACE_LIVE, info.flags & TX_SPECTRUM_FLAG_TRACE_MASK);

    for (uint8_t i = 0; i < enc.binCount; i++)
    {
        TEST_ASSERT_EQUAL_INT8(in[i], out[i]);
    }
}

void test_negative_and_sentinel_rssi(void)
{
    // int8 must survive the uint8 wire round trip, including the sentinel
    int8_t in[4] = {TX_SPECTRUM_RSSI_INVALID, -110, -1, 0};

    uint8_t payload[TX_SPECTRUM_MAX_PAYLOAD_BYTES];
    txSpectrumFrameInfo_t enc = mkInfo(4, 0, 0, 0, 1000, 1);
    const uint8_t len = TxSpectrumEncodeFrame(payload, in, &enc);
    TEST_ASSERT_EQUAL(4, enc.binCount);

    txSpectrumFrameInfo_t info;
    int8_t out[4];
    TEST_ASSERT_TRUE(TxSpectrumDecodeFrame(payload, len, &info, out, 4));
    TEST_ASSERT_EQUAL_INT8(TX_SPECTRUM_RSSI_INVALID, out[0]);
    TEST_ASSERT_EQUAL_INT8(-110, out[1]);
    TEST_ASSERT_EQUAL_INT8(-1, out[2]);
    TEST_ASSERT_EQUAL_INT8(0, out[3]);
}

void test_trace_id_roundtrip(void)
{
    // Only flags the firmware actually emits. MODE_COMPARE/RADIO_2 are reserved
    // bit positions with no emitter (DESIGN.md P6) -- asserting on them here
    // would imply a feature that does not exist.
    int8_t in[2] = {-90, -91};
    uint8_t payload[TX_SPECTRUM_MAX_PAYLOAD_BYTES];

    txSpectrumFrameInfo_t enc = mkInfo(2, 0, TX_SPECTRUM_TRACE_MAXHOLD, 7, 1000, 1);
    const uint8_t len = TxSpectrumEncodeFrame(payload, in, &enc);

    txSpectrumFrameInfo_t info;
    int8_t out[2];
    TEST_ASSERT_TRUE(TxSpectrumDecodeFrame(payload, len, &info, out, 2));
    TEST_ASSERT_EQUAL(TX_SPECTRUM_TRACE_MAXHOLD, info.flags & TX_SPECTRUM_FLAG_TRACE_MASK);
    TEST_ASSERT_TRUE(info.flags & TX_SPECTRUM_FLAG_SWEEP_END);
}

/////////////// sweep splitting ///////////////

void test_multi_frame_sweep_reassembles(void)
{
    // The headline case: an 80 bin 2.4GHz sweep must split into exactly two
    // frames and reassemble byte-identically.
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);

    int8_t reassembled[G2G4_BINS];
    memset(reassembled, 0, sizeof(reassembled));

    uint8_t offset = 0;
    uint8_t frames = 0;
    bool sawEnd = false;

    while (offset < G2G4_BINS)
    {
        uint8_t payload[TX_SPECTRUM_MAX_PAYLOAD_BYTES];
        txSpectrumFrameInfo_t enc = mkInfo(G2G4_BINS, offset, TX_SPECTRUM_TRACE_LIVE, 3,
                                           G2G4_START_KHZ, G2G4_STEP_KHZ);
        const uint8_t len = TxSpectrumEncodeFrame(payload, in, &enc);
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_TRUE(enc.binCount > 0);

        txSpectrumFrameInfo_t info;
        int8_t out[TX_SPECTRUM_MAX_BINS_PER_FRAME];
        TEST_ASSERT_TRUE(TxSpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
        TEST_ASSERT_EQUAL(offset, info.binOffset);
        TEST_ASSERT_EQUAL(3, info.sweepSeq);

        // Every frame repeats the axis, so a decoder joining late is never lost
        TEST_ASSERT_EQUAL_UINT32(G2G4_START_KHZ, info.startFreqKhz);
        TEST_ASSERT_EQUAL_UINT16(G2G4_STEP_KHZ, info.stepKhz);

        for (uint8_t i = 0; i < info.binCount; i++)
        {
            reassembled[info.binOffset + i] = out[i];
        }

        offset += enc.binCount;
        frames++;
        if (info.flags & TX_SPECTRUM_FLAG_SWEEP_END)
        {
            sawEnd = true;
            TEST_ASSERT_EQUAL(G2G4_BINS, offset); // END must mean END
        }
    }

    TEST_ASSERT_EQUAL(2, frames);
    TEST_ASSERT_TRUE(sawEnd);
    TEST_ASSERT_EQUAL_INT8_ARRAY(in, reassembled, G2G4_BINS);
}

void test_sweep_end_only_on_last_frame(void)
{
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);

    uint8_t payload[TX_SPECTRUM_MAX_PAYLOAD_BYTES];
    txSpectrumFrameInfo_t info;
    int8_t out[TX_SPECTRUM_MAX_BINS_PER_FRAME];

    // frame 0 of 2 -> no END
    txSpectrumFrameInfo_t a = mkInfo(G2G4_BINS, 0, 0, 0, G2G4_START_KHZ, G2G4_STEP_KHZ);
    uint8_t len = TxSpectrumEncodeFrame(payload, in, &a);
    TEST_ASSERT_TRUE(TxSpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
    TEST_ASSERT_FALSE(info.flags & TX_SPECTRUM_FLAG_SWEEP_END);

    // frame 1 of 2 -> END
    txSpectrumFrameInfo_t b = mkInfo(G2G4_BINS, 40, 0, 0, G2G4_START_KHZ, G2G4_STEP_KHZ);
    len = TxSpectrumEncodeFrame(payload, in, &b);
    TEST_ASSERT_TRUE(TxSpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
    TEST_ASSERT_TRUE(info.flags & TX_SPECTRUM_FLAG_SWEEP_END);
}

void test_encoder_owns_sweep_end_flag(void)
{
    // The encoder derives SWEEP_END from the data; a caller asserting it on a
    // non-final frame must not be able to corrupt reassembly.
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);

    uint8_t payload[TX_SPECTRUM_MAX_PAYLOAD_BYTES];
    txSpectrumFrameInfo_t enc = mkInfo(G2G4_BINS, 0,
                                       TX_SPECTRUM_FLAG_SWEEP_END, // caller lies
                                       0, G2G4_START_KHZ, G2G4_STEP_KHZ);
    const uint8_t len = TxSpectrumEncodeFrame(payload, in, &enc);

    txSpectrumFrameInfo_t info;
    int8_t out[TX_SPECTRUM_MAX_BINS_PER_FRAME];
    TEST_ASSERT_TRUE(TxSpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
    TEST_ASSERT_FALSE(info.flags & TX_SPECTRUM_FLAG_SWEEP_END);
    TEST_ASSERT_FALSE(enc.flags & TX_SPECTRUM_FLAG_SWEEP_END); // stripped in-place too
}

void test_exactly_full_and_boundary_sweeps(void)
{
    int8_t in[TX_SPECTRUM_MAX_BINS];
    fillRamp(in, TX_SPECTRUM_MAX_BINS_PER_FRAME + 1);

    uint8_t payload[TX_SPECTRUM_MAX_PAYLOAD_BYTES];
    txSpectrumFrameInfo_t info;
    int8_t out[TX_SPECTRUM_MAX_BINS_PER_FRAME];

    // exactly one full frame -> single frame, END set
    txSpectrumFrameInfo_t a = mkInfo(TX_SPECTRUM_MAX_BINS_PER_FRAME, 0, 0, 0, 1000, 1);
    uint8_t len = TxSpectrumEncodeFrame(payload, in, &a);
    TEST_ASSERT_EQUAL(TX_SPECTRUM_MAX_BINS_PER_FRAME, a.binCount);
    TEST_ASSERT_TRUE(TxSpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
    TEST_ASSERT_TRUE(info.flags & TX_SPECTRUM_FLAG_SWEEP_END);

    // one over -> second frame carries a single bin
    txSpectrumFrameInfo_t b = mkInfo(TX_SPECTRUM_MAX_BINS_PER_FRAME + 1,
                                     TX_SPECTRUM_MAX_BINS_PER_FRAME, 0, 0, 1000, 1);
    len = TxSpectrumEncodeFrame(payload, in, &b);
    TEST_ASSERT_EQUAL(1, b.binCount);
    TEST_ASSERT_TRUE(TxSpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
    TEST_ASSERT_TRUE(info.flags & TX_SPECTRUM_FLAG_SWEEP_END);
    TEST_ASSERT_EQUAL_INT8(in[TX_SPECTRUM_MAX_BINS_PER_FRAME], out[0]);
}

/////////////// frequency axis ///////////////

void test_bin_freq_matches_real_fhss_grids(void)
{
    // The axis must land exactly on the FHSS table's freq_stop, or the plot is
    // silently misaligned against the channel grid it is meant to inform.
    txSpectrumFrameInfo_t info;
    info.startFreqKhz = G2G4_START_KHZ;
    info.stepKhz = G2G4_STEP_KHZ;
    TEST_ASSERT_EQUAL_UINT32(2400400u, TxSpectrumBinFreqKhz(&info, 0));
    TEST_ASSERT_EQUAL_UINT32(2440400u, TxSpectrumBinFreqKhz(&info, 40));
    TEST_ASSERT_EQUAL_UINT32(2479400u, TxSpectrumBinFreqKhz(&info, G2G4_BINS - 1)); // == freq_stop

    info.startFreqKhz = G915_START_KHZ;
    info.stepKhz = G915_STEP_KHZ;
    TEST_ASSERT_EQUAL_UINT32(903500u, TxSpectrumBinFreqKhz(&info, 0));
    TEST_ASSERT_EQUAL_UINT32(926900u, TxSpectrumBinFreqKhz(&info, G915_BINS - 1)); // == freq_stop
}

/////////////// size budget ///////////////

void test_payload_size_is_pinned(void)
{
    // Pins the two numbers the rest of the design is calibrated against: 52
    // payload bytes -> 58 on the wire, which is what DESIGN.md 5's "1 frame per
    // 25ms" pacing assumes against EdgeTX's 255-byte Lua queue (~4 frames).
    // Whether 58 still fits a CRSF frame is checked by the static_assert in
    // TxSpectrum.cpp -- see the note at the top of this file.
    TEST_ASSERT_EQUAL(52, TX_SPECTRUM_MAX_PAYLOAD_BYTES);
    TEST_ASSERT_EQUAL(TX_SPECTRUM_HEADER_BYTES + TX_SPECTRUM_MAX_BINS_PER_FRAME,
                      TX_SPECTRUM_MAX_PAYLOAD_BYTES);
}

void test_axis_is_big_endian_on_the_wire(void)
{
    // Pins byte order, which a roundtrip test cannot: encode/decode agree with
    // each other under either endianness. The handset decoder is elrs.lua, which
    // reads these with fieldGetValue() -- a BIG-endian reader. Flip these bytes
    // and the plot silently renders against a garbage frequency axis, with no
    // build error and nothing else failing.
    int8_t in[2] = {-90, -91};
    uint8_t p[TX_SPECTRUM_MAX_PAYLOAD_BYTES];
    txSpectrumFrameInfo_t enc = mkInfo(2, 0, 0, 0, G2G4_START_KHZ, G2G4_STEP_KHZ);
    TEST_ASSERT_TRUE(TxSpectrumEncodeFrame(p, in, &enc) > 0);

    // 2400400 == 0x0024A090, MSB first
    TEST_ASSERT_EQUAL_HEX8(0x00, p[6]);
    TEST_ASSERT_EQUAL_HEX8(0x24, p[7]);
    TEST_ASSERT_EQUAL_HEX8(0xA0, p[8]);
    TEST_ASSERT_EQUAL_HEX8(0x90, p[9]);
    // 1000 == 0x03E8, MSB first
    TEST_ASSERT_EQUAL_HEX8(0x03, p[10]);
    TEST_ASSERT_EQUAL_HEX8(0xE8, p[11]);
}

void test_golden_vector_frame_is_pinned(void)
{
    // Pins frame 1 of lua/mockup/spectrumgolden.lua byte for byte. That file is
    // generated from THIS encoder by lua/mockup/gen_spectrumgolden.cpp, then fed
    // to elrs.lua's hand-rolled decoder in the EdgeTX simulator -- which is the
    // only place the two decoders are ever compared.
    //
    // The golden is checked in, so nothing else notices when it goes stale. If
    // this fails, the wire format moved: regenerate the golden, or the simulator
    // is validating the Lua decoder against bytes no TX would ever send -- a
    // green check over zero coverage, which is worse than no check.
    //
    //   cd src/lua/mockup
    //   g++ -I../../lib/TxSpectrum -o gen_spectrumgolden gen_spectrumgolden.cpp
    //   ./gen_spectrumgolden > spectrumgolden.lua
    int8_t bins[G2G4_BINS];
    for (int i = 0; i < G2G4_BINS; i++)
    {
        bins[i] = (int8_t)(-110 + i); // ramp spanning elrs.lua's plot window
    }
    for (int i = 0; i < 3; i++)
    {
        bins[39 + i] = TX_SPECTRUM_RSSI_INVALID; // the 3-bin notch at bin 40
    }

    uint8_t p[TX_SPECTRUM_MAX_PAYLOAD_BYTES];
    txSpectrumFrameInfo_t enc = mkInfo(G2G4_BINS, 0, TX_SPECTRUM_TRACE_LIVE, 7,
                                       G2G4_START_KHZ, G2G4_STEP_KHZ);
    const uint8_t len = TxSpectrumEncodeFrame(p, bins, &enc);

    // 52 payload bytes + the 2 ext-header addresses = the 54-entry table EdgeTX
    // hands Lua, whose last index is data[54] -- exactly the highest index
    // parseSpectrumMessage reads. Zero slack, by construction.
    TEST_ASSERT_EQUAL(TX_SPECTRUM_HEADER_BYTES + TX_SPECTRUM_MAX_BINS_PER_FRAME, len);

    static const uint8_t expected[] = {
        0x01,                   // version
        0x00,                   // flags: LIVE, and the encoder withheld SWEEP_END
        0x07,                   // sweepSeq
        0x00,                   // binOffset
        0x28,                   // binCount == 40
        0x50,                   // totalBins == 80
        0x00, 0x24, 0xA0, 0x90, // startFreqKhz 2400400, big endian
        0x03, 0xE8,             // stepKhz 1000, big endian
        0x92, 0x93,             // bins 1-2 == -110, -109
    };
    for (size_t i = 0; i < sizeof(expected); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(expected[i], p[i]);
    }

    // Bin 40 is both the notch's first bin and the last bin of frame 1 -- the
    // golden's notch straddles the frame boundary deliberately, so this byte
    // also pins where the split lands. See gen_spectrumgolden.cpp.
    TEST_ASSERT_EQUAL_HEX8(0x80, p[TX_SPECTRUM_HEADER_BYTES + 39]);
}

void test_max_bins_covers_every_fhss_domain(void)
{
    // 80 is the largest freq_count in any domain table (FHSS.cpp). If a domain
    // with more channels is ever added, TX_SPECTRUM_MAX_BINS must grow with it
    // or BeginScan() will silently truncate the sweep.
    //
    // Note this is a PER-BAND bound, and that is deliberate: the design scans one
    // band per screen (DESIGN.md 10.1), so 80 covers the widest view that can
    // exist. A *combined* dual-band plot would need 120 (FCC915 40 + ISM2G4 80)
    // and would lose 40 bins at BeginScan()'s clamp without a sound anywhere.
    TEST_ASSERT_EQUAL(80, TX_SPECTRUM_MAX_BINS);
    TEST_ASSERT_TRUE(G2G4_BINS <= TX_SPECTRUM_MAX_BINS);
    TEST_ASSERT_TRUE(G915_BINS <= TX_SPECTRUM_MAX_BINS);
}

/////////////// malformed input ///////////////

void test_decode_rejects_malformed(void)
{
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);

    uint8_t good[TX_SPECTRUM_MAX_PAYLOAD_BYTES];
    txSpectrumFrameInfo_t enc = mkInfo(G2G4_BINS, 0, 0, 0, G2G4_START_KHZ, G2G4_STEP_KHZ);
    const uint8_t goodLen = TxSpectrumEncodeFrame(good, in, &enc);

    txSpectrumFrameInfo_t info;
    int8_t out[TX_SPECTRUM_MAX_BINS_PER_FRAME];
    uint8_t bad[TX_SPECTRUM_MAX_PAYLOAD_BYTES];

    // sanity: the unmodified frame decodes
    TEST_ASSERT_TRUE(TxSpectrumDecodeFrame(good, goodLen, &info, out, sizeof(out)));

    // Single-byte corruptions. Table-driven so a failure names itself rather
    // than reporting a bare FALSE at one of seven identical-looking lines.
    static const struct
    {
        uint8_t idx;
        uint8_t val;
        const char *why;
    } mutations[] = {
        {0, TX_SPECTRUM_PROTO_VERSION + 1, "wrong protocol version"},
        {4, 0, "zero bins"},
        {4, TX_SPECTRUM_MAX_BINS_PER_FRAME + 1, "binCount over the per-frame cap"},
        {5, 0, "totalBins zero"},
        {5, TX_SPECTRUM_MAX_BINS + 1, "totalBins over the cap"},
        {3, 60, "window runs past the end of the sweep (60+40 > 80)"},
    };

    for (unsigned i = 0; i < sizeof(mutations) / sizeof(mutations[0]); i++)
    {
        memcpy(bad, good, goodLen);
        bad[mutations[i].idx] = mutations[i].val;
        TEST_ASSERT_FALSE_MESSAGE(TxSpectrumDecodeFrame(bad, goodLen, &info, out, sizeof(out)),
                                  mutations[i].why);
    }

    // truncated below the header
    TEST_ASSERT_FALSE(TxSpectrumDecodeFrame(good, TX_SPECTRUM_HEADER_BYTES - 1, &info, out, sizeof(out)));

    // length disagrees with binCount
    TEST_ASSERT_FALSE(TxSpectrumDecodeFrame(good, goodLen - 1, &info, out, sizeof(out)));

    // caller's buffer is smaller than binCount -> must refuse, not overflow
    TEST_ASSERT_FALSE(TxSpectrumDecodeFrame(good, goodLen, &info, out, enc.binCount - 1));

    // offset + count must not wrap uint8 back into a valid-looking range
    memcpy(bad, good, goodLen);
    bad[3] = 250; // 250 + 40 = 290, wraps to 34 in uint8
    bad[5] = 70;
    TEST_ASSERT_FALSE_MESSAGE(TxSpectrumDecodeFrame(bad, goodLen, &info, out, sizeof(out)),
                              "binOffset + binCount must be guarded in 16 bit");
}

void test_encode_rejects_bad_args(void)
{
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);
    uint8_t payload[TX_SPECTRUM_MAX_PAYLOAD_BYTES];

    // zero-length sweep
    txSpectrumFrameInfo_t a = mkInfo(0, 0, 0, 0, 1000, 1);
    TEST_ASSERT_EQUAL(0, TxSpectrumEncodeFrame(payload, in, &a));
    TEST_ASSERT_EQUAL(0, a.binCount);

    // sweep longer than the protocol allows
    txSpectrumFrameInfo_t b = mkInfo(TX_SPECTRUM_MAX_BINS + 1, 0, 0, 0, 1000, 1);
    TEST_ASSERT_EQUAL(0, TxSpectrumEncodeFrame(payload, in, &b));
    TEST_ASSERT_EQUAL(0, b.binCount);

    // offset at or past the end
    txSpectrumFrameInfo_t c = mkInfo(G2G4_BINS, G2G4_BINS, 0, 0, 1000, 1);
    TEST_ASSERT_EQUAL(0, TxSpectrumEncodeFrame(payload, in, &c));
    TEST_ASSERT_EQUAL(0, c.binCount);
}

// Unity setup/teardown
void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_roundtrip);
    RUN_TEST(test_negative_and_sentinel_rssi);
    RUN_TEST(test_trace_id_roundtrip);
    RUN_TEST(test_multi_frame_sweep_reassembles);
    RUN_TEST(test_sweep_end_only_on_last_frame);
    RUN_TEST(test_encoder_owns_sweep_end_flag);
    RUN_TEST(test_exactly_full_and_boundary_sweeps);
    RUN_TEST(test_bin_freq_matches_real_fhss_grids);
    RUN_TEST(test_payload_size_is_pinned);
    RUN_TEST(test_axis_is_big_endian_on_the_wire);
    RUN_TEST(test_golden_vector_frame_is_pinned);
    RUN_TEST(test_max_bins_covers_every_fhss_domain);
    RUN_TEST(test_decode_rejects_malformed);
    RUN_TEST(test_encode_rejects_bad_args);
    UNITY_END();

    return 0;
}
