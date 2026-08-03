#pragma once

#include "device.h"

#if defined(RX_SPECTRUM_SCAN)

// For spectrumRbw_e: the trigger carries a bandwidth selection straight through
// to the sweep, so callers need the enum the same way they need the port one.
#include "SpectrumSweep.h"

#include <stdint.h>

extern device_t RxSpectrum_device;

// Antenna-port / band selector carried in the trigger command's payload[2].
// On a cross-band LR1121 RX, port == band == radio: each chip is wired to its
// own band-matched front end and antenna connector, so selecting the band
// already routes to the matching port.
enum
{
    RX_SPECTRUM_PORT_900  = 0, // sub-GHz front end (radio 1 / 900 MHz antenna port)
    RX_SPECTRUM_PORT_2G4  = 1, // 2.4 GHz front end (radio 2 / 2.4 GHz antenna port)
    RX_SPECTRUM_PORT_BOTH = 2, // sweep both ports, dwelling on each in turn
};

/**
 * Enter spectrum-scan mode: drops the RC link and starts a receive-only sweep,
 * streaming CRSF ELRS_VENDOR frames out the serial (FC/host) port.
 *
 * @param band one of RX_SPECTRUM_PORT_*. A second-band request on a single-band
 *             device falls back to the primary band.
 *
 * Re-invoking restarts the measurement. Refused while a live RC link is up --
 * this is a bench diagnostic. There is no matching Stop(): the mode exits by
 * resetting the receiver.
 */
void RxSpectrumStart(uint8_t band, uint8_t rbw, bool compare);

#else
inline void RxSpectrumStart(uint8_t, uint8_t, bool) {}
#endif
