#pragma once

#include "device.h"

#if defined(TX_SPECTRUM_SCAN) && defined(PLATFORM_ESP32)

extern device_t TxSpectrum_device;

// Enter spectrum scan mode: drops the RC link and starts sweeping. The caller
// must refuse to start while armed; arming mid-scan aborts. No Stop() -- the
// mode exits by reboot, so no restore path can leave the radio misconfigured
void TxSpectrumStart();

void TxSpectrumResetMaxHold();

// Sensing bandwidth, a spectrumRbw_e; applies from the next scan
void TxSpectrumSetRbw(uint8_t rbw);

// Next (band, radio) source: a radio flip keeps both max-holds (both radios
// are swept), a band flip starts cold
void TxSpectrumNextSource();

#else
inline void TxSpectrumStart() {}
inline void TxSpectrumResetMaxHold() {}
inline void TxSpectrumSetRbw(uint8_t) {}
inline void TxSpectrumNextSource() {}
#endif
