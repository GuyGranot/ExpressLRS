#pragma once

#include <stdint.h>

// A contiguous FHSS channel subset: `count` channels starting at channel index
// `start` on the domain's full-band grid. count == 0 means no subset (full band).
// Stored in options.json keyed by physical band: "fhss-subset-subghz-*" and
// "fhss-subset-2g4-*". Which FHSS domain table carries which band is a
// radio-family detail, resolved once in FHSSrandomiseFHSSsequence().

// EN 300 328 requires >= 15 hopping channels for FHSS equipment in 2.4GHz
#ifndef FHSS_SUBSET_MIN
#define FHSS_SUBSET_MIN 15
#endif

// Structural bound used at options parse time, before the FHSS domain tables
// are consulted; the real domain channel count is enforced at FHSS init. 255 so
// that start and count are always representable in the uint8_t they are kept in.
#define FHSS_SUBSET_MAX_CHANNELS 255

static inline bool FHSSsubsetIsValid(uint32_t start, uint32_t count, uint32_t domainChannelCount)
{
    if (count == 0)
        return true; // subset disabled
    return count >= FHSS_SUBSET_MIN && (start + count) <= domainChannelCount;
}
