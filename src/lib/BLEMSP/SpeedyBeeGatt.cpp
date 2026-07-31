#if defined(TARGET_TX) && defined(PLATFORM_ESP32) && defined(TX_BLE_MSP)

#include <Arduino.h>

#include "SpeedyBeeGatt.h"
#include "logging.h"

#include <NimBLEDevice.h>

namespace
{
// The advertised UUID is what the app filters on; ABF0 is found after
// connecting. See the header for the capture these came from.
constexpr uint16_t UUID_ADVERTISED = 0x00FF;
constexpr uint16_t UUID_SVC_SPP = 0xABF0;
constexpr uint16_t UUID_CHR_DATA_RECV = 0xABF1;   // app -> device serial
constexpr uint16_t UUID_CHR_DATA_NOTIFY = 0xABF2; // device -> app serial
constexpr uint16_t UUID_CHR_CMD_RECV = 0xABF3;    // app -> device control
constexpr uint16_t UUID_CHR_CMD_NOTIFY = 0xABF4;  // device -> app control

// No manufacturer-specific advertising data. A genuine SpeedyBee F405 V4
// advertises company 0x1183 plus 18 opaque bytes, and we cloned them verbatim
// while getting recognition working -- but a bisect on hardware showed the
// app neither scans nor connects on them: omitting the field entirely still
// reaches the Betaflight config screen. The app identifies the product from
// the device-info blob (see SpeedyBeeHandshake), which is why an arbitrary
// product code works. Do not reintroduce a captured blob here; it would make
// every unit advertise another board's identity for no benefit.

// ble_spp_server fragmentation of long notifications.
constexpr uint8_t FRAG_MARKER = '#';
constexpr size_t FRAG_HEADER_LEN = 4; // '#','#',total,index
constexpr size_t ATT_OVERHEAD = 3;

NimBLEServer *server = nullptr;
NimBLECharacteristic *chrDataNotify = nullptr;
NimBLECharacteristic *chrCmdNotify = nullptr;
SpeedyBeeGatt::serialSink_t serialSink = nullptr;
SpeedyBeeHandshake *handshake = nullptr;
bool running = false;
bool clientConnected = false;
uint16_t currentMtu = 23;

// Handshake trace for the handset readout. Latched across disconnect so the
// post-mortem survives the app dropping the link, and cleared on the next
// connect so each attempt reads clean. Written only from the NimBLE task; the
// struct IS the state, so the field list lives in exactly one place.
SpeedyBeeGatt::HandshakeTrace traceState;

void notifyCmd(void *, const uint8_t *data, size_t len)
{
    if (chrCmdNotify != nullptr)
    {
        DBGLN("BLEMSP ABF4 -> %u bytes", (unsigned)len);
        traceState.lastRespLen = (uint8_t)len;
        chrCmdNotify->notify(data, len);
    }
}

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
    {
        clientConnected = true;
        currentMtu = connInfo.getMTU();
        traceState = {};
        DBGLN("BLEMSP client connected, mtu=%u", currentMtu);
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override
    {
        clientConnected = false;
        currentMtu = 23;
        // 0x13 = peer chose to disconnect (it judged us wrong); 0x08 =
        // supervision timeout (we stopped answering). Very different bugs.
        traceState.disconnectReason = (uint8_t)reason;
        if (handshake != nullptr)
        {
            handshake->reset();
        }
        DBGLN("BLEMSP client disconnected (reason %d), advertising again", reason);
        NimBLEDevice::startAdvertising();
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
        if (value.size() != 0 && serialSink != nullptr)
        {
            serialSink(value.data(), value.size());
        }
    }
};

// ABF3: proprietary handshake. Answered inline -- the responder owns no
// state shared with the loop core, so this stays on the NimBLE task.
class CmdRecvCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
    {
        const NimBLEAttValue value = pCharacteristic->getValue();
        if (value.size() == 0 || handshake == nullptr)
        {
            return;
        }
        traceState.ctrlWrites++;
        traceState.lastCmd = value.data()[0];
        traceState.lastRespLen = 0; // notifyCmd overwrites this if we answer
        DBGLN("BLEMSP ABF3 <- cmd 0x%02x (%u bytes): %02x %02x %02x %02x %02x %02x",
              value.data()[0], (unsigned)value.size(),
              value.size() > 0 ? value.data()[0] : 0, value.size() > 1 ? value.data()[1] : 0,
              value.size() > 2 ? value.data()[2] : 0, value.size() > 3 ? value.data()[3] : 0,
              value.size() > 4 ? value.data()[4] : 0, value.size() > 5 ? value.data()[5] : 0);
        if (!handshake->handleControlWrite(value.data(), value.size(), notifyCmd, nullptr))
        {
            traceState.unhandledCmd = value.data()[0];
            DBGLN("BLEMSP unhandled ABF3 cmd 0x%02x (%u bytes) -- NO REPLY SENT",
                  value.data()[0], (unsigned)value.size());
        }
    }
};

ServerCallbacks serverCallbacks;
DataRecvCallbacks dataRecvCallbacks;
CmdRecvCallbacks cmdRecvCallbacks;
} // namespace

void SpeedyBeeGatt::begin(const SpeedyBeeDeviceInfo &info, const char *deviceName,
                          serialSink_t onSerialBytes)
{
    if (running)
    {
        return;
    }
    serialSink = onSerialBytes;

    NimBLEDevice::init(deviceName);
    NimBLEDevice::setMTU(517);

    // The device-info blob carries the MAC as bare uppercase hex, and the app
    // reads it. Report our own rather than a captured board's, which means
    // filling it after the stack is up and the address exists.
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
    handshake->seedRandom(esp_random());
    DBGLN("BLEMSP identity: %s / %s / mac %s", ourInfo.productCode,
          ourInfo.name, macHex);

    server = NimBLEDevice::createServer();
    server->setCallbacks(&serverCallbacks);

    NimBLEService *spp = server->createService(NimBLEUUID(UUID_SVC_SPP));
    // Property set mirrors the ble_spp_server attribute table. WRITE_NR is
    // required: iOS silently discards the app's write-without-response if the
    // characteristic does not advertise it, which looks like a dead device.
    NimBLECharacteristic *chrDataRecv = spp->createCharacteristic(
        NimBLEUUID(UUID_CHR_DATA_RECV), READ | WRITE_NR);
    chrDataRecv->setCallbacks(&dataRecvCallbacks);

    chrDataNotify = spp->createCharacteristic(
        NimBLEUUID(UUID_CHR_DATA_NOTIFY), READ | NOTIFY);

    NimBLECharacteristic *chrCmdRecv = spp->createCharacteristic(
        NimBLEUUID(UUID_CHR_CMD_RECV), READ | WRITE_NR);
    chrCmdRecv->setCallbacks(&cmdRecvCallbacks);

    chrCmdNotify = spp->createCharacteristic(
        NimBLEUUID(UUID_CHR_CMD_NOTIFY), READ | NOTIFY);

    spp->start();

    // Two-packet layout, as the captured FC uses:
    //
    //   ADV (7 of 31 bytes): flags(3) + 16-bit service 0x00FF(4)
    //   SCAN RESPONSE:       complete local name
    //
    // The name goes in the scan response because on the real device it cannot
    // fit alongside manufacturer data in the 31-byte ADV packet. We no longer
    // send manufacturer data so ours would fit either way, but keeping the
    // captured layout costs nothing and is what the app is known to accept.
    // It is also why a stack that cannot send a scan response (e.g. Windows
    // GattServiceProvider, which reserves the local name) can never look like
    // one of these devices.
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
    DBGLN("BLEMSP advertising '%s' (svc 0x%04x), free heap %u",
          deviceName, UUID_ADVERTISED, (unsigned)ESP.getFreeHeap());
}

bool SpeedyBeeGatt::isRunning()
{
    return running;
}

bool SpeedyBeeGatt::isClientConnected()
{
    return clientConnected;
}

uint16_t SpeedyBeeGatt::negotiatedMtu()
{
    return currentMtu;
}

bool SpeedyBeeGatt::sessionReady()
{
    return handshake != nullptr && handshake->sessionReady();
}

SpeedyBeeGatt::HandshakeTrace SpeedyBeeGatt::trace()
{
    return traceState;
}

size_t SpeedyBeeGatt::maxSerialChunk()
{
    // A single unfragmented notify; anything longer gets '##' chunked.
    return currentMtu > ATT_OVERHEAD ? currentMtu - ATT_OVERHEAD : 20;
}

bool SpeedyBeeGatt::notifySerial(const uint8_t *data, size_t len)
{
    if (!running || !clientConnected || chrDataNotify == nullptr || len == 0)
    {
        return false;
    }

    const size_t unfragmented = maxSerialChunk();
    if (len <= unfragmented)
    {
        return chrDataNotify->notify(data, len);
    }

    const size_t chunkSize = currentMtu > (ATT_OVERHEAD + FRAG_HEADER_LEN)
                                 ? currentMtu - ATT_OVERHEAD - FRAG_HEADER_LEN
                                 : 16;
    const size_t total = (len + chunkSize - 1) / chunkSize;
    if (total > 255)
    {
        DBGLN("BLEMSP notify too long (%u bytes) -- dropped", (unsigned)len);
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
            DBGLN("BLEMSP notify failed at chunk %u/%u", (unsigned)(i + 1),
                  (unsigned)total);
            return false;
        }
    }
    return true;
}

#endif // TARGET_TX && PLATFORM_ESP32 && TX_BLE_MSP
