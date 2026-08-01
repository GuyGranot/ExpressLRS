#if defined(TARGET_RX)

#include "TcpMspConnector.h"

#if defined(PLATFORM_ESP8266)
#include "ESPAsyncTCP.h"
#else
#include "AsyncTCP.h"
#endif
#include "logging.h"

#include "CRSFRouter.h"
#include "crsf2msp.h"
#include "msp2crsf.h"

#define TCP_PORT_BETAFLIGHT 5761 //port 5761 as used by BF configurator

TcpMspConnector::TcpMspConnector() : CRSFConnector()
{
    addDevice(CRSF_ADDRESS_BLUETOOTH_WIFI);
}

void TcpMspConnector::begin()
{
    if (updateDualDiscovery())
    {
        // ownership is undecided: accept connections, but stay off the CRSF
        // router until the loop-core coordinator attaches the claim winner
        startServer();
        return;
    }
    attachRouter();
    startServer();
}

void TcpMspConnector::startServer()
{
    if (TCPserver != nullptr)
    {
        return;
    }
    TCPserver = new AsyncServer(TCP_PORT_BETAFLIGHT);
    TCPserver->onClient(handleNewClient, this);
    TCPserver->begin();
}

void TcpMspConnector::attachRouter()
{
#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32)
    if (routerAttached)
    {
        return;
    }
    crsfRouter.addConnector(this);
    // replay frames validated between the claim and this attach, in order.
    // The condition cannot change mid-drain (ownership never resets and
    // msp2crsf is only ever set), so evaluate it once.
    const bool replay = msp2crsf != nullptr && getUpdateTransport() == TRANSPORT_WIFI;
    uint8_t frame[MspFrameAssembler::MAX_FRAME];
    for (;;)
    {
        pendingFrames.lock();
        const uint16_t frameLen = pendingFrames.popSize();
        if (frameLen == 0 || frameLen > sizeof(frame))
        {
            pendingFrames.flush();
            pendingCount = 0;
            pendingFrames.unlock();
            break;
        }
        pendingFrames.popBytes(frame, frameLen);
        pendingCount--;
        pendingFrames.unlock();
        if (replay)
        {
            msp2crsf->parse(this, frame, frameLen, CRSF_ADDRESS_BLUETOOTH_WIFI, CRSF_ADDRESS_FLIGHT_CONTROLLER);
        }
    }
    routerAttached = true;
#else
    crsfRouter.addConnector(this);
#endif
}

void TcpMspConnector::stopServer()
{
    if (TCPclient != nullptr)
    {
        // handleDisconnect fires from the close and deletes the client
        AsyncClient *client = TCPclient;
        TCPclient = nullptr;
        client->close(true);
    }
    if (TCPserver != nullptr)
    {
        TCPserver->end();
        delete TCPserver;
        TCPserver = nullptr;
    }
#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32)
    pendingFrames.lock();
    pendingFrames.flush();
    pendingCount = 0;
    pendingFrames.unlock();
    assembler.reset();
#endif
}

void TcpMspConnector::handleNewClient(void *arg, AsyncClient *client)
{
    DBGLN("TCP(%x) connected ip %s", client, client->remoteIP().toString().c_str());
    ((TcpMspConnector *)arg)->clientConnect(client);
}

void TcpMspConnector::handleDataIn(void *arg, AsyncClient *client, void *data, const size_t len)
{
    DBGLN("TCP(%x) read %u", client, len);
    ((TcpMspConnector *)arg)->processData(client, data, len);
}

void TcpMspConnector::handleDisconnect(void *arg, AsyncClient *client)
{
    DBGLN("TCP(%x) disconnected", client);
    ((TcpMspConnector *)arg)->clientDisconnect(client);
}

void TcpMspConnector::handleTimeOut(void *arg, AsyncClient *client, uint32_t time)
{
    DBGLN("TCP(%x) timeout", client);
}

void TcpMspConnector::handleError(void *arg, AsyncClient *client, int8_t error)
{
    DBGLN("TCP(%x) connection error %s", client, client->errorToString(error));
    ((TcpMspConnector *)arg)->clientDisconnect(client);
}

void TcpMspConnector::clientConnect(AsyncClient *client)
{
    if (crsf2msp == nullptr) {
        crsf2msp = new CROSSFIRE2MSP();
        msp2crsf = new MSP2CROSSFIRE();
    }
    if (TCPclient != nullptr)
    {
        crsf2msp->reset();
        TCPclient->close();
        TCPclient = client;
    }

    // register events
    client->onData(handleDataIn, this);
    client->onError(handleError, this);
    client->onDisconnect(handleDisconnect, this);
    client->onTimeout(handleTimeOut, this);
    client->setRxTimeout(clientTimeoutS);
}

void TcpMspConnector::clientDisconnect(AsyncClient *client)
{
    if (client == TCPclient)
    {
        TCPclient = nullptr;
#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32)
        // a partial frame must not glue itself to the next client's bytes
        assembler.reset();
#endif
    }
    client->close();
    delete client;
}

void TcpMspConnector::processData(AsyncClient *client, void *data, const size_t len)
{
    TCPclient = client;
#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32)
    if (!updateTransportAcceptsWifiInput())
    {
        return; // BLE owns the update session
    }
    if (!routerAttached)
    {
        // discovery: only a complete validated MSP request claims the session,
        // and frames queue until the loop core attaches the connector
        const uint8_t *bytes = (const uint8_t *)data;
        for (size_t i = 0; i < len; i++)
        {
            if (assembler.push(bytes[i]) != MspFrameAssembler::MSP_ASM_COMPLETE)
            {
                continue;
            }
            pendingFrames.lock();
            // drop whole frames only when full; the queued ones stay intact
            if (pendingCount < TCP_PENDING_FRAMES && pendingFrames.available(assembler.frameLen() + 2))
            {
                pendingFrames.pushSize(assembler.frameLen());
                pendingFrames.pushBytes(assembler.frame(), assembler.frameLen());
                pendingCount++;
            }
            pendingFrames.unlock();
            claimUpdateTransport(TRANSPORT_WIFI);
        }
        return;
    }
#endif
    msp2crsf->parse(this, (uint8_t *)data, len, CRSF_ADDRESS_BLUETOOTH_WIFI, CRSF_ADDRESS_FLIGHT_CONTROLLER);
}

void TcpMspConnector::forwardMessage(const crsf_header_t *message)
{
    if (TCPclient != nullptr && (message->type == CRSF_FRAMETYPE_MSP_RESP || message->type == CRSF_FRAMETYPE_MSP_REQ))
    {
        DBGLN("TCP(CRSF) msg %u", message->frame_size);
        crsf2msp->parse((uint8_t *)message, [&](const uint8_t *data, const size_t len) {
            TCPclient->write((const char *)data, len);
            DBGLN("TCP(%x) write %u", TCPclient, len);
        });
    }
}

#endif
