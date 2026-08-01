#pragma once

#include <stdint.h>

/**
 * Ownership arbitration for the receiver's update session (USE_BLE_MSP).
 *
 * While both update transports are discoverable, the first intentional
 * client session claims the receiver and the other transport is shut down:
 *
 *   WiFi:  a station associating with our AP, or (in home-network STA mode,
 *          where captive-portal probes cannot occur) a real HTTP request or a
 *          complete valid MSP request on the Betaflight TCP port
 *   BLE:   the first complete valid MSP frame; connecting, subscribing or
 *          finishing the SpeedyBee handshake does not count
 *
 * The owner never resets, so a firmware upload is locked to WiFi for the rest
 * of the session by construction. Reboot is the only way back. With the flag
 * off (or on a SoC with no BLE) the inline fallbacks below say WiFi owns
 * everything and the whole mechanism compiles away.
 */

typedef enum : uint8_t
{
    TRANSPORT_UNCLAIMED,
    TRANSPORT_WIFI,
    TRANSPORT_BLE,
} updateTransport_e;

typedef enum : uint8_t
{
    EXPOSURE_WIFI_ONLY,
    EXPOSURE_WIFI_AND_BLE,
} updateExposure_e;

void setWifiUpdateMode();

#if defined(TARGET_RX) && defined(PLATFORM_ESP32) && defined(USE_BLE_MSP)
/**
 * @brief Selects the transport exposure atomically with the mode change; the
 * no-arg setWifiUpdateMode() keeps its WiFi-only meaning for all existing
 * callers.
 */
void setWifiUpdateMode(updateExposure_e exposure);

updateTransport_e getUpdateTransport();

/**
 * @brief Compare-and-swap: returns true when `owner` owns the session after the
 * call. Input permission is derived from the owner, so a winning claim revokes
 * the loser's input in the same atomic step and the loser cannot forward
 * anything between the claim and its later teardown. Callable from any task;
 * stack teardown is NOT performed here.
 */
bool claimUpdateTransport(updateTransport_e owner);

bool updateTransportAcceptsWifiInput();
bool updateTransportAcceptsBleInput();

/**
 * @brief True from an EXPOSURE_WIFI_AND_BLE entry until reboot: connector
 * attachment to the CRSF router is deferred to the claim winner instead of
 * happening at service start.
 */
bool updateDualDiscovery();

/**
 * @brief The BLE device reports here once NimBLE is fully deinitialized (or was
 * never started), unblocking work that needs the RF path exclusively (/cw).
 */
void updateBleTeardownComplete();

/**
 * @brief WiFi ownership trigger for STA mode, where AP association never fires.
 */
void claimUpdateWifiIfSta();
#else
inline void setWifiUpdateMode(updateExposure_e) { setWifiUpdateMode(); }
inline updateTransport_e getUpdateTransport() { return TRANSPORT_WIFI; }
inline bool claimUpdateTransport(updateTransport_e owner) { return owner == TRANSPORT_WIFI; }
inline bool updateTransportAcceptsWifiInput() { return true; }
inline bool updateTransportAcceptsBleInput() { return false; }
inline bool updateDualDiscovery() { return false; }
inline void updateBleTeardownComplete() {}
inline void claimUpdateWifiIfSta() {}
#endif
