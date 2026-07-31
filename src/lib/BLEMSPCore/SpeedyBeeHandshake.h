#pragma once

#include <stddef.h>
#include <stdint.h>

// Device identity served in the 0xf6 device-info response. Field positions came
// from capturing a real board (python/ble-msp-emulator/capture_fc.py); the app
// validates the blob, so the layout is fixed even though the values are ours.
// buildDeviceInfoResponse clamps each field to its width, so an over-long value
// loses its tail rather than disturbing its neighbour. Widths are the usable
// string length, one less than the gap to the next field.
struct SpeedyBeeDeviceInfo
{
    // offset 0, 9 chars. The real board reports "SBF4V4085", but the app also
    // drives third-party boards, so a known code is not expected.
    const char *productCode;
    const char *mac;        // offset 10, 12 chars: uppercase hex, no colons
    const char *name;       // offset 24, 31 chars, model name the app displays
    const char *wifiName;   // offset 56, 9 chars
    const char *serial;     // offset 66, 10 chars
};

// Device/peripheral side of the SpeedyBee app's proprietary BLE control
// channel (GATT service 0xABF0, writes on ABF3, notify responses on ABF4).
// The app runs this handshake before it uses the serial channel ABF1/ABF2.
//
// Wire formats (from HCI snoop analysis, dunaevai135/speedybee_ble_bridge;
// bench-validated against the emulator in python/ble-msp-emulator):
//   app -> device:  [cmd] [0x00] [protobuf-lite payload]
//   device -> app:  [cmd] [len_hi] [len_lo] [protobuf-lite payload]
// protobuf-lite: field1 varint (tag 0x08), field2 varint (0x10) or
// string (0x12), field3 length-delimited (0x1a).
//
// Dependency-free (no Arduino/NimBLE): host-testable under the native env,
// transport-agnostic for reuse on any BLE stack.
class SpeedyBeeHandshake
{
    // The reference client hard-skips a 3-byte protobuf header after the
    // packet header, which only lines up when field3's length needs a
    // two-byte varint -- so the info blob is padded to at least 128 bytes.
    static constexpr size_t DEVICE_INFO_BLOB_MIN = 128;

public:
    // Writer for ABF4 notification payloads produced by handleControlWrite.
    typedef void (*responseWriter_t)(void *ctx, const uint8_t *data, size_t len);

    // password == nullptr emulates a board with no BLE password (default app
    // flow); non-null demands the cmd-0x08 password exchange first.
    explicit SpeedyBeeHandshake(const SpeedyBeeDeviceInfo &info,
                                const char *password = nullptr)
        : info(info), password(password), ready(false),
          authed(password == nullptr), rngState(0x5b1e5eedU) {}

    // Feed one ABF3 write; responses (zero or more) are emitted through cb.
    // Returns false when the command is unrecognized.
    bool handleControlWrite(const uint8_t *data, size_t len,
                            responseWriter_t cb, void *ctx);

    // True once the app requested the session key: serial channel is live.
    bool sessionReady() const { return ready; }

    // Call on BLE disconnect so the next client starts fresh.
    void reset()
    {
        ready = false;
        authed = password == nullptr;
    }

    void seedRandom(uint32_t seed) { rngState = seed ? seed : 1; }

    // Largest response is device info: 4-byte header, 2 for field1, 3 for the
    // field3 tag+length, and the blob itself (239 bytes as the real FC sends
    // it). Rounded up to leave room for a longer blob.
    static constexpr size_t RESPONSE_MAX = 288;

    // field3 length in the cmd 0x26 session-key reply, as a real FC sends it.
    static constexpr size_t SESSION_KEY_LEN = 33;

private:
    static constexpr uint8_t CMD_INIT_OR_KEY = 0x02;
    static constexpr uint8_t CMD_PASSWORD = 0x08;
    static constexpr uint8_t CMD_DEVICE_INFO = 0x0E;
    static constexpr uint8_t INIT_ARG = 3;
    static constexpr uint8_t SESSION_KEY_ARG = 0x2D;

    size_t buildInitResponse(uint8_t *out);
    size_t buildPasswordResponse(uint8_t *out, const uint8_t *submitted, size_t submittedLen);
    size_t buildDeviceInfoResponse(uint8_t *out);
    size_t buildSessionKeyResponse(uint8_t *out);

    static size_t encodeVarint(uint8_t *out, uint32_t value);
    static size_t encodeVarintField(uint8_t *out, uint8_t fieldNum, uint32_t value);
    static size_t beginBytesField(uint8_t *out, uint8_t fieldNum, size_t len);
    static size_t finishPacket(uint8_t *buf, uint8_t cmd, size_t payloadLen);
    // Decodes field1 (varint) and locates field2 (varint or bytes); any
    // missing field is left untouched. Returns false on malformed input.
    static bool decodeFields(const uint8_t *payload, size_t len,
                             uint32_t *field1, const uint8_t **field2,
                             size_t *field2Len, uint32_t *field2Varint);

    uint8_t nextRandomByte();

    const SpeedyBeeDeviceInfo info;
    const char *password;
    bool ready;
    bool authed;
    uint32_t rngState;
};
