#pragma once

/*
 * Sampling helpers shared by the Phase 0 bench experiment (RX_SURVEY_PHASE0)
 * and the in-flight survey (RX_FLIGHT_SURVEY). What is shared is measurement
 * plumbing only -- RSSI read dispatch, LNA referral, the channel axis and the
 * link bandwidth. The two features' gates are deliberately NOT shared: they run
 * in different ISRs and mean different things.
 */

#if defined(RX_SURVEY_PHASE0) || defined(RX_FLIGHT_SURVEY)

#include "SurveyProtocol.h"
#include "common.h"

/** Instantaneous RSSI of one radio; the SX127x driver names the read differently. */
int8_t ICACHE_RAM_ATTR SurveyReadRssiInst(SX12XX_Radio_Number_t radio);

/**
 * Antenna-referred, exactly as SpectrumSweep::StoreBin does it -- the survey and
 * the sweep are only comparable if the same receive-path gain is backed out of
 * both. Clamped so a real value can never collide with SURVEY_RSSI_INVALID.
 */
int8_t ICACHE_RAM_ATTR SurveyReferred(int8_t raw, int8_t lnaGainDb);

/**
 * The channel axis (or both, on a dual-band link), computed exactly as
 * SpectrumSweep::ComputeAxis does. The two must agree to the kHz or the
 * per-channel join between a survey and a sweep compares different frequencies.
 * Leaves the second axis at 0 unless FHSSuseDualBand.
 */
void SurveyFillAxis(surveyFrameInfo_t *info);

/**
 * The live link's own sensing bandwidth in kHz. Only meaningful on a LoRa rate,
 * where every rate in a band shares the band's widest bandwidth -- the same
 * figure lib/SpectrumSweep uses for Wide.
 */
uint16_t SurveyLinkBandwidthKhz();

/**
 * RX -> flight controller / host: stamp the vendor-frame header and CRC on buf
 * (payload already written past the extended header) and route it, mirroring
 * devRxSpectrum's SendVendorFrame. Loop context.
 */
void SurveySendVendorFrame(uint8_t *buf, uint8_t payloadLen);

/**
 * Build and send one data (or sample-less heartbeat) frame. Everything the
 * caller does not pass -- rate, bandwidth, the channel axes -- is read from the
 * live link; flags come from the caller because the two features' gates mean
 * different things. Loop context.
 */
void SurveyEmitDataFrame(uint8_t flags, uint8_t reqOffsetQus, int8_t lnaGainDb, uint8_t seq,
                         const surveySample_t *samples, uint8_t count, uint16_t dropped);

#endif // RX_SURVEY_PHASE0 || RX_FLIGHT_SURVEY
