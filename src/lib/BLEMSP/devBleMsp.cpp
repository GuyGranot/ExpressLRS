#include "devBleMsp.h"

#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32) && defined(TARGET_TX)

#include <Arduino.h>

#include "BleMspConnector.h"
#include "SpeedyBeeGatt.h"
#include "common.h"
#include "config.h"
#include "rxtx_intf.h"
#include "options.h"
#include "logging.h"
#include "helpers.h"

// Identity comes from the image's own baked options; product_name/device_name
// are arrays filled by options_init() at boot, so a static initializer holds
static const SpeedyBeeDeviceInfo BLE_DEVICE_INFO = {
    "ELRSTXMOD",  // productCode
    nullptr,      // mac, SpeedyBeeGatt fills this from NimBLE
    product_name, // the name the app displays
    "ELRSTX",     // wifiName
    "ELRSTX0001", // serial
};

static bool startRequested = false;
static bool wasConnected = false;

// Teardown is deferred until the air rate is back where the pilot left it;
// reboot anyway if the revert cannot finish, so the command never hangs
static bool sessionEnding = false;
static uint32_t endRequestedMs = 0;
static constexpr uint32_t REVERT_TIMEOUT_MS = 3000;

// Written from the NimBLE task, read for display only, so torn reads are harmless
static volatile uint16_t appWriteCount = 0; // ABF1 writes (MSP from the app)

// Wide enough for the longest status line at 3-digit counters
static char statusText[48];
// set when a start is refused, so the handset shows why
static bool refused = false;

// Writes into statusText: the Lua handler holds the pointer it got before calling us
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

    if (appWriteCount != 0)
    {
        // w = BLE writes in, u = frames forwarded to the FC, r = replies back,
        // x = dropped with the RF link down, so a stall names the stage
        snprintf(statusText, sizeof(statusText), "%s w%u u%u r%u x%u", state,
                 appWriteCount, bleMspConnector.framesUp(), bleMspConnector.framesDown(),
                 bleMspConnector.framesDropped());
    }
    else if (t.unhandledCmd != 0)
    {
        // A command with no builder: we sent nothing and the app is waiting
        snprintf(statusText, sizeof(statusText), "%s c%u !%02X d%u", state,
                 t.ctrlWrites, t.unhandledCmd, t.disconnectReason);
    }
    else if (t.ctrlWrites != 0)
    {
        // Handshake in progress or finished; show what we last answered
        snprintf(statusText, sizeof(statusText), "%s c%u %02X>%u d%u", state,
                 t.ctrlWrites, t.lastCmd, t.lastRespLen, t.disconnectReason);
    }
    else
    {
        snprintf(statusText, sizeof(statusText), "%s w0", state);
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
        // Never bring a BLE radio up mid-flight: starting NimBLE allocates tens
        // of KB and stalls the loop core for tens of ms
        refuse("Disarm first");
        return;
    }
    if (connectionState > MODE_STATES)
    {
        // BLE Joystick owns NimBLE; WiFi/serial update own the flash and the
        // heap NimBLE would take. Never jeopardize an update in progress.
        refuse("Busy: WiFi/BLE");
        return;
    }
    refused = false;
    startRequested = true;
}

bool BleMspIsRunning()
{
    return SpeedyBeeGatt::isRunning();
}

void BleMspStop()
{
    if (sessionEnding)
    {
        return; // already winding down; a second stop must not restart the clock
    }
    sessionEnding = true;
    endRequestedMs = millis();
    DBGLN("BLEMSP stopping, reverting the link before reboot");
}

bool BleMspMayForward()
{
    return connectionState == connected && !isArmed;
}

// Started only by the Lua command, so a freshly flashed module behaves exactly
// like stock until asked; polled at 5 Hz idle rather than claiming an event bit
static constexpr int TICK_IDLE_MS = 200;
// MSP responsiveness only matters once a phone is actually talking to us
static constexpr int TICK_CONNECTED_MS = 5;
static constexpr int TICK_ADVERTISING_MS = 100;

static int start()
{
    return TICK_IDLE_MS;
}

static int timeout()
{
    if (!SpeedyBeeGatt::isRunning())
    {
        if (!startRequested)
        {
            return TICK_IDLE_MS;
        }
        startRequested = false;
        // Only read by DBGLN, which compiles away without DEBUG_LOG
        const uint32_t heapBefore = ESP.getFreeHeap();
        UNUSED(heapBefore);
        if (!SpeedyBeeGatt::begin(BLE_DEVICE_INFO, device_name, onAppSerialBytes))
        {
            refuse("BLE failed");
            return TICK_IDLE_MS;
        }
        // claim the router address only while the bridge is in use
        bleMspConnector.begin();
        DBGLN("BLEMSP started, heap %u -> %u (%d used)", heapBefore, ESP.getFreeHeap(), (int)heapBefore - (int)ESP.getFreeHeap());
        return TICK_ADVERTISING_MS;
    }

    if (sessionEnding)
    {
        // Hold the reboot until the radio is fully handed back. Reads true
        // immediately when no session rate ever engaged, the common case.
        const bool home = TxSessionRateIsHome();
        if (home || (millis() - endRequestedMs) >= REVERT_TIMEOUT_MS)
        {
            DBGLN("BLEMSP rebooting, revert %s", home ? "complete" : "TIMED OUT");
            scheduleRebootTime(50);
            // One-way: sessionEnding stays latched and this device never ticks
            // again, so nothing can restart a session before the reboot lands
            return DURATION_NEVER;
        }
        return TICK_CONNECTED_MS; // poll while winding down
    }

    const bool clientConnected = SpeedyBeeGatt::isClientConnected();
    if (!clientConnected)
    {
        // phone gone: drop partial frames so a reconnect starts clean
        if (wasConnected)
        {
            bleMspConnector.reset();
        }
        wasConnected = false;
        // still pumped: the FC probe goes out with or without a phone attached
        bleMspConnector.pump();
        return TICK_ADVERTISING_MS;
    }
    if (!wasConnected)
    {
        bleMspConnector.startSession();
        appWriteCount = 0;
    }
    wasConnected = true;

    // move one frame uplink, drain whatever the FC has answered
    bleMspConnector.pump();

    return TICK_CONNECTED_MS;
}

device_t BleMsp_device = {
    .initialize = nullptr,
    .start = start,
    .event = nullptr,
    .timeout = timeout,
    .subscribe = 0,
};

#endif
