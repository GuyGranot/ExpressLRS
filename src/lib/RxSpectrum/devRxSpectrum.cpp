#include "devRxSpectrum.h"

#if defined(RX_SPECTRUM_SCAN)

#include "SpectrumSweep.h"

#include "common.h"
#include "device.h"
#include "logging.h"
#include "CRSFRouter.h"
#include "crsf_protocol.h"
#include "RXOTAConnector.h"
#include "FHSS.h"
#include "POWERMGNT.h"
#include "hwTimer.h"
#include "rxtx_intf.h"   // scheduleRebootTime
#if defined(PLATFORM_ESP32)
#include <WiFi.h>
#elif defined(PLATFORM_ESP8266)
#include <ESP8266WiFi.h>
#endif

/*
 * Trigger and transport for the RX side; the measurement is lib/SpectrumSweep.
 * In and out over the FC/host UART, which means Betaflight passthrough -- and a
 * flight controller in passthrough cannot arm, hence no arm guard here.
 */

extern RXOTAConnector otaConnector; // the RF-side connector, used only as the
                                    // "source" to exclude when routing our frames
                                    // to the serial (FC/host) connector.

// Dwell long enough on each port in "both" mode to emit several complete
// live+max-hold traces before switching, so the host never sees a torn trace.
#define RX_SPECTRUM_BAND_DWELL_MS 500

static uint8_t requestedBand; // the RX_SPECTRUM_PORT_* the host asked for
static bool wantSubGHz;       // band currently swept; flips per dwell in "both"
static uint32_t bandDwellStartMs;
static bool running;
static uint32_t lastEmitMs;

// The sweep takes "primary or dual config"; the host asks for a band. On a
// cross-band device the dual config is whichever one the primary is not.
static bool useSecondBandFor(const bool subGHz)
{
    return SpectrumSweepHasSecondBand() && (subGHz != SpectrumSweepPrimaryIsSubGHz());
}

static void BeginScan()
{
    SpectrumSweepBegin(useSecondBandFor(wantSubGHz));
    lastEmitMs = millis();
    bandDwellStartMs = millis();
}

// RX -> flight controller / host. deliverMessage(&otaConnector, ...) forwards to
// every OTHER connector, i.e. the serial connector, mirroring how the RX already
// emits link statistics (rx_main.cpp). buf must hold the header, payloadLen bytes
// of already-filled payload, and the CRC.
static void SendVendorFrame(uint8_t *const buf, const uint8_t payloadLen)
{
    crsfRouter.SetExtendedHeaderAndCrc((crsf_ext_header_t *)buf,
                                       CRSF_FRAMETYPE_ELRS_VENDOR,
                                       CRSF_EXT_FRAME_SIZE(payloadLen),
                                       CRSF_ADDRESS_FLIGHT_CONTROLLER,
                                       CRSF_ADDRESS_CRSF_RECEIVER);
    crsfRouter.deliverMessage(&otaConnector, (crsf_header_t *)buf);
}

static void EmitNextFrame()
{
    uint8_t buf[sizeof(crsf_ext_header_t) + SPECTRUM_MAX_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE];
    const uint8_t len = SpectrumSweepEncodeFrame(buf + sizeof(crsf_ext_header_t));
    if (len == 0)
    {
        return;
    }
    SendVendorFrame(buf, len);
}

// Answer a scan trigger. Sent on every trigger, accepted or not: silence on the
// host cannot be told apart from a trigger that never arrived, the wrong port, or
// passthrough that has dropped -- all of which look identical to a refusal.
static void SendScanStatus(const uint8_t status)
{
    uint8_t buf[sizeof(crsf_ext_header_t) + SPECTRUM_STATUS_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE];
    uint8_t *const payload = buf + sizeof(crsf_ext_header_t);
    payload[0] = SPECTRUM_SUBTYPE_STATUS;
    payload[1] = SPECTRUM_PROTO_VERSION;
    payload[2] = status;
    SendVendorFrame(buf, SPECTRUM_STATUS_PAYLOAD_BYTES);
}

void RxSpectrumStart(uint8_t band, const uint8_t rbw, const bool compare)
{
    // Bench / passthrough diagnostic only: refuse to tear down a live RC link.
    if (connectionState == connected)
    {
        DBGLN("RxSpectrum: refused, RC link is up");
        SendScanStatus(SPECTRUM_STATUS_REFUSED_LINKED);
        return;
    }

    // Out of range, or a second-band request on a single-band device: either way
    // there is one band to sweep.
    if (band > RX_SPECTRUM_PORT_BOTH || !SpectrumSweepHasSecondBand())
    {
        band = RX_SPECTRUM_PORT_900;
    }
    SpectrumSweepSetRbw(rbw); // out-of-range falls back to Wide
    SpectrumSweepSetCompare(compare);
    // A host has the bandwidth and the screen for both traces, unlike the
    // handset, so the receiver streams every source rather than one at a time.
    SpectrumSweepStreamSource(compare ? -1 : 0);
    requestedBand = band;
    // "900" and "both" open on the sub-GHz port; "2.4" opens on the 2.4 port.
    wantSubGHz = (band != RX_SPECTRUM_PORT_2G4);

    // Re-trigger while scanning = restart the measurement (also clears max-hold).
    // Runs on the loop core, the same single-threaded core as the sweep, so
    // there is no race.
    if (connectionState == spectrumScan)
    {
        SendScanStatus(SPECTRUM_STATUS_ACCEPTED);
        BeginScan();
        return;
    }

    DBGLN("Starting spectrum scan");
    // Answer BEFORE entering the mode: setConnectionState tears down WiFi and
    // re-inits the radio, and a reply queued behind that can be lost.
    SendScanStatus(SPECTRUM_STATUS_ACCEPTED);

    // setPower only queues; it lands on LR1121 because the sweep calls Config(),
    // and is inert on SX1280, which commits only on a TX that never happens.
    // Not the interlock either way -- that is hwTimer stopped and the ISR gone.
    POWERMGNT::setPower(MinPower);
    setConnectionState(spectrumScan); // event() does the one-shot hardware init
}

static int event()
{
    if (connectionState != spectrumScan)
    {
        running = false;
        return DURATION_NEVER;
    }

    if (!running)
    {
        // Never open the RX front end with a transmitter live, and the WiFi AP
        // is one. WIFI tears it down on this same event, but that is device
        // order; doing it here is explicit and idempotent.
#if defined(PLATFORM_ESP32) || defined(PLATFORM_ESP8266)
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
#if defined(PLATFORM_ESP8266)
        WiFi.forceSleepBegin();
#endif
#endif
        // Stop the telemetry-TX timer before the radio comes up.
        hwTimer::stop();

        // Once, at entry: the only thing that recovers a radio from the
        // Radio.End() a WiFi AP leaves behind. In-sweep restarts stay on the
        // lightweight path so "both" mode stays responsive.
        //
        // End() first because Begin() is not idempotent. SX1280Hal::init()
        // re-runs SPIEx.begin() and re-attaches the second hardware chip
        // select, which breaks the bus on an already-initialised dual-radio
        // ESP32 -- so entering a scan without WiFi having run first left both
        // radios unconfigured. End() makes entry the same clean cycle either
        // way.
        Radio.End();
        if (!Radio.Begin(FHSSgetMinimumFreq(), FHSSgetMaximumFreq()))
        {
            // Unconfigured radios answer every RSSI read with 0xFF, which
            // encodes as a legible full-scale-low trace. Say the measurement
            // failed rather than stream something a host will happily plot.
            SendScanStatus(SPECTRUM_STATUS_RADIO_FAILED);
            scheduleRebootTime(400);
            running = false;
            return DURATION_NEVER;
        }

        // Re-assert isolation AFTER Begin(): it reprograms the chip DIO/IRQ
        // mapping and re-inits the HAL, which re-attaches the MCU RXdone ISR.
        SpectrumSweepIsolateRadio();
        BeginScan();
        running = true;
    }

    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    if (connectionState != spectrumScan)
    {
        return DURATION_NEVER;
    }

    SpectrumSweepChunk();

    const uint32_t now = millis();

    // "Both" mode: after dwelling long enough on the current port, flip at a
    // sweep boundary. Restarting reconfigures the radio for the new band (PA
    // path / RF switch / antenna port) and resets the axis.
    if (requestedBand == RX_SPECTRUM_PORT_BOTH && SpectrumSweepAtSweepStart() &&
        (uint32_t)(now - bandDwellStartMs) >= RX_SPECTRUM_BAND_DWELL_MS)
    {
        wantSubGHz = !wantSubGHz;
        BeginScan();
        return DURATION_IMMEDIATELY;
    }

    if ((uint32_t)(now - lastEmitMs) >= SPECTRUM_SWEEP_EMIT_INTERVAL_MS)
    {
        EmitNextFrame();
        lastEmitMs = now;
    }

    return DURATION_IMMEDIATELY;
}

device_t RxSpectrum_device = {
    .initialize = nullptr,
    .start = nullptr,
    .event = event,
    .timeout = timeout,
    .subscribe = EVENT_CONNECTION_CHANGED,
};

#endif // RX_SPECTRUM_SCAN
