#include <cstdint>
#include <cstring>
#include <unity.h>

#include "SurveyProtocol.h"

// The real 2.4GHz FHSS grid (FHSS.cpp:33,47): 2400.4MHz .. 2479.4MHz, 80 channels.
#define G2G4_START_KHZ 2400400u
#define G2G4_STEP_KHZ 1000u
#define G2G4_CHANNELS 80

// The real FCC915 grid (FHSS.cpp:16): 903.5MHz .. 926.9MHz, 40 channels.
#define G915_START_KHZ 903500u
#define G915_STEP_KHZ 600u
#define G915_CHANNELS 40

static surveyFrameInfo_t mkInfo(const uint8_t sampleCount)
{
    surveyFrameInfo_t info;
    memset(&info, 0, sizeof(info));
    info.flags = SURVEY_FLAG_DUAL_RADIO;
    info.seq = 7;
    info.enumRate = 4;
    info.bwKhz = 812;
    info.lnaGainDb = 12;
    info.channelCount = G2G4_CHANNELS;
    info.startFreqKhz = G2G4_START_KHZ;
    info.stepKhz = G2G4_STEP_KHZ;
    info.dropped = 0;
    info.sampleCount = sampleCount;
    return info;
}

static surveySample_t mkSample(const uint8_t i)
{
    surveySample_t s;
    memset(&s, 0, sizeof(s));
    s.chan1 = i;
    s.chan2 = (uint8_t)(i + G2G4_CHANNELS / 2);
    s.rssi1 = (int8_t)(-100 + i);
    s.rssi2 = (int8_t)(-95 + i);
    s.flags = (uint8_t)((i & 1) ? SURVEY_SFLAG_PACKET_ON_RADIO2 : SURVEY_SFLAG_CLEAN);
    return s;
}

static void test_payload_fits_crsf_frame()
{
    // 58 = CRSF_MAX_PACKET_LEN minus the extended header and CRC; devRxSurvey.cpp
    // static_asserts this against the real constants on every firmware build.
    TEST_ASSERT_LESS_OR_EQUAL_UINT(58, SURVEY_MAX_PAYLOAD_BYTES);
    TEST_ASSERT_EQUAL_UINT(50, SURVEY_MAX_PAYLOAD_BYTES);
}

static void test_round_trip_full_frame()
{
    surveySample_t in[SURVEY_MAX_SAMPLES_PER_FRAME];
    for (uint8_t i = 0; i < SURVEY_MAX_SAMPLES_PER_FRAME; i++)
    {
        in[i] = mkSample(i);
    }

    uint8_t payload[SURVEY_MAX_PAYLOAD_BYTES];
    surveyFrameInfo_t info = mkInfo(SURVEY_MAX_SAMPLES_PER_FRAME);
    const uint8_t len = SurveyEncodeFrame(payload, in, &info);
    TEST_ASSERT_EQUAL_UINT(SURVEY_MAX_PAYLOAD_BYTES, len);
    TEST_ASSERT_EQUAL_UINT(SURVEY_PROTO_VERSION, info.version);

    surveyFrameInfo_t out;
    surveySample_t got[SURVEY_MAX_SAMPLES_PER_FRAME];
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(SurveyDecodeFrame(payload, len, &out, got,
                                       SURVEY_MAX_SAMPLES_PER_FRAME));

    TEST_ASSERT_EQUAL_UINT(info.flags, out.flags);
    TEST_ASSERT_EQUAL_UINT(info.seq, out.seq);
    TEST_ASSERT_EQUAL_UINT(info.enumRate, out.enumRate);
    TEST_ASSERT_EQUAL_UINT(info.bwKhz, out.bwKhz);
    TEST_ASSERT_EQUAL_INT(info.lnaGainDb, out.lnaGainDb);
    TEST_ASSERT_EQUAL_UINT(info.channelCount, out.channelCount);
    TEST_ASSERT_EQUAL_UINT32(info.startFreqKhz, out.startFreqKhz);
    TEST_ASSERT_EQUAL_UINT(info.stepKhz, out.stepKhz);
    TEST_ASSERT_EQUAL_UINT(SURVEY_MAX_SAMPLES_PER_FRAME, out.sampleCount);

    for (uint8_t i = 0; i < SURVEY_MAX_SAMPLES_PER_FRAME; i++)
    {
        TEST_ASSERT_EQUAL_UINT(in[i].chan1, got[i].chan1);
        TEST_ASSERT_EQUAL_UINT(in[i].chan2, got[i].chan2);
        TEST_ASSERT_EQUAL_INT(in[i].rssi1, got[i].rssi1);
        TEST_ASSERT_EQUAL_INT(in[i].rssi2, got[i].rssi2);
        TEST_ASSERT_EQUAL_UINT(in[i].flags, got[i].flags);
    }
}

// A frame with no samples is the gate report and must round-trip intact.
static void test_gate_report_carries_no_samples()
{
    uint8_t payload[SURVEY_MAX_PAYLOAD_BYTES];
    surveyFrameInfo_t info = mkInfo(0);
    info.flags = SURVEY_FLAG_GATED_RATE | SURVEY_FLAG_GATED_TIMER;
    const uint8_t len = SurveyEncodeFrame(payload, nullptr, &info);
    TEST_ASSERT_EQUAL_UINT(SURVEY_HEADER_BYTES, len);

    surveyFrameInfo_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(SurveyDecodeFrame(payload, len, &out, nullptr, 0));
    TEST_ASSERT_EQUAL_UINT(0, out.sampleCount);
    TEST_ASSERT_TRUE((out.flags & SURVEY_FLAG_GATED_RATE) != 0);
    TEST_ASSERT_TRUE((out.flags & SURVEY_FLAG_GATED_TIMER) != 0);
    TEST_ASSERT_TRUE((out.flags & SURVEY_FLAG_GATED_LINK) == 0);
}

// A lost sign turns a -100 dBm floor into a plausible +156, which plots.
static void test_signed_fields_survive()
{
    surveySample_t in = mkSample(0);
    in.rssi1 = SURVEY_RSSI_INVALID + 1; // -127, the lowest a real reading clamps to
    in.rssi2 = SURVEY_RSSI_INVALID;     // the sentinel itself

    uint8_t payload[SURVEY_MAX_PAYLOAD_BYTES];
    surveyFrameInfo_t info = mkInfo(1);
    info.lnaGainDb = -3; // negative gain is legal and must not read as 253
    const uint8_t len = SurveyEncodeFrame(payload, &in, &info);

    surveyFrameInfo_t out;
    surveySample_t got;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(SurveyDecodeFrame(payload, len, &out, &got, 1));
    TEST_ASSERT_EQUAL_INT(-127, got.rssi1);
    TEST_ASSERT_EQUAL_INT(SURVEY_RSSI_INVALID, got.rssi2);
    TEST_ASSERT_EQUAL_INT(-3, out.lnaGainDb);
}

static void test_dropped_count_is_16_bit()
{
    uint8_t payload[SURVEY_MAX_PAYLOAD_BYTES];
    surveyFrameInfo_t info = mkInfo(0);
    info.dropped = 0xBEEF;
    const uint8_t len = SurveyEncodeFrame(payload, nullptr, &info);

    surveyFrameInfo_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(SurveyDecodeFrame(payload, len, &out, nullptr, 0));
    TEST_ASSERT_EQUAL_UINT(0xBEEF, out.dropped);
}

// The channel axis must land on the real FHSS grids exactly.
static void test_channel_axis_matches_the_fhss_grid()
{
    surveyFrameInfo_t info = mkInfo(0);
    TEST_ASSERT_EQUAL_UINT32(2400400u, SurveyChanFreqKhz(&info, 0));
    TEST_ASSERT_EQUAL_UINT32(2479400u, SurveyChanFreqKhz(&info, G2G4_CHANNELS - 1));

    info.startFreqKhz = G915_START_KHZ;
    info.stepKhz = G915_STEP_KHZ;
    info.channelCount = G915_CHANNELS;
    TEST_ASSERT_EQUAL_UINT32(903500u, SurveyChanFreqKhz(&info, 0));
    TEST_ASSERT_EQUAL_UINT32(926900u, SurveyChanFreqKhz(&info, G915_CHANNELS - 1));
}

// A dual-band link carries a second axis; single-band frames leave it all-zero.
static void test_second_band_axis_round_trips()
{
    uint8_t payload[SURVEY_MAX_PAYLOAD_BYTES];
    surveyFrameInfo_t info = mkInfo(0);

    // the X-rate shape: sub-GHz primary, 2.4GHz second band
    info.bwKhz = 500;
    info.channelCount = G915_CHANNELS;
    info.startFreqKhz = G915_START_KHZ;
    info.stepKhz = G915_STEP_KHZ;
    info.startFreqKhz2 = G2G4_START_KHZ;
    info.stepKhz2 = G2G4_STEP_KHZ;
    info.channelCount2 = G2G4_CHANNELS;
    const uint8_t len = SurveyEncodeFrame(payload, nullptr, &info);
    TEST_ASSERT_EQUAL_UINT(SURVEY_HEADER_BYTES, len);

    surveyFrameInfo_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(SurveyDecodeFrame(payload, len, &out, nullptr, 0));
    TEST_ASSERT_EQUAL_UINT32(G2G4_START_KHZ, out.startFreqKhz2);
    TEST_ASSERT_EQUAL_UINT(G2G4_STEP_KHZ, out.stepKhz2);
    TEST_ASSERT_EQUAL_UINT(G2G4_CHANNELS, out.channelCount2);
    TEST_ASSERT_EQUAL_UINT32(903500u, SurveyChanFreqKhz(&out, 0));
    TEST_ASSERT_EQUAL_UINT32(2400400u, SurveyChan2FreqKhz(&out, 0));
    TEST_ASSERT_EQUAL_UINT32(2479400u, SurveyChan2FreqKhz(&out, G2G4_CHANNELS - 1));

    // a single-band frame (mkInfo leaves the second axis zeroed) says so
    surveyFrameInfo_t single = mkInfo(0);
    const uint8_t slen = SurveyEncodeFrame(payload, nullptr, &single);
    memset(&out, 0xA5, sizeof(out));
    TEST_ASSERT_TRUE(SurveyDecodeFrame(payload, slen, &out, nullptr, 0));
    TEST_ASSERT_EQUAL_UINT32(0, out.startFreqKhz2);
    TEST_ASSERT_EQUAL_UINT(0, out.stepKhz2);
    TEST_ASSERT_EQUAL_UINT(0, out.channelCount2);
}

// Pins exact bit positions: a swapped bit decodes as plausible data, not garbage.
static void test_debug_bytes_round_trip()
{
    const surveyDebug_t cases[] = {
        {0, 0, 0, false, false, false},
        {126, 126, 126, true, true, true},
        {SURVEY_DBG_MAG_INVALID, 0, SURVEY_DBG_CHAN_NONE, false, true, false},
        {100, 95, 42, true, false, true},
        {95, 100, 79, false, true, false}, // the highest real 2.4 channel index
    };
    for (uint8_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        uint8_t bytes[3];
        surveyDebug_t got;
        SurveyPackDebug(bytes, &cases[i]);
        SurveyUnpackDebug(bytes, &got);
        TEST_ASSERT_EQUAL_UINT(cases[i].magA, got.magA);
        TEST_ASSERT_EQUAL_UINT(cases[i].magB, got.magB);
        TEST_ASSERT_EQUAL_UINT(cases[i].chan, got.chan);
        TEST_ASSERT_EQUAL(cases[i].toggle, got.toggle);
        TEST_ASSERT_EQUAL(cases[i].clean, got.clean);
        TEST_ASSERT_EQUAL(cases[i].bit7, got.bit7);
    }

    // pin the bit layout itself, not just the round trip
    const surveyDebug_t d = {100, 95, 42, true, false, true};
    uint8_t bytes[3];
    SurveyPackDebug(bytes, &d);
    TEST_ASSERT_EQUAL_HEX8(0xE4, bytes[0]); // 100 | toggle<<7
    TEST_ASSERT_EQUAL_HEX8(0x5F, bytes[1]); // 95, clean clear
    TEST_ASSERT_EQUAL_HEX8(0xAA, bytes[2]); // 42 | bit7<<7
}

// A positive or absurd reading must clamp instead of wrapping into a plausible one.
static void test_debug_magnitude_clamps()
{
    TEST_ASSERT_EQUAL_UINT(100, SurveyDbgMagFromDbm(-100));
    TEST_ASSERT_EQUAL_UINT(0, SurveyDbgMagFromDbm(0));
    TEST_ASSERT_EQUAL_UINT(0, SurveyDbgMagFromDbm(12));
    TEST_ASSERT_EQUAL_UINT(126, SurveyDbgMagFromDbm(-126));
    TEST_ASSERT_EQUAL_UINT(126, SurveyDbgMagFromDbm(-127)); // never packs as INVALID
    TEST_ASSERT_EQUAL_UINT(126, SurveyDbgMagFromDbm(-300));
}

static void test_decode_rejects_malformed()
{
    surveySample_t in[SURVEY_MAX_SAMPLES_PER_FRAME];
    for (uint8_t i = 0; i < SURVEY_MAX_SAMPLES_PER_FRAME; i++)
    {
        in[i] = mkSample(i);
    }
    uint8_t payload[SURVEY_MAX_PAYLOAD_BYTES];
    surveyFrameInfo_t info = mkInfo(SURVEY_MAX_SAMPLES_PER_FRAME);
    const uint8_t len = SurveyEncodeFrame(payload, in, &info);

    surveyFrameInfo_t out;
    surveySample_t got[SURVEY_MAX_SAMPLES_PER_FRAME];

    // a foreign sub-type on the shared frame type is rejected, not crashed on
    uint8_t bad[SURVEY_MAX_PAYLOAD_BYTES];
    memcpy(bad, payload, len);
    bad[0] = SURVEY_SUBTYPE + 1;
    TEST_ASSERT_FALSE(SurveyDecodeFrame(bad, len, &out, got, SURVEY_MAX_SAMPLES_PER_FRAME));

    // wrong protocol version
    memcpy(bad, payload, len);
    bad[1] = SURVEY_PROTO_VERSION + 1;
    TEST_ASSERT_FALSE(SurveyDecodeFrame(bad, len, &out, got, SURVEY_MAX_SAMPLES_PER_FRAME));

    // sample count that does not match the length
    memcpy(bad, payload, len);
    bad[24] = SURVEY_MAX_SAMPLES_PER_FRAME - 1;
    TEST_ASSERT_FALSE(SurveyDecodeFrame(bad, len, &out, got, SURVEY_MAX_SAMPLES_PER_FRAME));

    // more samples than this build can hold
    memcpy(bad, payload, len);
    bad[24] = SURVEY_MAX_SAMPLES_PER_FRAME + 1;
    TEST_ASSERT_FALSE(SurveyDecodeFrame(bad, len, &out, got, SURVEY_MAX_SAMPLES_PER_FRAME));

    // truncated header
    TEST_ASSERT_FALSE(SurveyDecodeFrame(payload, SURVEY_HEADER_BYTES - 1, &out, got,
                                        SURVEY_MAX_SAMPLES_PER_FRAME));

    // well formed, but the caller's buffer cannot take it
    TEST_ASSERT_FALSE(SurveyDecodeFrame(payload, len, &out, got,
                                        SURVEY_MAX_SAMPLES_PER_FRAME - 1));
}

static void test_encode_rejects_impossible_input()
{
    uint8_t payload[SURVEY_MAX_PAYLOAD_BYTES];
    surveySample_t s = mkSample(0);

    surveyFrameInfo_t tooMany = mkInfo(SURVEY_MAX_SAMPLES_PER_FRAME + 1);
    TEST_ASSERT_EQUAL_UINT(0, SurveyEncodeFrame(payload, &s, &tooMany));

    // claiming samples while supplying none would encode whatever is on the stack
    surveyFrameInfo_t noBuffer = mkInfo(1);
    TEST_ASSERT_EQUAL_UINT(0, SurveyEncodeFrame(payload, nullptr, &noBuffer));
}

void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_payload_fits_crsf_frame);
    RUN_TEST(test_round_trip_full_frame);
    RUN_TEST(test_gate_report_carries_no_samples);
    RUN_TEST(test_signed_fields_survive);
    RUN_TEST(test_dropped_count_is_16_bit);
    RUN_TEST(test_channel_axis_matches_the_fhss_grid);
    RUN_TEST(test_second_band_axis_round_trips);
    RUN_TEST(test_debug_bytes_round_trip);
    RUN_TEST(test_debug_magnitude_clamps);
    RUN_TEST(test_decode_rejects_malformed);
    RUN_TEST(test_encode_rejects_impossible_input);
    return UNITY_END();
}
