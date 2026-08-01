#include "MspFrameAssembler.h"

// v1 layout after "$M<": [len][cmd][payload...][xor of len..payload]
// jumbo escapes len with 0xFF:  [FF][cmd][len_lo][len_hi][payload...][xor]
// v2 layout after "$X<": [flag][cmd_lo][cmd_hi][len_lo][len_hi][payload...][crc]
static constexpr size_t V1_HEADER_BYTES = 2;    // len, cmd
static constexpr size_t JUMBO_HEADER_BYTES = 4; // FF, cmd, len16
static constexpr size_t V2_HEADER_BYTES = 5;    // flag, cmd16, len16

static uint8_t crc8_dvb_s2(uint8_t crc, uint8_t b)
{
    crc ^= b;
    for (int i = 0; i < 8; i++)
    {
        crc = (crc & 0x80) ? (crc << 1) ^ 0xD5 : crc << 1;
    }
    return crc;
}

void MspFrameAssembler::store(uint8_t b)
{
    // unreachable while headerComplete() bounds total, kept as a write backstop
    if (len < sizeof(buf))
    {
        buf[len++] = b;
    }
}

bool MspFrameAssembler::headerComplete()
{
    const bool isV1 = buf[1] == 'M';
    if (isV1 && buf[3] != 0xFF)
    {
        if (len < 3 + V1_HEADER_BYTES)
        {
            return true; // still collecting; total stays 0
        }
        total = 3 + V1_HEADER_BYTES + buf[3] + 1;
    }
    else if (isV1)
    {
        if (len < 3 + JUMBO_HEADER_BYTES)
        {
            return true;
        }
        total = 3 + JUMBO_HEADER_BYTES + ((buf[6] << 8) | buf[5]) + 1;
    }
    else
    {
        if (len < 3 + V2_HEADER_BYTES)
        {
            return true;
        }
        total = 3 + V2_HEADER_BYTES + ((buf[7] << 8) | buf[6]) + 1;
    }
    return total <= sizeof(buf);
}

bool MspFrameAssembler::checksumValid() const
{
    // both checksums cover everything between the 3-byte header and the
    // trailing checksum byte
    uint8_t sum = 0;
    if (buf[1] == 'M')
    {
        for (size_t i = 3; i < total - 1; i++)
        {
            sum ^= buf[i];
        }
    }
    else
    {
        for (size_t i = 3; i < total - 1; i++)
        {
            sum = crc8_dvb_s2(sum, buf[i]);
        }
    }
    return sum == buf[total - 1];
}

MspFrameAssembler::result_e MspFrameAssembler::push(uint8_t b)
{
    switch (state)
    {
    case SYNC:
        if (b == '$')
        {
            len = 0;
            total = 0;
            store(b);
            state = VER;
        }
        return MSP_ASM_NONE;

    case VER:
        if (b == 'M' || b == 'X')
        {
            store(b);
            state = DIR;
        }
        else
        {
            state = SYNC;
            if (b == '$') // "$$M<..." restarts on the second sync byte
            {
                return push(b);
            }
        }
        return MSP_ASM_NONE;

    case DIR:
        if (b != '<')
        {
            return reject(); // responses and '!' never come from a client
        }
        store(b);
        state = HEADER;
        return MSP_ASM_NONE;

    case HEADER:
        store(b);
        if (!headerComplete())
        {
            return reject(); // declared length cannot fit MAX_FRAME
        }
        if (total != 0)
        {
            state = BODY; // the smallest possible total still ends after the header
        }
        return MSP_ASM_NONE;

    case BODY:
        store(b);
        if (len < total)
        {
            return MSP_ASM_NONE;
        }
        state = SYNC;
        return checksumValid() ? MSP_ASM_COMPLETE : reject();
    }
    return MSP_ASM_NONE;
}
