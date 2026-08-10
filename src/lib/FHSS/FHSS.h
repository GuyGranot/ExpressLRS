#pragma once

#include "targets.h"
#include "fhss_subset.h"
#include "random.h"

#if defined(RADIO_SX127X)
#define FreqCorrectionMax ((int32_t)(100000/FREQ_STEP))
#elif defined(RADIO_LR1121)
#define FreqCorrectionMax ((int32_t)(100000/FREQ_STEP)) // TODO - This needs checking !!!
#elif defined(RADIO_SX128X)
#define FreqCorrectionMax ((int32_t)(200000/FREQ_STEP))
#endif
#define FreqCorrectionMin (-FreqCorrectionMax)

// The two directions sit together so they cannot be updated separately. The
// reverse is only for showing a table value to a human, so it is a double and
// the caller rounds; nothing on a hop path uses it.
#if defined(RADIO_LR1121)
#define FREQ_HZ_TO_REG_VAL(freq) (freq)
#define REG_VAL_TO_FREQ_HZ(val) ((double)(val))
#define FREQ_SPREAD_SCALE 1
#else
#define FREQ_HZ_TO_REG_VAL(freq) ((uint32_t)((double)freq/(double)FREQ_STEP))
#define REG_VAL_TO_FREQ_HZ(val) ((double)(val)*(double)FREQ_STEP)
#define FREQ_SPREAD_SCALE 256
#endif

#define FHSS_SEQUENCE_LEN 256

// Dual-band FHSS is an LR1121 hardware feature. The native test env defines it
// too, against the synthetic secondary domain in FHSS.cpp.
#if defined(RADIO_LR1121)
#define FHSS_HAS_DUAL_BAND
#endif

typedef struct {
    const char  *domain;
    uint32_t    freq_start;
    uint32_t    freq_stop;
    uint32_t    freq_count;
    uint32_t    freq_center;
} fhss_config_t;

extern volatile uint8_t FHSSptr;
extern int32_t FreqCorrection;      // Only used for the SX1276
extern int32_t FreqCorrection_2;    // Only used for the SX1276

// Primary Band
extern uint16_t primaryBandCount;
extern uint32_t freq_spread;
extern uint8_t FHSSsequence[];
extern uint_fast8_t sync_channel;
extern const fhss_config_t *FHSSconfig;

// DualBand Variables
extern bool FHSSusePrimaryFreqBand;
extern bool FHSSuseDualBand;
extern uint16_t secondaryBandCount;
extern uint32_t freq_spread_DualBand;
extern uint8_t FHSSsequence_DualBand[];
extern uint_fast8_t sync_channel_DualBand;
extern const fhss_config_t *FHSSconfigDualBand;

#if defined(USE_FHSS_SUBSET)
// Which physical band each domain table carries is the one radio-family fact
// this feature needs: on an SX128X the primary domain is 2.4GHz, everywhere
// else it is sub-GHz and the dual-band table is 2.4GHz. Stated once here; every
// band-to-table and band-to-option mapping derives from it, as does whether a
// band is fitted at all.
#if defined(RADIO_SX128X)
static constexpr fhss_band_e PRIMARY_BAND = FHSS_BAND_2G4;
#else
static constexpr fhss_band_e PRIMARY_BAND = FHSS_BAND_SUBGHZ;
#endif
static constexpr fhss_band_e SECONDARY_BAND =
    PRIMARY_BAND == FHSS_BAND_SUBGHZ ? FHSS_BAND_2G4 : FHSS_BAND_SUBGHZ;

// Whether this radio has a band at all - a compile-time fact, unlike the domain
// tables, which are not populated until the first sequence build
static constexpr bool FHSSbandFitted(fhss_band_e band)
{
#if defined(FHSS_HAS_DUAL_BAND)
    return band == PRIMARY_BAND || band == SECONDARY_BAND;
#else
    return band == PRIMARY_BAND;
#endif
}

// Effective (possibly subset-restricted) geometry, per band: first raw-grid
// channel and channel count of the effective domain. With no subset active they
// are 0 and the raw domain count; freq_spread is never changed by a subset.
extern bool FHSSsubsetActive;
extern uint_fast8_t subset_offset;
extern uint32_t effective_freq_count;

#if defined(FHSS_HAS_DUAL_BAND)
extern bool FHSSsubsetActive_DualBand;
extern uint_fast8_t subset_offset_DualBand;
extern uint32_t effective_freq_count_DualBand;
#else
// a single band radio never restricts the band it does not have
static constexpr bool FHSSsubsetActive_DualBand = false;
static constexpr uint_fast8_t subset_offset_DualBand = 0;
static constexpr uint32_t effective_freq_count_DualBand = 0;
#endif

// Whether the last build left any band running a subset, whatever the mode
static inline bool FHSSanySubsetActive(void)
{
    return FHSSsubsetActive || FHSSsubsetActive_DualBand;
}
#else
// Compile-time zeroes so the hop maths and the CRC seed keep one source form
// and fold back to the full-band expressions
static constexpr bool FHSSsubsetActive = false;
static constexpr bool FHSSsubsetActive_DualBand = false;
static constexpr uint_fast8_t subset_offset = 0;
static constexpr uint_fast8_t subset_offset_DualBand = 0;
#endif

// version/domain string
extern char version_domain[];

// Create and randomise the FHSS sequence(s). useSubset requests the
// options-defined channel subset(s); each band falls back to its full domain
// independently if its subset is absent or does not fit. Binding and full-band
// acquisition call with useSubset=false.
void FHSSrandomiseFHSSsequence(uint32_t seed, bool useSubset);
// build one band's sequence from freqCount alone and return the number of
// entries written, always a whole multiple of freqCount
uint16_t FHSSrandomiseFHSSsequenceBuild(uint32_t seed, uint32_t freqCount, uint_fast8_t sync_channel, uint8_t *sequence);

// add domain info for Lua
void addDomainInfo(char *version_domain, uint8_t maxlen);

#if defined(USE_FHSS_SUBSET)
// The hash of the effective geometry of the band(s) the active mode transmits
// on. 0 = no subset effective for that mode, so a subset-free build hashes to
// nothing and seeds its packet CRC exactly as an unmodified build does. Valid
// only after a sequence build, and it follows the band mode, so re-read it
// rather than holding it across either.
uint16_t FHSSgetGeometryHash(void);

// Whether a subset is defined in options for a band this radio has, valid or not
bool FHSSsubsetConfigured(void);

// Whether a rebuild requesting the subset would restrict a band the active band
// mode transmits on -- the build-time judgement as a pure query, so the
// acquisition scan can settle its phase before building
bool FHSSsubsetWouldApply(void);

// Editing the definition, for the handset parameter layers on both sides. A band
// this radio does not have reports no channels and refuses every write.
uint32_t FHSSsubsetBandChannels(fhss_band_e band);

// A band's frequency grid for the layers that show it to a human: the centre
// frequency of channel 0 and the spacing between channels, both in kHz. The
// FHSS tables hold freq_start/freq_stop in register units on every radio except
// the LR1121, so this is the one place that conversion happens. False for a
// band this radio does not have. Channel n is startKhz + n * stepKhz; use that
// expression rather than re-deriving, so the value shown and the value written
// back come from the same arithmetic.
bool FHSSsubsetBandAxis(fhss_band_e band, uint32_t *startKhz, uint32_t *stepKhz);

// The inverse of that expression, for turning an edited frequency back into a
// channel. The handset sends min + n * step except at the top, where incrField()
// clamps to max instead of landing on a step boundary, so this rounds rather
// than truncates. Out of range below the band is channel 0, above it the last.
uint8_t FHSSsubsetChannelForKhz(fhss_band_e band, uint32_t khz);

void FHSSgetBandSubset(fhss_band_e band, uint8_t *start, uint8_t *count);

// Validate against that band's live domain and store it. Returns false and
// changes nothing if the pair does not fit; the caller persists on true.
bool FHSSsetBandSubset(fhss_band_e band, uint8_t start, uint8_t count);

// What a start/count pair resolves to on a band, for a read-only display field:
// the channel span, or why there is not one. The pair is explicit rather than
// read from options, so a caller can describe one that was never stored - which
// is how a value the firmware would not keep explains itself. Kept under ten
// characters because it is drawn at COL2, 58px from the edge of a 128px handset.
#define FHSS_SUBSET_HINT_LEN 12     // "241-255/255" plus the terminator
void FHSSdescribeSubset(fhss_band_e band, uint8_t start, uint8_t count, char *dst, uint8_t maxlen);
#else
static inline bool FHSSsubsetConfigured(void) { return false; }
static inline bool FHSSsubsetWouldApply(void) { return false; }
static inline uint16_t FHSSgetGeometryHash(void) { return 0; }
#endif

// Raw domain bounds, the radio tuning range passed to Radio.Begin(); never
// subset-restricted (binding runs full-band and the radio must tune the whole domain)
static inline uint32_t FHSSgetMinimumFreq(void)
{
    return FHSSconfig->freq_start;
}

static inline uint32_t FHSSgetMaximumFreq(void)
{
    return FHSSconfig->freq_stop;
}

// The frequency of one channel of a band. The raw-grid index is the band's
// subset offset plus the effective-domain index, summed before the spread
// multiply so subset frequencies are bit-identical to the corresponding
// full-band grid channels. Callers subtract their own FreqCorrection.
static inline uint32_t FHSSfreqForChannel(const fhss_config_t *config, uint32_t spread,
                                          uint_fast8_t offset, uint32_t idx)
{
    return config->freq_start + ((offset + idx) * spread / FREQ_SPREAD_SCALE);
}

// The number of frequencies in the effective (possibly subset-restricted)
// domain; this is what the hop/sync/LQ logic uses
static inline uint32_t FHSSgetChannelCount(void)
{
#if defined(USE_FHSS_SUBSET)
    return FHSSusePrimaryFreqBand ? effective_freq_count : effective_freq_count_DualBand;
#else
    return FHSSusePrimaryFreqBand ? FHSSconfig->freq_count : FHSSconfigDualBand->freq_count;
#endif
}

// how far the shared hop index may advance in the current mode. This is not any
// one band's table length, so never size a sequence build from it
static inline uint16_t FHSSgetSequenceCount()
{
    if (FHSSuseDualBand) // Use the smaller of the 2 bands as not to go beyond the max index for each sequence.
    {
        if (primaryBandCount < secondaryBandCount)
        {
            return primaryBandCount;
        }
        else
        {
            return secondaryBandCount;
        }
    }

    if (FHSSusePrimaryFreqBand)
    {
        return primaryBandCount;
    }
    else
    {
        return secondaryBandCount;
    }
}

// get the initial frequency, which is also the sync channel
static inline uint32_t FHSSgetInitialFreq()
{
    if (FHSSusePrimaryFreqBand)
    {
        return FHSSfreqForChannel(FHSSconfig, freq_spread, subset_offset, sync_channel) - FreqCorrection;
    }
    else
    {
        return FHSSfreqForChannel(FHSSconfigDualBand, freq_spread_DualBand, subset_offset_DualBand, sync_channel_DualBand);
    }
}

// Get the current sequence pointer
static inline uint8_t FHSSgetCurrIndex()
{
    return FHSSptr;
}

// Is the current frequency the sync frequency
static inline uint8_t FHSSonSyncChannel()
{
    if (FHSSusePrimaryFreqBand)
    {
        return FHSSsequence[FHSSptr] == sync_channel;
    }
    else
    {
        return FHSSsequence_DualBand[FHSSptr] == sync_channel_DualBand;
    }
}

// Set the sequence pointer, used by RX on SYNC
static inline void FHSSsetCurrIndex(const uint8_t value)
{
    FHSSptr = value % FHSSgetSequenceCount();
}

// Advance the pointer to the next hop and return the frequency of that channel
static inline uint32_t FHSSgetNextFreq()
{
    FHSSptr = (FHSSptr + 1) % FHSSgetSequenceCount();

    if (FHSSusePrimaryFreqBand)
    {
        return FHSSfreqForChannel(FHSSconfig, freq_spread, subset_offset, FHSSsequence[FHSSptr]) - FreqCorrection;
    }
    else
    {
        return FHSSfreqForChannel(FHSSconfigDualBand, freq_spread_DualBand, subset_offset_DualBand, FHSSsequence_DualBand[FHSSptr]);
    }
}

static inline const char *FHSSgetRegulatoryDomain()
{
    if (FHSSusePrimaryFreqBand)
    {
        return FHSSconfig->domain;
    }
    else
    {
        return FHSSconfigDualBand->domain;
    }
}

// Get frequency offset by half of the effective domain frequency range, so
// the Gemini partner stays inside the subset when one is active
static inline uint32_t FHSSGeminiFreq(uint8_t FHSSsequenceIdx)
{
    uint32_t freq;
    uint32_t numfhss = FHSSgetChannelCount();
    uint8_t offSetIdx = (FHSSsequenceIdx + (numfhss / 2)) % numfhss;

    if (FHSSusePrimaryFreqBand)
    {
        freq = FHSSfreqForChannel(FHSSconfig, freq_spread, subset_offset, offSetIdx) - FreqCorrection_2;
    }
    else
    {
        freq = FHSSfreqForChannel(FHSSconfigDualBand, freq_spread_DualBand, subset_offset_DualBand, offSetIdx);
    }

    return freq;
}

static inline uint32_t FHSSgetGeminiFreq()
{
    if (FHSSuseDualBand)
    {
        // When using Dual Band there is no need to calculate an offset frequency. Unlike Gemini with 2 frequencies in the same band.
        return FHSSfreqForChannel(FHSSconfigDualBand, freq_spread_DualBand, subset_offset_DualBand, FHSSsequence_DualBand[FHSSptr]);
    }
    else
    {
        if (FHSSusePrimaryFreqBand)
        {
            return FHSSGeminiFreq(FHSSsequence[FHSSgetCurrIndex()]);
        }
        else
        {
            return FHSSGeminiFreq(FHSSsequence_DualBand[FHSSgetCurrIndex()]);
        }
    }
}

static inline uint32_t FHSSgetInitialGeminiFreq()
{
    if (FHSSuseDualBand)
    {
        return FHSSfreqForChannel(FHSSconfigDualBand, freq_spread_DualBand, subset_offset_DualBand, sync_channel_DualBand);
    }
    else
    {
        if (FHSSusePrimaryFreqBand)
        {
            return FHSSGeminiFreq(sync_channel);
        }
        else
        {
            return FHSSGeminiFreq(sync_channel_DualBand);
        }
    }
}
