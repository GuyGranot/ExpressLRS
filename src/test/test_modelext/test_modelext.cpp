#include <cstdint>
#include <config_modelext.h>
#include <unity.h>

void test_modelext_absent_key_defaults_off(void)
{
    model_extra_config_t ext;
    ext.raw = 0; // an absent NVS key loads as raw 0
    TEST_ASSERT_EQUAL(0, ext.val.extSchema);
    TEST_ASSERT_FALSE(ext.val.bandSubset);
}

void test_modelext_roundtrip(void)
{
    model_extra_config_t ext;
    ext.raw = 0;
    ext.val.extSchema = MODEL_EXTRA_SCHEMA;
    ext.val.bandSubset = true;

    model_extra_config_t loaded;
    loaded.raw = ModelExtraSanitize(ext.raw);
    TEST_ASSERT_EQUAL(MODEL_EXTRA_SCHEMA, loaded.val.extSchema);
    TEST_ASSERT_TRUE(loaded.val.bandSubset);
}

void test_modelext_sanitize_accepts_current_schema(void)
{
    model_extra_config_t ext;
    ext.raw = 0;
    ext.val.extSchema = MODEL_EXTRA_SCHEMA;
    ext.val.bandSubset = true;
    TEST_ASSERT_EQUAL_UINT32(ext.raw, ModelExtraSanitize(ext.raw));
}

void test_modelext_sanitize_rejects_unknown_schema(void)
{
    // A record written by a hypothetical newer firmware must not be
    // interpreted with this firmware's bit layout
    model_extra_config_t ext;
    ext.raw = 0;
    ext.val.extSchema = MODEL_EXTRA_SCHEMA + 1;
    ext.val.bandSubset = true;
    TEST_ASSERT_EQUAL_UINT32(0, ModelExtraSanitize(ext.raw));

    // schema 0 with stray bits is equally untrusted
    model_extra_config_t stray;
    stray.raw = 0;
    stray.val.bandSubset = true;
    TEST_ASSERT_EQUAL_UINT32(0, ModelExtraSanitize(stray.raw));
}

void test_modelext_sanitize_preserves_reserved_bits(void)
{
    // reserved bits written under the current schema must survive a
    // load/store cycle so a same-schema future field is not silently cleared
    model_extra_config_t ext;
    ext.raw = 0;
    ext.val.extSchema = MODEL_EXTRA_SCHEMA;
    ext.val.reserved = 0x5A5A5A5;
    TEST_ASSERT_EQUAL_UINT32(ext.raw, ModelExtraSanitize(ext.raw));
}

// Unity setup/teardown
void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_modelext_absent_key_defaults_off);
    RUN_TEST(test_modelext_sanitize_accepts_current_schema);
    RUN_TEST(test_modelext_roundtrip);
    RUN_TEST(test_modelext_sanitize_rejects_unknown_schema);
    RUN_TEST(test_modelext_sanitize_preserves_reserved_bits);
    UNITY_END();

    return 0;
}
