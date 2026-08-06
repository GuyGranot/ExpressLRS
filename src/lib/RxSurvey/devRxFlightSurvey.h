#pragma once

/*
 * In-flight passive RF survey (RX_FLIGHT_SURVEY).
 *
 * While an ordinary link flies, sample instantaneous noise-floor RSSI on the
 * FHSS channels the link visits -- at the top of the timer tock ISR, where the
 * radio has sat in RX_CONT on the current channel for a full packet period and
 * where ESP32's hwTimer critical section makes the SPI reads race-free against
 * the RXdone ISR -- and export one sample per link-statistics frame through the
 * receiver's three dead downlink bytes. Betaflight logs them untouched as
 * debug[0..2] under debug_mode = CRSF_LINK_STATISTICS_DOWN, next to GPS and
 * aircraft state, which is the whole point: aircraft self-noise under load,
 * per-antenna comparison, and per-locale spectrum from real flights.
 *
 * A test feature: armed from the handset Lua (or the bench 'sf' command),
 * volatile, no config writes, and OFF at every boot. Disarmed cost is one RAM
 * read and a predicted branch per tock.
 *
 * Levels carry the bench sweep's caveats -- antenna-referred via power_lna_gain
 * but uncalibrated in absolute terms -- and are comparable to a default (wide
 * RBW) sweep of the same band because sampling is gated to LoRa air rates.
 */

#include <stdint.h>

#if defined(RX_FLIGHT_SURVEY)

#if defined(DEBUG_BF_LINK_STATS)
#error "RX_FLIGHT_SURVEY and DEBUG_BF_LINK_STATS both write the downlink debug bytes; pick one"
#endif
#if !defined(PLATFORM_ESP32)
// ESP8266's tock runs without a critical section, so a tock-time SPI read can
// collide with the RXdone GPIO ISR mid-transfer (SPIEx has no locking).
#error "RX_FLIGHT_SURVEY requires an ESP32-family receiver"
#endif
#if defined(RADIO_SX127X)
#error "RX_FLIGHT_SURVEY is untested on SX127x receivers"
#endif

// Values of the volatile survey mode, in the order the Lua selector lists them.
// On a dual-band link the selection names the band(s) to sample; on a
// single-band link the link fixes the band, so Both and the link's own band
// mean On, and naming the absent band yields no samples.
enum
{
    FLIGHT_SURVEY_OFF = 0,
    FLIGHT_SURVEY_BOTH,
    FLIGHT_SURVEY_900,
    FLIGHT_SURVEY_2G4,
};

/**
 * Arm or disarm. Volatile, RAM only: no config write, so no version bump, no
 * downgrade wipe, and no LostConnection() from CheckConfigChangePending. A
 * power cycle is an unconditional disable. Loop context only.
 */
void RxFlightSurveySetMode(uint8_t mode);

uint8_t RxFlightSurveyGetMode();

/**
 * The sampler. Call from HWtimerCallbackTock() after sendImmediateRC() and
 * before OtaNonce++ / HandleFHSS(): the channel is still the pre-hop one, the
 * radio is still tuned to it, and a sampling tock never delays the RC frame.
 */
void RxFlightSurveyTock();

/**
 * Copy the staged sample into linkStats' three downlink bytes. Call from
 * checkSendLinkStatsToFc() immediately before makeLinkStatisticsPacket(): the
 * three bytes are one logical record, and only this loop-context copy -- never
 * the ISR -- may touch linkStats, or the router's bulk memcpy can pair one
 * channel's RSSI with another channel's index and call it data.
 */
void RxFlightSurveyPublish();

/** Link-stats cadence: 40 ms while armed (25 Hz), the caller's default otherwise. */
uint32_t RxFlightSurveyLinkStatsInterval(uint32_t defaultIntervalMs);

#else

static inline void RxFlightSurveySetMode(uint8_t) {}
static inline uint8_t RxFlightSurveyGetMode() { return 0; }
static inline void RxFlightSurveyTock() {}
static inline void RxFlightSurveyPublish() {}
static inline uint32_t RxFlightSurveyLinkStatsInterval(uint32_t defaultIntervalMs)
{
    return defaultIntervalMs;
}

#endif // RX_FLIGHT_SURVEY
