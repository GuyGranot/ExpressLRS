#ifndef TX_OTA_CONNECTOR_H
#define TX_OTA_CONNECTOR_H

#include "CRSFConnector.h"
#include "FIFO.h"
#include "telemetry_protocol.h"

class TXOTAConnector final : public CRSFConnector {
public:
    TXOTAConnector();

    void forwardMessage(const crsf_header_t *message) override;

    void resetOutputQueue();

    void pumpSender();

    /**
     * @brief True while an uplink message is in flight or still queued.
     *
     * The output queue is 256 bytes, so a caller that pushes MSP faster than
     * the uplink drains it (5 bytes per OTA packet, ack-gated by telemetry)
     * can evict the front of a frame it already started sending. That is a
     * silent whole-frame loss -- CROSSFIRE2MSP has no NAK -- so feed one
     * frame at a time and only when this is false.
     *
     * Loop-core only: both members are written from the sender pump.
     */
    bool uplinkBusy() const { return currentTransmissionLength != 0 || outputQueue.size() != 0; }

private:
    void unlockMessage();

    static constexpr auto MSP_SERIAL_OUT_FIFO_SIZE = 256U;
    FIFO<MSP_SERIAL_OUT_FIFO_SIZE> outputQueue;
    uint8_t currentTransmissionBuffer[ELRS_DATA_UL_BUFFER] = {};
    uint8_t currentTransmissionLength = 0;
};

#endif //TX_OTA_CONNECTOR_H
