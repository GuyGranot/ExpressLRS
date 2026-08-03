#pragma once

#include "device.h"

#if defined(TX_SPECTRUM_SCAN)

#include "SpectrumProtocol.h"

#include <stdint.h>

/*
 * Receive-only RSSI sweep over the FHSS channel grid, kept apart from the
 * transport that carries the result: lib/TxSpectrum is the handset side and
 * lib/RxSpectrum the receiver side, both on this engine unchanged.
 *
 * Never enables a transmit front end: Config(), SetFrequencyReg(..., doRx) and
 * the RSSI reads are the only radio calls, so RFAMP.TXenable() is unreachable
 * from here. Callers must stop hwTimer first. Not re-entrant or ISR-safe --
 * every entry point runs on the loop core.
 */

/**
 * Sensing bandwidth. Wide is the bandwidth the band's own air rates use, so it
 * is both the default and the like-for-like setting; each step down halves it,
 * lowering the noise floor and resolving closer signals at the cost of leaving
 * gaps between bins and a slower sweep.
 */
typedef enum
{
    rbwWide = 0,
    rbwMedium,
    rbwNarrow,
    rbwCount
} spectrumRbw_e;

/**
 * Select the sensing bandwidth. Volatile: RAM only, back to Wide on reboot,
 * which is also how both scan modes exit. Applies from the next
 * SpectrumSweepBegin(), which a band flip and a re-trigger both call.
 */
void SpectrumSweepSetRbw(uint8_t rbw);

/**
 * Sweep both radios over the same band instead of one, so each trace comes from
 * one antenna and the pair can be compared. Ignored on single-radio hardware.
 *
 * This replaces the old split, where the two radios took half the band each: it
 * doubled sweep rate but stitched two antennas into one trace, and an imbalance
 * between them read as a step at midband that was not in the air.
 *
 * Applies from the next SpectrumSweepBegin(). Volatile, like the resolution.
 */
void SpectrumSweepSetCompare(bool on);

/**
 * Which source SpectrumSweepEncodeFrame() emits: 0 or 1, or -1 for every source
 * in turn. The handset streams one at a time because EdgeTX's Lua queue is the
 * slowest consumer in the chain; a host on a UART takes them all.
 */
void SpectrumSweepStreamSource(int8_t source);

/** True when a second (cross-band) FHSS config exists to scan. */
bool SpectrumSweepHasSecondBand();

/** True when the primary FHSS band is sub-GHz rather than 2.4GHz. */
bool SpectrumSweepPrimaryIsSubGHz();

/**
 * Configure the radio for one band, clear both traces and restart the sweep.
 *
 * @param useSecondBand scan the dual-band config on the band-matched radio
 *        instead of the primary one. Ignored unless SpectrumSweepHasSecondBand().
 *
 * Safe to call again mid-scan: it is how a band flip and a re-trigger are both
 * implemented. Callers must already be in a mode that halts the link.
 */
void SpectrumSweepBegin(bool useSecondBand);

/**
 * Sample the next group of dwell positions. Bounds the BLOCKING TIME rather than
 * the bin count, because the per-bin settle spans 80us (FLRC) to 1000us (LR1121);
 * each call is flat at ~1.3ms whatever the radio.
 */
void SpectrumSweepChunk();

/** True when the cursor sits at the start of a sweep, i.e. one just completed. */
bool SpectrumSweepAtSweepStart();

/**
 * Detach the OTA RXdone interrupt so the sweep owns the radio outright.
 *
 * The ISR fires asynchronously -- with LoRa CRC off, ambient energy raises
 * RX_DONE and the ISR issues its own SPI on the radio the sweep is mid-read on.
 * Both modes exit by reboot, which restores it.
 */
void SpectrumSweepIsolateRadio();

/** Clear the max-hold trace and restart accumulation. */
void SpectrumSweepResetMaxHold();

/**
 * Encode the next frame body and advance the emit cursor, alternating the live
 * and max-hold traces and chunking each across as many frames as it needs.
 *
 * @param payload at least SPECTRUM_MAX_PAYLOAD_BYTES; written past whatever
 *        CRSF header the caller is building.
 * @return payload length, or 0 if nothing could be encoded.
 */
uint8_t SpectrumSweepEncodeFrame(uint8_t *payload);

/**
 * Pace frames rather than bursting a whole sweep. One frame per interval is what
 * the slowest consumer (EdgeTX's Lua telemetry queue) can absorb, and it lives
 * here so every transport produces the same frame rate.
 */
#define SPECTRUM_SWEEP_EMIT_INTERVAL_MS 50

#endif
