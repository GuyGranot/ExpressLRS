#pragma once

/*
 * The RX-side spectrum analyzer streams the exact same wire format as the
 * TX-side one, so it reuses the dependency-free codec verbatim rather than
 * forking it. Frame type, sub-type byte, big-endian layout, 40-bins/frame
 * chunking and the int8 RSSI bins are all identical -- only the CRSF address
 * pair and the transport direction change (RX module -> flight controller /
 * host, over the Betaflight-passthrough UART, instead of TX module -> handset).
 *
 * See ../TxSpectrum/TxSpectrumProtocol.h for the full layout and codec and
 * ../TxSpectrum/DESIGN.md 3 ("Control and transport") for the rationale.
 */

#include "../TxSpectrum/TxSpectrumProtocol.h"
