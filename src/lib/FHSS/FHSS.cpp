#include "FHSS.h"
#include "fhss_subset.h"
#include "logging.h"
#include "options.h"
#include <string.h>

#if defined(UNIT_TEST)
#define POWER_OUTPUT_VALUES_COUNT 4
#define POWER_OUTPUT_VALUES_DUAL_COUNT 0
#endif

#if defined(RADIO_SX127X) || defined(RADIO_LR1121)

#if defined(RADIO_LR1121)
#include "LR1121Driver.h"
#else
#include "SX127xDriver.h"
#endif

const fhss_config_t domains[] = {
    {"AU915",  FREQ_HZ_TO_REG_VAL(915500000), FREQ_HZ_TO_REG_VAL(926900000), 20, 921000000},
    {"FCC915", FREQ_HZ_TO_REG_VAL(903500000), FREQ_HZ_TO_REG_VAL(926900000), 40, 915000000},
    {"EU868",  FREQ_HZ_TO_REG_VAL(863275000), FREQ_HZ_TO_REG_VAL(869575000), 13, 868000000},
    {"IN866",  FREQ_HZ_TO_REG_VAL(865375000), FREQ_HZ_TO_REG_VAL(866950000), 4, 866000000},
    {"AU433",  FREQ_HZ_TO_REG_VAL(433420000), FREQ_HZ_TO_REG_VAL(434420000), 3, 434000000},
    {"EU433",  FREQ_HZ_TO_REG_VAL(433100000), FREQ_HZ_TO_REG_VAL(434450000), 3, 434000000},
    {"US433",  FREQ_HZ_TO_REG_VAL(433250000), FREQ_HZ_TO_REG_VAL(438000000), 8, 434000000},
    {"US433W",  FREQ_HZ_TO_REG_VAL(423500000), FREQ_HZ_TO_REG_VAL(438000000), 20, 434000000},
};

#if defined(FHSS_HAS_DUAL_BAND)
const fhss_config_t domainsDualBand[] = {
    {
    #if defined(Regulatory_Domain_EU_CE_2400)
        "CE_LBT",
    #else
        "ISM2G4",
    #endif
    FREQ_HZ_TO_REG_VAL(2400400000), FREQ_HZ_TO_REG_VAL(2479400000), 80, 2440000000}
};
#endif

#elif defined(RADIO_SX128X)
#include "SX1280Driver.h"

const fhss_config_t domains[] = {
    {
    #if defined(Regulatory_Domain_EU_CE_2400)
        "CE_LBT",
    #elif defined(Regulatory_Domain_ISM_2400)
        "ISM2G4",
    #endif
    FREQ_HZ_TO_REG_VAL(2400400000), FREQ_HZ_TO_REG_VAL(2479400000), 80, 2440000000}
};

#if defined(UNIT_TEST)
// A synthetic secondary domain, so the native tests can reach the dual-band code
// paths. Real dual-band hardware is LR1121-only and never compiles this arm.
const fhss_config_t domainsDualBand[] = {
    {"FCC915", FREQ_HZ_TO_REG_VAL(903500000), FREQ_HZ_TO_REG_VAL(926900000), 40, 915000000},
};
#endif
#endif

// Our table of FHSS frequencies. Define a regulatory domain to select the correct set for your location and radio
const fhss_config_t *FHSSconfig;
const fhss_config_t *FHSSconfigDualBand;

// Actual sequence of hops as indexes into the frequency list
uint8_t FHSSsequence[FHSS_SEQUENCE_LEN];
uint8_t FHSSsequence_DualBand[FHSS_SEQUENCE_LEN];

// Which entry in the sequence we currently are on
uint8_t volatile FHSSptr;

// Channel for sync packets and initial connection establishment
uint_fast8_t sync_channel;
uint_fast8_t sync_channel_DualBand;

// Offset from the predefined frequency determined by AFC on Team900 (register units)
int32_t FreqCorrection;
int32_t FreqCorrection_2;

// Frequency hop separation
uint32_t freq_spread;
uint32_t freq_spread_DualBand;

// Variable for Dual Band radios
bool FHSSusePrimaryFreqBand = true;
bool FHSSuseDualBand = false;

uint16_t primaryBandCount;
uint16_t secondaryBandCount;

#if defined(USE_FHSS_SUBSET)
// Effective (possibly subset-restricted) geometry, per band (see FHSS.h)
bool FHSSsubsetActive;
uint_fast8_t subset_offset;
uint32_t effective_freq_count;
uint32_t effective_freq_count_DualBand;

// Per-band-mode geometry hashes (see FHSS.h)
uint16_t FHSSgeometryHashPrimary;
#if defined(FHSS_HAS_DUAL_BAND)
bool FHSSsubsetActive_DualBand;
uint_fast8_t subset_offset_DualBand;
uint16_t FHSSgeometryHashSecondary;
uint16_t FHSSgeometryHashDual;
#endif
#endif

constexpr uint8_t VERSION_DOMAIN_MAXLEN = 26 + 1;   // max. number of characters (plus '\0') the Lua script can display
                                                    // on color LCD radios w/o being overwritten by the commit info
char version_domain[VERSION_DOMAIN_MAXLEN] {};

#if defined(USE_FHSS_SUBSET)
// Which physical band each domain table carries is the one radio-family fact
// this feature needs: on an SX128X the primary domain is 2.4GHz, everywhere
// else it is sub-GHz and the dual-band table is 2.4GHz.
#if defined(RADIO_SX128X)
#define PRIMARY_SUBSET(field)   firmwareOptions.fhss_subset_2g4_##field
#define SECONDARY_SUBSET(field) firmwareOptions.fhss_subset_subghz_##field
#else
#define PRIMARY_SUBSET(field)   firmwareOptions.fhss_subset_subghz_##field
#define SECONDARY_SUBSET(field) firmwareOptions.fhss_subset_2g4_##field
#endif

static uint16_t geometryCrc16(const uint8_t *data, uint8_t len)
{
    // CRC16-CCITT, bitwise: only six bytes to hash, too few to repay lib/CRC's
    // 256-entry table
    uint16_t crc = 0xFFFF;
    while (len--)
    {
        crc ^= (uint16_t)(*data++) << 8;
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

// Hash the effective geometry of the band(s) one band mode uses. 0 when no
// included band runs a subset; a hash landing on zero is nudged off it, since
// an active subset must never read as plain full band. The domain table a band
// came from is deliberately not an input, so that two radios holding the same
// band in different tables still agree.
static uint16_t geometryHashCompute(const bool includePrimary, const bool includeSecondary)
{
    const bool primary = includePrimary && FHSSsubsetActive;
    const bool secondary = includeSecondary && FHSSsubsetActive_DualBand;
    if (!primary && !secondary)
        return 0;

    uint8_t geometry[6];
    uint8_t len = 0;
    if (includePrimary)
    {
        geometry[len++] = FHSSsubsetActive;
        geometry[len++] = subset_offset;
        geometry[len++] = effective_freq_count;
    }
    if (includeSecondary)
    {
        geometry[len++] = FHSSsubsetActive_DualBand;
        geometry[len++] = subset_offset_DualBand;
        geometry[len++] = effective_freq_count_DualBand;
    }
    const uint16_t hash = geometryCrc16(geometry, len);
    return hash ? hash : 0xFFFF;
}

// Only bands this radio actually has count: a sub-GHz subset left in the
// options of a 2.4GHz-only receiver must not cost it a subset acquisition pass
bool FHSSsubsetConfigured(void)
{
#if defined(FHSS_HAS_DUAL_BAND)
    return PRIMARY_SUBSET(count) != 0 || SECONDARY_SUBSET(count) != 0;
#else
    return PRIMARY_SUBSET(count) != 0;
#endif
}

// Compute one band's effective geometry: the options-defined subset when it is
// requested, present, and fits this domain; the full domain otherwise. Each
// band falls back independently.
static bool FHSSapplySubset(const bool useSubset, const uint32_t subsetStart, const uint32_t subsetCount,
                            const fhss_config_t *config, uint_fast8_t *offsetOut, uint32_t *countOut)
{
    if (useSubset && subsetCount != 0 && FHSSsubsetIsValid(subsetStart, subsetCount, config->freq_count))
    {
        *offsetOut = subsetStart;
        *countOut = subsetCount;
        return true;
    }
    *offsetOut = 0;
    *countOut = config->freq_count;
    return false;
}

// Would a rebuild that requests the subset actually restrict a band the
// current band mode has on air? The same judgement FHSSapplySubset makes at
// build time, made before any build, so the acquisition scan can settle its
// phase first and build the right geometry once. Band inclusion mirrors
// FHSSgetGeometryHash's mode selection.
bool FHSSsubsetWouldApply(void)
{
    uint_fast8_t offset;
    uint32_t count;
    const bool primaryFits = FHSSapplySubset(true, PRIMARY_SUBSET(start), PRIMARY_SUBSET(count),
                                             &domains[firmwareOptions.domain], &offset, &count);
#if defined(FHSS_HAS_DUAL_BAND)
    const bool secondaryFits = FHSSapplySubset(true, SECONDARY_SUBSET(start), SECONDARY_SUBSET(count),
                                               &domainsDualBand[0], &offset, &count);
    if (FHSSuseDualBand)
    {
        return primaryFits || secondaryFits;
    }
    return FHSSusePrimaryFreqBand ? primaryFits : secondaryFits;
#else
    return primaryFits;
#endif
}
#endif // USE_FHSS_SUBSET

void FHSSrandomiseFHSSsequence(const uint32_t seed, const bool useSubset)
{
    // the hop pointer indexes sequences that are about to be replaced
    FHSSptr = 0;

    FHSSconfig = &domains[firmwareOptions.domain];
    freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfig->freq_count - 1);
#if defined(USE_FHSS_SUBSET)
    FHSSsubsetActive = FHSSapplySubset(useSubset, PRIMARY_SUBSET(start), PRIMARY_SUBSET(count),
                                       FHSSconfig, &subset_offset, &effective_freq_count);
    const uint32_t freqCount = effective_freq_count;
#else
    (void)useSubset;
    const uint32_t freqCount = FHSSconfig->freq_count;
#endif
    sync_channel = freqCount / 2;

    DBGLN("Primary Domain %s, %u channels, sync=%u",
        FHSSconfig->domain, FHSSconfig->freq_count, sync_channel);
    if (FHSSsubsetActive)
        DBGLN("Primary subset active: channels %u-%u", subset_offset, subset_offset + freqCount - 1);

    primaryBandCount = FHSSrandomiseFHSSsequenceBuild(seed, freqCount, sync_channel, FHSSsequence);

#if defined(FHSS_HAS_DUAL_BAND)
    FHSSconfigDualBand = &domainsDualBand[0];
    freq_spread_DualBand = (FHSSconfigDualBand->freq_stop - FHSSconfigDualBand->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfigDualBand->freq_count - 1);
#if defined(USE_FHSS_SUBSET)
    FHSSsubsetActive_DualBand = FHSSapplySubset(useSubset, SECONDARY_SUBSET(start), SECONDARY_SUBSET(count),
                                                FHSSconfigDualBand, &subset_offset_DualBand, &effective_freq_count_DualBand);
    const uint32_t freqCountDualBand = effective_freq_count_DualBand;
#else
    const uint32_t freqCountDualBand = FHSSconfigDualBand->freq_count;
#endif
    sync_channel_DualBand = freqCountDualBand / 2;

    DBGLN("Dual Domain %s, %u channels, sync=%u",
        FHSSconfigDualBand->domain, FHSSconfigDualBand->freq_count, sync_channel_DualBand);
    if (FHSSsubsetActive_DualBand)
        DBGLN("Dual subset active: channels %u-%u", subset_offset_DualBand, subset_offset_DualBand + freqCountDualBand - 1);

    secondaryBandCount = FHSSrandomiseFHSSsequenceBuild(seed, freqCountDualBand, sync_channel_DualBand, FHSSsequence_DualBand);
#endif

#if defined(USE_FHSS_SUBSET)
    // Per-band-mode geometry hashes from the geometry just built
    FHSSgeometryHashPrimary = geometryHashCompute(true, false);
#if defined(FHSS_HAS_DUAL_BAND)
    FHSSgeometryHashSecondary = geometryHashCompute(false, true);
    FHSSgeometryHashDual = geometryHashCompute(true, true);
#endif
#endif

    // add frequency and regulatory domain to the string used by the Lua script
    addDomainInfo(version_domain, VERSION_DOMAIN_MAXLEN);
}

/**
Requirements:
1. 0 every n hops
2. No two repeated channels
3. Equal occurance of each (or as even as possible) of each channel
4. Pseudorandom

Approach:
  Fill the sequence array with the sync channel every FHSS_FREQ_CNT
  Iterate through the array, and for each block, swap each entry in it with
  another random entry, excluding the sync channel.

*/
uint16_t FHSSrandomiseFHSSsequenceBuild(const uint32_t seed, const uint32_t freqCount, const uint_fast8_t syncChannel, uint8_t *inSequence)
{
    const uint16_t sequenceCount = (FHSS_SEQUENCE_LEN / freqCount) * freqCount;

    rngSeed(seed);

    // Blocks are filled and shuffled independently, so doing both in one pass
    // draws the same random numbers in the same order as two separate walks
    for (uint16_t base = 0; base < sequenceCount; base += freqCount)
    {
        // initialize the block
        for (uint32_t j = 0; j < freqCount; j++)
            inSequence[base + j] = j;
        // the sync channel leads every block and the entry it displaced takes
        // the 0. A sync channel outside the block leaves the 0 unplaced rather
        // than writing past the block, which is what the old modulo form did
        inSequence[base] = syncChannel;
        if (syncChannel < freqCount)
            inSequence[base + syncChannel] = 0;

        // entry 0 of each block is the sync channel and stays put
        for (uint32_t j = 1; j < freqCount; j++)
        {
            uint8_t rand = rngN(freqCount - 1) + 1;         // random number between 1 and FHSS_FREQ_CNT

            // switch this entry and another random entry in the same block
            uint8_t temp = inSequence[base + j];
            inSequence[base + j] = inSequence[base + rand];
            inSequence[base + rand] = temp;
        }
    }

    return sequenceCount;
}

/**
 * @brief Add frequency and regulatory domain to the version string used by the Lua script. Outputs the version_domain string as:
 * [version:0..20] [subGHz domain | 2.4GHz domain] truncated to maxlen-1 for single band devices
 * [version:0..20] [subGHz domain]/[2.4GHz domain] truncated to maxlen-1 for dual band devices
 * Examples:
 *   4.0.0 CE_LBT
 *   4.1.7 AU915
 *   4.11.17 FCC915/ISM2G4
 *   someBranch EU868/CE_LBT
 *
 * @param version_domain a pointer to a buffer holding the version and extra space for additional data
 * @param maxlen the size of the provided buffer
 */
void addDomainInfo(char *version_domain, uint8_t maxlen)
{
    if (strlen(version) < 21)
    {
        strlcpy(version_domain, version, 21);
        strlcat(version_domain, " ", maxlen);
    }
    else
    {
        strlcpy(version_domain, version, 18);
        strlcat(version_domain, "... ", maxlen);
    }

    if (POWER_OUTPUT_VALUES_COUNT != 0)
    {
        strlcat(version_domain, FHSSconfig->domain, maxlen);            // single band: subghz or 2.4GHz, dual band: subghz
    }
    if (POWER_OUTPUT_VALUES_COUNT != 0 && POWER_OUTPUT_VALUES_DUAL_COUNT != 0)
    {
        strlcat(version_domain, "/", maxlen);
    }
    if (POWER_OUTPUT_VALUES_DUAL_COUNT != 0)
    {
        strlcat(version_domain, FHSSconfigDualBand->domain, maxlen);    // 2.4GHz
    }
}

bool isUsingPrimaryFreqBand()
{
    return FHSSusePrimaryFreqBand;
}