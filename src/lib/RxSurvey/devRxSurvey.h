#pragma once

#include "device.h"

#if defined(RX_SURVEY_PHASE0)

// For the sentinels and flags: callers latch channel indices and read the
// wire-format constants, the same way devRxSpectrum.h re-exports SpectrumSweep.h.
#include "SurveyProtocol.h"

#include <stdint.h>

extern device_t RxSurvey_device;

/**
 * Phase 0 bench experiment: does instantaneous RSSI, read a controlled offset
 * after the end of a received packet, agree with what lib/SpectrumSweep reports
 * for the same channel with the link down?
 *
 * That question decides whether an in-flight passive survey can measure a noise
 * floor at all. The sweep drops to STDBY_RC and settles 0.5-4 ms per bin because
 * gain-state carry-over was observed to flatten a whole trace; a live link can
 * do neither. Listen-before-talk reads RSSI live today with a 22-480 us settle,
 * which says the mechanism works -- it does not say the value is unbiased 200 us
 * after a strong wanted packet. This measures that.
 *
 * BENCH ONLY. Built out unless -DRX_SURVEY_PHASE0 is defined, disarmed at every
 * boot, and it busy-waits inside the RXdone ISR to hit the requested offset --
 * which is exactly what a shipping feature must never do. Nothing here is a
 * template for the survey itself; it exists to produce one number.
 */

/**
 * Arm or disarm sampling. Volatile, RAM only: no config write, so no version
 * bump, no downgrade wipe, and no LostConnection() from CheckConfigChangePending.
 * A power cycle is an unconditional disable.
 *
 * @param offsetUs  target offset from packet end, clamped to
 *                  [0, SURVEY_OFFSET_MAX_US] and quantised to 4 us on the wire
 * @param periodMs  minimum spacing between samples; bounds the cost of a deep
 *                  offset to one disturbed packet period per period
 * @param on        false disarms and leaves the hook a single predicated branch
 */
void RxSurveyArm(uint16_t offsetUs, uint8_t periodMs, bool on);

/**
 * Latch the channel index each radio was just tuned to. Called from HandleFHSS
 * with the hop's own state, so the sample never has to reconstruct the Gemini
 * swap parity from an OtaNonce that has moved on since.
 *
 * @param chan2  SURVEY_CHAN_INVALID when there is no comparable second radio
 */
void RxSurveyNoteHop(uint8_t chan1, uint8_t chan2, bool gemini);

/**
 * Take one sample, if armed and the gates allow it. Called from the RXdone ISR
 * at the end of ProcessRFPacket().
 *
 * That site is chosen for two reasons. The radio is still on the channel the
 * packet arrived on -- HandleFHSS has not run yet -- and the SPI reads happen in
 * the same ISR that already drives the bus for GetLastPacketStats(), so they
 * cannot interleave with it. SPIEx has no locking, so a read from any other
 * context could resume with this ISR's chip select and transfer length.
 *
 * @param packetEndUs   ProcessRFPacket()'s own micros() stamp; the offset is
 *                      measured from here, which excludes a small constant of
 *                      DIO-to-ISR latency
 * @param packetRssi    RSSI of the packet that just ended, i.e. the wanted
 *                      signal whose AGC state this sample inherits
 */
void RxSurveySamplePostPacket(uint32_t packetEndUs, int8_t packetRssi, bool packetOnRadio2);

/**
 * Answer an arm/disarm command. Sent on every command, armed or not: silence on
 * the host cannot be told apart from a command that never arrived, a receiver
 * built without the flag, or passthrough that has dropped.
 */
void RxSurveySendStatus();

#else
inline void RxSurveyArm(uint16_t, uint8_t, bool) {}
inline void RxSurveyNoteHop(uint8_t, uint8_t, bool) {}
inline void RxSurveySamplePostPacket(uint32_t, int8_t, bool) {}
inline void RxSurveySendStatus() {}
#endif
