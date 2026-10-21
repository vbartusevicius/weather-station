#pragma once

#include <cstddef>
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
    float pm10_ug_m3{12.0F};
    std::uint16_t voc_index{100};  // Sensirion VOC Index (SGP41 + gas index algorithm, 1..500, 100 = average)
    std::uint16_t nox_index{1};    // Sensirion NOx Index (SGP41 + gas index algorithm, 1..500, 1 = clean outdoor air)
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
    // US EPA NowCast concentration from hourly means, hourly[0] = most recent hour (n <= 12).
    // Returns false when fewer than 2 of the 3 most recent hours exist (EPA rule).
    static bool nowcast(const float* hourly, const bool* present, std::size_t n, float& out);
    // European Air Quality Index band (1 Good .. 6 Extremely poor) from 24-h PM means (EEA 2021 bands).
    static std::uint8_t eaqi_band(float pm25_24h_ug_m3, float pm10_24h_ug_m3);
    // US EPA AQI from PM only (VOC / NOx indices are relative Sensirion indices, not EPA pollutants).
    static AirQualityReport evaluate(const AirQualityInput& in);
    static AqiLevel level_for_aqi(std::uint16_t aqi);
    static const char* level_name(AqiLevel lvl);
};

}  // namespace airquality
}  // namespace station
