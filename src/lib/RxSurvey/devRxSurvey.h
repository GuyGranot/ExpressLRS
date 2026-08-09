#pragma once

/*
 * In-flight passive RF survey (DEBUG_RF_SURVEY).
 *
 * While an ordinary link flies, sample instantaneous noise-floor RSSI on the
 * FHSS channels the link visits -- at the top of the timer tock ISR, where the
 * radio has sat in RX_CONT on the current channel for a full packet period --
 * and export one sample per link-statistics frame through the receiver's three
 * unused downlink bytes, which Betaflight logs as debug[0..2] under
 * debug_mode = CRSF_LINK_STATISTICS_DOWN, next to GPS and aircraft state.
 * Armed from the handset Lua, volatile, OFF at every boot; disarmed cost is
 * one RAM read and a predicted branch per tock.
 */

#include <stdint.h>

#include "device.h"

#if defined(DEBUG_RF_SURVEY)

#if defined(DEBUG_BF_LINK_STATS)
#error "DEBUG_RF_SURVEY and DEBUG_BF_LINK_STATS both write the downlink debug bytes; pick one"
#endif
#if !defined(PLATFORM_ESP32)
// ESP8266's tock runs without a critical section, so a tock-time SPI read can
// collide with the RXdone GPIO ISR mid-transfer (SPIEx has no locking).
#error "DEBUG_RF_SURVEY requires an ESP32-family receiver"
#endif

// Values of the volatile survey mode, in the order the Lua selector lists them.
// On a dual-band link the selection names the band(s) to sample; on a
// single-band link the link fixes the band, so Both and the link's own band
// mean On, and naming the absent band yields no samples.
enum
{
    RX_SURVEY_OFF = 0,
    RX_SURVEY_BOTH,
    RX_SURVEY_900,
    RX_SURVEY_2G4,
};

/**
 * Arm or disarm. Volatile, RAM only: no config write, so no version bump and
 * no LostConnection() from CheckConfigChangePending. A power cycle is an
 * unconditional disable. Loop context only.
 */
void RxSurveySetMode(uint8_t mode);

/**
 * The sampler. Call from HWtimerCallbackTock() after sendImmediateRC() and
 * before OtaNonce++ / HandleFHSS(): the channel is still the pre-hop one, the
 * radio is still tuned to it, and a sampling tock never delays the RC frame.
 */
void RxSurveyTock();

/**
 * Copy the staged sample into linkStats' three downlink bytes. Call from
 * checkSendLinkStatsToFc() immediately before makeLinkStatisticsPacket(): only
 * this loop-context copy -- never the ISR -- may touch linkStats, or the
 * router's bulk memcpy can pair one channel's RSSI with another channel's
 * index and call it data.
 */
void RxSurveyPublish();

/** Link-stats cadence: 40 ms while armed (25 Hz), the caller's default otherwise. */
uint32_t RxSurveyLinkStatsInterval(uint32_t defaultIntervalMs);

/**
 * Turn the 0x83 bench stream on or off (the 'sf' serial command). While on AND
 * armed, every staged sample is also emitted in full fidelity over the FC UART
 * so the bench tool can validate the sampler without a Blackbox. Off at boot
 * and cleared by SetMode(Off); a flight never asks, so a flight never pays.
 */
void RxSurveyBenchStream(bool on);

/** Emit the status frame (mode, export interval, worst tock us, coverage generation). */
void RxSurveySendStatus();

extern device_t RxSurvey_device;

#endif // DEBUG_RF_SURVEY
