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

// Trigger and transport for the receive-only sweep (lib/SpectrumSweep), in and out
// over the FC/host UART via Betaflight passthrough -- which cannot arm, so no arm guard.

extern RXOTAConnector otaConnector;

// Long enough per port in "both" mode to emit several complete traces before switching
#define RX_SPECTRUM_BAND_DWELL_MS 500

static uint8_t requestedBand; // the RX_SPECTRUM_PORT_* the host asked for
static bool wantSubGHz;       // band currently swept; flips per dwell in "both"
static uint32_t bandDwellStartMs;
static bool running;
static uint32_t lastEmitMs;

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

// deliverMessage(&otaConnector, ...) forwards to every OTHER connector, i.e. the
// serial (FC/host) connector, the same way the RX emits link statistics
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

// Answer a scan trigger. Sent accepted or not, so a refusal is never silent.
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
    // Bench diagnostic only: refuse to tear down a live RC link
    if (connectionState == connected)
    {
        DBGLN("RxSpectrum: refused, RC link is up");
        SendScanStatus(SPECTRUM_STATUS_REFUSED_LINKED);
        return;
    }

    if (band > RX_SPECTRUM_PORT_BOTH || !SpectrumSweepHasSecondBand())
    {
        band = RX_SPECTRUM_PORT_900;
    }
    SpectrumSweepSetRbw(rbw); // out-of-range falls back to Wide
    SpectrumSweepSetCompare(compare);
    // unlike the handset, the host can take every source's trace at once
    SpectrumSweepStreamSource(compare ? -1 : 0);
    requestedBand = band;
    wantSubGHz = (band != RX_SPECTRUM_PORT_2G4);

    // Re-trigger while scanning restarts the measurement (clears max-hold);
    // same single-threaded loop core as the sweep, so no race.
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

    // setPower only queues (lands via the sweep's Config() on LR1121, inert on
    // SX1280); the interlock is the stopped hwTimer, not this
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
        // The WiFi AP is a transmitter; never open the RX front end with it live
#if defined(PLATFORM_ESP32) || defined(PLATFORM_ESP8266)
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
#if defined(PLATFORM_ESP8266)
        WiFi.forceSleepBegin();
#endif
#endif
        hwTimer::stop();

        // One-shot hardware init, recovering the SLEEP state a WiFi AP leaves the
        // radio in. End() first: Begin() is not idempotent -- SX1280Hal::init()
        // re-runs SPIEx.begin() and re-attaches the second chip select, which
        // breaks the bus on an already-initialised dual-radio ESP32.
        Radio.End();
        if (!Radio.Begin(FHSSgetMinimumFreq(), FHSSgetMaximumFreq()))
        {
            // Unconfigured radios read RSSI as 0xFF, a plausible full-scale-low
            // trace; report the failure rather than stream it
            SendScanStatus(SPECTRUM_STATUS_RADIO_FAILED);
            scheduleRebootTime(400);
            running = false;
            return DURATION_NEVER;
        }

        // after Begin(): the HAL re-init re-attaches the MCU RXdone ISR
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

    // "Both" mode flips ports at a sweep boundary after the dwell; the restart
    // reconfigures the radio for the new band and resets the axis
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
