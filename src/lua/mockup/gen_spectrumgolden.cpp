/*
 * Generates spectrumgolden.lua: golden ELRS_VENDOR frames from the real
 * firmware encoder, letting the simulator prove the encoder and elrs.lua's
 * decoder agree. Builds with bare g++ (recipe in README.md); never compiled
 * into any firmware or test target.
 */

#include <cstdint>
#include <cstdio>

#include "SpectrumProtocol.h"

// What devTxSpectrum.cpp puts in the extended header, and therefore what EdgeTX
// hands the script as data[1] and data[2].
#define ADDR_RADIO_TRANSMITTER 0xEA // dest
#define ADDR_CRSF_TRANSMITTER  0xEE // orig -- elrs.lua's default deviceId

// One band's golden pattern, on the two real grids a cross-band TX scans:
//   2.4GHz ISM (FHSS.cpp): 2400.4 .. 2479.4 MHz, 80 ch, 1000 kHz step (2 frames)
//   FCC915 (FHSS.cpp): 903.5 .. 926.9 MHz, 40 ch, 600 kHz step (1 frame)
typedef struct
{
    const char *suffix;    // "" -> frames/expect, "900" -> frames900/expect900
    uint32_t startKhz;
    uint32_t stepKhz;
    int bins;
    int notchFirstBin;     // 1-indexed, as the Lua cursor counts
    int notchWidth;
    uint16_t rbwKhz;       // sensing bandwidth, the band's Wide default
} band_t;

static const band_t BAND_2G4 = {"", 2400400u, 1000u, 80, 40, 3, 812};
static const band_t BAND_900 = {"900", 903500u, 600u, 40, 20, 3, 500};

static int8_t liveBins[SPECTRUM_MAX_BINS];
static int8_t maxHoldBins[SPECTRUM_MAX_BINS];

static void buildPattern(const band_t *b)
{
    for (int i = 0; i < b->bins; i++)
    {
        // Ramp spanning the plot window; a decode error breaks the diagonal.
        // Max-hold rides 5dB above so its dots clear the bars.
        liveBins[i] = (int8_t)(-110 + i);
        maxHoldBins[i] = (int8_t)(liveBins[i] + 5);
    }
    for (int i = 0; i < b->notchWidth; i++)
    {
        const int idx = (b->notchFirstBin - 1) + i; // 1-indexed bin -> 0-indexed
        liveBins[idx] = SPECTRUM_RSSI_INVALID;
        maxHoldBins[idx] = SPECTRUM_RSSI_INVALID;
    }
}

// Emit one frame as a Lua array shaped like crossfireTelemetryPop()'s second
// return: dest, orig, then the payload (EdgeTX strips sync, type and CRC).
static void emitFrameArray(const band_t *b, const char *name, uint8_t extraFlags);

static void emitFrame(const band_t *b, const uint8_t trace, const uint8_t binOffset,
                      const uint8_t sweepSeq, const uint8_t extraFlags)
{
    uint8_t payload[SPECTRUM_MAX_PAYLOAD_BYTES];
    spectrumFrameInfo_t info = {};
    info.flags = trace | extraFlags;
    info.sweepSeq = sweepSeq;
    info.binOffset = binOffset;
    info.totalBins = (uint8_t)b->bins;
    info.startFreqKhz = b->startKhz;
    info.stepKhz = (uint16_t)b->stepKhz;
    info.rbwKhz = b->rbwKhz;

    const int8_t *src = (trace == SPECTRUM_TRACE_MAXHOLD) ? maxHoldBins : liveBins;
    const uint8_t len = SpectrumEncodeFrame(payload, src, &info);
    if (len == 0)
    {
        fprintf(stderr, "encoder rejected frame (trace=%u offset=%u)\n", trace, binOffset);
        return;
    }

    printf("    { 0x%02X, 0x%02X", ADDR_RADIO_TRANSMITTER, ADDR_CRSF_TRANSMITTER);
    for (uint8_t i = 0; i < len; i++)
    {
        if (i % 12 == 0) printf(",\n     ");
        else printf(", ");
        printf("0x%02X", payload[i]);
    }
    printf(" },\n");
}

// The expect{} block a correct decoder must read back, then the frames{}.
static void emitBand(const band_t *b)
{
    buildPattern(b);

    printf("  expect%s = {\n", b->suffix);
    printf("    total     = %u,\n", b->bins);
    printf("    startKhz  = %u,\n", b->startKhz);
    printf("    stepKhz   = %u,\n", b->stepKhz);
    printf("    rbwKhz    = %u,\n", b->rbwKhz);
    printf("    -- Rendered labels exactly as drawSpectrum writes them: an endianness\n");
    printf("    -- flip reads startKhz as %u, which is not subtle on screen.\n",
           __builtin_bswap32(b->startKhz));
    printf("    bin1Label   = \"%.1fMHz\",\n", b->startKhz / 1000.0);
    printf("    binLastLabel = \"%.1fMHz\",\n",
           (b->startKhz + (b->bins - 1) * b->stepKhz) / 1000.0);
    printf("    -- The notch is the offset oracle: an index-base error slides it\n");
    printf("    notchFirstBin = %u,\n", b->notchFirstBin);
    printf("    notchWidth    = %u,\n", b->notchWidth);
    printf("    notchLabel    = \"%.1fMHz\",\n",
           (b->startKhz + (b->notchFirstBin - 1) * b->stepKhz) / 1000.0);
    printf("    bin1Dbm    = %d,\n", liveBins[0]);
    printf("    binLastDbm = %d,\n", liveBins[b->bins - 1]);
    printf("  },\n");
    printf("\n");

    emitFrameArray(b, b->suffix, 0);
}

// Frames only, no expect block: a compare pair rides the 2.4 axis and reads
// back the same numbers.
static void emitCompareTraces(const band_t *b)
{
    printf("\n");
    printf("  -- Antenna compare on the 2.4 axis: MODE_COMPARE on both, RADIO_2 on B.\n");
    emitFrameArray(b, "CmpA", SPECTRUM_FLAG_MODE_COMPARE);
    emitFrameArray(b, "CmpB", SPECTRUM_FLAG_MODE_COMPARE | SPECTRUM_FLAG_RADIO_2);
}

// Owns both braces of the table it fills, so they cannot drift apart.
static void emitFrameArray(const band_t *b, const char *name, const uint8_t extraFlags)
{
    const uint8_t seq = 7; // arbitrary; proves sweepSeq survives the round trip
    printf("  frames%s = {\n", name);
    for (uint8_t off = 0; off < b->bins; off += SPECTRUM_MAX_BINS_PER_FRAME)
    {
        emitFrame(b, SPECTRUM_TRACE_LIVE, off, seq, extraFlags);
    }
    for (uint8_t off = 0; off < b->bins; off += SPECTRUM_MAX_BINS_PER_FRAME)
    {
        emitFrame(b, SPECTRUM_TRACE_MAXHOLD, off, seq, extraFlags);
    }
    printf("  },\n");
}

int main()
{
    printf("-- GENERATED FILE -- do not edit by hand; regenerate per README.md here.\n");
    printf("-- Golden CRSF_FRAMETYPE_ELRS_VENDOR sub-type 0x01 frames from the real\n");
    printf("-- firmware encoder (SpectrumEncodeFrame), so rendering them proves the\n");
    printf("-- encoder and elrs.lua's decoder agree. Each frame is shaped exactly as\n");
    printf("-- crossfireTelemetryPop() returns it: [1]=ext dest, [2]=ext orig, [3..]=payload.\n");
    printf("\n");
    printf("return {\n");
    printf("  proto = %u,\n", SPECTRUM_PROTO_VERSION);
    printf("\n");
    printf("  -- What a correct decoder must read back; also the numbers to eyeball.\n");

    emitBand(&BAND_2G4);
    printf("\n");
    emitBand(&BAND_900);
    emitCompareTraces(&BAND_2G4);

    printf("}\n");
    return 0;
}
