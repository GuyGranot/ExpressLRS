#pragma once

#include "device.h"

#if defined(TX_SPECTRUM_SCAN) && defined(PLATFORM_ESP32)

extern device_t TxSpectrum_device;

/**
 * Enter spectrum scan mode: drops the RC link and starts sweeping the band.
 * The caller must refuse to start while armed; arming after the scan is running
 * aborts it. There is no matching Stop() -- the mode exits by reboot, so no
 * restore path can leave the radio misconfigured but looking connected.
 */
void TxSpectrumStart();

/** Clear the max-hold trace and restart accumulation. */
void TxSpectrumResetMaxHold();

/**
 * Choose the sensing bandwidth, a spectrumRbw_e. Applies from the next scan:
 * a running sweep keeps the bandwidth its trace was measured at.
 */
void TxSpectrumSetRbw(uint8_t rbw);

/**
 * Move to the next source mid-scan, where a source is a (band, radio) pair: the
 * other band on cross-band hardware, the other radio when comparing antennas.
 * No-op when there is only one source or no scan is running.
 *
 * A radio flip keeps both max-holds, because both radios are being swept and
 * only the streamed one changes. A band flip starts cold: nothing was measured
 * on the band that was not being swept.
 */
void TxSpectrumNextSource();

#else
inline void TxSpectrumStart() {}
inline void TxSpectrumResetMaxHold() {}
inline void TxSpectrumSetRbw(uint8_t) {}
inline void TxSpectrumNextSource() {}
#endif
