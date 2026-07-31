#if defined(TARGET_TX) && defined(PLATFORM_ESP32) && defined(TX_BLE_MSP)

#include "devBleMsp.h"

#include <Arduino.h>

#include "BleMspConnector.h"
#include "OTA.h"
#include "SpeedyBeeGatt.h"
#include "common.h"
#include "config.h"
#include "rxtx_intf.h"   // scheduleRebootTime, TxRequestSessionRate
#include "options.h" // product_name / device_name, baked per target
#include "telemetry_protocol.h"
#include "logging.h"

static volatile bool linkShapeWanted = false;
// Opt-in, because shaping moves the air rate and telemetry ratio out from
// under the pilot. Not persisted -- that would bump the config version.
static bool linkShapingEnabled = false;

// Identity comes from the image's own baked options so a Boxer does not
// advertise itself as a Nomad. product_name/device_name are arrays filled by
// options_init() at boot, so this stays a static initialiser. wifiName and
// serial are constants: the app displays them but never matches on them.
static const SpeedyBeeDeviceInfo BLE_DEVICE_INFO = {
    "ELRSTXMOD",  // productCode
    nullptr,      // mac -- SpeedyBeeGatt fills this from NimBLE
    product_name, // the name the app displays
    "ELRSTX",     // wifiName
    "ELRSTX0001", // serial
};

static bool startRequested = false;
static bool wasConnected = false;

// Teardown is deferred until the air rate is back where the pilot left it; see
// BleMspStop() for why a bare reboot strands the receiver.
static bool sessionEnding = false;
static uint32_t endRequestedMs = 0;
// Reboot anyway if the revert cannot finish, so the command never hangs
static constexpr uint32_t REVERT_TIMEOUT_MS = 3000;

// How long the link took to come back after a rate hop, latched until the next
// one. The only observable proxy for whether the pre-hop sync warning worked.
static uint32_t hopStartedMs = 0;
static uint16_t resyncMs = 0;
static uint8_t lastRateIndex = 0xFF;

// Written from the NimBLE task, read for display only, so torn reads are harmless
static volatile uint16_t appWriteCount = 0;   // ABF1 writes (MSP from the app)

// Wide enough for the longest line at 3-digit counters:
// "Con+ 333F:2+ w999 u999 r999 x999 s65535" is 40 bytes with the NUL.
static char statusText[48];
// Set when a start is refused, so the handset shows why rather than an
// unchanged line that reads as a dead button
static bool refused = false;

// Writes into statusText because BleMspStatus() returns that buffer, and the
// Lua handler holds the pointer it got before calling us.
static void refuse(const char *why)
{
    strlcpy(statusText, why, sizeof(statusText));
    refused = true;
    DBGLN("BLEMSP refused: %s", why);
}

// Serial bytes arriving from the app on ABF1. Runs on the NimBLE host task:
// enqueue only, never touch the router or NimBLE-unsafe state here.
static void onAppSerialBytes(const uint8_t *data, size_t len)
{
    appWriteCount++;
    bleMspConnector.pushFromBle(data, (uint16_t)len);
}

// Packets per second, from the table's interval and send count
static uint32_t ratePps(const expresslrs_mod_settings_t *mod)
{
    return 1000000UL / ((uint32_t)mod->interval * mod->numOfSends);
}

// What the link is actually doing, as "333F:2+" -- live packet rate, live
// telemetry denominator, shaping state. The Lua's Packet Rate and Telem Ratio
// items show the CONFIGURED values, since shaping never writes config.
// Shaping marker, never blank:
//   .  not enabled       +  engaged
//   A  held off armed    -  enabled but not engaged
static uint8_t linkDescription(char *out, size_t len)
{
    const char *shape = !linkShapingEnabled       ? "."
                        : BleMspShouldShapeLink() ? "+"
                        : isArmed                 ? "A"
                                                  : "-";
    return (uint8_t)snprintf(out, len, "%lu%s:%u%s",
                             (unsigned long)ratePps(ExpressLRS_currAirRate_Modparams),
                             OtaIsFullRes ? "F" : "", ExpressLRS_currTlmDenom, shape);
}

const char *BleMspStatus()
{
    if (!SpeedyBeeGatt::isRunning())
    {
        if (!refused)
        {
            strlcpy(statusText, "Off", sizeof(statusText));
        }
        return statusText;
    }

    const char *state = !SpeedyBeeGatt::isClientConnected() ? "Adv"
                        : SpeedyBeeGatt::sessionReady()     ? "Con+"
                                                            : "Con";
    const SpeedyBeeGatt::HandshakeTrace t = SpeedyBeeGatt::trace();

    // On every line, not just the ones with MSP counters: shaping is armed from
    // the Lua selector long before a phone connects.
    char link[12];
    linkDescription(link, sizeof(link));

    if (appWriteCount != 0)
    {
        // w = BLE writes in, u = frames forwarded to the FC, r = replies back,
        // x = dropped with the RF link down, so a stall names the stage it
        // happened at. s<ms> is the link's recovery time across the last rate
        // hop, appended rather than swapped in so a hop keeps u and x visible.
        char resync[10] = "";
        if (resyncMs != 0)
        {
            snprintf(resync, sizeof(resync), " s%u", resyncMs);
        }
        snprintf(statusText, sizeof(statusText), "%s %s w%u u%u r%u x%u%s", state,
                 link, appWriteCount, bleMspConnector.framesUp(),
                 bleMspConnector.framesDown(), bleMspConnector.framesDropped(),
                 resync);
    }
    else if (t.unhandledCmd != 0)
    {
        // A command with no builder: we sent nothing and the app is waiting.
        snprintf(statusText, sizeof(statusText), "%s %s c%u !%02X d%u", state,
                 link, t.ctrlWrites, t.unhandledCmd, t.disconnectReason);
    }
    else if (t.ctrlWrites != 0)
    {
        // Handshake in progress or finished; show what we last answered.
        snprintf(statusText, sizeof(statusText), "%s %s c%u %02X>%u d%u", state,
                 link, t.ctrlWrites, t.lastCmd, t.lastRespLen, t.disconnectReason);
    }
    else
    {
        snprintf(statusText, sizeof(statusText), "%s %s w0", state, link);
    }
    return statusText;
}

void BleMspStart()
{
    if (SpeedyBeeGatt::isRunning())
    {
        return;
    }
    if (isArmed)
    {
        // Never bring a BLE radio up mid-flight. Starting NimBLE allocates
        // tens of KB and stalls the loop core for tens of ms, and a config
        // session has no business running while the model is armed.
        refuse("Disarm first");
        return;
    }
    if (connectionState > MODE_STATES)
    {
        // BLE Joystick owns NimBLE exclusively and has already stopped the
        // radio; WiFi/serial update own the flash and want the heap NimBLE
        // would take. Refuse rather than fight either for the stack -- in
        // particular, never jeopardise a firmware update in progress.
        refuse("Busy: WiFi/BLE");
        return;
    }
    refused = false;
    startRequested = true;
    devicesTriggerEvent(EVENT_BLE_MSP);
}

bool BleMspIsRunning()
{
    return SpeedyBeeGatt::isRunning();
}

void BleMspStop()
{
    if (sessionEnding)
    {
        return; // already winding down; a second RTN must not restart the clock
    }
    sessionEnding = true;
    endRequestedMs = millis();
    DBGLN("BLEMSP stopping, reverting the link before reboot");
}

ICACHE_RAM_ATTR bool BleMspShouldShapeLink()
{
    return linkShapeWanted;
}

void BleMspSetLinkShaping(const bool enabled)
{
    linkShapingEnabled = enabled;
    DBGLN("BLEMSP link shaping %s", enabled ? "on" : "off");
}

bool BleMspGetLinkShaping()
{
    return linkShapingEnabled;
}

// Telemetry payload bytes/sec a rate yields at the 1:2 ratio shaping pins.
// A relative figure for ranking candidates, not the pilot-facing bps the Lua
// shows -- that one honours the configured ratio and burst maths.
static uint32_t tlmBytesPerSec(const expresslrs_mod_settings_t *mod)
{
    // Tests PayloadLength rather than reading OtaIsFullRes: this ranks
    // CANDIDATE rates, and that global only ever describes the live one.
    const uint32_t perPacket = mod->PayloadLength == OTA8_PACKET_SIZE
                                   ? ELRS8_DATA_DL_BYTES_PER_CALL
                                   : ELRS4_DATA_DL_BYTES_PER_CALL;
    return (ratePps(mod) / 2) * perPacket;
}

// The fastest rate a config session can use. Long-range setups fly slow with
// telemetry off, where a session is unusably slow -- 150Hz standard is ~375 B/s
// against 1665 B/s at 333Hz Full, and full-res doubles the uplink too.
static uint8_t selectSessionRate(const uint8_t configuredRate)
{
    const auto *current = get_elrs_airRateConfig(configuredRate);
    const auto band = RadioBandMod::getBand(current->radio_type);
    uint8_t best = configuredRate;
    uint32_t bestBps = tlmBytesPerSec(current);

    for (uint8_t i = 0; i < RATE_MAX; i++)
    {
        if (!isSupportedRFRate(i))
        {
            continue;
        }
        const auto *mod = get_elrs_airRateConfig(i);

        // Dual-band hardware can run dual, 2.4 or sub-GHz rates; single-band
        // cannot cross either way
        const auto candidateBand = RadioBandMod::getBand(mod->radio_type);
        if (band != RadioBandMod::BDUAL && band != candidateBand)
        {
            continue;
        }

        // Full-res LoRa only: full-res carries 10 telemetry bytes against 5,
        // and a receiver is most likely not to implement FSK or FLRC -- a rate
        // it cannot follow costs the link. Derived rather than whitelisted so
        // a future rate is not silently skipped.
        if (!RadioBandMod::isLoRa(mod->radio_type) ||
            mod->PayloadLength != OTA8_PACKET_SIZE || mod->numOfSends != 1)
        {
            continue;
        }

        const uint32_t bps = tlmBytesPerSec(mod);
        if (bps > bestBps)
        {
            bestBps = bps;
            best = i;
        }
    }
    if (best != configuredRate)
    {
        DBGLN("BLEMSP session rate %u (%u B/s) replacing %u", best, bestBps, configuredRate);
    }
    return best;
}

bool BleMspMayForward()
{
    return connectionState == connected && !isArmed;
}


static bool initialize()
{
    return true;
}

// The "BLE MSP" Lua command starts the bridge, so nothing happens at boot: a
// freshly flashed module behaves exactly like stock until you ask for BLE,
// which keeps WiFi update mode (and its heap) untouched for recovery.

// MSP responsiveness only matters once a phone is actually talking to us;
// until then this is a heartbeat, not a pump.
static constexpr int TICK_CONNECTED_MS = 5;
static constexpr int TICK_ADVERTISING_MS = 100;

static int event()
{
    if (startRequested && !SpeedyBeeGatt::isRunning())
    {
        // Defer NimBLE init to timeout(): it allocates tens of KB and takes
        // tens of ms, which we would rather not do inside the event callback.
        return 200;
    }
    return DURATION_NEVER;
}

static int timeout()
{
    if (!SpeedyBeeGatt::isRunning())
    {
        if (!startRequested)
        {
            return DURATION_NEVER;
        }
        // Only read by DBGLN, which compiles away without DEBUG_LOG.
        __attribute__((unused)) const uint32_t heapBefore = ESP.getFreeHeap();
        SpeedyBeeGatt::begin(BLE_DEVICE_INFO, device_name, onAppSerialBytes);
        // Claim 0x12 only now, so the address is taken while the bridge is in
        // use rather than from boot.
        bleMspConnector.begin();
        DBGLN("BLEMSP started, heap %u -> %u (%d used)", heapBefore,
              ESP.getFreeHeap(), (int)heapBefore - (int)ESP.getFreeHeap());
        return TICK_ADVERTISING_MS;
    }

    // Watch for the air rate moving under us -- shaping hops it, and the
    // interesting number is how long connectionState takes to recover.
    const uint8_t rateNow = ExpressLRS_currAirRate_Modparams->index;
    if (rateNow != lastRateIndex)
    {
        lastRateIndex = rateNow;
        hopStartedMs = millis();
        resyncMs = 0;
    }
    else if (hopStartedMs != 0 && connectionState == connected)
    {
        resyncMs = (uint16_t)std::min<uint32_t>(millis() - hopStartedMs, 65535U);
        hopStartedMs = 0;
        DBGLN("BLEMSP link back %u ms after rate hop", resyncMs);
    }

    // Shaping follows the session, not the phone: keying it on the phone put
    // the hop and its reacquisition exactly where the app's first MSP burst
    // lands, and the app timed out.
    linkShapeWanted = linkShapingEnabled && !isArmed && !sessionEnding;

    // Which rate a session wants is ours; getting there safely is tx_main's.
    // Chosen once per session so the choice cannot drift.
    static uint8_t shapedRate = TX_SESSION_RATE_NONE;
    if (!linkShapeWanted)
    {
        shapedRate = TX_SESSION_RATE_NONE;
    }
    else if (shapedRate == TX_SESSION_RATE_NONE)
    {
        shapedRate = selectSessionRate(config.GetRate());
    }
    TxRequestSessionRate(shapedRate);

    if (sessionEnding)
    {
        // Hold the reboot until the radio is fully handed back. Reads true
        // immediately when shaping never engaged, the common case.
        const bool home = TxSessionRateIsHome();
        if (home || (millis() - endRequestedMs) >= REVERT_TIMEOUT_MS)
        {
            DBGLN("BLEMSP rebooting, revert %s", home ? "complete" : "TIMED OUT");
            scheduleRebootTime(50);
            // One-way: sessionEnding stays latched and this device never ticks
            // again, so nothing can re-arm a hop before the reboot lands.
            return DURATION_NEVER;
        }
        return TICK_CONNECTED_MS; // poll while winding down
    }

    const bool clientConnected = SpeedyBeeGatt::isClientConnected();
    if (!clientConnected)
    {
        // Phone gone: drop partial frames so a reconnect cannot resume
        // mid-frame against a fresh app session.
        if (wasConnected)
        {
            bleMspConnector.reset();
        }
        wasConnected = false;
        // Still pumped: the FC probe goes out with or without a phone
        // attached, which is the whole point of it.
        bleMspConnector.pump();
        return TICK_ADVERTISING_MS;
    }
    if (!wasConnected)
    {
        bleMspConnector.startSession();
        appWriteCount = 0;
    }
    wasConnected = true;

    // The connector owns the cross-task FIFO; this moves one frame uplink and
    // drains whatever the FC has answered.
    bleMspConnector.pump();

    return TICK_CONNECTED_MS;
}

device_t BleMsp_device = {
    .initialize = initialize,
    .start = nullptr,
    .event = event,
    .timeout = timeout,
    .subscribe = EVENT_BLE_MSP,
};

#endif // TARGET_TX && PLATFORM_ESP32 && TX_BLE_MSP
