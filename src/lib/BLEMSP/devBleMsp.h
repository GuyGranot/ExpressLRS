#pragma once

#include "device.h"

#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32)

extern device_t BleMsp_device;

#if defined(TARGET_TX)
/**
 * @brief Starts the SpeedyBee-compatible BLE bridge.
 *
 * Unlike BLE Joystick this does NOT enter a mode state: the RF link keeps
 * running, because the bridge tunnels MSP over it. Refused while armed or while
 * WiFi/BLE Joystick own the module; the refusal reason shows in BleMspStatus().
 */
void BleMspStart();

/**
 * @brief True while the bridge is up. For the Lua command's cancel/reboot check:
 * connectionState never changes, so the running test cannot use it.
 */
bool BleMspIsRunning();

/**
 * @brief Ends the session: reverts any session rate, then reboots once the radio
 * is back on the configured rate and the sync-spam warning has drained. Bounded
 * by a timeout so the command can never hang.
 */
void BleMspStop();

/**
 * @brief Live one-line status for the ELRS Lua screen, e.g. "Adv w0" or
 * "Con+ w12 u12 r12 x0". The only debug channel on an assembled module, where
 * DBGLN goes to the backpack UART rather than the module's USB port.
 */
const char *BleMspStatus();

/**
 * @brief True when it is safe to put MSP into the uplink: RF link up, disarmed.
 */
bool BleMspMayForward();
#endif

#endif
