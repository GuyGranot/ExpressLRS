#pragma once

/*
 * In-flight passive RF survey (DEBUG_RF_SURVEY): while an ordinary link flies,
 * sample noise-floor RSSI in the timer tock on the FHSS channels the link
 * visits, and export one sample per link-statistics frame through the three
 * unused downlink bytes (Betaflight's debug[0..2]). Armed from the handset
 * Lua, volatile, off at every boot.
 */

#include <stdint.h>

#include "device.h"

#if defined(DEBUG_RF_SURVEY)

#if defined(DEBUG_BF_LINK_STATS)
#error "DEBUG_RF_SURVEY and DEBUG_BF_LINK_STATS both write the downlink debug bytes; pick one"
#endif
#if !defined(PLATFORM_ESP32)
// ESP8266's tock has no critical section, so a tock-time SPI read can collide
// with the RXdone GPIO ISR mid-transfer
#error "DEBUG_RF_SURVEY requires an ESP32-family receiver"
#endif

// Volatile survey mode, in Lua selector order.
enum
{
    RX_SURVEY_OFF = 0,
    RX_SURVEY_BOTH,
    RX_SURVEY_900,
    RX_SURVEY_2G4,
};

// Arm or disarm; RAM only, no config write. Loop context only.
void RxSurveySetMode(uint8_t mode);

// Call from HWtimerCallbackTock() before OtaNonce++ / HandleFHSS(), while the
// radio still sits on the pre-hop channel.
void RxSurveyTock();

// Copy the staged sample into linkStats' downlink bytes; loop context only.
void RxSurveyPublish();

// Link-stats cadence: 40 ms while armed (25 Hz), the caller's default otherwise.
uint32_t RxSurveyLinkStatsInterval(uint32_t defaultIntervalMs);

// Turn the 0x83 bench stream on or off (the 'sf' serial command); off at boot
// and cleared by SetMode(Off), so a flight never pays for it.
void RxSurveyBenchStream(bool on);

// Emit the status frame (mode, export interval, worst tock us, coverage generation).
void RxSurveySendStatus();

extern device_t RxSurvey_device;

#endif // DEBUG_RF_SURVEY
