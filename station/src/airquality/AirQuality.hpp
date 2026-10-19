#pragma once

#include <cstdint>

namespace station {
namespace airquality {

enum class AqiLevel : std::uint8_t {
    Good = 0,
    Moderate = 1,
    UnhealthySensitive = 2,
    Unhealthy = 3,
    VeryUnhealthy = 4,
    Hazardous = 5,
};

struct AirQualityInput {
    float pm1_0_ug_m3{4.0F};
    float pm2_5_ug_m3{6.5F};
    float pm4_0_ug_m3{8.0F};
    float pm10_ug_m3{12.0F};
    std::uint16_t voc_index{100};  // Sensirion SGP41 VOC Index (1..500, 100 = average)
    std::uint16_t nox_index{1};    // Sensirion SGP41 NOx Index (1..500, 1 = clean outdoor air)
    std::uint16_t co2_ppm{420};    // Sensirion SCD41 photoacoustic CO2 (ppm)
    float relative_humidity_rh{50.0F};
    float pressure_hpa{1013.25F};
};

struct AirQualityReport {
    std::uint16_t us_epa_aqi{27};
    std::uint8_t eu_eaqi{1};
    AqiLevel level{AqiLevel::Good};
    const char* dominant_pollutant{"PM2.5"};
    float dry_pm2_5_ug_m3{6.5F};
    float dry_pm10_ug_m3{12.0F};
    std::uint16_t pressure_comp_co2_ppm{420};
    std::uint16_t aqi_pm25{27};
    std::uint16_t aqi_pm10{11};
    std::uint16_t aqi_nox{2};
    std::uint16_t aqi_voc{25};
};

class AirQualityEvaluator {
public:
    static float compensate_pm_hygroscopic_growth(float raw_pm_ug_m3, float rh_percent);
    static std::uint16_t compensate_co2_pressure_ppm(std::uint16_t raw_co2_ppm, float pressure_hpa);
    static std::uint16_t compute_pm25_aqi(float pm25_ug_m3);
    static std::uint16_t compute_pm10_aqi(float pm10_ug_m3);
    static std::uint16_t compute_nox_subindex(std::uint16_t nox_index);
    static std::uint16_t compute_voc_subindex(std::uint16_t voc_index);
    static AirQualityReport evaluate(const AirQualityInput& in);
    static const char* level_name(AqiLevel lvl);
};

}  // namespace airquality
}  // namespace station
