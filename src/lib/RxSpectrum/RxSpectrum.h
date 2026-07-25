#pragma once

#include "device.h"

#if defined(RX_SPECTRUM_SCAN)

#include <stdint.h>

extern device_t RxSpectrum_device;

// Antenna-port / band selector carried in the trigger command's payload[2].
// On a cross-band LR1121 RX (e.g. GEPRC SuperX Nano) port == band == radio: each
// chip is hard-wired to its own band-matched front end and antenna connector, so
// selecting the band already routes to the frequency-matched port (there is no
// independent antenna GPIO on these boards). The plotted frequency axis reports
// the true band, so the host always sees which port is which.
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
 * @param band one of RX_SPECTRUM_PORT_*. Clamped to a valid band; a second-band
 *             request on a single-band device falls back to the primary band.
 *
 * Re-invoking while a scan is already running restarts the measurement (clears
 * max-hold and re-selects the band). Refused while a live RC link is up
 * (connected) -- this is a bench / passthrough diagnostic.
 *
 * There is no matching Stop(): the mode exits by resetting the receiver, so
 * there is no restore path that could leave the radio misconfigured but looking
 * connected. See ../TxSpectrum/DESIGN.md R1.5.
 */
void RxSpectrumStart(uint8_t band);

#endif
