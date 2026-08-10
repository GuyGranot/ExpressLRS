#pragma once

#include "crsf_protocol.h"

#include <functional>

struct propertiesCommon
{
    const char *name; // display name
    crsf_value_type_e type;
    uint8_t id;     // Sequential id assigned by enumeration
    uint8_t parent; // id of parent folder
} PACKED;

struct selectionParameter
{
    propertiesCommon common;
    uint8_t value;
    const char *options; // selection options, separated by ';'
    const char *units;
} PACKED;

enum commandStep_e : uint8_t
{
    lcsIdle = 0,
    lcsClick = 1,      // user has clicked the command to execute
    lcsExecuting = 2,  // command is executing
    lcsAskConfirm = 3, // command pending user OK
    lcsConfirmed = 4,  // user has confirmed
    lcsCancel = 5,     // user has requested cancel
    lcsQuery = 6,      // UI is requesting status update
};

struct commandParameter
{
    propertiesCommon common;
    commandStep_e step; // state
    const char *info;   // status info to display
} PACKED;

struct int8Parameter
{
    propertiesCommon common;
    union {
        struct
        {
            uint8_t value;
            const uint8_t min;
            const uint8_t max;
        } u;
        struct
        {
            int8_t value;
            const int8_t min;
            const int8_t max;
        } s;
    } PACKED properties;
    const char *const units;
} PACKED;

struct int16Parameter
{
    propertiesCommon common;
    union {
        struct
        {
            uint16_t value;
            const uint16_t min;
            const uint16_t max;
        } u;
        struct
        {
            int16_t value;
            const int16_t min;
            const int16_t max;
        } s;
    } PACKED properties;
    const char *const units;
} PACKED;

struct floatParameter
{
    propertiesCommon common;
    struct
    {
        // value, min, max, and def are all signed, but stored as BE unsigned.
        // Unlike every other parameter's, min/max/step are not const: this is
        // the first parameter whose range and increment come from the live
        // configuration rather than the declaration, so setFloatRange()
        // restates them at each refresh. That is filterOptions() for a numeric
        // field, and where setInt16Range() would go.
        uint32_t value;
        uint32_t min;
        uint32_t max;
        const uint32_t def; // default value
        const uint8_t precision;
        uint32_t step;
    } PACKED properties;
    const char *const units;
} PACKED;

// The handset reads value/min/max/def at offsets 0/4/8/12, the precision at 16
// and the step at 17, with the units string starting at 21 (fieldFloatLoad in
// elrs.lua). Padding this apart would send a silently different field.
static_assert(sizeof(decltype(floatParameter::properties)) == 21, "float parameter properties must stay packed");

struct stringParameter
{
    propertiesCommon common;
    const char *value;
} PACKED;

struct folderParameter
{
    propertiesCommon common;
    char *dyn_name;
} PACKED;

struct elrsStatusParameter
{
    uint8_t pktsBad;
    uint16_t pktsGood; // Big-Endian
    uint8_t flags;
    char msg[1]; // null-terminated string
} PACKED;

#define LUA_FIELD_HIDE(fld)                                                                  \
    {                                                                                        \
        fld.common.type = (crsf_value_type_e)((uint8_t)fld.common.type | CRSF_FIELD_HIDDEN); \
    }
#define LUA_FIELD_SHOW(fld)                                                                   \
    {                                                                                         \
        fld.common.type = (crsf_value_type_e)((uint8_t)fld.common.type & ~CRSF_FIELD_HIDDEN); \
    }
#define LUA_FIELD_VISIBLE(fld, cond) \
    {                                \
        if (cond)                    \
            LUA_FIELD_SHOW(fld)      \
        else                         \
            LUA_FIELD_HIDE(fld)      \
    }

typedef std::function<void(propertiesCommon *item, int32_t arg)> parameterHandlerCallback;

uint8_t findSelectionLabel(const selectionParameter *parameter, char *outArray, uint8_t value);

constexpr char STR_EMPTYSPACE[1] = {};

// The options string of every plain on/off selection. Here rather than in one
// endpoint's parameter file because both endpoints now build parameters.
constexpr char STR_OFF_ON[] = "Off;On";

#define LUASYM_ARROW_UP "\xc0"
#define LUASYM_ARROW_DN "\xc1"
