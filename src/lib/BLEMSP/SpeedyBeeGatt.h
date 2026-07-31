#pragma once

#include "SpeedyBeeHandshake.h"

#include <stddef.h>
#include <stdint.h>

// NimBLE peripheral presenting the BLE profile a genuine SpeedyBee flight
// controller exposes, so the stock SpeedyBee phone app connects to us. The real
// module is Espressif's ble_spp_server example. Captured from a real SpeedyBee
// FC with nRF Connect:
//   advertisement:  service 0x00FF   (NOT 0xABF0 -- the stock example's value)
//   GATT:           0x1800, 0x1801, 0xABF0(primary)
//   0xABF0 chars:   ABF1..ABF4 only, no ABF5 heartbeat, no 0x180A DIS
// Advertising 0xABF0 instead makes the app ignore the device entirely.
//
// Everything here runs on the NimBLE host task. Keep the serial sink callback
// allocation-free and non-blocking -- it must not touch the CRSF router.
namespace SpeedyBeeGatt
{
    typedef void (*serialSink_t)(const uint8_t *data, size_t len);

    // Starts NimBLE, publishes the service and begins advertising. deviceName
    // is only for the user picking the right device in a scan list; the app
    // filters on the 0x00FF service UUID.
    void begin(const SpeedyBeeDeviceInfo &info, const char *deviceName,
               serialSink_t onSerialBytes);

    bool isRunning();
    bool isClientConnected();
    // 23 until the peer negotiates larger; notifySerial fragments to fit.
    uint16_t negotiatedMtu();

    // Notify FC->app serial bytes on ABF2, applying the ble_spp_server
    // fragmentation the app expects for payloads over the MTU:
    //   '#' '#' <total_chunks> <chunk_index(1-based)> <MTU-7 payload bytes>
    // False if no client is subscribed or the stack refused; retry the
    // remainder on a later tick.
    bool notifySerial(const uint8_t *data, size_t len);

    // Largest payload notifySerial can take in one call at the current MTU.
    size_t maxSerialChunk();

    // True once the app finished the ABF3/ABF4 handshake. Not required for
    // serial traffic -- older app builds skip straight to MSP on ABF1.
    bool sessionReady();

    // Last handshake exchange, for the handset readout. Latched across
    // disconnect, cleared on the next connect.
    struct HandshakeTrace
    {
        uint8_t ctrlWrites;     // ABF3 writes this session
        uint8_t lastCmd;        // command byte of the most recent one
        uint8_t lastRespLen;    // bytes we notified back; 0 means we stayed silent
        uint8_t unhandledCmd;   // a command we had no builder for, else 0
        uint8_t disconnectReason; // BLE reason code; 0x13 peer chose to, 0x08 timeout
    };
    HandshakeTrace trace();
}
