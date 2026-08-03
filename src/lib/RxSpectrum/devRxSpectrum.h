#pragma once

#include "device.h"

#if defined(RX_SPECTRUM_SCAN)

#include "SpectrumSweep.h" // spectrumRbw_e

#include <stdint.h>

extern device_t RxSpectrum_device;

// Band / antenna-port selector carried in the trigger command's payload[2];
// on a cross-band RX, port == band == radio
enum
{
    RX_SPECTRUM_PORT_900  = 0,
    RX_SPECTRUM_PORT_2G4  = 1,
    RX_SPECTRUM_PORT_BOTH = 2, // dwell on each port in turn
};

// Enter spectrum-scan mode: drops the RC link and streams receive-only sweep
// frames out the serial (FC/host) port. Re-invoking restarts the measurement;
// refused while linked. No Stop() -- the mode exits by resetting the receiver.
void RxSpectrumStart(uint8_t band, uint8_t rbw, bool compare);

#else
inline void RxSpectrumStart(uint8_t, uint8_t, bool) {}
#endif
