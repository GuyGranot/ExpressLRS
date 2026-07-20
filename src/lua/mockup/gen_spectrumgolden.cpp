/*
 * Generates spectrumgolden.lua -- golden 0x30 frames for the simulator harness.
 *
 * WHY THIS IS C++ AND NOT A SCRIPT
 * The whole point is that these frames come out of the *real* firmware encoder.
 * Hand-writing them in Lua (or reimplementing the layout in Python) would encode
 * someone's *reading* of the wire format -- which is exactly the thing under
 * test. TxSpectrumEncodeFrame() below is the same function the TX calls.
 *
 * This closes the structural gap in the test ladder: TxSpectrumDecodeFrame() is
 * unit tested, but the handset never runs it -- parseSpectrumMessage() in
 * elrs.lua is a hand-rolled second decoder. Feeding it real encoder output is
 * what proves the two agree on endianness, offsets and the index base.
 *
 * TWO BANDS. The Nomad cross-band port streams one band at a time and flips with
 * the page button (DESIGN §10). So this emits two golden sets from the real grids:
 * the 2.4GHz ISM band (frames / expect) and the sub-GHz FCC915 band (frames900 /
 * expect900). The 900 set exercises the sub-GHz axis (start 903500 kHz, a value
 * whose byte order is its own endianness oracle) and the band-flip re-latch.
 *
 * REGENERATE (from this directory):
 *   g++ -I../../lib/TxSpectrum -o gen_spectrumgolden gen_spectrumgolden.cpp
 *   ./gen_spectrumgolden > spectrumgolden.lua
 *
 * TxSpectrumProtocol.h is <stdint.h>-only by design, so this needs no PlatformIO
 * and no ELRS build. Nothing compiles this file into any firmware or test target
 * -- src/lua/ is outside build_src_filter, lib_dir and test_dir.
 */

#include <cstdint>
#include <cstdio>

#include "TxSpectrumProtocol.h"

// What TxSpectrum.cpp puts in the extended header (TxSpectrum.cpp:311-316), and
// therefore what EdgeTX hands the script as data[1] and data[2]. Hardcoded
// rather than #included: crsf_protocol.h drags lib/CrsfProtocol in, which is the
// same trap DESIGN.md 9.3 documents for the native tests.
#define ADDR_RADIO_TRANSMITTER 0xEA // dest
#define ADDR_CRSF_TRANSMITTER  0xEE // orig -- elrs.lua's default deviceId

// One band's golden pattern. The two real grids the Nomad scans:
//   2.4GHz ISM  (FHSS.cpp:33): 2400.4 .. 2479.4 MHz, 80 channels, 1000 kHz step.
//   sub-GHz FCC915 (FHSS.cpp:16): 903.5 .. 926.9 MHz, 40 channels, 600 kHz step.
// FCC915 is 40 bins == exactly one frame, so its live/max-hold traces are one
// frame each; the 2.4 band is two. The notch is the offset oracle (see below).
typedef struct
{
    const char *suffix;    // "" -> frames/expect, "900" -> frames900/expect900
    uint32_t startKhz;
    uint32_t stepKhz;
    int bins;
    int notchFirstBin;     // 1-indexed, as the Lua cursor counts
    int notchWidth;
} band_t;

static const band_t BAND_2G4 = {"", 2400400u, 1000u, 80, 40, 3};
// 900 notch sits at bin 20 -- mid-band, well inside the single 40-bin frame, so a
// binOffset/index-base slip still slides it visibly on the sub-GHz axis too.
static const band_t BAND_900 = {"900", 903500u, 600u, 40, 20, 3};

static int8_t liveBins[TX_SPECTRUM_MAX_BINS];
static int8_t maxHoldBins[TX_SPECTRUM_MAX_BINS];

static void buildPattern(const band_t *b)
{
    for (int i = 0; i < b->bins; i++)
    {
        // Ramp -110 -> up across the band: spans elrs.lua's plot window
        // (SPEC_BOT -110 .. SPEC_TOP -30) on the 80-bin band, so a correct decode
        // draws a clean diagonal. Any reordering, sign error or offset breaks it.
        liveBins[i] = (int8_t)(-110 + i);
        // Max-hold sits 5dB above the live trace, so its dots ride just over the
        // bars. Overlapping them would not prove the trace split works.
        maxHoldBins[i] = (int8_t)(liveBins[i] + 5);
    }
    for (int i = 0; i < b->notchWidth; i++)
    {
        const int idx = (b->notchFirstBin - 1) + i; // 1-indexed bin -> 0-indexed
        liveBins[idx] = TX_SPECTRUM_RSSI_INVALID;
        maxHoldBins[idx] = TX_SPECTRUM_RSSI_INVALID;
    }
}

// Emit one frame as a Lua array, shaped exactly like crossfireTelemetryPop()'s
// second return: dest, orig, then the payload. Sync byte, type and CRC are
// stripped by EdgeTX before Lua ever sees them, so they are absent here too.
static void emitFrame(const band_t *b, const uint8_t trace, const uint8_t binOffset,
                      const uint8_t sweepSeq)
{
    uint8_t payload[TX_SPECTRUM_MAX_PAYLOAD_BYTES];
    txSpectrumFrameInfo_t info = {};
    info.flags = trace;
    info.sweepSeq = sweepSeq;
    info.binOffset = binOffset;
    info.totalBins = (uint8_t)b->bins;
    info.startFreqKhz = b->startKhz;
    info.stepKhz = (uint16_t)b->stepKhz;

    const int8_t *src = (trace == TX_SPECTRUM_TRACE_MAXHOLD) ? maxHoldBins : liveBins;
    const uint8_t len = TxSpectrumEncodeFrame(payload, src, &info);
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

// The expect{} block a correct decoder must read back, and the frames{} for one
// full emit cycle -- in EmitNextFrame()'s order: live[0:40], live[40:80],
// maxhold[0:40], maxhold[40:80] (or just [0:bins] when the band is <= 40 wide).
static void emitBand(const band_t *b)
{
    buildPattern(b);

    printf("  expect%s = {\n", b->suffix);
    printf("    total     = %u,\n", b->bins);
    printf("    startKhz  = %u,\n", b->startKhz);
    printf("    stepKhz   = %u,\n", b->stepKhz);
    printf("    -- Cursor readouts. THE decisive check: an endianness flip reads\n");
    printf("    -- startKhz as 0x%08X = %u, which is not subtle on screen.\n",
           __builtin_bswap32(b->startKhz), __builtin_bswap32(b->startKhz));
    printf("    bin1Mhz   = %u,\n", b->startKhz / 1000);
    printf("    binLastMhz = %u,\n", (b->startKhz + (b->bins - 1) * b->stepKhz) / 1000);
    printf("    -- A %u-bin notch starting at bin %u: the offset oracle. Reads as a gap\n",
           b->notchWidth, b->notchFirstBin);
    printf("    -- in the diagonal; a binOffset or index-base error slides it.\n");
    printf("    notchFirstBin = %u,\n", b->notchFirstBin);
    printf("    notchWidth    = %u,\n", b->notchWidth);
    printf("    notchMhz      = %u,\n",
           (b->startKhz + (b->notchFirstBin - 1) * b->stepKhz) / 1000);
    printf("    -- Live ramp endpoints, in dBm.\n");
    printf("    bin1Dbm    = %d,\n", liveBins[0]);
    printf("    binLastDbm = %d,\n", liveBins[b->bins - 1]);
    printf("  },\n");
    printf("\n");

    printf("  frames%s = {\n", b->suffix);
    const uint8_t seq = 7; // arbitrary; proves sweepSeq survives the round trip
    for (uint8_t off = 0; off < b->bins; off += TX_SPECTRUM_MAX_BINS_PER_FRAME)
    {
        emitFrame(b, TX_SPECTRUM_TRACE_LIVE, off, seq);
    }
    for (uint8_t off = 0; off < b->bins; off += TX_SPECTRUM_MAX_BINS_PER_FRAME)
    {
        emitFrame(b, TX_SPECTRUM_TRACE_MAXHOLD, off, seq);
    }
    printf("  },\n");
}

int main()
{
    printf("-- GENERATED FILE -- do not edit by hand.\n");
    printf("--\n");
    printf("-- Golden CRSF_FRAMETYPE_ELRS_TX_SPECTRUM (0x30) frames, produced by the real\n");
    printf("-- firmware encoder (TxSpectrumEncodeFrame in lib/TxSpectrum/TxSpectrumProtocol.h)\n");
    printf("-- via lua/mockup/gen_spectrumgolden.cpp. That provenance is the point: these\n");
    printf("-- are the bytes a TX actually emits, not someone's reading of the spec, so\n");
    printf("-- rendering them proves the firmware encoder and elrs.lua's decoder agree.\n");
    printf("--\n");
    printf("-- Two bands: the 2.4GHz set (frames/expect) and the sub-GHz FCC915 set\n");
    printf("-- (frames900/expect900). The mock streams one at a time and swaps on the\n");
    printf("-- page-button band flip, exactly as the cross-band TX does (DESIGN §10).\n");
    printf("--\n");
    printf("-- Each frame is shaped exactly as crossfireTelemetryPop() returns it:\n");
    printf("--   [1]=ext dest  [2]=ext orig (elrs.lua's deviceId)  [3..]=payload\n");
    printf("-- Sync byte, frame type and CRC are stripped by EdgeTX before Lua sees them.\n");
    printf("--\n");
    printf("-- Regenerate after ANY change to the wire format:\n");
    printf("--   cd src/lua/mockup\n");
    printf("--   g++ -I../../lib/TxSpectrum -o gen_spectrumgolden gen_spectrumgolden.cpp\n");
    printf("--   ./gen_spectrumgolden > spectrumgolden.lua\n");
    printf("\n");
    printf("return {\n");
    printf("  proto = %u,\n", TX_SPECTRUM_PROTO_VERSION);
    printf("\n");
    printf("  -- What a correct decoder must read back. spectrummock.lua asserts the\n");
    printf("  -- readouts against these; they are also the numbers to eyeball on screen.\n");

    emitBand(&BAND_2G4);
    printf("\n");
    emitBand(&BAND_900);

    printf("}\n");
    return 0;
}
