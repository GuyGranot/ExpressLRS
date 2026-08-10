#pragma once

#include "CRSFEndpoint.h"
#include "msptypes.h"

class RxTxEndpoint : public CRSFEndpoint {
public:
    explicit RxTxEndpoint(const crsf_addr_e device_id)
        : CRSFEndpoint(device_id) {}
    ~RxTxEndpoint() override = default;

protected:
    bool handleRxTxMessage(const crsf_header_t *message);

    virtual void handleMspGetRxTxConfig(crsf_ext_header_t *extMessage);
    virtual void handleMspSetRxTxConfig(crsf_ext_header_t *extMessage);

    // Send one MSP_ELRS_RXTX_CONFIG frame: subcommand byte then payload
    void sendRxTxConfig(bool isResponse, MSP_ELRS_RXTX_CONFIG_SUBCMD subcmd,
                        const uint8_t *payload, uint8_t len, crsf_addr_e destination);
#if defined(USE_FHSS_SUBSET)
    // The "Subset Bands" folder: an on/off, a first and a last frequency, and
    // the resulting channel span, per physical band. Both ends present the same
    // folder, so it is built once here rather than in each parameter layer.
    // Call the update from the endpoint's parameter refresh.
    //
    // Folder and fields register separately so an endpoint can place its own
    // control between them - registration order is display order. Anything that
    // changes these fields' visibility MUST sit inside this folder:
    // reloadRelatedFields() in elrs.lua re-reads only the edited field's parent
    // and its same-parent siblings, so a control outside would flip a visibility
    // the handset never asks about again until the script is reloaded.
    uint8_t registerBandSubsetFolder();
    void registerBandSubsetFields();
    void updateBandSubsetParameters();
#endif
};
