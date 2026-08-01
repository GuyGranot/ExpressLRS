/***
 * This file defines the interface from device units to functions in
 * either rx_main or tx_main (or rxtx_common but exposed to other units)
 * Use this instead of drectly declaring externs in your unit
 ***/

#include "common.h"

/***
 * In both RX and TX builds
 */
void EnterBindingModeSafely();
void scheduleRebootTime(unsigned long inMs);

/***
 * TX interface
 ***/
#if defined(TARGET_TX)
void SetSyncSpam();

#if defined(USE_BLE_MSP) && defined(PLATFORM_ESP32)
// Sentinel for TxRequestSessionRate: run the rate from config.
#define TX_SESSION_RATE_NONE 0xFF

/**
 * @brief Temporarily run a different air rate without touching config.
 *
 * Applied asynchronously: the receiver is warned with sync packets on the old
 * rate before the radio hops, so the change costs a reacquisition rather than
 * a dropout. Pass TX_SESSION_RATE_NONE to return to the configured rate.
 */
void TxRequestSessionRate(uint8_t rateIndex);

/**
 * @brief True once the radio is back on the configured rate and both sync-spam
 * counters have drained, i.e. the warning telling the receiver where the TX
 * went has actually gone out. Rebooting before this strands the receiver on
 * the rate the TX just left.
 */
bool TxSessionRateIsHome();
#endif
#endif

/***
 * RX interface
 ***/
#if defined(TARGET_RX)
uint8_t getLq();
#endif
