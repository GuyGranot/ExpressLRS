#pragma once

#include "device.h"

#if defined(TARGET_TX) && defined(PLATFORM_ESP32) && defined(TX_BLE_MSP)

extern device_t BleMsp_device;

// Start the SpeedyBee-compatible BLE bridge. Unlike BLE Joystick this does
// NOT enter a mode state -- the RF link keeps running.
void BleMspStart();

// For the Lua command's cancel/reboot check: connectionState never changes,
// so the running test cannot use it.
bool BleMspIsRunning();

// End the session. Drops shaping, lets the revert hop the rate back, and
// reboots once the radio is home -- rebooting straight away while still shaped
// strands a receiver that followed us onto the session rate, because
// LOCK_ON_FIRST_CONNECTION stops it searching for the rate we returned to.
void BleMspStop();

// Live one-line status for the ELRS Lua screen, e.g. "Adv w0" / "Con+ w12 r3".
// The only debug channel on an assembled module: DBGLN goes to the backpack
// UART (GPIO5), not the module's USB port.
const char *BleMspStatus();

// True for the whole bridge session while disarmed: telemetry pins to 1:2 and,
// if a faster one is reachable, the air rate switches. The definition carries
// ICACHE_RAM_ATTR -- UpdateTlmRatioEffective calls this from the packet-timer
// path, which must not fetch from flash.
bool BleMspShouldShapeLink();

// True when it is safe to put MSP into the uplink: RF link up, model disarmed
bool BleMspMayForward();

// Link shaping is opt-in from the Lua "BLE MSP" folder. Not persisted: storing
// it would bump the config version, and a session does not survive a reboot.
void BleMspSetLinkShaping(bool enabled);
bool BleMspGetLinkShaping();

#endif
