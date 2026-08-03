#include <cstdint>
#include <cstring>
#include <unity.h>

#include "SpectrumProtocol.h"

// Wire-format header only, no <crsf_protocol.h>: the CRSF size budget is
// asserted by a static_assert in SpectrumSweep.cpp on every firmware build.

// The real 2.4GHz FHSS grid (FHSS.cpp:33,47): 2400.4MHz .. 2479.4MHz, 80 channels
#define G2G4_START_KHZ 2400400u
#define G2G4_STEP_KHZ 1000u
#define G2G4_BINS 80

// The real FCC915 grid (FHSS.cpp:16): 903.5MHz .. 926.9MHz, 40 channels
#define G915_START_KHZ 903500u
#define G915_STEP_KHZ 600u
#define G915_BINS 40

static void fillRamp(int8_t *bins, const uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        bins[i] = (int8_t)(-100 + i);
    }
}

static spectrumFrameInfo_t mkInfo(const uint8_t totalBins, const uint8_t binOffset,
                                    const uint8_t flags, const uint8_t sweepSeq,
                                    const uint32_t startFreqKhz, const uint16_t stepKhz)
{
    spectrumFrameInfo_t info;
    memset(&info, 0, sizeof(info));
    info.totalBins = totalBins;
    info.binOffset = binOffset;
    info.flags = flags;
    info.sweepSeq = sweepSeq;
    info.startFreqKhz = startFreqKhz;
    info.stepKhz = stepKhz;
    return info;
}

void test_frame_roundtrip(void)
{
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);

    uint8_t payload[SPECTRUM_MAX_PAYLOAD_BYTES];
    spectrumFrameInfo_t enc = mkInfo(G2G4_BINS, 0, SPECTRUM_TRACE_LIVE, 42,
                                       G2G4_START_KHZ, G2G4_STEP_KHZ);
    const uint8_t len = SpectrumEncodeFrame(payload, in, &enc);
    TEST_ASSERT_EQUAL(SPECTRUM_MAX_BINS_PER_FRAME, enc.binCount);
    TEST_ASSERT_EQUAL(SPECTRUM_HEADER_BYTES + SPECTRUM_MAX_BINS_PER_FRAME, len);
    TEST_ASSERT_EQUAL(SPECTRUM_PROTO_VERSION, enc.version); // encoder stamps it

    spectrumFrameInfo_t info;
    int8_t out[SPECTRUM_MAX_BINS_PER_FRAME];
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));

    TEST_ASSERT_EQUAL(SPECTRUM_PROTO_VERSION, info.version);
    TEST_ASSERT_EQUAL(42, info.sweepSeq);
    TEST_ASSERT_EQUAL(0, info.binOffset);
    TEST_ASSERT_EQUAL(SPECTRUM_MAX_BINS_PER_FRAME, info.binCount);
    TEST_ASSERT_EQUAL(G2G4_BINS, info.totalBins);
    TEST_ASSERT_EQUAL_UINT32(G2G4_START_KHZ, info.startFreqKhz);
    TEST_ASSERT_EQUAL_UINT16(G2G4_STEP_KHZ, info.stepKhz);
    TEST_ASSERT_EQUAL(SPECTRUM_TRACE_LIVE, info.flags & SPECTRUM_FLAG_TRACE_MASK);

    for (uint8_t i = 0; i < enc.binCount; i++)
    {
        TEST_ASSERT_EQUAL_INT8(in[i], out[i]);
    }
}

void test_negative_and_sentinel_rssi(void)
{
    // int8 must survive the uint8 wire round trip, including the sentinel
    int8_t in[4] = {SPECTRUM_RSSI_INVALID, -110, -1, 0};

    uint8_t payload[SPECTRUM_MAX_PAYLOAD_BYTES];
    spectrumFrameInfo_t enc = mkInfo(4, 0, 0, 0, 1000, 1);
    const uint8_t len = SpectrumEncodeFrame(payload, in, &enc);
    TEST_ASSERT_EQUAL(4, enc.binCount);

    spectrumFrameInfo_t info;
    int8_t out[4];
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(payload, len, &info, out, 4));
    TEST_ASSERT_EQUAL_INT8(SPECTRUM_RSSI_INVALID, out[0]);
    TEST_ASSERT_EQUAL_INT8(-110, out[1]);
    TEST_ASSERT_EQUAL_INT8(-1, out[2]);
    TEST_ASSERT_EQUAL_INT8(0, out[3]);
}

void test_trace_id_roundtrip(void)
{
    int8_t in[2] = {-90, -91};
    uint8_t payload[SPECTRUM_MAX_PAYLOAD_BYTES];

    spectrumFrameInfo_t enc = mkInfo(2, 0, SPECTRUM_TRACE_MAXHOLD, 7, 1000, 1);
    const uint8_t len = SpectrumEncodeFrame(payload, in, &enc);

    spectrumFrameInfo_t info;
    int8_t out[2];
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(payload, len, &info, out, 2));
    TEST_ASSERT_EQUAL(SPECTRUM_TRACE_MAXHOLD, info.flags & SPECTRUM_FLAG_TRACE_MASK);
    TEST_ASSERT_TRUE(info.flags & SPECTRUM_FLAG_SWEEP_END);
}

void test_compare_flags_identify_the_radio(void)
{
    // Compare mode puts two traces on ONE axis, so a decoder keying on the
    // axis alone would blend an antenna pair into one plausible-looking trace
    int8_t in[2] = {-90, -91};
    uint8_t a[SPECTRUM_MAX_PAYLOAD_BYTES];
    uint8_t b[SPECTRUM_MAX_PAYLOAD_BYTES];

    spectrumFrameInfo_t encA = mkInfo(2, 0, SPECTRUM_TRACE_LIVE |
                                        SPECTRUM_FLAG_MODE_COMPARE,
                                        7, G2G4_START_KHZ, G2G4_STEP_KHZ);
    spectrumFrameInfo_t encB = mkInfo(2, 0, SPECTRUM_TRACE_LIVE |
                                        SPECTRUM_FLAG_MODE_COMPARE |
                                        SPECTRUM_FLAG_RADIO_2,
                                        7, G2G4_START_KHZ, G2G4_STEP_KHZ);
    const uint8_t lenA = SpectrumEncodeFrame(a, in, &encA);
    const uint8_t lenB = SpectrumEncodeFrame(b, in, &encB);

    spectrumFrameInfo_t infoA, infoB;
    int8_t outA[2], outB[2];
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(a, lenA, &infoA, outA, 2));
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(b, lenB, &infoB, outB, 2));

    TEST_ASSERT_TRUE(infoA.flags & SPECTRUM_FLAG_MODE_COMPARE);
    TEST_ASSERT_TRUE(infoB.flags & SPECTRUM_FLAG_MODE_COMPARE);
    TEST_ASSERT_FALSE(infoA.flags & SPECTRUM_FLAG_RADIO_2);
    TEST_ASSERT_TRUE(infoB.flags & SPECTRUM_FLAG_RADIO_2);

    TEST_ASSERT_EQUAL_UINT32(infoA.startFreqKhz, infoB.startFreqKhz);
    TEST_ASSERT_EQUAL_UINT16(infoA.stepKhz, infoB.stepKhz);
    TEST_ASSERT_EQUAL(infoA.flags & SPECTRUM_FLAG_TRACE_MASK,
                      infoB.flags & SPECTRUM_FLAG_TRACE_MASK);
    TEST_ASSERT_EQUAL_INT8_ARRAY(outA, outB, 2);
}

void test_multi_frame_sweep_reassembles(void)
{
    // An 80 bin 2.4GHz sweep must split into exactly two frames and reassemble
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);

    int8_t reassembled[G2G4_BINS];
    memset(reassembled, 0, sizeof(reassembled));

    uint8_t offset = 0;
    uint8_t frames = 0;
    bool sawEnd = false;

    while (offset < G2G4_BINS)
    {
        uint8_t payload[SPECTRUM_MAX_PAYLOAD_BYTES];
        spectrumFrameInfo_t enc = mkInfo(G2G4_BINS, offset, SPECTRUM_TRACE_LIVE, 3,
                                           G2G4_START_KHZ, G2G4_STEP_KHZ);
        const uint8_t len = SpectrumEncodeFrame(payload, in, &enc);
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_TRUE(enc.binCount > 0);

        spectrumFrameInfo_t info;
        int8_t out[SPECTRUM_MAX_BINS_PER_FRAME];
        TEST_ASSERT_TRUE(SpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
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
        if (info.flags & SPECTRUM_FLAG_SWEEP_END)
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

    uint8_t payload[SPECTRUM_MAX_PAYLOAD_BYTES];
    spectrumFrameInfo_t info;
    int8_t out[SPECTRUM_MAX_BINS_PER_FRAME];

    // frame 0 of 2 -> no END
    spectrumFrameInfo_t a = mkInfo(G2G4_BINS, 0, 0, 0, G2G4_START_KHZ, G2G4_STEP_KHZ);
    uint8_t len = SpectrumEncodeFrame(payload, in, &a);
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
    TEST_ASSERT_FALSE(info.flags & SPECTRUM_FLAG_SWEEP_END);

    // frame 1 of 2 -> END
    spectrumFrameInfo_t b = mkInfo(G2G4_BINS, 40, 0, 0, G2G4_START_KHZ, G2G4_STEP_KHZ);
    len = SpectrumEncodeFrame(payload, in, &b);
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
    TEST_ASSERT_TRUE(info.flags & SPECTRUM_FLAG_SWEEP_END);
}

void test_encoder_owns_sweep_end_flag(void)
{
    // A caller asserting SWEEP_END on a non-final frame must not corrupt reassembly
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);

    uint8_t payload[SPECTRUM_MAX_PAYLOAD_BYTES];
    spectrumFrameInfo_t enc = mkInfo(G2G4_BINS, 0,
                                       SPECTRUM_FLAG_SWEEP_END, // caller lies
                                       0, G2G4_START_KHZ, G2G4_STEP_KHZ);
    const uint8_t len = SpectrumEncodeFrame(payload, in, &enc);

    spectrumFrameInfo_t info;
    int8_t out[SPECTRUM_MAX_BINS_PER_FRAME];
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
    TEST_ASSERT_FALSE(info.flags & SPECTRUM_FLAG_SWEEP_END);
    TEST_ASSERT_FALSE(enc.flags & SPECTRUM_FLAG_SWEEP_END); // stripped in-place too
}

void test_exactly_full_and_boundary_sweeps(void)
{
    int8_t in[SPECTRUM_MAX_BINS];
    fillRamp(in, SPECTRUM_MAX_BINS_PER_FRAME + 1);

    uint8_t payload[SPECTRUM_MAX_PAYLOAD_BYTES];
    spectrumFrameInfo_t info;
    int8_t out[SPECTRUM_MAX_BINS_PER_FRAME];

    // exactly one full frame -> single frame, END set
    spectrumFrameInfo_t a = mkInfo(SPECTRUM_MAX_BINS_PER_FRAME, 0, 0, 0, 1000, 1);
    uint8_t len = SpectrumEncodeFrame(payload, in, &a);
    TEST_ASSERT_EQUAL(SPECTRUM_MAX_BINS_PER_FRAME, a.binCount);
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
    TEST_ASSERT_TRUE(info.flags & SPECTRUM_FLAG_SWEEP_END);

    // one over -> second frame carries a single bin
    spectrumFrameInfo_t b = mkInfo(SPECTRUM_MAX_BINS_PER_FRAME + 1,
                                     SPECTRUM_MAX_BINS_PER_FRAME, 0, 0, 1000, 1);
    len = SpectrumEncodeFrame(payload, in, &b);
    TEST_ASSERT_EQUAL(1, b.binCount);
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(payload, len, &info, out, sizeof(out)));
    TEST_ASSERT_TRUE(info.flags & SPECTRUM_FLAG_SWEEP_END);
    TEST_ASSERT_EQUAL_INT8(in[SPECTRUM_MAX_BINS_PER_FRAME], out[0]);
}

void test_bin_freq_matches_real_fhss_grids(void)
{
    // The axis must land exactly on the FHSS table's freq_stop or the plot is
    // silently misaligned against the channel grid it is meant to inform
    spectrumFrameInfo_t info;
    info.startFreqKhz = G2G4_START_KHZ;
    info.stepKhz = G2G4_STEP_KHZ;
    TEST_ASSERT_EQUAL_UINT32(2400400u, SpectrumBinFreqKhz(&info, 0));
    TEST_ASSERT_EQUAL_UINT32(2440400u, SpectrumBinFreqKhz(&info, 40));
    TEST_ASSERT_EQUAL_UINT32(2479400u, SpectrumBinFreqKhz(&info, G2G4_BINS - 1)); // == freq_stop

    info.startFreqKhz = G915_START_KHZ;
    info.stepKhz = G915_STEP_KHZ;
    TEST_ASSERT_EQUAL_UINT32(903500u, SpectrumBinFreqKhz(&info, 0));
    TEST_ASSERT_EQUAL_UINT32(926900u, SpectrumBinFreqKhz(&info, G915_BINS - 1)); // == freq_stop
}

void test_payload_size_is_pinned(void)
{
    // 55 payload bytes -> 61 on the wire, which the emit pacing assumes
    // against EdgeTX's 255-byte Lua queue (~4 frames)
    TEST_ASSERT_EQUAL(55, SPECTRUM_MAX_PAYLOAD_BYTES);
    TEST_ASSERT_EQUAL(SPECTRUM_HEADER_BYTES + SPECTRUM_MAX_BINS_PER_FRAME,
                      SPECTRUM_MAX_PAYLOAD_BYTES);
}

void test_axis_is_big_endian_on_the_wire(void)
{
    // Pins byte order, which a roundtrip cannot: elrs.lua reads big endian
    int8_t in[2] = {-90, -91};
    uint8_t p[SPECTRUM_MAX_PAYLOAD_BYTES];
    spectrumFrameInfo_t enc = mkInfo(2, 0, 0, 0, G2G4_START_KHZ, G2G4_STEP_KHZ);
    enc.rbwKhz = 812; // the 2.4 band's Wide default
    TEST_ASSERT_TRUE(SpectrumEncodeFrame(p, in, &enc) > 0);

    TEST_ASSERT_EQUAL_HEX8(0x03, p[13]); // 812 == 0x032C, MSB first
    TEST_ASSERT_EQUAL_HEX8(0x2C, p[14]);
    spectrumFrameInfo_t back;
    int8_t out[SPECTRUM_MAX_BINS_PER_FRAME];
    TEST_ASSERT_TRUE(SpectrumDecodeFrame(p, SPECTRUM_HEADER_BYTES + 2, &back, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(812, back.rbwKhz);

    // 2400400 == 0x0024A090, MSB first
    TEST_ASSERT_EQUAL_HEX8(0x00, p[7]);
    TEST_ASSERT_EQUAL_HEX8(0x24, p[8]);
    TEST_ASSERT_EQUAL_HEX8(0xA0, p[9]);
    TEST_ASSERT_EQUAL_HEX8(0x90, p[10]);
    // 1000 == 0x03E8, MSB first
    TEST_ASSERT_EQUAL_HEX8(0x03, p[11]);
    TEST_ASSERT_EQUAL_HEX8(0xE8, p[12]);
}

void test_golden_vector_frame_is_pinned(void)
{
    // Pins frame 1 of lua/mockup/spectrumgolden.lua byte for byte. If this
    // fails the wire format moved: regenerate the golden (recipe in
    // lua/mockup/README.md) or the simulator is replaying stale bytes.
    int8_t bins[G2G4_BINS];
    for (int i = 0; i < G2G4_BINS; i++)
    {
        bins[i] = (int8_t)(-110 + i);
    }
    for (int i = 0; i < 3; i++)
    {
        bins[39 + i] = SPECTRUM_RSSI_INVALID; // the 3-bin notch at bin 40
    }

    uint8_t p[SPECTRUM_MAX_PAYLOAD_BYTES];
    spectrumFrameInfo_t enc = mkInfo(G2G4_BINS, 0, SPECTRUM_TRACE_LIVE, 7,
                                       G2G4_START_KHZ, G2G4_STEP_KHZ);
    enc.rbwKhz = 812; // matches gen_spectrumgolden.cpp's 2.4 band
    const uint8_t len = SpectrumEncodeFrame(p, bins, &enc);

    // 55 payload bytes + 2 ext-header addresses = the 57-entry table EdgeTX
    // hands Lua, exactly the highest index parseSpectrumMessage reads
    TEST_ASSERT_EQUAL(SPECTRUM_HEADER_BYTES + SPECTRUM_MAX_BINS_PER_FRAME, len);

    static const uint8_t expected[] = {
        0x01,                   // ELRS sub-type: spectrum sweep
        0x01,                   // proto version
        0x00,                   // flags: LIVE, and the encoder withheld SWEEP_END
        0x07,                   // sweepSeq
        0x00,                   // binOffset
        0x28,                   // binCount == 40
        0x50,                   // totalBins == 80
        0x00, 0x24, 0xA0, 0x90, // startFreqKhz 2400400, big endian
        0x03, 0xE8,             // stepKhz 1000, big endian
        0x03, 0x2C,             // sensing bandwidth 812kHz, big endian
        0x92, 0x93,             // bins 1-2 == -110, -109
    };
    for (size_t i = 0; i < sizeof(expected); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(expected[i], p[i]);
    }

    // The notch straddles the frame boundary deliberately, so this byte also
    // pins where the split lands
    TEST_ASSERT_EQUAL_HEX8(0x80, p[SPECTRUM_HEADER_BYTES + 39]);
}

void test_max_bins_covers_every_fhss_domain(void)
{
    // A larger domain would silently truncate at BeginScan()'s clamp. Per-band:
    // one band is scanned per screen, so a combined plot never needs 120
    TEST_ASSERT_EQUAL(80, SPECTRUM_MAX_BINS);
    TEST_ASSERT_TRUE(G2G4_BINS <= SPECTRUM_MAX_BINS);
    TEST_ASSERT_TRUE(G915_BINS <= SPECTRUM_MAX_BINS);
}

void test_decode_rejects_malformed(void)
{
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);

    uint8_t good[SPECTRUM_MAX_PAYLOAD_BYTES];
    spectrumFrameInfo_t enc = mkInfo(G2G4_BINS, 0, 0, 0, G2G4_START_KHZ, G2G4_STEP_KHZ);
    const uint8_t goodLen = SpectrumEncodeFrame(good, in, &enc);

    spectrumFrameInfo_t info;
    int8_t out[SPECTRUM_MAX_BINS_PER_FRAME];
    uint8_t bad[SPECTRUM_MAX_PAYLOAD_BYTES];

    TEST_ASSERT_TRUE(SpectrumDecodeFrame(good, goodLen, &info, out, sizeof(out)));

    // Table-driven so a failure names itself
    static const struct
    {
        uint8_t idx;
        uint8_t val;
        const char *why;
    } mutations[] = {
        // A foreign sub-type parsed as spectrum data would render a plot of its own bytes
        {0, SPECTRUM_SUBTYPE + 1, "foreign ELRS sub-type"},
        {1, SPECTRUM_PROTO_VERSION + 1, "wrong protocol version"},
        {5, 0, "zero bins"},
        {5, SPECTRUM_MAX_BINS_PER_FRAME + 1, "binCount over the per-frame cap"},
        {6, 0, "totalBins zero"},
        {6, SPECTRUM_MAX_BINS + 1, "totalBins over the cap"},
        {4, 60, "window runs past the end of the sweep (60+40 > 80)"},
    };

    for (unsigned i = 0; i < sizeof(mutations) / sizeof(mutations[0]); i++)
    {
        memcpy(bad, good, goodLen);
        bad[mutations[i].idx] = mutations[i].val;
        TEST_ASSERT_FALSE_MESSAGE(SpectrumDecodeFrame(bad, goodLen, &info, out, sizeof(out)),
                                  mutations[i].why);
    }

    TEST_ASSERT_FALSE(SpectrumDecodeFrame(good, SPECTRUM_HEADER_BYTES - 1, &info, out, sizeof(out)));
    TEST_ASSERT_FALSE(SpectrumDecodeFrame(good, goodLen - 1, &info, out, sizeof(out)));

    // caller's buffer is smaller than binCount -> must refuse, not overflow
    TEST_ASSERT_FALSE(SpectrumDecodeFrame(good, goodLen, &info, out, enc.binCount - 1));

    // offset + count must not wrap uint8 back into a valid-looking range
    memcpy(bad, good, goodLen);
    bad[3] = 250; // 250 + 40 = 290, wraps to 34 in uint8
    bad[5] = 70;
    TEST_ASSERT_FALSE_MESSAGE(SpectrumDecodeFrame(bad, goodLen, &info, out, sizeof(out)),
                              "binOffset + binCount must be guarded in 16 bit");
}

void test_encode_rejects_bad_args(void)
{
    int8_t in[G2G4_BINS];
    fillRamp(in, G2G4_BINS);
    uint8_t payload[SPECTRUM_MAX_PAYLOAD_BYTES];

    spectrumFrameInfo_t a = mkInfo(0, 0, 0, 0, 1000, 1); // zero-length sweep
    TEST_ASSERT_EQUAL(0, SpectrumEncodeFrame(payload, in, &a));
    TEST_ASSERT_EQUAL(0, a.binCount);

    spectrumFrameInfo_t b = mkInfo(SPECTRUM_MAX_BINS + 1, 0, 0, 0, 1000, 1); // over the cap
    TEST_ASSERT_EQUAL(0, SpectrumEncodeFrame(payload, in, &b));
    TEST_ASSERT_EQUAL(0, b.binCount);

    spectrumFrameInfo_t c = mkInfo(G2G4_BINS, G2G4_BINS, 0, 0, 1000, 1); // offset past the end
    TEST_ASSERT_EQUAL(0, SpectrumEncodeFrame(payload, in, &c));
    TEST_ASSERT_EQUAL(0, c.binCount);
}

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
    RUN_TEST(test_compare_flags_identify_the_radio);
    RUN_TEST(test_max_bins_covers_every_fhss_domain);
    RUN_TEST(test_decode_rejects_malformed);
    RUN_TEST(test_encode_rejects_bad_args);
    UNITY_END();

    return 0;
}
