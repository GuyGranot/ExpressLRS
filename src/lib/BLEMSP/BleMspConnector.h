#pragma once

#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32)

#include "CRSFConnector.h"
#include "FIFO.h"
#include "crsf2msp.h"
#include "msp2crsf.h"

#if defined(TARGET_TX)
/**
 * @class BleMspConnector
 *
 * @brief Bridges the SpeedyBee app's BLE serial channel to the flight controller
 * over the RF link.
 *
 * Modeled on lib/WIFI/TcpMspConnector, which does the same job for the receiver's
 * WiFi bridge. Claiming CRSF_ADDRESS_BLUETOOTH_WIFI needs no router or receiver
 * changes: TXOTAConnector already claims the FC address, and the receiver learns
 * the 0x12 origin when the first request arrives.
 *
 * pushFromBle() runs on the NimBLE host task and only takes the FIFO's lock;
 * everything else is loop-core only, because pump() re-enters
 * crsfRouter.processMessage().
 */
class BleMspConnector final : public CRSFConnector
{
public:
    BleMspConnector();

    /**
     * @brief Claims 0x12 on the router and arms the interlock probe. Called when
     * the bridge starts, not at boot.
     */
    void begin();

    /**
     * @brief App-to-FC bytes, from the NimBLE task. Enqueue only.
     */
    void pushFromBle(const uint8_t *data, uint16_t len);

    /**
     * @brief FC-to-app: the router hands us MSP frames addressed to 0x12.
     */
    void forwardMessage(const crsf_header_t *message) override;

    /**
     * @brief Call every tick from the loop core: assembles what the app sent,
     * moves one MSP frame uplink when the OTA sender is idle, and drains
     * reassembled replies back out over BLE.
     */
    void pump();

    /**
     * @brief Drops partial frames when the phone disconnects. Counters survive so
     * they can be read after the app drops the link; startSession() clears them.
     */
    void reset();
    void startSession();

    // Handset readout. Separates the pipeline stages, so a stall shows which.
    uint16_t framesUp() const { return framesUpCount; }
    uint16_t framesDown() const { return framesDownCount; }
    // Sent by the app but never forwarded because the RF link was down
    uint16_t framesDropped() const { return framesDroppedCount; }

private:
    // NimBLE task to loop core. atomicPushBytes takes the portMUX, so nothing
    // else needs locking.
    FIFO<512> fromBle;

    // One MSP frame at a time, gated on TxUplinkBusy(). Anything
    // longer is dropped with a log line rather than truncated into a frame the
    // FC would misparse.
    static constexpr uint16_t INBOUND_MAX = 512;
    uint8_t inbound[INBOUND_MAX] = {};
    uint16_t inboundLen = 0;

    // Reassembled MSP replies waiting for BLE notifies, stored as length-prefixed
    // whole frames. Loop-core both ends. Each notify carries exactly one MSP
    // frame: the app treats a notification as a framing unit, and gluing two
    // responses into one notify makes it drop the second. A frame only leaves
    // once the stack accepted the notify: a refused frame stays in pendingOut for
    // the next tick instead of being lost.
    FIFO<1024> outbound;
    uint8_t pendingOut[MSP_FRAME_MAX_LEN + 12] = {};
    uint16_t pendingOutLen = 0;

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

#if defined(TARGET_RX)
#include "UpdateTransport.h"
#include "MspFrameAssembler.h"

/**
 * @class BleMspConnector
 *
 * @brief Bridges the SpeedyBee app's BLE serial channel to the flight controller
 * over the receiver's CRSF UART.
 *
 * Modeled on lib/WIFI/TcpMspConnector, which does the same job for the WiFi
 * bridge; both claim CRSF_ADDRESS_BLUETOOTH_WIFI, which is safe because the
 * update transport arbiter attaches exactly one of them to the router, ever.
 *
 * pushFromBle() runs on the NimBLE host task and only takes the FIFO's lock;
 * everything else is loop-core only, because pump() re-enters
 * crsfRouter.processMessage(). The first complete validated MSP frame is what
 * claims the update session for BLE: claim, router attachment and the
 * forward of that same frame all happen inside one pump() call.
 */
class BleMspConnector final : public CRSFConnector
{
public:
    BleMspConnector();

    /**
     * @brief App-to-FC bytes, from the NimBLE task. Enqueue only.
     */
    void pushFromBle(const uint8_t *data, uint16_t len);

    /**
     * @brief FC-to-app: the router hands us MSP frames addressed to 0x12.
     */
    void forwardMessage(const crsf_header_t *message) override;

    /**
     * @brief Call every tick from the loop core: validates what the app sent
     * (frames forward only once BLE owns the session) and drains reassembled
     * replies back out over BLE.
     */
    void pump();

    /**
     * @brief Drops partial frames when the phone disconnects; sawValidFrame()
     * survives, and startSession() clears it for the next client.
     */
    void reset();
    void startSession();

    /**
     * @brief True once a complete valid MSP frame has arrived from the app: the
     * BLE ownership event, also used by the passive-connection eviction.
     */
    bool sawValidFrame() const { return sawValid; }

private:
    void forwardFrame(const uint8_t *frame, uint16_t len);

    // NimBLE task to loop core. atomicPushBytes takes the portMUX, so nothing
    // else needs locking.
    FIFO<512> fromBle;

    // Only complete frames the assembler has validated are ever forwarded;
    // garbage and oversized declared lengths are rejected in the assembler
    MspFrameAssembler assembler;

    // Reassembled MSP replies waiting for BLE notifies, stored as length-prefixed
    // whole frames. Loop-core both ends. Each notify carries exactly one MSP
    // frame: the app treats a notification as a framing unit, and gluing two
    // responses into one notify makes it drop the second. A frame only leaves
    // once the stack accepted the notify: a refused frame stays in pendingOut
    // for the next tick instead of being lost.
    FIFO<1024> outbound;
    uint8_t pendingOut[MSP_FRAME_MAX_LEN + 12] = {};
    uint16_t pendingOutLen = 0;

    CROSSFIRE2MSP crsf2msp;
    MSP2CROSSFIRE msp2crsf;

    bool routerAttached = false;
    bool sawValid = false;
};

extern BleMspConnector bleMspConnector;
#endif

#endif
