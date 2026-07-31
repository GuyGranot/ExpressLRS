#if defined(TARGET_TX) && defined(PLATFORM_ESP32) && defined(TX_BLE_MSP)

#include "BleMspConnector.h"

#include "CRSFRouter.h"
#include "SpeedyBeeGatt.h"
#include "devBleMsp.h"
#include "TXOTAConnector.h"
#include "common.h"
#include "logging.h"

// Flight-controller setup, counterintuitive enough to record:
//   Telemetry on the receiver -- REQUIRED. MSP replies ride the telemetry
//       downlink, so with it off nothing can come back.
//   MSP function on that port -- NOT needed. Betaflight's CRSF driver unwraps
//       MSP_REQ/MSP_RESP itself; the port's MSP function is for a serial MSP
//       link. The app's "enable MSP at 115200 on UART4" error points the wrong
//       way here.

extern TXOTAConnector otaConnector;

BleMspConnector bleMspConnector;

BleMspConnector::BleMspConnector() : CRSFConnector()
{
    addDevice(CRSF_ADDRESS_BLUETOOTH_WIFI);
}

void BleMspConnector::begin()
{
    // addConnector inserts into a std::set, so calling this again is a no-op.
    crsfRouter.addConnector(this);
    probePending = true;
    DBGLN("BLEMSP connector registered, claiming 0x%02x", CRSF_ADDRESS_BLUETOOTH_WIFI);
}

void BleMspConnector::reset()
{
    fromBle.flush();
    inboundLen = 0;
    outbound.flush();
    crsf2msp.reset();
}

void BleMspConnector::startSession()
{
    reset();
    framesUpCount = 0;
    framesDownCount = 0;
    framesDroppedCount = 0;
}

void BleMspConnector::pushFromBle(const uint8_t *data, const uint16_t len)
{
    fromBle.atomicPushBytes(data, len);
}

void BleMspConnector::pump()
{
    // Open the MSP connection from the FC's point of view as soon as the link
    // allows, so its arming interlock covers the whole session rather than only
    // once a phone connects. MSP v1 request: '$M<', zero payload,
    // MSP_API_VERSION, checksum = len ^ cmd.
    //
    // Gated on the link, NOT on being disarmed -- unlike everything else we
    // send. isArmed comes from the handset, so it can be true with the FC still
    // powered off, and the probe would never go out. Safe while armed: the
    // ARMING_DISABLED flag blocks arming, it does not disarm.
    if (probePending && connectionState == connected && !otaConnector.uplinkBusy())
    {
        static const uint8_t probe[] = {'$', 'M', '<', 0x00, 0x01, 0x01};
        msp2crsf.parse(this, probe, sizeof(probe), CRSF_ADDRESS_BLUETOOTH_WIFI,
                       CRSF_ADDRESS_FLIGHT_CONTROLLER);
        probePending = false;
        DBGLN("BLEMSP opened MSP to the FC (arming interlock)");
        return; // one frame at a time
    }

    // Straight from the cross-task FIFO into the assembly buffer: no staging
    // copy, and a whole write lands in one go rather than in 64-byte steps.
    const uint16_t avail = fromBle.size();
    if (avail != 0)
    {
        if (inboundLen + avail > INBOUND_MAX)
        {
            // Truncating would hand the FC a frame whose length header lies,
            // so drop and let the app's own timeout retry.
            DBGLN("BLEMSP inbound overflow (%u + %u), dropping", inboundLen, avail);
            fromBle.flush();
            inboundLen = 0;
        }
        else
        {
            fromBle.popBytes(inbound + inboundLen, avail);
            inboundLen += avail;
        }
    }

    // App -> FC. One frame at a time: msp2crsf splits an MSP frame into
    // 57-byte chunks and pushes them all at the router, which lands them in
    // TXOTAConnector's 256-byte queue. Handing it a second frame before the
    // first has drained can evict the front of the first, and a missing chunk
    // is a silent whole-frame loss on the receiver.
    if (inboundLen != 0 && !otaConnector.uplinkBusy())
    {
        if (BleMspMayForward())
        {
            msp2crsf.parse(this, inbound, inboundLen, CRSF_ADDRESS_BLUETOOTH_WIFI,
                           CRSF_ADDRESS_FLIGHT_CONTROLLER);
            framesUpCount++;
            DBGLN("BLEMSP -> FC %u bytes", inboundLen);
        }
        else
        {
            // No link, or armed: drop rather than queue, so the app fails
            // fast and retries instead of getting an answer to a stale
            // request later.
            //
            // The armed half is defence in depth rather than the primary
            // interlock -- Betaflight refuses to arm while an MSP client is
            // connected, so the FC already prevents this. It costs nothing
            // and covers the window before the FC treats the connection as
            // established. Refusing to START while armed (BleMspStart) is the
            // guard that actually matters, because the FC's protection does
            // not exist until MSP is open.
            framesDroppedCount++;
            DBGLN("BLEMSP dropping %u bytes uplink, may-forward false", inboundLen);
        }
        inboundLen = 0;
    }

    // FC -> app. notifySerial owns the MTU arithmetic and fragments anything
    // that does not fit into '##'-marked chunks, so hand it whole buffers and
    // do not duplicate the chunk-size decision here. A refusal (no subscriber
    // yet, or a congested stack) leaves the rest queued for the next tick.
    if (!SpeedyBeeGatt::isClientConnected())
    {
        // Nobody to hand replies to -- the probe's answer lands here and is
        // of no interest, so do not let it sit and fill the buffer.
        outbound.flush();
        return;
    }
    while (outbound.size() != 0)
    {
        uint8_t buf[256];
        const uint16_t take = std::min(outbound.size(), (uint16_t)sizeof(buf));
        outbound.popBytes(buf, take);
        if (!SpeedyBeeGatt::notifySerial(buf, take))
        {
            break;
        }
    }
}

void BleMspConnector::forwardMessage(const crsf_header_t *message)
{
    if (message->type != CRSF_FRAMETYPE_MSP_RESP && message->type != CRSF_FRAMETYPE_MSP_REQ)
    {
        return;
    }
    crsf2msp.parse((uint8_t *)message, [&](const uint8_t *data, const uint32_t len) {
        if (outbound.free() < len)
        {
            // Dropping here loses a whole reply; the app retries. Better than
            // evicting the front and sending the phone a corrupt frame.
            DBGLN("BLEMSP outbound full, dropping %u byte reply", (unsigned)len);
            return;
        }
        outbound.pushBytes(data, len);
        framesDownCount++;
        DBGLN("BLEMSP FC -> app %u bytes", (unsigned)len);
    });
}

#endif // TARGET_TX && PLATFORM_ESP32 && TX_BLE_MSP
