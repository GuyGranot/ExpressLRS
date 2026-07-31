#pragma once

#if defined(TARGET_TX) && defined(PLATFORM_ESP32) && defined(TX_BLE_MSP)

#include "CRSFConnector.h"
#include "FIFO.h"
#include "crsf2msp.h"
#include "msp2crsf.h"

// Bridges the SpeedyBee app's BLE serial channel to the flight controller over
// the RF link. Modelled on lib/WIFI/TcpMspConnector, which does the same job
// for the receiver's WiFi bridge. Claiming CRSF_ADDRESS_BLUETOOTH_WIFI needs no
// router or receiver changes: TXOTAConnector already claims the FC address, and
// the receiver learns the 0x12 origin when the first request arrives.
//
// pushFromBle() runs on the NimBLE host task and only takes the FIFO's lock;
// everything else is loop-core only, because pump() re-enters
// crsfRouter.processMessage().
class BleMspConnector final : public CRSFConnector
{
public:
    BleMspConnector();

    // Claims 0x12 on the router and arms the interlock probe. Called when the
    // bridge starts, not at boot.
    void begin();

    // App -> FC, from the NimBLE task. Enqueue only.
    void pushFromBle(const uint8_t *data, uint16_t len);

    // FC -> app. Router hands us MSP frames addressed to 0x12.
    void forwardMessage(const crsf_header_t *message) override;

    // Call every tick from the loop core: assembles what the app sent, moves
    // one MSP frame uplink when the OTA sender is idle, and drains
    // reassembled replies back out over BLE.
    void pump();

    // Drop partial frames when the phone disconnects. Counters survive, so
    // they can be read after the app drops the link; startSession() clears them.
    void reset();
    void startSession();

    // Handset readout. Separates the pipeline stages, so a stall shows which.
    uint16_t framesUp() const { return framesUpCount; }
    uint16_t framesDown() const { return framesDownCount; }
    // Sent by the app but never forwarded because the RF link was down
    uint16_t framesDropped() const { return framesDroppedCount; }

private:
    // NimBLE task -> loop core. atomicPushBytes takes the portMUX, so nothing
    // else needs locking.
    FIFO<512> fromBle;

    // One MSP frame at a time, gated on TXOTAConnector::uplinkBusy(). Anything
    // longer is dropped with a log line rather than truncated into a frame the
    // FC would misparse.
    static constexpr uint16_t INBOUND_MAX = 512;
    uint8_t inbound[INBOUND_MAX] = {};
    uint16_t inboundLen = 0;

    // Reassembled MSP replies waiting for BLE notifies. Loop-core both ends.
    FIFO<1024> outbound;

    CROSSFIRE2MSP crsf2msp;
    MSP2CROSSFIRE msp2crsf;

    uint16_t framesUpCount = 0;
    uint16_t framesDownCount = 0;
    uint16_t framesDroppedCount = 0;

    // One MSP request sent to the FC when the bridge starts, before any phone
    // connects. Sent for safety, not for its answer: Betaflight refuses to arm
    // while an MSP client is connected, but only counts one as connected once
    // MSP traffic has reached it. ARMING_DISABLED_MSP, Betaflight 4.0 onwards.
    bool probePending = false;
};

extern BleMspConnector bleMspConnector;

#endif
