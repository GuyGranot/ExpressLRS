#include "RxTxEndpoint.h"

#if !defined(UNIT_TEST)

#include "CRSFRouter.h"
#include "rxtx_intf.h"
#include "config.h"
#include "FHSS.h"
#include "helpers.h"
#include "options.h"
#include "logging.h"

bool RxTxEndpoint::handleRxTxMessage(const crsf_header_t *message)
{
    const auto extMessage = (crsf_ext_header_t *)message;

    if (message->type == CRSF_FRAMETYPE_MSP_REQ && extMessage->payload[2] == MSP_ELRS_RXTX_CONFIG)
    {
        handleMspGetRxTxConfig(extMessage);
        return true;
    }
    if (message->type == CRSF_FRAMETYPE_MSP_WRITE && extMessage->payload[2] == MSP_ELRS_RXTX_CONFIG)
    {
        handleMspSetRxTxConfig(extMessage);
        return true;
    }
    return false;
}

/**
 * One MSP_ELRS_RXTX_CONFIG frame: the subcommand byte, then its payload. Every
 * frame this endpoint sends has that shape, so the layout is stated once here.
 * A response answers a request; a command asks one (the over-the-air MSP path
 * carries only WRITE frames, so a zero-length command doubles as a query).
 */
void RxTxEndpoint::sendRxTxConfig(const bool isResponse, const MSP_ELRS_RXTX_CONFIG_SUBCMD subcmd,
                                  const uint8_t *payload, const uint8_t len, const crsf_addr_e destination)
{
    mspPacket_t msp;
    msp.reset();
    if (isResponse)
    {
        msp.makeResponse();
    }
    else
    {
        msp.makeCommand();
    }
    msp.function = MSP_ELRS_RXTX_CONFIG;
    msp.addByte((uint8_t)subcmd);
    for (uint8_t i = 0; i < len; i++)
    {
        msp.addByte(payload[i]);
    }
    crsfRouter.AddMspMessage(&msp, destination, getDeviceId());
}

#if defined(USE_FHSS_SUBSET)
// What each band's configured pair resolves to, one line per band
static char bandSubsetHint[2][FHSS_SUBSET_HINT_LEN];

// The folder says whether the definition is live, because that is a property of
// the whole subset rather than of one band, and there is no room for it beside
// the per-band values. dyn_name null means "use common.name", so this is a
// pointer swap between two constants rather than a buffer to copy into.
static char subsetOnAirName[] = "Subset (on air)";

static folderParameter luaBandSubsetFolder = {
    {"Subset Bands", CRSF_FOLDER},
    nullptr
};
static selectionParameter luaSubsetEnable[2] = {
    {{"SubGHz", CRSF_TEXT_SELECTION}, 0, STR_OFF_ON, STR_EMPTYSPACE},
    {{"2.4G", CRSF_TEXT_SELECTION},   0, STR_OFF_ON, STR_EMPTYSPACE}
};
// Frequencies, not channel indices: a channel number says nothing about what is
// being transmitted on. Value, bounds and step are all kHz with a precision of
// 3, so the handset renders MHz and one click is one FHSS channel. No units
// string - "2479.400" already fills the value column on a 128px handset.
static floatParameter luaSubsetFrom[2] = {
    {{"SubGHz From", CRSF_FLOAT}, {0, 0, 0, 0, 3, 0}, STR_EMPTYSPACE},
    {{"2.4G From", CRSF_FLOAT},   {0, 0, 0, 0, 3, 0}, STR_EMPTYSPACE}
};
static floatParameter luaSubsetTo[2] = {
    {{"SubGHz To", CRSF_FLOAT}, {0, 0, 0, 0, 3, 0}, STR_EMPTYSPACE},
    {{"2.4G To", CRSF_FLOAT},   {0, 0, 0, 0, 3, 0}, STR_EMPTYSPACE}
};
static stringParameter luaSubsetHint[2] = {
    {{"SubGHz Ch", CRSF_INFO}, bandSubsetHint[FHSS_BAND_SUBGHZ]},
    {{"2.4G Ch", CRSF_INFO},   bandSubsetHint[FHSS_BAND_2G4]}
};

/**
 * Take a band's definition from the handset and store it if this radio can hop
 * it. Ignored entirely while a subset is on air: storing would leave the
 * definition and the live geometry diverged until some later rebuild, which
 * could be a rate change in flight. A pair that changes nothing does not rewrite
 * options.json, so confirming a field costs no flash.
 */
static void writeBandSubset(const fhss_band_e band, const uint8_t start, const uint8_t count)
{
    if (FHSSanySubsetActive())
    {
        return;
    }
    uint8_t curStart, curCount;
    FHSSgetBandSubset(band, &curStart, &curCount);
    if ((start != curStart || count != curCount) && FHSSsetBandSubset(band, start, count))
    {
        // Not saveOptions(): that blocks on flash for tens of milliseconds and
        // this runs from a parameter write, mid-link. The main loop writes it
        // where a stall is affordable.
        saveOptionsDeferred();
    }
}

/**
 * Move one edge of a band's subset, carrying the other with it when it has to.
 * From bounds To, so raising From past an overtaken To takes To up rather than
 * producing a pair too narrow to be legal. Both edges clamp here, in one place:
 * the bounds the handset was given already stop it short of an illegal pair,
 * and this catches the value incrField() clamps to max on its own.
 */
static void setBandSubsetEdges(const fhss_band_e band, uint8_t from, uint8_t to)
{
    const uint32_t channels = FHSSsubsetBandChannels(band);
    if (channels == 0)
    {
        return;
    }
    const uint8_t widest = (uint8_t)(channels - FHSS_SUBSET_MIN);
    if (from > widest)
    {
        from = widest;
    }
    if (to < from + FHSS_SUBSET_MIN - 1)
    {
        to = (uint8_t)(from + FHSS_SUBSET_MIN - 1);
    }
    writeBandSubset(band, from, (uint8_t)(to - from + 1));
}

/**
 * Move the From or the To edge to a frequency the handset sent, leaving the
 * other where it is. Both fields do the same thing to opposite ends of the pair,
 * so they share this rather than keeping two copies of "read the stored pair,
 * ignore the write if the band is off, convert kHz to a channel".
 */
static void moveBandSubsetEdge(const fhss_band_e band, const bool movingFrom, const uint32_t khz)
{
    uint8_t start, count;
    FHSSgetBandSubset(band, &start, &count);
    if (count == 0)
    {
        return;
    }
    const uint8_t edge = FHSSsubsetChannelForKhz(band, khz);
    setBandSubsetEdges(band, movingFrom ? edge : start,
                       movingFrom ? (uint8_t)(start + count - 1) : edge);
}

/**
 * A band is on exactly when it has a pair stored. Switching it on seeds the
 * whole band, which restricts nothing until a frequency is moved and shows the
 * real band edges to move from. Switching it off discards the pair, and does so
 * for good: the write reaches options.json immediately, so holding a copy back
 * in RAM would only make Off look undoable until the next reboot.
 */
static void setBandSubsetEnabled(const fhss_band_e band, const bool enabled)
{
    uint8_t start, count;
    FHSSgetBandSubset(band, &start, &count);
    if (enabled == (count != 0))
    {
        // Already there. Re-confirming On must not overwrite the stored pair
        // with the whole band.
        return;
    }
    writeBandSubset(band, 0, enabled ? (uint8_t)FHSSsubsetBandChannels(band) : 0);
}

/**
 * The folder is registered on its own so an endpoint can put its own control
 * inside it before the per-band fields. Registration order is display order.
 */
uint8_t RxTxEndpoint::registerBandSubsetFolder()
{
    registerParameter(&luaBandSubsetFolder);
    return luaBandSubsetFolder.common.id;
}

void RxTxEndpoint::registerBandSubsetFields()
{
    // Only the band(s) this radio is fitted with get fields. That is a
    // compile-time fact, unlike the domain tables, which are still null here -
    // the first sequence build runs after this. A registered-but-hidden band
    // would cost four CRSF parameter slots and four reads over the link every
    // time the handset opened the folder.
    //
    // Every write callback ends by refreshing the fields itself. Nothing else
    // here would: updateParameters() runs only when devicesTriggerEvent() fires,
    // and this definition lives in options.json rather than config, so a write
    // goes through none of the config-commit paths that raise one. Without the
    // refresh the handset's read-back a moment later returns the previous value
    // and the edit looks like it snapped back. Raising an event instead would
    // also work, but it would rebuild every parameter on the device to report
    // that one options key moved. The interlocked bounds need the refresh too -
    // moving From changes what To may be, and the handset re-reads its siblings
    // right after an edit.
    for (uint8_t b = 0; b < ARRAY_SIZE(luaSubsetEnable); b++)
    {
        const auto band = (fhss_band_e)b;
        if (!FHSSbandFitted(band))
        {
            continue;
        }
        registerParameter(&luaSubsetEnable[b], [this, band](propertiesCommon *item, int32_t arg) {
            setBandSubsetEnabled(band, arg != 0);
            updateBandSubsetParameters();
        }, luaBandSubsetFolder.common.id);

        registerParameter(&luaSubsetFrom[b], [this, band](propertiesCommon *item, int32_t arg) {
            moveBandSubsetEdge(band, true, (uint32_t)arg);
            updateBandSubsetParameters();
        }, luaBandSubsetFolder.common.id);

        registerParameter(&luaSubsetTo[b], [this, band](propertiesCommon *item, int32_t arg) {
            moveBandSubsetEdge(band, false, (uint32_t)arg);
            updateBandSubsetParameters();
        }, luaBandSubsetFolder.common.id);

        registerParameter(&luaSubsetHint[b], nullptr, luaBandSubsetFolder.common.id);
    }
}

void RxTxEndpoint::updateBandSubsetParameters()
{
    // The definition is only ever about the next sequence build, so while one is
    // on air there is nothing to edit: the per-model toggle is the single place
    // geometry is allowed to change.
    const bool editable = !FHSSanySubsetActive();
    luaBandSubsetFolder.dyn_name = editable ? nullptr : subsetOnAirName;

    for (uint8_t b = 0; b < ARRAY_SIZE(luaSubsetEnable); b++)
    {
        const auto band = (fhss_band_e)b;
        if (!FHSSbandFitted(band))
        {
            continue;
        }
        uint32_t startKhz = 0, stepKhz = 0;
        // A band too small to hold a legal subset has nothing to offer at all:
        // EU868 has 13 channels, IN866 four, the 433 domains three.
        const uint32_t channels = FHSSsubsetBandChannels(band);
        const bool present = channels >= FHSS_SUBSET_MIN && FHSSsubsetBandAxis(band, &startKhz, &stepKhz);

        uint8_t start, count;
        FHSSgetBandSubset(band, &start, &count);
        const bool enabled = present && count != 0;
        // A pair baked into options.json is only checked against the parser's
        // structural bound of 255, so it can be one this domain cannot hold. The
        // frequency fields have no way to show that - To's floor would sit above
        // its ceiling - so they stay away and the line says what is wrong.
        // Switching the band off and on again is the way out.
        const bool showPair = enabled && FHSSsubsetIsValid(start, count, channels);

        setTextSelectionValue(&luaSubsetEnable[b], enabled);
        if (showPair)
        {
            // From stops where a minimum-width subset still fits, and To starts
            // where one from the current From does. Both bounds travel with the
            // value, so every pair the handset can dial is one that stores.
            const uint8_t to = (uint8_t)(start + count - 1);
            setFloatRange(&luaSubsetFrom[b], startKhz,
                          startKhz + (channels - FHSS_SUBSET_MIN) * stepKhz, stepKhz);
            setFloatRange(&luaSubsetTo[b], startKhz + (start + FHSS_SUBSET_MIN - 1) * stepKhz,
                          startKhz + (channels - 1) * stepKhz, stepKhz);
            setFloatValue(&luaSubsetFrom[b], startKhz + start * stepKhz);
            setFloatValue(&luaSubsetTo[b], startKhz + to * stepKhz);
        }

        FHSSdescribeSubset(band, start, count, bandSubsetHint[b], FHSS_SUBSET_HINT_LEN);

        LUA_FIELD_VISIBLE(luaSubsetEnable[b], present && editable)
        LUA_FIELD_VISIBLE(luaSubsetFrom[b], showPair && editable)
        LUA_FIELD_VISIBLE(luaSubsetTo[b], showPair && editable)
        LUA_FIELD_VISIBLE(luaSubsetHint[b], present)
    }
}
#endif

/**
 * Handles REQ(get) of MSP_ELRS_RXTX_CONFIG command
 */
void RxTxEndpoint::handleMspGetRxTxConfig(crsf_ext_header_t *extMessage)
{
    switch ((MSP_ELRS_RXTX_CONFIG_SUBCMD)extMessage->payload[3])
    {
        case MSP_ELRS_RXTX_CONFIG_SUBCMD::UID:
            sendRxTxConfig(true, MSP_ELRS_RXTX_CONFIG_SUBCMD::UID,
                           UID, UID_LEN, (crsf_addr_e)extMessage->orig_addr);
            break;

        default:
            break;
    }
}

/**
 * Handles WRITE(set) of MSP_ELRS_RXTX_CONFIG command
 */
void RxTxEndpoint::handleMspSetRxTxConfig(crsf_ext_header_t *extMessage)
{
    // Encapsulated MSP header is (0x30, mspPayloadSize, command)
    // Subtract one from mspPayloadSize for the subcommand in payload[3]
    auto payloadLen = extMessage->payload[1] - 1;
    auto mspPayload = &extMessage->payload[4];

    switch ((MSP_ELRS_RXTX_CONFIG_SUBCMD)extMessage->payload[3])
    {
        case MSP_ELRS_RXTX_CONFIG_SUBCMD::UID:
            if (payloadLen > 5)
            {
                //DBGLN("Set UID");
                config.SetUID(mspPayload);
                scheduleRebootTime(200);
            }
            break;

        case MSP_ELRS_RXTX_CONFIG_SUBCMD::BIND_PHRASE:
            // 0 len payload supported to clear binding
            #if defined(DEBUG_LOG)
            mspPayload[payloadLen] = 0; // will overwrite CRC
            DBGLN("Set bindphrase '%s'", (char *)mspPayload);
            #endif
            config.SetBindPhrase(mspPayload, payloadLen);
            scheduleRebootTime(200);
            break;

        case MSP_ELRS_RXTX_CONFIG_SUBCMD::MODEL_ID:
            #if defined(TARGET_RX)
            if (payloadLen > 0)
            {
                DBGLN("Set ModelId=%u", extMessage->payload[4]);
                config.SetModelId(extMessage->payload[4]);
            }
            #endif
            break;

        default:
            break;
    }
}

#endif /* !UNIT_TEST */