#include "RXEndpoint.h"

#if !defined(UNIT_TEST)
#include "config.h"
#include "devMSPVTX.h"
#include "devVTXSPI.h"
#include "freqTable.h"
#include "rxtx_intf.h"
#include "logging.h"

#if defined(RX_SPECTRUM_SCAN)
#include "devRxSpectrum.h"
#endif

#if defined(RX_SURVEY_PHASE0)
#include "devRxSurvey.h"
#endif

extern void reset_into_bootloader();

RXEndpoint::RXEndpoint()
    : RxTxEndpoint(CRSF_ADDRESS_CRSF_RECEIVER)
{
}

/**
 * Handle any non-CRSF commands that we receive
 * @param message
 * @return
 */
bool RXEndpoint::handleRaw(const crsf_header_t *message)
{
    if (message->sync_byte == CRSF_ADDRESS_CRSF_RECEIVER && message->frame_size >= 4 && message->type == CRSF_FRAMETYPE_COMMAND)
    {
        uint8_t *payload = (uint8_t *)message + sizeof(crsf_header_t);
        // Non CRSF, dest=b src=l -> reboot to bootloader
        if (payload[0] == 'b' && payload[1] == 'l')
        {
            reset_into_bootloader();
            return true;
        }
        if (payload[0] == 'b' && payload[1] == 'd')
        {
            EnterBindingModeSafely();
            return true;
        }
        if (payload[0] == 'm' && payload[1] == 'm')
        {
            config.SetModelId(payload[2]);
            return true;
        }
#if defined(RX_SPECTRUM_SCAN)
        // Non CRSF, dest=s src=p -> enter receive-only spectrum scan.
        // payload[2] selects the antenna port/band (0=900, 1=2.4, 2=both),
        // payload[3] the sensing bandwidth (spectrumRbw_e) and payload[4] a
        // non-zero value to sweep both radios and compare their antennas. Both
        // trailing bytes are optional, so a shorter trigger keeps its old
        // meaning and older senders still work.
        // Drops the RC link; exits by resetting the receiver.
        if (payload[0] == 's' && payload[1] == 'p')
        {
            RxSpectrumStart(payload[2],
                            (message->frame_size >= 6) ? payload[3] : rbwWide,
                            (message->frame_size >= 7) && payload[4] != 0);
            return true;
        }
#endif
#if defined(RX_SURVEY_PHASE0)
        // Non CRSF, dest=s src=v -> arm/disarm the Phase 0 in-flight survey.
        // payload[2] non-zero arms, payload[3] is the sample offset from packet
        // end in us/4 and payload[4] the minimum sample spacing in ms. Trailing
        // bytes are optional, matching the 'sp' trigger above.
        //
        // Unlike 'sp' this does NOT touch the radio or the link: it sets a
        // volatile flag. The survey needs the RC link up, so it is armed with
        // the transmitter on and the flight controller in passthrough.
        if (payload[0] == 's' && payload[1] == 'v' && message->frame_size >= 5)
        {
            RxSurveyArm((message->frame_size >= 6)
                            ? (uint16_t)payload[3] * SURVEY_OFFSET_QUANTUM_US : 200,
                        (message->frame_size >= 7) ? payload[4] : 100,
                        payload[2] != 0);
            RxSurveySendStatus();
            return true;
        }
#endif
    }
    return false;
}

void RXEndpoint::handleMessage(const crsf_header_t *message)
{
    const auto extMessage = (crsf_ext_header_t *)message;

    if (handleRxTxMessage(message))
    {
        return;
    }
    else if (message->type == CRSF_FRAMETYPE_COMMAND && extMessage->payload[0] == CRSF_COMMAND_SUBCMD_RX && extMessage->payload[1] == CRSF_COMMAND_SUBCMD_RX_BIND)
    {
        EnterBindingModeSafely();
    }
#if defined(PLATFORM_ESP32)
    else if (message->type == CRSF_FRAMETYPE_MSP_RESP)
    {
        mspVtxProcessPacket((uint8_t *)message);
    }
    else if (OPT_HAS_VTX_SPI && message->type == CRSF_FRAMETYPE_MSP_WRITE && extMessage->payload[2] == MSP_SET_VTX_CONFIG)
    {
        vtxSPIFrequency = getFreqByIdx(extMessage->payload[3]);
        if (extMessage->payload[1] >= 4) // If packet has 4 bytes it also contains power idx and pitmode.
        {
            vtxSPIPowerIdx = extMessage->payload[5];
            vtxSPIPitmode = extMessage->payload[6];
        }
        devicesTriggerEvent(EVENT_VTX_CHANGE);
    }
#endif
    else if (message->type == CRSF_FRAMETYPE_DEVICE_PING ||
             message->type == CRSF_FRAMETYPE_PARAMETER_READ ||
             message->type == CRSF_FRAMETYPE_PARAMETER_WRITE)
    {
        parameterUpdateReq(
            extMessage->orig_addr,
            extMessage->type,
            extMessage->payload[0],  // parameter index
            extMessage->payload + 1  // start of parameter payload
        );
    }
}
#endif
