#include "SpeedyBeeHandshake.h"

#include <string.h>

// Each builder returns the complete [cmd][len16-be][payload] packet length.
// Observed golden bytes are noted where the exchange came from HCI snoops.

bool SpeedyBeeHandshake::handleControlWrite(const uint8_t *data, size_t len,
                                            responseWriter_t cb, void *ctx)
{
    if (len < 2)
    {
        return false;
    }
    const uint8_t cmd = data[0];
    uint32_t field1 = 0;
    const uint8_t *field2 = nullptr;
    size_t field2Len = 0;
    uint32_t field2Varint = 0;
    if (!decodeFields(data + 2, len - 2, &field1, &field2, &field2Len, &field2Varint))
    {
        return false;
    }

    uint8_t out[RESPONSE_MAX];
    size_t outLen = 0;

    switch (cmd)
    {
    case CMD_INIT_OR_KEY:
        outLen = field1 == SESSION_KEY_ARG ? buildSessionKeyResponse(out)
                                           : buildInitResponse(out);
        break;
    case CMD_PASSWORD:
        outLen = buildPasswordResponse(out, field2, field2Len);
        break;
    case CMD_DEVICE_INFO:
        outLen = buildDeviceInfoResponse(out);
        break;
    default:
        return false;
    }

    if (outLen != 0 && cb != nullptr)
    {
        cb(ctx, out, outLen);
    }
    return true;
}

// no password: 03 00 02 08 03 / password required: 07 00 06 08 03 10 02 1a 00
size_t SpeedyBeeHandshake::buildInitResponse(uint8_t *out)
{
    size_t p = 3;
    p += encodeVarintField(out + p, 1, INIT_ARG);
    if (password != nullptr && !authed)
    {
        p += encodeVarintField(out + p, 2, 2);
        p += beginBytesField(out + p, 3, 0);
        return finishPacket(out, 0x07, p - 3);
    }
    return finishPacket(out, 0x03, p - 3);
}

// accepted: 05 00 04 08 04 1a 00
size_t SpeedyBeeHandshake::buildPasswordResponse(uint8_t *out,
                                                 const uint8_t *submitted,
                                                 size_t submittedLen)
{
    const bool ok = password == nullptr ||
                    (submitted != nullptr && submittedLen == strlen(password) &&
                     memcmp(submitted, password, submittedLen) == 0);
    authed = ok;
    size_t p = 3;
    p += encodeVarintField(out + p, 1, 4);
    if (!ok)
    {
        p += encodeVarintField(out + p, 2, 2);
    }
    p += beginBytesField(out + p, 3, 0);
    return finishPacket(out, ok ? 0x05 : 0x07, p - 3);
}

// Verbatim field3 blob from a real SpeedyBee F405 V4, captured with
// python/ble-msp-emulator/capture_fc.py and generated from that hexdump rather
// than transcribed. Structure, for when we parameterise it:
//   @0   "SBF4V4085"          product code -- almost certainly the app's key
//   @10  "24587C9C40C5"       the FC's MAC as hex
//   @24  "SpeedyBee F405 V4"  model name
//   @56  "SBWiFi"             wifi name
//   @66  <serial the app sent>
//   then ~170 bytes we have not decoded
static const uint8_t DEVICE_INFO_BLOB[] = {
#include "DeviceInfoBlob.inc"
};

// Copy s into the blob at off, NUL-terminate, and zero the rest of the field
// so nothing of the captured board's value shows through a shorter string.
static void setBlobField(uint8_t *blob, size_t off, size_t fieldLen,
                         const char *s)
{
    size_t n = s != nullptr ? strlen(s) : 0;
    if (n > fieldLen - 1)
    {
        n = fieldLen - 1;
    }
    memcpy(blob + off, s, n);
    memset(blob + off + n, 0, fieldLen - n);
}

// cmd 0xf6. Correct framing was not enough -- the app read a well-formed
// 137-byte reply and still terminated -- so it validates the blob's content.
// The captured layout is kept because it is what the app parses, but the
// identity in it is ours: impersonating a specific board's product code and
// MAC is not something to ship.
size_t SpeedyBeeHandshake::buildDeviceInfoResponse(uint8_t *out)
{
    // Offset and width declared once per field, so the two cannot drift and
    // overwrite a neighbour. Widths are the gaps between captured offsets.
    const struct { size_t off, width; const char *value; } fields[] = {
        {0, 10, info.productCode},
        {10, 13, info.mac},
        {24, 32, info.name},
        {56, 10, info.wifiName},
        {66, 11, info.serial},
    };

    const size_t blobLen = sizeof(DEVICE_INFO_BLOB);
    size_t p = 3;
    p += encodeVarintField(out + p, 1, 13);
    p += beginBytesField(out + p, 3, blobLen);
    // Straight into the output; no staging copy of the 239-byte blob.
    uint8_t *blob = out + p;
    memcpy(blob, DEVICE_INFO_BLOB, blobLen);
    for (const auto &f : fields)
    {
        setBlobField(blob, f.off, f.width, f.value);
    }
    p += blobLen;
    return finishPacket(out, 0xF6, p - 3);
}

// cmd 0x26 with a 33-byte field3. Captured from a real F405 V4:
//   26 00 25 08 2d 1a 21 fc ff*16 c9 58 05 a9 06 3f 45 13 83 3d 88 6d df 58 6b 21
// i.e. a 0xFC lead byte, sixteen 0xFF (an unset field, by the look of it),
// then sixteen varying bytes. We had been sending 16 bytes with no lead-in;
// the app treats the key opaquely, but matching the real length costs nothing
// and a wrong length is exactly the class of bug that broke device info.
size_t SpeedyBeeHandshake::buildSessionKeyResponse(uint8_t *out)
{
    ready = true;
    size_t p = 3;
    p += encodeVarintField(out + p, 1, SESSION_KEY_ARG);
    p += beginBytesField(out + p, 3, SESSION_KEY_LEN);
    out[p++] = 0xFC;
    for (int i = 0; i < 16; i++)
    {
        out[p++] = 0xFF;
    }
    for (int i = 0; i < 16; i++)
    {
        out[p++] = nextRandomByte();
    }
    return finishPacket(out, 0x26, p - 3);
}

size_t SpeedyBeeHandshake::encodeVarint(uint8_t *out, uint32_t value)
{
    size_t n = 0;
    while (value > 0x7F)
    {
        out[n++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }
    out[n++] = value & 0x7F;
    return n;
}

size_t SpeedyBeeHandshake::encodeVarintField(uint8_t *out, uint8_t fieldNum,
                                             uint32_t value)
{
    out[0] = (fieldNum << 3) | 0;
    return 1 + encodeVarint(out + 1, value);
}

size_t SpeedyBeeHandshake::beginBytesField(uint8_t *out, uint8_t fieldNum,
                                           size_t len)
{
    out[0] = (fieldNum << 3) | 2;
    return 1 + encodeVarint(out + 1, (uint32_t)len);
}

// Response framing is [cmd][0x00][varint payloadLen], captured from a real
// SpeedyBee F405 V4:
//   init        03 00 02     08 03                 payload 2
//   session key 26 00 25     08 2d 1a 21 ...       payload 37
//   device info f6 00 f4 01  08 0d 1a ef 01 ...    payload 244
// Below 128 a varint is a single byte, indistinguishable from the 2-byte
// big-endian length this used to write -- which is why init and session key
// worked by accident and only device info (133 bytes here) was rejected: the
// app read 0x85 as a varint with the continuation bit set and swallowed the
// following payload byte as its high bits.
size_t SpeedyBeeHandshake::finishPacket(uint8_t *buf, uint8_t cmd,
                                        size_t payloadLen)
{
    // Builders lay their payload down at buf+3, which is right only for a
    // 1-byte length; shift it along when the varint needs two.
    uint8_t lenBytes[2];
    const size_t n = encodeVarint(lenBytes, (uint32_t)payloadLen);
    if (n != 1)
    {
        memmove(buf + 2 + n, buf + 3, payloadLen);
    }
    buf[0] = cmd;
    buf[1] = 0x00;
    memcpy(buf + 2, lenBytes, n);
    return 2 + n + payloadLen;
}

bool SpeedyBeeHandshake::decodeFields(const uint8_t *payload, size_t len,
                                      uint32_t *field1, const uint8_t **field2,
                                      size_t *field2Len, uint32_t *field2Varint)
{
    size_t p = 0;
    while (p < len)
    {
        const uint8_t tag = payload[p++];
        const uint8_t fieldNum = tag >> 3;
        const uint8_t wire = tag & 0x07;
        uint32_t value = 0;
        if (wire == 0)
        {
            int shift = 0;
            while (p < len)
            {
                const uint8_t b = payload[p++];
                value |= (uint32_t)(b & 0x7F) << shift;
                if ((b & 0x80) == 0)
                {
                    break;
                }
                shift += 7;
            }
            if (fieldNum == 1)
            {
                *field1 = value;
            }
            else if (fieldNum == 2)
            {
                *field2Varint = value;
            }
        }
        else if (wire == 2)
        {
            int shift = 0;
            while (p < len)
            {
                const uint8_t b = payload[p++];
                value |= (uint32_t)(b & 0x7F) << shift;
                if ((b & 0x80) == 0)
                {
                    break;
                }
                shift += 7;
            }
            if (p + value > len)
            {
                return false;
            }
            if (fieldNum == 2)
            {
                *field2 = payload + p;
                *field2Len = value;
            }
            p += value;
        }
        else
        {
            return false; // wire types 1/5 never appear in this protocol
        }
    }
    return true;
}

uint8_t SpeedyBeeHandshake::nextRandomByte()
{
    // xorshift32 -- session keys need uniqueness, not cryptographic strength.
    // Callers on real hardware should seedRandom() with hardware entropy.
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return (uint8_t)(rngState & 0xFF);
}
