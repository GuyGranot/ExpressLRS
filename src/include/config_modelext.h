#pragma once

#include <stdint.h>

// Extended per-model TX config, stored OUTSIDE the fully-packed model_config_t
// as separate NVS entries "modelext0".."modelext63" (ESP32 only). Old firmware
// ignores the unknown keys, so no TX_CONFIG_VERSION bump is needed and the
// per-model config survives a firmware downgrade untouched.
// An absent key loads as raw 0: schema 0, all features off.

// The record lives in NVS, so it needs an ESP32 TX, and its only field today is
// the band subset - hence the feature flag in the condition, so a build without
// the subset carries neither the storage nor the Lua toggle. Downgrade-safe
// per-model additions go here; anything else bumps TX_CONFIG_VERSION. A second
// tenant splits this in two: PLATFORM_ESP32 alone for the record, and a
// per-feature symbol for each tenant's parameters.
#if defined(PLATFORM_ESP32) && defined(USE_FHSS_SUBSET)
#define HAS_MODEL_EXTRAS
#endif

#define MODEL_EXTRA_SCHEMA 1

typedef union {
    struct {
        uint32_t extSchema:3,       // MODEL_EXTRA_SCHEMA when written by this firmware
                 bandSubset:1,      // restrict FHSS to the channel subset configured in options
                 reserved:28;
    } val;
    uint32_t raw;
} model_extra_config_t;

// Returns raw if it was written with a schema this firmware understands,
// otherwise 0 (all features off). A newer-schema record is never interpreted.
static inline uint32_t ModelExtraSanitize(uint32_t raw)
{
    model_extra_config_t ext;
    ext.raw = raw;
    return (ext.val.extSchema == MODEL_EXTRA_SCHEMA) ? raw : 0;
}
