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

// Which source the handset is viewing: a band index when the radios are split
// across bands, a radio index when they are comparing antennas. The page button
// walks it; see TxSpectrumNextSource().
// Slow enough that a full CRSF packet fits in one handset window at any
// supported baud; the link is down, so nothing is paying for the RC cadence.
#define SPECTRUM_HANDSET_INTERVAL_US 10000

static uint8_t scanSource;

// The sweep mirrors what the link does with the radios, so every trace comes
// through a chain the link actually uses and nothing has to be chosen twice.
//
// A dual-band rate puts one radio on each band, and the sweep does the same:
// the page button walks bands. Any other rate has the link running BOTH radios
// on its one band, so the sweep compares them and the page button walks
// antennas.
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
        // The band the link is on; there is only one when comparing.
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
    // Encoded straight into the frame buffer past the header, as
    // sendELRSstatus does.
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
    // Re-invoking the running command restarts the measurement. There is no
    // separate Reset Max-Hold parameter because it would be unreachable while
    // scanning and a no-op otherwise.
    if (connectionState == spectrumScan)
    {
        TxSpectrumResetMaxHold();
        return;
    }

    // Start on the band the link is using, or on radio 1 when comparing.
    scanSource = (!ComparingRadios() && SpectrumSweepHasSecondBand() && !FHSSusePrimaryFreqBand) ? 1 : 0;

    // setPower only queues; it lands on LR1121 because the sweep calls Config(),
    // and is inert on SX1280, which commits only on a TX that never happens.
    // Not the interlock either way -- that is hwTimer, stopped and guarded at
    // every resume() site in tx_main.cpp.
    POWERMGNT::setPower(MinPower);
    setConnectionState(spectrumScan);
}

void TxSpectrumNextSource()
{
    // Loop core, same as the sweep, so no race with one in flight.
    // Two sources exist either as two radios on one band, or two bands.
    if (connectionState != spectrumScan ||
        (!ComparingRadios() && !SpectrumSweepHasSecondBand()))
    {
        return;
    }
    scanSource ^= 1;
    if (ComparingRadios())
    {
        // Both radios are already sweeping this band, so this only changes which
        // trace is streamed -- instant, and both max-holds keep accumulating.
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

    // Never sweep while armed. Abort via the same reboot the mode uses to exit
    // normally, so there is no restore path to get wrong.
    if (isArmed)
    {
        DBGLN("TxSpectrum: armed, aborting scan");
        scheduleRebootTime(400);
        return DURATION_NEVER;
    }

    if (!running)
    {
        DBGLN("Starting spectrum scan");
        // Unlike WiFi/BLE we keep the radio alive -- it is the instrument.
        hwTimer::stop();

        // The handset's CRSF window is sized from the packet interval:
        // adjustMaxPacketSize() gives maxPeriodBytes = baud/10/rate*0.87, then
        // takes a Lua chunk query off it. At 1 kHz and 400 kbaud that leaves
        // room for 8-byte packets, and a sweep frame is over 60 -- so frames
        // simply never fit and the trace stops updating. RC data is going
        // nowhere while scanning, so relax the interval until they fit again.
        // Exit is a reboot, so there is no restore path to get wrong.
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
    // A level check, not only the arm event above: this is what actually
    // sweeps, so it must not depend on EVENT_ARM_FLAG_CHANGED being delivered.
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
