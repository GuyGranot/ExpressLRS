#include "BleMspConnector.h"

#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32) && defined(TARGET_TX)

#include "CRSFRouter.h"
#include "SpeedyBeeGatt.h"
#include "devBleMsp.h"
#include "common.h"
#include "rxtx_intf.h"
#include "logging.h"

// FC setup: telemetry must be enabled on the receiver (replies ride the
// telemetry downlink); no serial MSP port function is needed or wanted

BleMspConnector bleMspConnector;

BleMspConnector::BleMspConnector() : CRSFConnector()
{
    addDevice(CRSF_ADDRESS_BLUETOOTH_WIFI);
}

void BleMspConnector::begin()
{
    // addConnector inserts into a std::set, so calling this again is a no-op
    crsfRouter.addConnector(this);
    probePending = true;
    DBGLN("BLEMSP connector registered, claiming 0x%x", CRSF_ADDRESS_BLUETOOTH_WIFI);
}

void BleMspConnector::reset()
{
    fromBle.flush();
    inboundLen = 0;
    outbound.flush();
    pendingOutLen = 0;
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
    // allows, so its arming interlock covers the whole session. Gated on the
    // link, NOT on isArmed: safe while armed, and isArmed can read true with
    // the FC still powered off.
    if (probePending && connectionState == connected && !TxUplinkBusy())
    {
        // MSP v1 request: '$M<', zero payload, MSP_API_VERSION, checksum = len ^ cmd
        static const uint8_t probe[] = {'$', 'M', '<', 0x00, 0x01, 0x01};
        msp2crsf.parse(this, probe, sizeof(probe), CRSF_ADDRESS_BLUETOOTH_WIFI, CRSF_ADDRESS_FLIGHT_CONTROLLER);
        probePending = false;
        DBGLN("BLEMSP opened MSP to the FC (arming interlock)");
        return; // one frame at a time
    }

    // whole writes move from the cross-task FIFO into the assembly buffer in one go
    const uint16_t avail = fromBle.size();
    if (avail != 0)
    {
        if (inboundLen + avail > INBOUND_MAX)
        {
            // truncating would hand the FC a lying length header; drop, the app retries
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

    // App to FC, one frame at a time: a second frame pushed before the first
    // drains TXOTAConnector's queue can evict the front of it -- a silent
    // whole-frame loss on the receiver.
    if (inboundLen != 0 && !TxUplinkBusy())
    {
        if (BleMspMayForward())
        {
            msp2crsf.parse(this, inbound, inboundLen, CRSF_ADDRESS_BLUETOOTH_WIFI, CRSF_ADDRESS_FLIGHT_CONTROLLER);
            framesUpCount++;
            DBGLN("BLEMSP -> FC %u bytes", inboundLen);
        }
        else
        {
            // no link or armed: drop so the app fails fast rather than
            // getting an answer to a stale request later
            framesDroppedCount++;
            DBGLN("BLEMSP dropping %u bytes uplink, may-forward false", inboundLen);
        }
        inboundLen = 0;
    }

    // FC to app. notifySerial owns the MTU arithmetic and fragmentation, so hand
    // it whole frames; a refusal leaves the rest queued for the next tick.
    if (!SpeedyBeeGatt::isClientConnected())
    {
        // nobody to hand replies to; do not let the probe's answer fill the buffer
        outbound.flush();
        pendingOutLen = 0;
        return;
    }
    for (;;)
    {
        if (pendingOutLen == 0)
        {
            if (outbound.size() == 0)
            {
                break;
            }
            // One length-prefixed frame per notify, never glue two replies
            pendingOutLen = outbound.popSize();
            outbound.popBytes(pendingOut, pendingOutLen);
        }
        if (!SpeedyBeeGatt::notifySerial(pendingOut, pendingOutLen))
        {
            break; // retry this frame next tick
        }
        pendingOutLen = 0;
    }
}

void BleMspConnector::forwardMessage(const crsf_header_t *message)
{
    if (message->type != CRSF_FRAMETYPE_MSP_RESP && message->type != CRSF_FRAMETYPE_MSP_REQ)
    {
        return;
    }
    crsf2msp.parse((uint8_t *)message, [&](const uint8_t *data, const uint32_t len) {
        if (outbound.free() < len + 2 || len > sizeof(pendingOut))
        {
            // drop the whole reply; evicting the front would corrupt a frame
            DBGLN("BLEMSP outbound full, dropping %u byte reply", (unsigned)len);
            return;
        }
        outbound.pushSize(len);
        outbound.pushBytes(data, len);
        framesDownCount++;
        DBGLN("BLEMSP FC -> app %u bytes", (unsigned)len);
    });
}

#endif

#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32) && defined(TARGET_RX)

#include "CRSFRouter.h"
#include "SpeedyBeeGatt.h"
#include "logging.h"

// FC setup: the CRSF UART needs no serial MSP port function; Betaflight's
// CRSF driver unwraps MSP_REQ/MSP_RESP itself

BleMspConnector bleMspConnector;

BleMspConnector::BleMspConnector() : CRSFConnector()
{
    addDevice(CRSF_ADDRESS_BLUETOOTH_WIFI);
}

void BleMspConnector::reset()
{
    fromBle.lock();
    fromBle.flush();
    fromBle.unlock();
    assembler.reset();
    outbound.flush();
    pendingOutLen = 0;
    crsf2msp.reset();
}

void BleMspConnector::startSession()
{
    reset();
    sawValid = false;
}

void BleMspConnector::pushFromBle(const uint8_t *data, const uint16_t len)
{
    fromBle.atomicPushBytes(data, len);
}

void BleMspConnector::forwardFrame(const uint8_t *frame, const uint16_t len)
{
    msp2crsf.parse(this, frame, len, CRSF_ADDRESS_BLUETOOTH_WIFI, CRSF_ADDRESS_FLIGHT_CONTROLLER);
    sawValid = true;
    DBGLN("BLEMSP -> FC %u bytes", len);
}

void BleMspConnector::pump()
{
    if (!updateTransportAcceptsBleInput())
    {
        return; // WiFi owns the update session; teardown follows
    }

    // App to FC, one validated frame at a time. The first one is the BLE
    // ownership event: claim, attach and forward all on the loop core, so the
    // router's connector set is never touched from a callback.
    uint8_t staging[64];
    while (fromBle.size() != 0)
    {
        fromBle.lock();
        const uint16_t take = std::min(fromBle.size(), (uint16_t)sizeof(staging));
        fromBle.popBytes(staging, take);
        fromBle.unlock();
        for (uint16_t i = 0; i < take; i++)
        {
            if (assembler.push(staging[i]) != MspFrameAssembler::MSP_ASM_COMPLETE)
            {
                continue;
            }
            if (!routerAttached)
            {
                if (!claimUpdateTransport(TRANSPORT_BLE))
                {
                    return; // lost the race; WiFi-owned teardown runs next tick
                }
                crsfRouter.addConnector(this);
                routerAttached = true;
                // open MSP to the FC first, so its arming interlock covers the session
                static const uint8_t probe[] = {'$', 'M', '<', 0x00, 0x01, 0x01};
                msp2crsf.parse(this, probe, sizeof(probe), CRSF_ADDRESS_BLUETOOTH_WIFI, CRSF_ADDRESS_FLIGHT_CONTROLLER);
                DBGLN("BLEMSP claimed the update session");
            }
            forwardFrame(assembler.frame(), assembler.frameLen());
        }
    }

    // FC to app. notifySerial owns the MTU arithmetic and fragmentation, so
    // hand it whole frames; a refusal leaves the rest queued for the next tick.
    if (!SpeedyBeeGatt::isClientConnected())
    {
        // nobody to hand replies to; do not let the probe's answer fill the buffer
        outbound.flush();
        pendingOutLen = 0;
        return;
    }
    for (;;)
    {
        if (pendingOutLen == 0)
        {
            if (outbound.size() == 0)
            {
                break;
            }
            // One length-prefixed frame per notify, never glue two replies
            pendingOutLen = outbound.popSize();
            outbound.popBytes(pendingOut, pendingOutLen);
        }
        if (!SpeedyBeeGatt::notifySerial(pendingOut, pendingOutLen))
        {
            break; // retry this frame next tick
        }
        pendingOutLen = 0;
    }
}

void BleMspConnector::forwardMessage(const crsf_header_t *message)
{
    if (message->type != CRSF_FRAMETYPE_MSP_RESP && message->type != CRSF_FRAMETYPE_MSP_REQ)
    {
        return;
    }
    crsf2msp.parse((uint8_t *)message, [&](const uint8_t *data, const uint32_t len) {
        if (outbound.free() < len + 2 || len > sizeof(pendingOut))
        {
            // drop the whole reply; evicting the front would corrupt a frame
            DBGLN("BLEMSP outbound full, dropping %u byte reply", (unsigned)len);
            return;
        }
        outbound.pushSize(len);
        outbound.pushBytes(data, len);
        DBGLN("BLEMSP FC -> app %u bytes", (unsigned)len);
    });
}

#endif
