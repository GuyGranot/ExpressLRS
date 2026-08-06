#if defined(RX_SURVEY_PHASE0) || defined(RX_FLIGHT_SURVEY)

#include "SurveyShared.h"

#include "FHSS.h"

int8_t ICACHE_RAM_ATTR SurveyReadRssiInst(const SX12XX_Radio_Number_t radio)
{
#if defined(RADIO_SX127X)
    return Radio.GetCurrRSSI(radio);
#else
    return Radio.GetRssiInst(radio);
#endif
}

int8_t ICACHE_RAM_ATTR SurveyReferred(const int8_t raw, const int8_t lnaGainDb)
{
    const int16_t v = (int16_t)raw - (int16_t)lnaGainDb;
    if (v < SURVEY_RSSI_INVALID + 1)
    {
        return SURVEY_RSSI_INVALID + 1;
    }
    if (v > INT8_MAX)
    {
        return INT8_MAX;
    }
    return (int8_t)v;
}

// One band's config -> one axis. FREQ_HZ_TO_REG_VAL is the identity on LR1121
// (register values are Hz); the other families store register units.
static void AxisFromConfig(const fhss_config_t *const cfg, const uint32_t spread,
                           uint32_t *const startKhz, uint16_t *const stepKhz)
{
#if defined(RADIO_LR1121)
    *startKhz = cfg->freq_start / 1000;
    *stepKhz = (uint16_t)((spread / FREQ_SPREAD_SCALE) / 1000);
#else
    *startKhz = (uint32_t)(((double)cfg->freq_start * FREQ_STEP / 1000.0) + 0.5);
    *stepKhz = (uint16_t)((((double)spread / FREQ_SPREAD_SCALE) * FREQ_STEP / 1000.0) + 0.5);
#endif
}

void SurveyFillAxis(surveyFrameInfo_t *const info)
{
    const bool primary = FHSSusePrimaryFreqBand;
    const fhss_config_t *const cfg = primary ? FHSSconfig : FHSSconfigDualBand;
    AxisFromConfig(cfg, primary ? freq_spread : freq_spread_DualBand,
                   &info->startFreqKhz, &info->stepKhz);
    info->channelCount = (uint8_t)cfg->freq_count;

    if (FHSSuseDualBand)
    {
        // Cross-band link: radio 1 hops the primary (sub-GHz) grid above, radio
        // 2 hops this one, both off the same sequence pointer.
        AxisFromConfig(FHSSconfigDualBand, freq_spread_DualBand,
                       &info->startFreqKhz2, &info->stepKhz2);
        info->channelCount2 = (uint8_t)FHSSconfigDualBand->freq_count;
    }
}

uint16_t SurveyLinkBandwidthKhz()
{
#if defined(RADIO_SX127X)
    return 500;
#elif defined(RADIO_LR1121)
    // DualBand counts as sub-GHz here: radio 1 is configured from bw/sf/cr and
    // radio 2 from bw2/sf2/cr2 (rx_main.cpp), so the bandwidth this reports is
    // the primary (sub-GHz) chain's. The second band's 812 kHz is implied by
    // the second axis being present.
    const uint8_t rt = ExpressLRS_currAirRate_Modparams->radio_type;
    return (RadioBandMod::isB900(rt) || RadioBandMod::isBDUAL(rt)) ? 500 : 812;
#else // RADIO_SX128X
    return 812;
#endif
}

#endif // RX_SURVEY_PHASE0 || RX_FLIGHT_SURVEY
