#include "MspFrameAssembler.h"
#include <cstring>
#include <unity.h>

// $M< len=2 cmd=100 payload={1,2} xor=0x65
const uint8_t V1_REQUEST[] = {0x24, 0x4d, 0x3c, 0x02, 0x64, 0x01, 0x02, 0x65};
// $X< flag=0 cmd=100 len=0 crc=0x8f (MSP_IDENT as SpeedyBee sends it)
const uint8_t V2_REQUEST[] = {0x24, 0x58, 0x3c, 0x00, 0x64, 0x00, 0x00, 0x00, 0x8f};
// $M< FF cmd=0x74 len16=5 payload="ABCDE" xor=0xcf
const uint8_t JUMBO_REQUEST[] = {0x24, 0x4d, 0x3c, 0xff, 0x74, 0x05, 0x00, 0x41, 0x42, 0x43, 0x44, 0x45, 0xcf};
// jumbo declaring 505 payload bytes: total 8 + 505 = 513 > MAX_FRAME
const uint8_t JUMBO_OVERSIZED_HEADER[] = {0x24, 0x4d, 0x3c, 0xff, 0x74, 0xf9, 0x01};
// v2 declaring 504 payload bytes: total 9 + 504 = 513 > MAX_FRAME
const uint8_t V2_OVERSIZED_HEADER[] = {0x24, 0x58, 0x3c, 0x00, 0x64, 0x00, 0xf8, 0x01};

static MspFrameAssembler assembler;

struct feedCount
{
    int completes;
    int rejects;
};

// Push a byte range, counting completions/rejections and verifying that every
// completed frame is byte-identical to `expect` (when given).
static feedCount feed(const uint8_t *data, size_t len, const uint8_t *expect = nullptr, size_t expectLen = 0)
{
    feedCount count = {0, 0};
    for (size_t i = 0; i < len; i++)
    {
        switch (assembler.push(data[i]))
        {
        case MspFrameAssembler::MSP_ASM_COMPLETE:
            count.completes++;
            if (expect != nullptr)
            {
                TEST_ASSERT_EQUAL_UINT32(expectLen, assembler.frameLen());
                TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, assembler.frame(), expectLen);
            }
            break;
        case MspFrameAssembler::MSP_ASM_REJECTED:
            count.rejects++;
            break;
        default:
            break;
        }
    }
    return count;
}

void test_v1_byte_at_a_time()
{
    // header and payload arbitrarily split: one byte per push is the worst case
    const feedCount count = feed(V1_REQUEST, sizeof(V1_REQUEST), V1_REQUEST, sizeof(V1_REQUEST));
    TEST_ASSERT_EQUAL_INT(1, count.completes);
    TEST_ASSERT_EQUAL_INT(0, count.rejects);
}

void test_v2_and_jumbo_accepted()
{
    feedCount count = feed(V2_REQUEST, sizeof(V2_REQUEST), V2_REQUEST, sizeof(V2_REQUEST));
    TEST_ASSERT_EQUAL_INT(1, count.completes);
    count = feed(JUMBO_REQUEST, sizeof(JUMBO_REQUEST), JUMBO_REQUEST, sizeof(JUMBO_REQUEST));
    TEST_ASSERT_EQUAL_INT(1, count.completes);
    TEST_ASSERT_EQUAL_INT(0, count.rejects);
}

void test_invalid_checksum_rejected_then_recovers()
{
    uint8_t bad[sizeof(V1_REQUEST)];
    memcpy(bad, V1_REQUEST, sizeof(bad));
    bad[sizeof(bad) - 1] ^= 0xff;
    feedCount count = feed(bad, sizeof(bad));
    TEST_ASSERT_EQUAL_INT(0, count.completes);
    TEST_ASSERT_EQUAL_INT(1, count.rejects);

    count = feed(V1_REQUEST, sizeof(V1_REQUEST), V1_REQUEST, sizeof(V1_REQUEST));
    TEST_ASSERT_EQUAL_INT(1, count.completes);
}

void test_oversized_declared_length_rejected_then_recovers()
{
    feedCount count = feed(JUMBO_OVERSIZED_HEADER, sizeof(JUMBO_OVERSIZED_HEADER));
    TEST_ASSERT_EQUAL_INT(1, count.rejects);
    count = feed(V2_OVERSIZED_HEADER, sizeof(V2_OVERSIZED_HEADER));
    TEST_ASSERT_EQUAL_INT(1, count.rejects);

    // rejection resynchronizes: a valid frame right after is accepted
    count = feed(V2_REQUEST, sizeof(V2_REQUEST), V2_REQUEST, sizeof(V2_REQUEST));
    TEST_ASSERT_EQUAL_INT(1, count.completes);
    TEST_ASSERT_EQUAL_INT(0, count.rejects);
}

void test_garbage_before_valid_frame()
{
    const uint8_t garbage[] = {0x00, 0x42, 0x24, 0x7a, 0x24, 0x24}; // includes lone '$' and "$$"
    feedCount count = feed(garbage, sizeof(garbage));
    TEST_ASSERT_EQUAL_INT(0, count.completes);

    count = feed(V1_REQUEST, sizeof(V1_REQUEST), V1_REQUEST, sizeof(V1_REQUEST));
    TEST_ASSERT_EQUAL_INT(1, count.completes);
}

void test_direction_response_rejected()
{
    uint8_t response[sizeof(V1_REQUEST)];
    memcpy(response, V1_REQUEST, sizeof(response));
    response[2] = '>';
    const feedCount count = feed(response, sizeof(response));
    TEST_ASSERT_EQUAL_INT(0, count.completes);
    TEST_ASSERT_EQUAL_INT(1, count.rejects);
}

void test_two_frames_in_one_write()
{
    uint8_t combined[sizeof(V1_REQUEST) + sizeof(V2_REQUEST) + 3];
    memcpy(combined, V1_REQUEST, sizeof(V1_REQUEST));
    memcpy(combined + sizeof(V1_REQUEST), V2_REQUEST, sizeof(V2_REQUEST));
    // trailing partial frame stays pending without disturbing the two completions
    combined[sizeof(V1_REQUEST) + sizeof(V2_REQUEST) + 0] = 0x24;
    combined[sizeof(V1_REQUEST) + sizeof(V2_REQUEST) + 1] = 0x4d;
    combined[sizeof(V1_REQUEST) + sizeof(V2_REQUEST) + 2] = 0x3c;

    const feedCount count = feed(combined, sizeof(combined));
    TEST_ASSERT_EQUAL_INT(2, count.completes);
    TEST_ASSERT_EQUAL_INT(0, count.rejects);
    assembler.reset();
}

void test_reset_discards_partial_frame()
{
    // half a frame, then a disconnect-style reset: the tail must not glue to
    // the next session's bytes
    feedCount count = feed(V1_REQUEST, 5);
    TEST_ASSERT_EQUAL_INT(0, count.completes);
    assembler.reset();

    count = feed(V1_REQUEST, sizeof(V1_REQUEST), V1_REQUEST, sizeof(V1_REQUEST));
    TEST_ASSERT_EQUAL_INT(1, count.completes);
    TEST_ASSERT_EQUAL_INT(0, count.rejects);
}

void setUp()
{
    assembler.reset();
}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_v1_byte_at_a_time);
    RUN_TEST(test_v2_and_jumbo_accepted);
    RUN_TEST(test_invalid_checksum_rejected_then_recovers);
    RUN_TEST(test_oversized_declared_length_rejected_then_recovers);
    RUN_TEST(test_garbage_before_valid_frame);
    RUN_TEST(test_direction_response_rejected);
    RUN_TEST(test_two_frames_in_one_write);
    RUN_TEST(test_reset_discards_partial_frame);

    UNITY_END();

    return 0;
}
