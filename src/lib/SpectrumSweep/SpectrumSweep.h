#pragma once

#include "device.h"

#if defined(TX_SPECTRUM_SCAN) || defined(RX_SPECTRUM_SCAN)

#include "SpectrumProtocol.h"

#include <stdint.h>

/*
 * Receive-only RSSI sweep over the FHSS channel grid, shared by the TX
 * (lib/TxSpectrum) and RX (lib/RxSpectrum) transports. Never enables a
 * transmit front end. Not re-entrant or ISR-safe; callers stop hwTimer first.
 */

// Wide is the bandwidth the band's own air rates use; each step down halves it
typedef enum
{
    rbwWide = 0,
    rbwMedium,
    rbwNarrow,
    rbwCount
} spectrumRbw_e;

// RAM only, Wide again on reboot; applies from the next SpectrumSweepBegin()
void SpectrumSweepSetRbw(uint8_t rbw);

// Sweep both radios over the same band, one trace per antenna; ignored on
// single-radio hardware, applies from the next Begin()
void SpectrumSweepSetCompare(bool on);

// Which source SpectrumSweepEncodeFrame() emits: 0, 1, or -1 for all in turn
void SpectrumSweepStreamSource(int8_t source);

bool SpectrumSweepHasSecondBand();

bool SpectrumSweepPrimaryIsSubGHz();

// Configure the radio for one band, clear both traces and restart the sweep.
// Safe mid-scan (band flips call it); the link must already be halted
void SpectrumSweepBegin(bool useSecondBand);

// Sample the next group of dwell positions; bounds blocking time, ~1.3ms per call
void SpectrumSweepChunk();

bool SpectrumSweepAtSweepStart();

// Detach the OTA RXdone interrupt so the sweep owns the radio; reboot restores it
void SpectrumSweepIsolateRadio();

void SpectrumSweepResetMaxHold();

// Encode the next frame body and advance the emit cursor, alternating live and
// max-hold traces. Returns the payload length, or 0 if nothing could be encoded
uint8_t SpectrumSweepEncodeFrame(uint8_t *payload);

// One frame per interval is what the slowest consumer (EdgeTX's Lua telemetry
// queue) can absorb; it lives here so every transport paces the same
#define SPECTRUM_SWEEP_EMIT_INTERVAL_MS 50

#endif
