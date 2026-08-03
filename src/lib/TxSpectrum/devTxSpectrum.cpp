#include "devTxSpectrum.h"

#if defined(TX_SPECTRUM_SCAN) && defined(PLATFORM_ESP32)

#include "SpectrumSweep.h"

#include "common.h"
#include "device.h"
#include "logging.h"
#include "CRSFRouter.h"
#include "crsf_protocol.h"
#include "FHSS.h"      // FHSSusePrimaryFreqBand
#include "POWERMGNT.h"
#include "OTA.h"       // isArmed
#include "hwTimer.h"
#include "rxtx_intf.h"  // scheduleRebootTime()
#include "handset.h"   // setPacketInterval()

// Trigger and handset transport; the measurement is lib/SpectrumSweep.

// Slow enough that a full CRSF packet fits one handset window at any supported
// baud; the link is down, so nothing is paying for the RC cadence
#define SPECTRUM_HANDSET_INTERVAL_US 10000

// A band index when the radios split across bands, a radio index when comparing
static uint8_t scanSource;

// The sweep mirrors the link's radio use: a dual-band rate puts one radio per
// band (the page button walks bands); any other rate runs both radios on its
// one band, so the sweep compares them (the page button walks antennas)
static bool ComparingRadios()
{
    return isDualRadio() && !FHSSuseDualBand;
}

static void BeginForCurrentSource()
{
    const bool comparing = ComparingRadios();
    SpectrumSweepSetCompare(comparing);
    if (comparing)
    {
        // The band the link is on; there is only one when comparing
        SpectrumSweepBegin(!FHSSusePrimaryFreqBand);
        SpectrumSweepStreamSource((int8_t)scanSource);
    }
    else
    {
        SpectrumSweepBegin(scanSource == 1);
    }
}
static bool running;
static uint32_t lastEmitMs;

static void EmitNextFrame()
{
    // Encoded straight into the frame buffer past the header, as sendELRSstatus does
    uint8_t buf[sizeof(crsf_ext_header_t) + SPECTRUM_MAX_PAYLOAD_BYTES + CRSF_FRAME_CRC_SIZE];
    const uint8_t len = SpectrumSweepEncodeFrame(buf + sizeof(crsf_ext_header_t));
    if (len == 0)
    {
        return;
    }

    crsfRouter.SetExtendedHeaderAndCrc((crsf_ext_header_t *)buf,
                                       CRSF_FRAMETYPE_ELRS_VENDOR,
                                       CRSF_EXT_FRAME_SIZE(len),
                                       CRSF_ADDRESS_RADIO_TRANSMITTER,
                                       CRSF_ADDRESS_CRSF_TRANSMITTER);
    crsfRouter.deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, (crsf_header_t *)buf);
}

void TxSpectrumSetRbw(const uint8_t rbw)
{
    SpectrumSweepSetRbw(rbw);
}

void TxSpectrumResetMaxHold()
{
    SpectrumSweepResetMaxHold();
}

void TxSpectrumStart()
{
    // Re-invoking the running command restarts the measurement
    if (connectionState == spectrumScan)
    {
        TxSpectrumResetMaxHold();
        return;
    }

    // Start on the band the link is using, or on radio 1 when comparing
    scanSource = (!ComparingRadios() && SpectrumSweepHasSecondBand() && !FHSSusePrimaryFreqBand) ? 1 : 0;

    // setPower only queues (lands via the sweep's Config() on LR1121, inert on
    // SX1280); the interlock is hwTimer, stopped and guarded at every resume()
    POWERMGNT::setPower(MinPower);
    setConnectionState(spectrumScan);
}

void TxSpectrumNextSource()
{
    // Loop core, same as the sweep, so no race with one in flight
    if (connectionState != spectrumScan ||
        (!ComparingRadios() && !SpectrumSweepHasSecondBand()))
    {
        return;
    }
    scanSource ^= 1;
    if (ComparingRadios())
    {
        // Only changes which trace streams; both max-holds keep accumulating
        SpectrumSweepStreamSource((int8_t)scanSource);
    }
    else
    {
        SpectrumSweepBegin(scanSource == 1);
    }
}

static int event()
{
    if (connectionState != spectrumScan)
    {
        running = false;
        return DURATION_NEVER;
    }

    // Never sweep while armed; abort via the same reboot the mode exits by
    if (isArmed)
    {
        DBGLN("TxSpectrum: armed, aborting scan");
        scheduleRebootTime(400);
        return DURATION_NEVER;
    }

    if (!running)
    {
        DBGLN("Starting spectrum scan");
        // Unlike WiFi/BLE the radio stays alive -- it is the instrument
        hwTimer::stop();

        // The handset CRSF window is sized from the packet interval
        // (adjustMaxPacketSize): at 1 kHz only 8-byte packets fit and a sweep
        // frame is over 60, so frames would never fit. RC data is going nowhere
        // while scanning; relax the interval. Exit is a reboot, nothing to restore
        handset->setPacketInterval(SPECTRUM_HANDSET_INTERVAL_US);
        SpectrumSweepIsolateRadio();
        BeginForCurrentSource();
        lastEmitMs = millis();
        running = true;
    }
    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    // A level check: sweeping must not depend on EVENT_ARM_FLAG_CHANGED delivery
    if (connectionState != spectrumScan || isArmed)
    {
        return DURATION_NEVER;
    }

    SpectrumSweepChunk();

    const uint32_t now = millis();
    if ((uint32_t)(now - lastEmitMs) >= SPECTRUM_SWEEP_EMIT_INTERVAL_MS)
    {
        EmitNextFrame();
        lastEmitMs = now;
    }

    return DURATION_IMMEDIATELY;
}

device_t TxSpectrum_device = {
    .initialize = nullptr,
    .start = nullptr,
    .event = event,
    .timeout = timeout,
    .subscribe = (uint32_t)(EVENT_CONNECTION_CHANGED | EVENT_ARM_FLAG_CHANGED),
};

#endif // TX_SPECTRUM_SCAN
