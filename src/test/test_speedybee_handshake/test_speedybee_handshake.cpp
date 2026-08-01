// Golden-byte tests for the SpeedyBee BLE handshake responder, anchored to a
// capture of a genuine SpeedyBee F405 V4 board.

#include "SpeedyBeeHandshake.h"
#include <string.h>
#include <unity.h>

// Fixtures, not the shipped identity (options.h is unavailable under UNIT_TEST)
static const SpeedyBeeDeviceInfo INFO = {
    "ELRSNOMAD", "AABBCCDDEEFF", "ELRS Nomad TX", "ELRSNomad", "ELRSNOMAD1"};

struct Capture
{
    uint8_t data[SpeedyBeeHandshake::RESPONSE_MAX];
    size_t len;
    int calls;
};

static void writer(void *ctx, const uint8_t *data, size_t len)
{
    Capture *c = (Capture *)ctx;
    TEST_ASSERT_TRUE(len <= sizeof(c->data));
    memcpy(c->data, data, len);
    c->len = len;
    c->calls++;
}

// Parse the [cmd][0x00][varint len] header the way the app does (a varint, NOT
// a 16-bit length -- the two are identical below 128); returns the payload offset
static size_t decodeHeader(const Capture &cap, uint8_t expectCmd)
{
    TEST_ASSERT_TRUE(cap.len >= 3);
    TEST_ASSERT_EQUAL_UINT8(expectCmd, cap.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, cap.data[1]);
    size_t p = 2, payloadLen = 0;
    int shift = 0;
    while (p < cap.len)
    {
        const uint8_t b = cap.data[p++];
        payloadLen |= (size_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
        {
            break;
        }
        shift += 7;
        TEST_ASSERT_TRUE(shift <= 21); // runaway varint
    }
    TEST_ASSERT_EQUAL_size_t(cap.len - p, payloadLen);
    return p;
}

void test_init_ack_golden_bytes()
{
    SpeedyBeeHandshake hs(INFO);
    Capture cap = {};
    const uint8_t init[] = {0x02, 0x00, 0x08, 0x03};
    TEST_ASSERT_TRUE(hs.handleControlWrite(init, sizeof(init), writer, &cap));
    const uint8_t expected[] = {0x03, 0x00, 0x02, 0x08, 0x03};
    TEST_ASSERT_EQUAL(1, cap.calls);
    TEST_ASSERT_EQUAL(sizeof(expected), cap.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, cap.data, sizeof(expected));
    TEST_ASSERT_FALSE(hs.sessionReady());
}

void test_password_required_golden_bytes()
{
    SpeedyBeeHandshake hs(INFO, "1234");
    Capture cap = {};
    const uint8_t init[] = {0x02, 0x00, 0x08, 0x03};
    TEST_ASSERT_TRUE(hs.handleControlWrite(init, sizeof(init), writer, &cap));
    const uint8_t expected[] = {0x07, 0x00, 0x06, 0x08, 0x03, 0x10, 0x02, 0x1a, 0x00};
    TEST_ASSERT_EQUAL(sizeof(expected), cap.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, cap.data, sizeof(expected));
}

void test_password_accept_golden_bytes()
{
    SpeedyBeeHandshake hs(INFO, "1234");
    Capture cap = {};
    // 08 00 08 04 12 04 "1234"
    const uint8_t pw[] = {0x08, 0x00, 0x08, 0x04, 0x12, 0x04, '1', '2', '3', '4'};
    TEST_ASSERT_TRUE(hs.handleControlWrite(pw, sizeof(pw), writer, &cap));
    const uint8_t expected[] = {0x05, 0x00, 0x04, 0x08, 0x04, 0x1a, 0x00};
    TEST_ASSERT_EQUAL(sizeof(expected), cap.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, cap.data, sizeof(expected));

    // after auth, init answers the no-password flavor
    Capture cap2 = {};
    const uint8_t init[] = {0x02, 0x00, 0x08, 0x03};
    TEST_ASSERT_TRUE(hs.handleControlWrite(init, sizeof(init), writer, &cap2));
    TEST_ASSERT_EQUAL_UINT8(0x03, cap2.data[0]);
}

void test_password_reject()
{
    SpeedyBeeHandshake hs(INFO, "1234");
    Capture cap = {};
    const uint8_t pw[] = {0x08, 0x00, 0x08, 0x04, 0x12, 0x04, 'n', 'o', 'p', 'e'};
    TEST_ASSERT_TRUE(hs.handleControlWrite(pw, sizeof(pw), writer, &cap));
    TEST_ASSERT_EQUAL_UINT8(0x07, cap.data[0]);
    TEST_ASSERT_FALSE(hs.sessionReady());
}

void test_device_info_layout()
{
    SpeedyBeeHandshake hs(INFO);
    Capture cap = {};
    // 0e 00 08 0d 12 0a <10-char serial> -- serial from the app is ignored
    const uint8_t req[] = {0x0E, 0x00, 0x08, 0x0D, 0x12, 0x0A,
                           'a', 'b', 'c', 'd', 'e', 'f', '0', '1', '2', '3'};
    TEST_ASSERT_TRUE(hs.handleControlWrite(req, sizeof(req), writer, &cap));
    // real FC replies f6 00 f4 01 08 0d 1a ef 01 ...
    const size_t payload = decodeHeader(cap, 0xF6);
    TEST_ASSERT_EQUAL_size_t(4, payload); // 133-byte payload -> 2-byte varint
    // payload: 08 0d (field1=13), then 1a <blob len varint> blob
    TEST_ASSERT_EQUAL_UINT8(0x08, cap.data[payload]);
    TEST_ASSERT_EQUAL_UINT8(0x0D, cap.data[payload + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x1A, cap.data[payload + 2]);
    TEST_ASSERT_TRUE(cap.data[payload + 3] & 0x80); // blob >= 128 -> 2-byte varint
    // offsets are load-bearing: the app reads fields at fixed positions
    const char *blob = (const char *)cap.data + payload + 5;
    TEST_ASSERT_EQUAL_STRING("ELRSNOMAD", blob);
    TEST_ASSERT_EQUAL_STRING("AABBCCDDEEFF", blob + 10);
    TEST_ASSERT_EQUAL_STRING("ELRS Nomad TX", blob + 24);
    TEST_ASSERT_EQUAL_STRING("ELRSNomad", blob + 56);
    TEST_ASSERT_EQUAL_STRING("ELRSNOMAD1", blob + 66);
    TEST_ASSERT_EQUAL(239, (int)(cap.len - (payload + 5)));
    // No trace of the board the layout came from.
    for (size_t i = payload; i + 9 < cap.len; i++)
    {
        TEST_ASSERT_TRUE(memcmp(cap.data + i, "SBF4V4085", 9) != 0);
        TEST_ASSERT_TRUE(memcmp(cap.data + i, "SpeedyBee", 9) != 0);
    }
}

void test_session_key()
{
    SpeedyBeeHandshake hs(INFO);
    hs.seedRandom(42);
    Capture cap = {};
    const uint8_t key[] = {0x02, 0x00, 0x08, 0x2D};
    TEST_ASSERT_TRUE(hs.handleControlWrite(key, sizeof(key), writer, &cap));
    // Real FC: 26 00 25 08 2d 1a 21 fc ff*16 <16 varying>
    const size_t payload = decodeHeader(cap, 0x26);
    TEST_ASSERT_EQUAL_size_t(3, payload); // 37-byte payload -> 1-byte varint
    TEST_ASSERT_EQUAL_UINT8(0x08, cap.data[payload]);
    TEST_ASSERT_EQUAL_UINT8(0x2D, cap.data[payload + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x1A, cap.data[payload + 2]);
    TEST_ASSERT_EQUAL_UINT8(0x21, cap.data[payload + 3]); // 33, as the FC sends
    TEST_ASSERT_EQUAL_UINT8(0xFC, cap.data[payload + 4]);
    for (size_t i = 0; i < 16; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0xFF, cap.data[payload + 5 + i]);
    }
    TEST_ASSERT_EQUAL(3 + 4 + 33, cap.len);
    TEST_ASSERT_TRUE(hs.sessionReady());

    hs.reset();
    TEST_ASSERT_FALSE(hs.sessionReady());
}

void test_unknown_and_runt()
{
    SpeedyBeeHandshake hs(INFO);
    Capture cap = {};
    const uint8_t unknown[] = {0x7F, 0x00, 0x08, 0x01};
    TEST_ASSERT_FALSE(hs.handleControlWrite(unknown, sizeof(unknown), writer, &cap));
    TEST_ASSERT_EQUAL(0, cap.calls);

    const uint8_t runt[] = {0x02};
    TEST_ASSERT_FALSE(hs.handleControlWrite(runt, sizeof(runt), writer, &cap));

    // truncated length-delimited field must not read out of bounds
    const uint8_t bad[] = {0x0E, 0x00, 0x12, 0x20, 'x'};
    TEST_ASSERT_FALSE(hs.handleControlWrite(bad, sizeof(bad), writer, &cap));
    TEST_ASSERT_EQUAL(0, cap.calls);
}

void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_ack_golden_bytes);
    RUN_TEST(test_password_required_golden_bytes);
    RUN_TEST(test_password_accept_golden_bytes);
    RUN_TEST(test_password_reject);
    RUN_TEST(test_device_info_layout);
    RUN_TEST(test_session_key);
    RUN_TEST(test_unknown_and_runt);
    UNITY_END();
    return 0;
}
