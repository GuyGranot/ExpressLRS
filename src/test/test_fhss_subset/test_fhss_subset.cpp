// Native tests: primary = 80ch 2.4GHz domain (native.h pins RADIO_SX128X),
// secondary = synthetic 40ch FCC915 table (UNIT_TEST dual-band).
#include <cstdint>
#include <cstring>
#include <FHSS.h>
#include <OTA.h>
#include <fhss_subset.h>
#include <helpers.h>
#include <options.h>
#include <unity.h>

// Golden values captured from the pre-subset implementation (master behaviour)
// for seed 0x01020304 on the 80ch domain: subset disabled must stay
// bit-identical to this forever.
static const uint32_t GOLDEN_HASH = 0xA3991E4B;
static const uint32_t GOLDEN_INITIAL_FREQ = 12302619;
static const uint8_t GOLDEN_FIRST16[16] = {40, 7, 6, 65, 17, 2, 19, 59, 73, 43, 12, 31, 24, 23, 18, 25};

static const uint32_t SEED = 0x01020304;
static const uint8_t POISON = 0xEE; // outside every legal channel range

static uint32_t fnv1a(const uint8_t *data, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++)
        h = (h ^ data[i]) * 16777619u;
    return h;
}

// Frequency of a raw-grid channel: subset offsets never change the grid spacing
static uint32_t rawFreq(const fhss_config_t *config, uint32_t spread, uint32_t chIdx)
{
    return config->freq_start + (spread * chIdx / FREQ_SPREAD_SCALE);
}

// Native builds as SX128X, so the primary domain is 2.4GHz and the synthetic
// secondary stands in for sub-GHz. The band names appear only here.
static void setSubsets(uint8_t primaryStart, uint8_t primaryCount,
                       uint8_t secondaryStart, uint8_t secondaryCount)
{
    firmwareOptions.fhss_subset[FHSS_BAND_2G4] = {primaryStart, primaryCount};
    firmwareOptions.fhss_subset[FHSS_BAND_SUBGHZ] = {secondaryStart, secondaryCount};
}

// Entries in range + sync channel at every block start, over the band's OWN
// full sequence length (not just the shared dual-band prefix)
static void assertBandSequence(const uint8_t *seq, uint16_t bandCount, uint32_t effCount, uint_fast8_t syncCh)
{
    for (uint16_t i = 0; i < bandCount; i++)
    {
        TEST_ASSERT_LESS_THAN_UINT32(effCount, seq[i]);
        if (i % effCount == 0)
            TEST_ASSERT_EQUAL_UINT8(syncCh, seq[i]);
    }
}

// Per-channel occurrence skew over the used (shared-wrap) window must be <= 1
// even when the window cuts a block mid-way (the seam case)
static void assertFairness(const uint8_t *seq, uint16_t usedCount, uint32_t effCount)
{
    uint16_t occurrences[256] = {0};
    for (uint16_t i = 0; i < usedCount; i++)
        occurrences[seq[i]]++;
    uint16_t lo = UINT16_MAX, hi = 0;
    for (uint32_t ch = 0; ch < effCount; ch++)
    {
        if (occurrences[ch] < lo) lo = occurrences[ch];
        if (occurrences[ch] > hi) hi = occurrences[ch];
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(1, hi - lo);
}

// The full-band build is bit-identical to master's, so any fallback path can be
// checked against the same golden hash
static void assertFullBandFallback(void)
{
    TEST_ASSERT_FALSE(FHSSsubsetActive);
    TEST_ASSERT_EQUAL_HEX32(GOLDEN_HASH, fnv1a(FHSSsequence, 240));
}

// --- Regression pins (subset disabled == master behaviour) ---

void test_subset_disabled_bit_identical_golden(void)
{
    FHSSrandomiseFHSSsequence(SEED, false);
    TEST_ASSERT_FALSE(FHSSsubsetActive);
    TEST_ASSERT_EQUAL_UINT32(0, subset_offset);
    TEST_ASSERT_EQUAL_UINT32(80, FHSSgetChannelCount());
    TEST_ASSERT_EQUAL_UINT32(80, FHSSconfig->freq_count);
    TEST_ASSERT_EQUAL_UINT16(240, FHSSgetSequenceCount());
    TEST_ASSERT_EQUAL_UINT32(40, sync_channel);
    TEST_ASSERT_EQUAL_UINT32(GOLDEN_INITIAL_FREQ, FHSSgetInitialFreq());
    TEST_ASSERT_EQUAL_HEX32(GOLDEN_HASH, fnv1a(FHSSsequence, 240));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(GOLDEN_FIRST16, FHSSsequence, 16);
}

void test_use_subset_without_definition_matches_disabled(void)
{
    // useSubset=true with no valid definition is subset-off (D2 safety rule)
    FHSSrandomiseFHSSsequence(SEED, true);
    assertFullBandFallback();
}

void test_subset_configured_but_not_requested_stays_full_band(void)
{
    setSubsets(10, 17, 0, 0);
    FHSSrandomiseFHSSsequence(SEED, false);
    assertFullBandFallback();
}

// --- Single-band subset geometry ---

void test_subset_basic_geometry(void)
{
    setSubsets(10, 17, 0, 0);
    FHSSrandomiseFHSSsequence(SEED, true);

    TEST_ASSERT_TRUE(FHSSsubsetActive);
    TEST_ASSERT_EQUAL_UINT32(10, subset_offset);
    TEST_ASSERT_EQUAL_UINT32(17, FHSSgetChannelCount());
    TEST_ASSERT_EQUAL_UINT32(80, FHSSconfig->freq_count); // the raw domain is untouched
    TEST_ASSERT_EQUAL_UINT32(8, sync_channel);         // subsetCount / 2
    TEST_ASSERT_EQUAL_UINT16(255, FHSSgetSequenceCount()); // (256/17)*17
    assertBandSequence(FHSSsequence, 255, 17, 8);

    // initial (sync) frequency = raw-grid channel offset + sync
    TEST_ASSERT_EQUAL_UINT32(rawFreq(FHSSconfig, freq_spread, 10 + 8), FHSSgetInitialFreq());

    // determinism: identical config + seed => identical sequence
    uint8_t first[FHSS_SEQUENCE_LEN];
    memcpy(first, FHSSsequence, sizeof(first));
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, FHSSsequence, 255);
}

void test_subset_frequencies_on_full_band_grid(void)
{
    setSubsets(10, 17, 0, 0);
    FHSSrandomiseFHSSsequence(SEED, true);

    const uint32_t lo = rawFreq(FHSSconfig, freq_spread, 10);
    const uint32_t hi = rawFreq(FHSSconfig, freq_spread, 10 + 17 - 1);
    for (uint16_t i = 0; i < FHSSgetSequenceCount(); i++)
    {
        uint32_t freq = FHSSgetNextFreq();
        // every hop is exactly a full-band grid channel inside the subset
        TEST_ASSERT_EQUAL_UINT32(rawFreq(FHSSconfig, freq_spread, 10 + FHSSsequence[FHSSgetCurrIndex()]), freq);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(lo, freq);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(hi, freq);
    }
}

void test_subset_invalid_falls_back_to_full_band(void)
{
    // overruns the domain: 70 + 17 > 80
    setSubsets(70, 17, 0, 0);
    FHSSrandomiseFHSSsequence(SEED, true);
    assertFullBandFallback();

    // below the regulatory floor
    setSubsets(0, FHSS_SUBSET_MIN - 1, 0, 0);
    FHSSrandomiseFHSSsequence(SEED, true);
    assertFullBandFallback();
}

void test_subset_flush_with_top_of_band(void)
{
    setSubsets(65, 15, 0, 0); // 65 + 15 == 80, minimum size, flush with the top
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_TRUE(FHSSsubsetActive);
    TEST_ASSERT_EQUAL_UINT32(15, FHSSgetChannelCount());
    assertBandSequence(FHSSsequence, FHSSgetSequenceCount(), 15, 7);
    for (uint16_t i = 0; i < FHSSgetSequenceCount(); i++)
    {
        uint32_t freq = FHSSgetNextFreq();
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(rawFreq(FHSSconfig, freq_spread, 65), freq);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(FHSSconfig->freq_stop, freq);
    }
}

void test_full_band_as_subset_is_bit_identical(void)
{
    // a subset spanning the whole domain produces the master sequence exactly
    setSubsets(0, 80, 0, 0);
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_TRUE(FHSSsubsetActive);
    TEST_ASSERT_EQUAL_HEX32(GOLDEN_HASH, fnv1a(FHSSsequence, 240));
    TEST_ASSERT_EQUAL_UINT32(GOLDEN_INITIAL_FREQ, FHSSgetInitialFreq());
}

void test_gemini_partner_stays_inside_subset(void)
{
    setSubsets(10, 17, 0, 0);
    FHSSrandomiseFHSSsequence(SEED, true);
    const uint32_t lo = rawFreq(FHSSconfig, freq_spread, 10);
    const uint32_t hi = rawFreq(FHSSconfig, freq_spread, 10 + 17 - 1);
    for (uint16_t i = 0; i < FHSSgetSequenceCount(); i++)
    {
        uint32_t partner = FHSSGeminiFreq(FHSSsequence[i]);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(lo, partner);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(hi, partner);
    }
}

// --- Dual-band ---

// Full dual-band invariant sweep for one subset combination
static void checkDualBand(uint8_t s1, uint8_t c1, uint8_t s2, uint8_t c2)
{
    FHSSuseDualBand = false;    // build in the normal mode, as the firmware does
    setSubsets(s1, c1, s2, c2);
    FHSSrandomiseFHSSsequence(SEED, true);
    FHSSuseDualBand = true;

    const uint32_t eff1 = c1 ? c1 : FHSSconfig->freq_count;
    const uint32_t eff2 = c2 ? c2 : FHSSconfigDualBand->freq_count;
    const uint16_t band1 = (FHSS_SEQUENCE_LEN / eff1) * eff1;
    const uint16_t band2 = (FHSS_SEQUENCE_LEN / eff2) * eff2;
    const uint16_t shared = (band1 < band2) ? band1 : band2;

    TEST_ASSERT_EQUAL_UINT32(eff1, effective_freq_count);
    TEST_ASSERT_EQUAL_UINT32(eff2, effective_freq_count_DualBand);
    TEST_ASSERT_EQUAL_UINT16(band1, primaryBandCount);
    TEST_ASSERT_EQUAL_UINT16(band2, secondaryBandCount);
    TEST_ASSERT_EQUAL_UINT16(shared, FHSSgetSequenceCount());

    // each band's sequence is valid over its OWN full length, incl. past the
    // shared wrap point (proves the length-explicit builder covers everything)
    assertBandSequence(FHSSsequence, band1, eff1, eff1 / 2);
    assertBandSequence(FHSSsequence_DualBand, band2, eff2, eff2 / 2);

    // the seam: per-channel fairness inside the actually-used shared window,
    // even when it cuts one band's blocks mid-way (coprime counts)
    assertFairness(FHSSsequence, shared, eff1);
    assertFairness(FHSSsequence_DualBand, shared, eff2);

    // walk the shared sequence through the accessors: primary hop frequency
    // and the dual-band Gemini (secondary radio) frequency stay in-range
    const uint32_t lo1 = rawFreq(FHSSconfig, freq_spread, s1);
    const uint32_t hi1 = rawFreq(FHSSconfig, freq_spread, s1 + eff1 - 1);
    const uint32_t lo2 = rawFreq(FHSSconfigDualBand, freq_spread_DualBand, s2);
    const uint32_t hi2 = rawFreq(FHSSconfigDualBand, freq_spread_DualBand, s2 + eff2 - 1);
    for (uint16_t i = 0; i < shared; i++)
    {
        uint32_t f1 = FHSSgetNextFreq();
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(lo1, f1);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(hi1, f1);
        uint32_t f2 = FHSSgetGeminiFreq();
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(lo2, f2);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(hi2, f2);
    }
    TEST_ASSERT_EQUAL_UINT32(rawFreq(FHSSconfigDualBand, freq_spread_DualBand, s2 + eff2 / 2),
                             FHSSgetInitialGeminiFreq());
}

void test_dualband_coprime_combinations(void)
{
    static const uint8_t cases[][4] = {
        {10, 17, 5, 24},    // 255 vs 240: the runtime wrap lands mid-block on the primary
        { 3, 19, 0,  0},    // 247 vs full-band 240: seam on the primary, secondary disabled
        { 2, 23, 1, 37},    // 253 vs 222: the shared window cuts both bands' blocks
    };
    for (unsigned i = 0; i < ARRAY_SIZE(cases); i++)
    {
        checkDualBand(cases[i][0], cases[i][1], cases[i][2], cases[i][3]);
    }
}

void test_dualband_independent_fallback(void)
{
    // secondary defined but invalid (38 + 24 > 40): falls back alone
    setSubsets(10, 17, 38, 24);
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_TRUE(FHSSsubsetActive);
    TEST_ASSERT_FALSE(FHSSsubsetActive_DualBand);
    TEST_ASSERT_EQUAL_UINT32(17, effective_freq_count);
    TEST_ASSERT_EQUAL_UINT32(40, effective_freq_count_DualBand);
    TEST_ASSERT_EQUAL_UINT32(0, subset_offset_DualBand);

    // primary invalid, secondary valid: the other way around
    setSubsets(70, 17, 5, 24);
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_FALSE(FHSSsubsetActive);
    TEST_ASSERT_TRUE(FHSSsubsetActive_DualBand);
    TEST_ASSERT_EQUAL_UINT32(80, effective_freq_count);
    TEST_ASSERT_EQUAL_UINT32(24, effective_freq_count_DualBand);
}

void test_poison_rebuild_while_dualband_mode_active(void)
{
    // The historic hazard: a rebuild with FHSSuseDualBand==true used the
    // shared min() as the build length, leaving stale entries in the final
    // partial block. The length-explicit builder must be immune.
    memset(FHSSsequence, POISON, FHSS_SEQUENCE_LEN);
    memset(FHSSsequence_DualBand, POISON, FHSS_SEQUENCE_LEN);

    // first build with one geometry...
    setSubsets(10, 24, 0, 32);
    FHSSrandomiseFHSSsequence(SEED, true);

    // ...then rebuild with smaller subsets while dual-band mode is ACTIVE
    setSubsets(12, 17, 5, 24);
    FHSSuseDualBand = true;
    FHSSrandomiseFHSSsequence(SEED, true);

    // every band-owned entry is from the NEW geometry: no poison, no stale
    // values from the previous (wider) subsets
    assertBandSequence(FHSSsequence, 255, 17, 8);
    assertBandSequence(FHSSsequence_DualBand, 240, 24, 12);

    // and the result is bit-identical to a fresh single-mode build: the
    // builder must not consult runtime band-mode state at all
    uint8_t inDual[FHSS_SEQUENCE_LEN], inDual2[FHSS_SEQUENCE_LEN];
    memcpy(inDual, FHSSsequence, sizeof(inDual));
    memcpy(inDual2, FHSSsequence_DualBand, sizeof(inDual2));
    FHSSuseDualBand = false;
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(inDual, FHSSsequence, FHSS_SEQUENCE_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(inDual2, FHSSsequence_DualBand, FHSS_SEQUENCE_LEN);
}

// --- geometry hash and the packet CRC seed ---

// The hash one band mode would see, without leaving that mode selected
static uint16_t hashInMode(bool usePrimary, bool useDual)
{
    const bool wasPrimary = FHSSusePrimaryFreqBand;
    const bool wasDual = FHSSuseDualBand;
    FHSSusePrimaryFreqBand = usePrimary;
    FHSSuseDualBand = useDual;
    const uint16_t hash = FHSSgetGeometryHash();
    FHSSusePrimaryFreqBand = wasPrimary;
    FHSSuseDualBand = wasDual;
    return hash;
}
static uint16_t hashPrimary(void)   { return hashInMode(true, false); }
static uint16_t hashSecondary(void) { return hashInMode(false, false); }
static uint16_t hashDual(void)      { return hashInMode(true, true); }

// Seed the CRC the way SetRFLinkRate() does, and return the initializer
static uint16_t crcSeedFor(uint8_t start, uint8_t count, bool useSubset)
{
    setSubsets(start, count, 0, 0);
    FHSSrandomiseFHSSsequence(SEED, useSubset);
    OtaUpdateCrcInit(false, FHSSgetGeometryHash());
    return OtaCrcInitializer;
}

// Build a packet under the seed in force, then check it against another one
static bool packetSurvivesSeed(uint16_t buildSeed, uint16_t checkSeed, uint8_t packetSize)
{
    OTA_Packet_s pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.std.type = PACKET_TYPE_RCDATA;
    OtaNonce = 7;
    OtaUpdateSerializers(smWideOr8ch, packetSize);

    OtaCrcInitializer = buildSeed;
    OtaGeneratePacketCrc(&pkt);
    OtaCrcInitializer = checkSeed;
    return OtaValidatePacketCrc(&pkt);
}

void test_geometry_hash_zero_without_subset(void)
{
    // in every band mode there is nothing to fold into the seed
    FHSSrandomiseFHSSsequence(SEED, false);
    TEST_ASSERT_EQUAL_UINT16(0, hashPrimary());
    TEST_ASSERT_EQUAL_UINT16(0, hashSecondary());
    TEST_ASSERT_EQUAL_UINT16(0, hashDual());
}

void test_crc_seed_unchanged_without_subset(void)
{
    // The whole feature has to be invisible on the air when no subset is in
    // force: an unmodified receiver and this one must agree on the seed.
    UID[4] = 0x56;
    UID[5] = 0x78;
    FHSSrandomiseFHSSsequence(SEED, false);
    OtaUpdateCrcInit(false, FHSSgetGeometryHash());
    const uint16_t expected = (uint16_t)((UID[4] << 8) | UID[5]) ^ ((uint16_t)OTA_VERSION_ID << 8);
    TEST_ASSERT_EQUAL_UINT16(expected, OtaCrcInitializer);

    // and binding is geometry-free on both ends, whatever is configured
    setSubsets(10, 17, 5, 24);
    FHSSrandomiseFHSSsequence(SEED, true);
    OtaUpdateCrcInit(true, 0);
    TEST_ASSERT_EQUAL_UINT16(OTA_VERSION_ID, OtaCrcInitializer);
}

void test_crc_seed_separates_geometries(void)
{
    UID[4] = 0x56;
    UID[5] = 0x78;

    const uint16_t full = crcSeedFor(0, 0, false);
    const uint16_t subsetA = crcSeedFor(10, 17, true);
    const uint16_t subsetB = crcSeedFor(11, 17, true);
    const uint16_t subsetC = crcSeedFor(10, 24, true);

    // every distinct geometry seeds the CRC differently, including against
    // plain full band in both directions
    TEST_ASSERT_NOT_EQUAL_UINT16(full, subsetA);
    TEST_ASSERT_NOT_EQUAL_UINT16(subsetA, subsetB);
    TEST_ASSERT_NOT_EQUAL_UINT16(subsetA, subsetC);
    TEST_ASSERT_NOT_EQUAL_UINT16(subsetB, subsetC);

    // the same definition rebuilt is the same seed, so an unrelated config
    // commit cannot cost a link
    TEST_ASSERT_EQUAL_UINT16(subsetA, crcSeedFor(10, 17, true));

    // a subset that cannot be honoured transmits full band and must seed as
    // full band, or a correctly configured receiver could never follow it
    TEST_ASSERT_EQUAL_UINT16(full, crcSeedFor(70, 17, true)); // 70 + 17 > 80
    assertFullBandFallback();
}

void test_mismatched_geometry_fails_the_packet_crc(void)
{
    // The point of seeding the CRC: a receiver on a different hop plan cannot
    // validate a packet even when it lands on a shared frequency. Both packet
    // widths, because OTA4 uses a 14-bit CRC that drops the seed's top 2 bits.
    UID[4] = 0x56;
    UID[5] = 0x78;
    const uint16_t full = crcSeedFor(0, 0, false);
    const uint16_t subsetA = crcSeedFor(10, 17, true);
    const uint16_t subsetB = crcSeedFor(11, 17, true);

    for (uint8_t packetSize : {OTA4_PACKET_SIZE, OTA8_PACKET_SIZE})
    {
        TEST_ASSERT_TRUE(packetSurvivesSeed(subsetA, subsetA, packetSize));
        TEST_ASSERT_FALSE(packetSurvivesSeed(subsetA, subsetB, packetSize));
        TEST_ASSERT_FALSE(packetSurvivesSeed(subsetA, full, packetSize));
        TEST_ASSERT_FALSE(packetSurvivesSeed(full, subsetA, packetSize));
    }
}

void test_geometry_hash_mode_selection(void)
{
    setSubsets(10, 17, 5, 24);
    FHSSrandomiseFHSSsequence(SEED, true);

    // every band mode hashes to something, and the accessor follows the mode
    TEST_ASSERT_NOT_EQUAL_UINT16(0, hashPrimary());
    TEST_ASSERT_NOT_EQUAL_UINT16(0, hashSecondary());
    TEST_ASSERT_NOT_EQUAL_UINT16(0, hashDual());

    // mode hashes describe different geometry sets, so they must differ
    TEST_ASSERT_NOT_EQUAL(hashPrimary(), hashDual());
    TEST_ASSERT_NOT_EQUAL(hashSecondary(), hashDual());
}

void test_geometry_hash_depends_only_on_effective_geometry(void)
{
    // The same physical 2.4GHz plan is the primary domain on an SX128X and the
    // dual-band domain on an LR1121, so a 2.4GHz link between the two runs one
    // side's primary hash against the other's secondary hash. Equal subsets
    // must therefore produce equal hashes: the hash keys on the effective
    // geometry, not on which domain table the band happened to come from.
    setSubsets(10, 17, 10, 17);
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_EQUAL_UINT16(hashPrimary(), hashSecondary());

    // and differing subsets still separate them
    setSubsets(10, 17, 11, 17);
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_NOT_EQUAL(hashPrimary(), hashSecondary());
}

void test_geometry_hash_full_band_subset_is_discriminated(void)
{
    // a subset spanning the whole domain has identical RF geometry to plain
    // full-band, but the hash must still discriminate the two modes
    setSubsets(0, 80, 0, 0);
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_NOT_EQUAL_UINT16(0, hashPrimary());
}

void test_geometry_hash_ignores_a_band_the_mode_does_not_use(void)
{
    // The transmitter forces a new epoch when the active mode's hash moves.
    // A change confined to a band this mode never transmits on leaves the air
    // byte-identical, so it must read as no change: forcing an epoch there
    // would cost a link for nothing.
    setSubsets(10, 17, 5, 24);
    FHSSrandomiseFHSSsequence(SEED, true);
    const uint16_t primaryHash = FHSSgetGeometryHash();

    setSubsets(10, 17, 6, 24);
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_EQUAL_UINT16(primaryHash, FHSSgetGeometryHash());
}

void test_effective_channel_count_per_band(void)
{
    setSubsets(10, 17, 5, 24);
    FHSSrandomiseFHSSsequence(SEED, true);
    TEST_ASSERT_EQUAL_UINT32(17, FHSSgetChannelCount());
    FHSSusePrimaryFreqBand = false;
    TEST_ASSERT_EQUAL_UINT32(24, FHSSgetChannelCount());
    FHSSusePrimaryFreqBand = true;
}

// --- options parse-time validation ---

void test_subset_disabled_is_valid(void)
{
    TEST_ASSERT_TRUE(FHSSsubsetIsValid(0, 0, 80));
    TEST_ASSERT_TRUE(FHSSsubsetIsValid(200, 0, 80)); // count 0 disables regardless of start
}

void test_subset_minimum_count(void)
{
    TEST_ASSERT_FALSE(FHSSsubsetIsValid(0, 1, 80));
    TEST_ASSERT_FALSE(FHSSsubsetIsValid(0, FHSS_SUBSET_MIN - 1, 80));
    TEST_ASSERT_TRUE(FHSSsubsetIsValid(0, FHSS_SUBSET_MIN, 80));
}

void test_subset_range_bounds(void)
{
    TEST_ASSERT_TRUE(FHSSsubsetIsValid(0, 80, 80));                                  // full band as a subset
    TEST_ASSERT_TRUE(FHSSsubsetIsValid(80 - FHSS_SUBSET_MIN, FHSS_SUBSET_MIN, 80));  // flush with the top
    TEST_ASSERT_FALSE(FHSSsubsetIsValid(81 - FHSS_SUBSET_MIN, FHSS_SUBSET_MIN, 80)); // one past the top
    TEST_ASSERT_FALSE(FHSSsubsetIsValid(0, 81, 80));                                 // wider than the band
}

void test_subset_small_domain(void)
{
    // domains with fewer channels than the minimum cannot host a subset at all
    TEST_ASSERT_FALSE(FHSSsubsetIsValid(0, 13, 13)); // EU868: 13 < FHSS_SUBSET_MIN
    TEST_ASSERT_TRUE(FHSSsubsetIsValid(0, 0, 13));   // but disabled stays valid
    TEST_ASSERT_TRUE(FHSSsubsetIsValid(0, 15, 20));  // AU915: minimum fits
}

void test_subset_structural_bound(void)
{
    // parse-time bound before the real domain is known; the last channel a
    // uint8_t start or count can name is 255
    TEST_ASSERT_TRUE(FHSSsubsetIsValid(199, 56, FHSS_SUBSET_MAX_CHANNELS));
    TEST_ASSERT_FALSE(FHSSsubsetIsValid(200, 56, FHSS_SUBSET_MAX_CHANNELS));
}

// The handset editing path: the parameter layers on both sides go through these
// rather than reaching into firmwareOptions themselves
void test_band_channels_per_domain(void)
{
    FHSSrandomiseFHSSsequence(SEED, false);
    TEST_ASSERT_EQUAL_UINT32(80, FHSSsubsetBandChannels(FHSS_BAND_2G4));
    TEST_ASSERT_EQUAL_UINT32(40, FHSSsubsetBandChannels(FHSS_BAND_SUBGHZ));
}

void test_set_band_subset_refuses_what_does_not_fit(void)
{
    FHSSrandomiseFHSSsequence(SEED, false);
    uint8_t start, count;

    TEST_ASSERT_TRUE(FHSSsetBandSubset(FHSS_BAND_2G4, 10, 17));
    FHSSgetBandSubset(FHSS_BAND_2G4, &start, &count);
    TEST_ASSERT_EQUAL_UINT8(10, start);
    TEST_ASSERT_EQUAL_UINT8(17, count);

    // below the regulatory floor, and past the end of the domain
    TEST_ASSERT_FALSE(FHSSsetBandSubset(FHSS_BAND_2G4, 10, FHSS_SUBSET_MIN - 1));
    TEST_ASSERT_FALSE(FHSSsetBandSubset(FHSS_BAND_2G4, 70, 20));

    // a refused write leaves the stored pair untouched
    FHSSgetBandSubset(FHSS_BAND_2G4, &start, &count);
    TEST_ASSERT_EQUAL_UINT8(10, start);
    TEST_ASSERT_EQUAL_UINT8(17, count);

    // disabling is always valid, and drops the start with it
    TEST_ASSERT_TRUE(FHSSsetBandSubset(FHSS_BAND_2G4, 10, 0));
    FHSSgetBandSubset(FHSS_BAND_2G4, &start, &count);
    TEST_ASSERT_EQUAL_UINT8(0, start);
    TEST_ASSERT_EQUAL_UINT8(0, count);
}

// What the parameter layer renders for the pair a band currently holds
static void describeStored(fhss_band_e band, char *dst, uint8_t maxlen)
{
    uint8_t start, count;
    FHSSgetBandSubset(band, &start, &count);
    FHSSdescribeSubset(band, start, count, dst, maxlen);
}

void test_describe_band_subset(void)
{
    FHSSrandomiseFHSSsequence(SEED, false);
    char hint[FHSS_SUBSET_HINT_LEN];

    setSubsets(0, 0, 0, 0);
    describeStored(FHSS_BAND_2G4, hint, sizeof(hint));
    TEST_ASSERT_EQUAL_STRING("full band", hint);

    setSubsets(10, 17, 0, 0);
    describeStored(FHSS_BAND_2G4, hint, sizeof(hint));
    TEST_ASSERT_EQUAL_STRING("10-26/80", hint);

    // A pair baked into options.json clears the parser's structural bound of 255
    // but not the real domain, which is the one bad stored pair describe sees.
    setSubsets(70, 20, 0, 0);
    describeStored(FHSS_BAND_2G4, hint, sizeof(hint));
    TEST_ASSERT_EQUAL_STRING("past 79", hint);
}

// Every line has to fit the value column of a 128px handset, which is about
// nine characters; the frequencies are on the editable fields, not here
void test_describe_subset_fits_the_display(void)
{
    FHSSrandomiseFHSSsequence(SEED, false);
    char hint[FHSS_SUBSET_HINT_LEN];

    const uint8_t pairs[][2] = {{0, 0}, {10, 17}, {70, 20}, {10, 1}, {0, 80}, {65, 15}};
    for (auto &pair : pairs)
    {
        FHSSdescribeSubset(FHSS_BAND_2G4, pair[0], pair[1], hint, sizeof(hint));
        TEST_ASSERT_LESS_OR_EQUAL_UINT(10, strlen(hint));
    }
}

void test_describe_subset_explains_a_pair_that_was_never_stored(void)
{
    // What a refused write shows: nothing reached storage, so the reason has to
    // come from describing the offered pair rather than the stored one.
    FHSSrandomiseFHSSsequence(SEED, false);
    char hint[FHSS_SUBSET_HINT_LEN];
    setSubsets(10, 17, 0, 0);

    FHSSdescribeSubset(FHSS_BAND_2G4, 10, FHSS_SUBSET_MIN - 1, hint, sizeof(hint));
    TEST_ASSERT_EQUAL_STRING("min 15ch", hint);

    FHSSdescribeSubset(FHSS_BAND_2G4, 70, 17, hint, sizeof(hint));
    TEST_ASSERT_EQUAL_STRING("past 79", hint);

    // and describing a legal pair reads the same as a stored one would
    FHSSdescribeSubset(FHSS_BAND_2G4, 45, 20, hint, sizeof(hint));
    TEST_ASSERT_EQUAL_STRING("45-64/80", hint);

    // the stored pair is untouched by any of that
    describeStored(FHSS_BAND_2G4, hint, sizeof(hint));
    TEST_ASSERT_EQUAL_STRING("10-26/80", hint);
}

// The grid the From/To fields are dialled on. Both directions have to use the
// same arithmetic, or a value the handset sends back lands on a different
// channel from the one it was showing.
void test_band_axis_matches_the_hop_grid(void)
{
    FHSSrandomiseFHSSsequence(SEED, false);
    uint32_t startKhz, stepKhz;

    TEST_ASSERT_TRUE(FHSSsubsetBandAxis(FHSS_BAND_2G4, &startKhz, &stepKhz));
    TEST_ASSERT_EQUAL_UINT32(2400400, startKhz);
    TEST_ASSERT_EQUAL_UINT32(1000, stepKhz);
    // the far end of the band is exactly the table's freq_stop, so the step has
    // not accumulated rounding error across 80 channels
    TEST_ASSERT_EQUAL_UINT32(2479400, startKhz + 79 * stepKhz);

    TEST_ASSERT_TRUE(FHSSsubsetBandAxis(FHSS_BAND_SUBGHZ, &startKhz, &stepKhz));
    TEST_ASSERT_EQUAL_UINT32(903500, startKhz);
    TEST_ASSERT_EQUAL_UINT32(600, stepKhz);
    TEST_ASSERT_EQUAL_UINT32(926900, startKhz + 39 * stepKhz);
}

// What the write callbacks do with the kHz the handset sends back
void test_channel_survives_the_round_trip_through_khz(void)
{
    FHSSrandomiseFHSSsequence(SEED, false);
    const fhss_band_e bands[] = {FHSS_BAND_2G4, FHSS_BAND_SUBGHZ};

    for (auto band : bands)
    {
        uint32_t startKhz, stepKhz;
        TEST_ASSERT_TRUE(FHSSsubsetBandAxis(band, &startKhz, &stepKhz));
        const uint32_t channels = FHSSsubsetBandChannels(band);
        for (uint32_t ch = 0; ch < channels; ch++)
        {
            TEST_ASSERT_EQUAL_UINT8(ch, FHSSsubsetChannelForKhz(band, startKhz + ch * stepKhz));
        }
        // below the band, and above it: incrField() clamps to max rather than
        // landing on a step boundary, so the top must not wrap to channel 0
        TEST_ASSERT_EQUAL_UINT8(0, FHSSsubsetChannelForKhz(band, startKhz - stepKhz));
        TEST_ASSERT_EQUAL_UINT8(channels - 1, FHSSsubsetChannelForKhz(band, startKhz + (channels + 4) * stepKhz));
        // and half a channel either side of a grid point still resolves to it
        TEST_ASSERT_EQUAL_UINT8(7, FHSSsubsetChannelForKhz(band, startKhz + 7 * stepKhz + stepKhz / 3));
        TEST_ASSERT_EQUAL_UINT8(7, FHSSsubsetChannelForKhz(band, startKhz + 7 * stepKhz - stepKhz / 3));
    }
}

// Unity setup/teardown
void setUp()
{
    setSubsets(0, 0, 0, 0);
    FHSSuseDualBand = false;
    FHSSusePrimaryFreqBand = true;
    FreqCorrection = 0;
    FreqCorrection_2 = 0;
}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_subset_disabled_bit_identical_golden);
    RUN_TEST(test_use_subset_without_definition_matches_disabled);
    RUN_TEST(test_subset_configured_but_not_requested_stays_full_band);
    RUN_TEST(test_subset_basic_geometry);
    RUN_TEST(test_subset_frequencies_on_full_band_grid);
    RUN_TEST(test_subset_invalid_falls_back_to_full_band);
    RUN_TEST(test_subset_flush_with_top_of_band);
    RUN_TEST(test_full_band_as_subset_is_bit_identical);
    RUN_TEST(test_gemini_partner_stays_inside_subset);
    RUN_TEST(test_dualband_coprime_combinations);
    RUN_TEST(test_dualband_independent_fallback);
    RUN_TEST(test_poison_rebuild_while_dualband_mode_active);
    RUN_TEST(test_geometry_hash_zero_without_subset);
    RUN_TEST(test_crc_seed_unchanged_without_subset);
    RUN_TEST(test_crc_seed_separates_geometries);
    RUN_TEST(test_mismatched_geometry_fails_the_packet_crc);
    RUN_TEST(test_geometry_hash_mode_selection);
    RUN_TEST(test_geometry_hash_depends_only_on_effective_geometry);
    RUN_TEST(test_geometry_hash_full_band_subset_is_discriminated);
    RUN_TEST(test_geometry_hash_ignores_a_band_the_mode_does_not_use);
    RUN_TEST(test_effective_channel_count_per_band);
    RUN_TEST(test_subset_disabled_is_valid);
    RUN_TEST(test_subset_minimum_count);
    RUN_TEST(test_subset_range_bounds);
    RUN_TEST(test_subset_small_domain);
    RUN_TEST(test_subset_structural_bound);
    RUN_TEST(test_band_channels_per_domain);
    RUN_TEST(test_set_band_subset_refuses_what_does_not_fit);
    RUN_TEST(test_describe_band_subset);
    RUN_TEST(test_describe_subset_fits_the_display);
    RUN_TEST(test_describe_subset_explains_a_pair_that_was_never_stored);
    RUN_TEST(test_band_axis_matches_the_hop_grid);
    RUN_TEST(test_channel_survives_the_round_trip_through_khz);
    UNITY_END();

    return 0;
}
