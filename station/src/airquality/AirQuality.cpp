#include "AirQuality.hpp"

#include <algorithm>
#include <cmath>

namespace station {
namespace airquality {

namespace {
float clamp_f(float v, float lo, float hi) {
    return std::min(hi, std::max(lo, v));
}

struct Breakpoint {
    float c_low;
    float c_high;
    std::uint16_t i_low;
    std::uint16_t i_high;
};

std::uint16_t piecewise_aqi(float c, const Breakpoint* table, std::size_t n) {
    const float clamped = std::max(0.0F, c);
    for (std::size_t k = 0; k < n; ++k) {
        if (clamped <= table[k].c_high || k == n - 1) {
            const float c_lo = table[k].c_low;
            const float c_hi = table[k].c_high;
            const float ratio = clamp_f((clamped - c_lo) / std::max(0.1F, c_hi - c_lo), 0.0F, 1.0F);
            const float val = static_cast<float>(table[k].i_low) +
                              ratio * static_cast<float>(table[k].i_high - table[k].i_low);
            return static_cast<std::uint16_t>(std::lround(val));
        }
    }
    return 500;
}
}  // namespace

float AirQualityEvaluator::compensate_pm_hygroscopic_growth(float raw_pm_ug_m3, float rh_percent) {
    // kappa-Kohler hygroscopic fog/mist growth correction (Crilley et al. / EPA outdoor PM correction)
    if (rh_percent <= 70.0F) {
        return std::max(0.0F, raw_pm_ug_m3);
    }
    constexpr float kKappa = 0.35F;
    const float aw = clamp_f(rh_percent / 100.0F, 0.70F, 0.94F);
    const float growth_factor = 1.0F + kKappa * (aw / (1.0F - aw));
    return std::max(0.0F, raw_pm_ug_m3 / growth_factor);
}

std::uint16_t AirQualityEvaluator::compensate_co2_pressure_ppm(std::uint16_t raw_co2_ppm, float pressure_hpa) {
    if (pressure_hpa < 500.0F || pressure_hpa > 1200.0F) {
        return raw_co2_ppm;
    }
    // SCD41 photoacoustic cell density scales with P_ambient / P_std (1013.25 hPa)
    const float corrected = static_cast<float>(raw_co2_ppm) * (1013.25F / pressure_hpa);
    return static_cast<std::uint16_t>(std::min(10000L, std::max(350L, std::lround(corrected))));
}

std::uint16_t AirQualityEvaluator::compute_pm25_aqi(float pm25_ug_m3) {
    static constexpr Breakpoint kPm25Table[] = {
        {0.0F, 9.0F, 0, 50},
        {9.0F, 35.4F, 51, 100},
        {35.4F, 55.4F, 101, 150},
        {55.4F, 125.4F, 151, 200},
        {125.4F, 225.4F, 201, 300},
        {225.4F, 500.4F, 301, 500},
    };
    return piecewise_aqi(pm25_ug_m3, kPm25Table, sizeof(kPm25Table) / sizeof(kPm25Table[0]));
}

std::uint16_t AirQualityEvaluator::compute_pm10_aqi(float pm10_ug_m3) {
    static constexpr Breakpoint kPm10Table[] = {
        {0.0F, 54.0F, 0, 50},
        {54.0F, 154.0F, 51, 100},
        {154.0F, 254.0F, 101, 150},
        {254.0F, 354.0F, 151, 200},
        {354.0F, 424.0F, 201, 300},
        {424.0F, 604.0F, 301, 500},
    };
    return piecewise_aqi(pm10_ug_m3, kPm10Table, sizeof(kPm10Table) / sizeof(kPm10Table[0]));
}

std::uint16_t AirQualityEvaluator::compute_nox_subindex(std::uint16_t nox_index) {
    static constexpr Breakpoint kNoxTable[] = {
        {1.0F, 20.0F, 0, 50},
        {20.0F, 75.0F, 51, 100},
        {75.0F, 150.0F, 101, 150},
        {150.0F, 250.0F, 151, 200},
        {250.0F, 500.0F, 201, 400},
    };
    return piecewise_aqi(static_cast<float>(nox_index), kNoxTable, sizeof(kNoxTable) / sizeof(kNoxTable[0]));
}

std::uint16_t AirQualityEvaluator::compute_voc_subindex(std::uint16_t voc_index) {
    static constexpr Breakpoint kVocTable[] = {
        {0.0F, 150.0F, 0, 50},
        {150.0F, 250.0F, 51, 100},
        {250.0F, 350.0F, 101, 150},
        {350.0F, 450.0F, 151, 200},
        {450.0F, 500.0F, 201, 300},
    };
    return piecewise_aqi(static_cast<float>(voc_index), kVocTable, sizeof(kVocTable) / sizeof(kVocTable[0]));
}

AirQualityReport AirQualityEvaluator::evaluate(const AirQualityInput& in) {
    AirQualityReport out{};
    out.dry_pm2_5_ug_m3 = compensate_pm_hygroscopic_growth(in.pm2_5_ug_m3, in.relative_humidity_rh);
    out.dry_pm10_ug_m3 = compensate_pm_hygroscopic_growth(in.pm10_ug_m3, in.relative_humidity_rh);
    out.pressure_comp_co2_ppm = compensate_co2_pressure_ppm(in.co2_ppm, in.pressure_hpa);

    out.aqi_pm25 = compute_pm25_aqi(out.dry_pm2_5_ug_m3);
    out.aqi_pm10 = compute_pm10_aqi(out.dry_pm10_ug_m3);
    out.aqi_nox = compute_nox_subindex(in.nox_index);
    out.aqi_voc = compute_voc_subindex(in.voc_index);

    out.us_epa_aqi = out.aqi_pm25;
    out.dominant_pollutant = "PM2.5";
    if (out.aqi_pm10 > out.us_epa_aqi) {
        out.us_epa_aqi = out.aqi_pm10;
        out.dominant_pollutant = "PM10";
    }
    if (out.aqi_nox > out.us_epa_aqi) {
        out.us_epa_aqi = out.aqi_nox;
        out.dominant_pollutant = "NOx";
    }
    if (out.aqi_voc > out.us_epa_aqi) {
        out.us_epa_aqi = out.aqi_voc;
        out.dominant_pollutant = "VOC";
    }

    if (out.us_epa_aqi <= 50) {
        out.level = AqiLevel::Good;
        out.eu_eaqi = 1;
    } else if (out.us_epa_aqi <= 100) {
        out.level = AqiLevel::Moderate;
        out.eu_eaqi = 2;
    } else if (out.us_epa_aqi <= 150) {
        out.level = AqiLevel::UnhealthySensitive;
        out.eu_eaqi = 3;
    } else if (out.us_epa_aqi <= 200) {
        out.level = AqiLevel::Unhealthy;
        out.eu_eaqi = 4;
    } else if (out.us_epa_aqi <= 300) {
        out.level = AqiLevel::VeryUnhealthy;
        out.eu_eaqi = 5;
    } else {
        out.level = AqiLevel::Hazardous;
        out.eu_eaqi = 6;
    }
    return out;
}

const char* AirQualityEvaluator::level_name(AqiLevel lvl) {
    switch (lvl) {
        case AqiLevel::Good:
            return "Good";
        case AqiLevel::Moderate:
            return "Moderate";
        case AqiLevel::UnhealthySensitive:
            return "Unhealthy for Sensitive";
        case AqiLevel::Unhealthy:
            return "Unhealthy";
        case AqiLevel::VeryUnhealthy:
            return "Very Unhealthy";
        case AqiLevel::Hazardous:
            return "Hazardous";
    }
    return "Good";
}

}  // namespace airquality
}  // namespace station
