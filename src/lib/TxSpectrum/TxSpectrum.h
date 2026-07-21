#pragma once

#include "device.h"

#if defined(TX_SPECTRUM_SCAN)

// Private commandStep_e value the handset's spectrum plot pushes at the Start Scan
// field id to flip bands (the page button). Deliberately outside the 0..6
// commandStep_e range so it can never be mistaken for a real lcs step, and only
// the spectrum command interprets it. Kept in sync with elrs.lua by comment.
#define TX_SPECTRUM_LCS_NEXT_BAND 8

extern device_t TxSpectrum_device;

/**
 * Enter spectrum scan mode: drops the RC link and starts sweeping the band.
 *
 * The caller must refuse to start while armed (DESIGN.md R1.4); if arming
 * happens after the scan is running the scan aborts itself.
 *
 * There is no matching Stop(): the mode exits by reboot, so there is no
 * restore path that can leave the radio misconfigured but looking connected.
 * See DESIGN.md R1.5.
 */
void TxSpectrumStart();

/** Clear the max-hold trace and restart accumulation. */
void TxSpectrumResetMaxHold();

/**
 * Flip to the other band mid-scan (cross-band LR1121, e.g. Nomad). Driven by the
 * handset's page button. No-op on single-band devices or when no scan is running.
 * The unviewed band's max-hold starts cold on return; see DESIGN.md 2.4.
 */
void TxSpectrumSwitchBand();

#endif
