#pragma once
#if defined(TARGET_RX)

#include "CRSFConnector.h"
#include "crsf2msp.h"
#include "msp2crsf.h"
#include "UpdateTransport.h"

#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32)
#include "FIFO.h"
#include "MspFrameAssembler.h"
#endif

// forward-declared so including this header does not require AsyncTCP on the
// include path (lib/BLEMSP includes it from outside this library)
class AsyncClient;
class AsyncServer;

class TcpMspConnector final : public CRSFConnector
{
public:
    TcpMspConnector();
    /**
     * @brief Stock path: server up and connector attached to the CRSF router.
     *
     * During dual-transport discovery only the server starts; the loop-core
     * coordinator attaches the winner's connector, so the router never holds two
     * connectors for CRSF_ADDRESS_BLUETOOTH_WIFI. All four are idempotent.
     */
    void begin();
    void startServer();
    void attachRouter();
    void stopServer();

    void forwardMessage(const crsf_header_t *message) override;

private:
    AsyncServer *TCPserver = nullptr;
    AsyncClient *TCPclient = nullptr;
    CROSSFIRE2MSP *crsf2msp = nullptr;
    MSP2CROSSFIRE *msp2crsf = nullptr;

    static void handleNewClient(void *arg, AsyncClient *client);
    static void handleDataIn(void *arg, AsyncClient *client, void *data, size_t len);
    static void handleDisconnect(void *arg, AsyncClient *client);
    static void handleTimeOut(void *arg, AsyncClient *client, uint32_t time);
    static void handleError(void *arg, AsyncClient *client, int8_t error);
    static constexpr uint32_t clientTimeoutS = 10U;

    void clientConnect(AsyncClient * client);
    void clientDisconnect(AsyncClient *client);
    void processData(AsyncClient * client, void * data, size_t len);

#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32)
    static_assert(MspFrameAssembler::MAX_FRAME == MSP_FRAME_MAX_LEN,
                  "the assembler's ceiling must match the CRSF2MSP bridge the frames feed");

    // written on the loop core, read from the async_tcp data callback
    volatile bool routerAttached = false;

    // Between a TCP claim (async_tcp task) and the loop-core attach, complete
    // validated frames wait here as [len16][frame] records; the drain replays
    // them in order, so no byte is ever parsed twice and a partial third
    // frame stays pending in the assembler.
    static constexpr size_t TCP_PENDING_FRAMES = 4;
    FIFO<1024> pendingFrames;
    uint8_t pendingCount = 0;
    MspFrameAssembler assembler;
#endif
};

#endif
