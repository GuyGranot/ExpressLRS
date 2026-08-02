#include "SpeedyBeeGatt.h"

#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32)

#include <Arduino.h>

#include "logging.h"

#include <NimBLEDevice.h>

#include <sdkconfig.h>
#include <soc/soc_caps.h>
// SOC_BT_SUPPORTED is the IDF 4 name, SOC_BLE_SUPPORTED the IDF 5 one; an
// undefined macro reads as 0, so this holds across both generations
#if !SOC_BLE_SUPPORTED && !SOC_BT_SUPPORTED
#error "USE_BLE_MSP requires a SoC with a BLE controller"
#endif

// The advertised UUID is what the app filters on; ABF0 is found after connecting
static constexpr uint16_t UUID_ADVERTISED = 0x00FF;
static constexpr uint16_t UUID_SVC_SPP = 0xABF0;
static constexpr uint16_t UUID_CHR_DATA_RECV = 0xABF1;   // app to device serial
static constexpr uint16_t UUID_CHR_DATA_NOTIFY = 0xABF2; // device to app serial
static constexpr uint16_t UUID_CHR_CMD_RECV = 0xABF3;    // app to device control
static constexpr uint16_t UUID_CHR_CMD_NOTIFY = 0xABF4;  // device to app control

// No manufacturer-specific advertising data: a genuine board sends some, but a
// hardware bisect showed the app neither scans nor connects on it

// ble_spp_server fragmentation of long notifications
static constexpr uint8_t FRAG_MARKER = '#';
static constexpr size_t FRAG_HEADER_LEN = 4; // '#','#',total,index
static constexpr size_t ATT_OVERHEAD = 3;

static NimBLEServer *server = nullptr;
static NimBLECharacteristic *chrDataNotify = nullptr;
static NimBLECharacteristic *chrCmdNotify = nullptr;
static SpeedyBeeHandshake *handshake = nullptr;
// Crossed between the NimBLE host task and the loop core; all single-word
static volatile bool running = false;
static volatile bool clientConnected = false;
static volatile SpeedyBeeGatt::serialSink_t serialSink = nullptr;
static volatile uint16_t currentMtu = 23;

#if defined(TARGET_TX)
// Written on the NimBLE task; loop-core reads are advisory display data
static SpeedyBeeGatt::HandshakeTrace traceState;
#endif

static void notifyCmd(void *, const uint8_t *data, size_t len)
{
    if (chrCmdNotify != nullptr)
    {
        DBGLN("BLEMSP ABF4 -> %u bytes", (unsigned)len);
#if defined(TARGET_TX)
        traceState.lastRespLen = (uint8_t)len;
#endif
        chrCmdNotify->notify(data, len);
    }
}

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
    {
        clientConnected = true;
        currentMtu = connInfo.getMTU();
#if defined(TARGET_TX)
        traceState = {};
#endif
        DBGLN("BLEMSP client connected, mtu=%u", currentMtu);
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override
    {
        clientConnected = false;
        currentMtu = 23;
#if defined(TARGET_TX)
        // 0x13 = peer chose to disconnect, 0x08 = supervision timeout
        traceState.disconnectReason = (uint8_t)reason;
#endif
        if (handshake != nullptr)
        {
            handshake->reset();
        }
        // No re-advertising when end() is dropping the client on purpose
        if (running)
        {
            DBGLN("BLEMSP client disconnected (reason %d), advertising again", reason);
            NimBLEDevice::startAdvertising();
        }
    }

    void onMTUChange(uint16_t MTU, NimBLEConnInfo &connInfo) override
    {
        currentMtu = MTU;
        DBGLN("BLEMSP mtu now %u", MTU);
    }
};

// ABF1: raw MSP from the app. Hand straight to the sink; no parsing here.
class DataRecvCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
    {
        const NimBLEAttValue value = pCharacteristic->getValue();
        const SpeedyBeeGatt::serialSink_t sink = serialSink;
        if (value.size() != 0 && sink != nullptr)
        {
            sink(value.data(), value.size());
        }
    }
};

// ABF3: proprietary handshake, answered inline on the NimBLE task
class CmdRecvCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
    {
        const NimBLEAttValue value = pCharacteristic->getValue();
        if (value.size() == 0 || handshake == nullptr)
        {
            return;
        }
#if defined(TARGET_TX)
        traceState.ctrlWrites++;
        traceState.lastCmd = value.data()[0];
        traceState.lastRespLen = 0; // notifyCmd overwrites this if we answer
#endif
        DBGLN("BLEMSP ABF3 <- cmd 0x%x (%u bytes)", value.data()[0], (unsigned)value.size());
        if (!handshake->handleControlWrite(value.data(), value.size(), notifyCmd, nullptr))
        {
#if defined(TARGET_TX)
            traceState.unhandledCmd = value.data()[0];
#endif
            DBGLN("BLEMSP unhandled ABF3 cmd 0x%x (%u bytes), NO REPLY SENT", value.data()[0], (unsigned)value.size());
        }
    }
};

static ServerCallbacks serverCallbacks;
static DataRecvCallbacks dataRecvCallbacks;
static CmdRecvCallbacks cmdRecvCallbacks;

bool SpeedyBeeGatt::begin(const SpeedyBeeDeviceInfo &info, const char *deviceName, serialSink_t onSerialBytes)
{
    if (running)
    {
        return true;
    }
    serialSink = onSerialBytes;

    if (!NimBLEDevice::init(deviceName))
    {
        DBGLN("BLEMSP NimBLE init FAILED, heap %u", (unsigned)ESP.getFreeHeap());
        serialSink = nullptr;
        return false;
    }
    NimBLEDevice::setMTU(517);

    // The device-info blob carries the MAC as bare uppercase hex; report our
    // own, which exists only once the stack is up
    static char macHex[13];
    const std::string addr = NimBLEDevice::getAddress().toString();
    size_t n = 0;
    for (size_t i = 0; i < addr.size() && n < sizeof(macHex) - 1; i++)
    {
        const char c = addr[i];
        if (c != ':')
        {
            macHex[n++] = (c >= 'a' && c <= 'f') ? (char)(c - 'a' + 'A') : c;
        }
    }
    macHex[n] = '\0';

    SpeedyBeeDeviceInfo ourInfo = info;
    ourInfo.mac = macHex;
    static SpeedyBeeHandshake hs(ourInfo);
    handshake = &hs;
    handshake->reset();
    handshake->seedRandom(esp_random());
    DBGLN("BLEMSP identity: %s / %s / mac %s", ourInfo.productCode, ourInfo.name, macHex);

    server = NimBLEDevice::createServer();
    // deleteCallbacks=false: ~NimBLEServer deletes the callbacks object by
    // default, and freeing a static is a heap-assert panic at deinit(true)
    server->setCallbacks(&serverCallbacks, false);

    NimBLEService *spp = server->createService(NimBLEUUID(UUID_SVC_SPP));
    // WRITE_NR is required: iOS silently discards the app's
    // write-without-response if the characteristic does not advertise it
    NimBLECharacteristic *chrDataRecv = spp->createCharacteristic(NimBLEUUID(UUID_CHR_DATA_RECV), READ | WRITE_NR);
    chrDataRecv->setCallbacks(&dataRecvCallbacks);

    chrDataNotify = spp->createCharacteristic(NimBLEUUID(UUID_CHR_DATA_NOTIFY), READ | NOTIFY);

    NimBLECharacteristic *chrCmdRecv = spp->createCharacteristic(NimBLEUUID(UUID_CHR_CMD_RECV), READ | WRITE_NR);
    chrCmdRecv->setCallbacks(&cmdRecvCallbacks);

    chrCmdNotify = spp->createCharacteristic(NimBLEUUID(UUID_CHR_CMD_NOTIFY), READ | NOTIFY);

    spp->start();

    // Captured two-packet layout: flags + 16-bit service in the ADV packet,
    // the complete local name in the scan response
    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();

    NimBLEAdvertisementData advData;
    advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    advData.addServiceUUID(NimBLEUUID(UUID_ADVERTISED));
    advertising->setAdvertisementData(advData);

    NimBLEAdvertisementData scanData;
    scanData.setName(deviceName);
    advertising->setScanResponseData(scanData);
    advertising->enableScanResponse(true);

    advertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);
    advertising->start();

    running = true;
    DBGLN("BLEMSP advertising '%s' (svc 0x%x), free heap %u", deviceName, UUID_ADVERTISED, (unsigned)ESP.getFreeHeap());
    return true;
}

void SpeedyBeeGatt::end()
{
    if (!NimBLEDevice::isInitialized())
    {
        return;
    }
    // detach the sink first so a racing ABF1 write cannot feed a FIFO that is
    // about to be flushed; running=false stops onDisconnect re-advertising
    serialSink = nullptr;
    running = false;
    NimBLEDevice::stopAdvertising();
    dropClient();
    // blocking, but one-shot: frees the host task and its heap
    NimBLEDevice::deinit(true);
    server = nullptr;
    chrDataNotify = nullptr;
    chrCmdNotify = nullptr;
    handshake = nullptr;
    clientConnected = false;
    currentMtu = 23;
    DBGLN("BLEMSP stopped, free heap %u", (unsigned)ESP.getFreeHeap());
}

void SpeedyBeeGatt::dropClient()
{
    if (server != nullptr)
    {
        for (const uint16_t handle : server->getPeerDevices())
        {
            server->disconnect(handle);
        }
    }
}

bool SpeedyBeeGatt::isRunning()
{
    return running;
}

bool SpeedyBeeGatt::isClientConnected()
{
    return clientConnected;
}

#if defined(TARGET_TX)
bool SpeedyBeeGatt::sessionReady()
{
    return handshake != nullptr && handshake->sessionReady();
}

SpeedyBeeGatt::HandshakeTrace SpeedyBeeGatt::trace()
{
    return traceState;
}
#endif

bool SpeedyBeeGatt::notifySerial(const uint8_t *data, size_t len)
{
    if (!running || !clientConnected || chrDataNotify == nullptr || len == 0)
    {
        return false;
    }

    // A single unfragmented notify; anything longer gets '##' chunked
    const size_t unfragmented = currentMtu > ATT_OVERHEAD ? currentMtu - ATT_OVERHEAD : 20;
    if (len <= unfragmented)
    {
        return chrDataNotify->notify(data, len);
    }

    size_t chunkSize = currentMtu > (ATT_OVERHEAD + FRAG_HEADER_LEN) ? currentMtu - ATT_OVERHEAD - FRAG_HEADER_LEN : 16;
    // The staging buffer below is BLE_ATT_ATTR_MAX_LEN; at MTU 517 the MTU
    // arithmetic alone would overflow it by two bytes
    if (chunkSize > BLE_ATT_ATTR_MAX_LEN - FRAG_HEADER_LEN)
    {
        chunkSize = BLE_ATT_ATTR_MAX_LEN - FRAG_HEADER_LEN;
    }
    const size_t total = (len + chunkSize - 1) / chunkSize;
    if (total > 255)
    {
        DBGLN("BLEMSP notify too long (%u bytes), dropped", (unsigned)len);
        return false;
    }

    uint8_t frame[BLE_ATT_ATTR_MAX_LEN];
    for (size_t i = 0; i < total; i++)
    {
        const size_t offset = i * chunkSize;
        const size_t thisChunk = (len - offset) < chunkSize ? (len - offset) : chunkSize;
        frame[0] = FRAG_MARKER;
        frame[1] = FRAG_MARKER;
        frame[2] = (uint8_t)total;
        frame[3] = (uint8_t)(i + 1);
        memcpy(frame + FRAG_HEADER_LEN, data + offset, thisChunk);
        if (!chrDataNotify->notify(frame, FRAG_HEADER_LEN + thisChunk))
        {
            DBGLN("BLEMSP notify failed at chunk %u/%u", (unsigned)(i + 1), (unsigned)total);
            return false;
        }
    }
    return true;
}

#endif
