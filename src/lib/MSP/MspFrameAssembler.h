#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @class MspFrameAssembler
 *
 * @brief Streaming assembler for one MSP frame (v1, v2 or v1-jumbo) arriving in
 * arbitrary write-sized pieces, as BLE characteristic writes and TCP segments
 * deliver them.
 *
 * A frame is reported only once it is complete AND valid: declared length
 * present, checksum correct (XOR for v1, CRC8/DVB-S2 for v2) and direction '<'
 * (an update session client only ever sends requests). Anything else, whether
 * garbage, a partial frame or an oversized declared length, can never surface as
 * a frame, which is what lets "first valid MSP frame" be a safe
 * transport-ownership trigger.
 *
 * The declared length is bounded before a byte of payload is stored and nothing
 * is allocated from it; after a rejection the stream resynchronizes on the next
 * '$'. Free of Arduino and NimBLE so it builds under the native test
 * environment, and shared by the BLE and TCP update transports.
 */
class MspFrameAssembler
{
public:
    // Hard ceiling on an accepted frame, header and checksum included.
    // Matches MSP_FRAME_MAX_LEN of the CRSF2MSP bridge the frame feeds next.
    static constexpr size_t MAX_FRAME = 512;

    typedef enum : uint8_t
    {
        MSP_ASM_NONE,     // byte consumed; garbage skipped or frame still building
        MSP_ASM_COMPLETE, // a full valid frame is available in frame()/frameLen()
        MSP_ASM_REJECTED, // frame discarded: bad direction, oversize or checksum
    } result_e;

    /**
     * @brief Feeds one byte. On MSP_ASM_COMPLETE the frame is valid until the
     * next push(), so multiple frames in one write are handled by the caller's
     * byte loop consuming each completion before feeding the next byte.
     */
    result_e push(uint8_t b);

    /**
     * @brief Full resync; call on client disconnect so a partial frame cannot
     * prepend itself to the next session's bytes.
     */
    void reset() { state = SYNC; len = 0; }

    const uint8_t *frame() const { return buf; }
    size_t frameLen() const { return len; }

private:
    typedef enum : uint8_t
    {
        SYNC,   // waiting for '$'
        VER,    // waiting for 'M' or 'X'
        DIR,    // waiting for '<'
        HEADER, // collecting bytes until the declared length is known
        BODY,   // collecting payload + checksum
    } state_e;

    result_e reject()
    {
        reset();
        return MSP_ASM_REJECTED;
    }
    void store(uint8_t b);
    bool headerComplete(); // sets total; false when the declared length cannot fit
    bool checksumValid() const;

    uint8_t buf[MAX_FRAME];
    size_t len = 0;
    size_t total = 0; // expected frame length once the header has been read
    state_e state = SYNC;
};
