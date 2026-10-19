#include "MetCalculations.hpp"

#include <algorithm>
#include <cmath>

namespace station {
namespace environment {

namespace {
float clamp_f(float v, float lo, float hi) {
    return std::min(hi, std::max(lo, v));
}
}  // namespace

float MetCalculator::solar_radiation_heating_error_c(float solar_w_m2, float wind_m_s) {
    if (solar_w_m2 <= 10.0F) {
        return 0.0F;
    }
    const float v_eff = std::max(0.25F, wind_m_s);
    const float dt = (0.0006F * clamp_f(solar_w_m2, 0.0F, 1500.0F)) / std::pow(v_eff, 0.65F);
    return clamp_f(dt, 0.0F, 2.5F);
}

float MetCalculator::compensate_wind_dynamic_pressure_hpa(float raw_pressure_hpa, float wind_m_s, float temp_c) {
    if (wind_m_s <= 0.5F) {
        return raw_pressure_hpa;
    }
    const float t_k = std::max(200.0F, temp_c + 273.15F);
    const float rho = (raw_pressure_hpa * 100.0F) / (287.058F * t_k);
    const float dyn_hpa = 0.22F * 0.5F * rho * wind_m_s * wind_m_s * 0.01F;
    return raw_pressure_hpa + dyn_hpa;
}

float MetCalculator::dew_point_c(float temp_c, float rh_percent) {
    const float rh = clamp_f(rh_percent, 1.0F, 100.0F);
    constexpr float kA = 17.625F;
    constexpr float kB = 243.04F;
    const float gamma = std::log(rh / 100.0F) + (kA * temp_c) / (kB + temp_c);
    return (kB * gamma) / (kA - gamma);
}

float MetCalculator::wet_bulb_c(float temp_c, float rh_percent) {
    const float rh = clamp_f(rh_percent, 1.0F, 100.0F);
    return temp_c * std::atan(0.151977F * std::sqrt(rh + 8.313659F)) +
           std::atan(temp_c + rh) - std::atan(rh - 1.676331F) +
           0.00391838F * std::pow(rh, 1.5F) * std::atan(0.023101F * rh) - 4.686035F;
}

float MetCalculator::wbgt_outdoor_c(float temp_c, float wet_bulb_c, float solar_w_m2, float wind_m_s) {
    // ISO 7243 outdoor Wet-Bulb Globe Temperature: WBGT = 0.7 * Tw + 0.2 * Tg + 0.1 * Ta
    const float v_eff = std::max(0.5F, wind_m_s);
    const float tg_c = temp_c + (0.014F * clamp_f(solar_w_m2, 0.0F, 1400.0F)) / std::pow(v_eff, 0.45F);
    return 0.7F * wet_bulb_c + 0.2F * tg_c + 0.1F * temp_c;
}

float MetCalculator::vapor_pressure_deficit_kpa(float temp_c, float rh_percent) {
    const float rh = clamp_f(rh_percent, 0.0F, 100.0F);
    const float es_kpa = 0.61078F * std::exp((17.27F * temp_c) / (temp_c + 237.3F));
    return std::max(0.0F, es_kpa * (1.0F - rh / 100.0F));
}

float MetCalculator::cloud_base_lcl_m(float temp_c, float dew_point_c) {
    return clamp_f(124.7F * std::max(0.0F, temp_c - dew_point_c), 0.0F, 12000.0F);
}

float MetCalculator::estimate_cloud_cover_pct(float solar_w_m2, float uv_index, float clear_sky_ref_w_m2) {
    // Combines Kasten-Czeplak clear-sky transmittance (Kc = I_solar / I_clear) with
    // the UV-to-Visible spectral scattering ratio: clouds scatter visible light (Mie) much
    // more strongly than Rayleigh diffuse UV-A/B, elevating (UVI * 105 / I_solar) > 1.15 under cloud cover.
    if (solar_w_m2 < 15.0F || clear_sky_ref_w_m2 < 50.0F) {
        return 0.0F;
    }
    const float kc = clamp_f(solar_w_m2 / clear_sky_ref_w_m2, 0.05F, 1.0F);
    const float attenuation = clamp_f((1.0F - kc) / 0.75F, 0.0F, 1.0F);
    const float n_kc_pct = 100.0F * std::pow(attenuation, 1.0F / 3.4F);

    // Spectral UV/Vis ratio boost: in clear direct sun UVI * 105 / I_solar ~ 0.90..1.05;
    // under overcast clouds diffuse UV passes through while direct visible drops -> ratio ~ 1.3..1.9
    const float uv_vis_ratio = (uv_index * 105.0F) / std::max(20.0F, solar_w_m2);
    const float diffuse_boost_pct = clamp_f((uv_vis_ratio - 1.05F) * 45.0F, -10.0F, 25.0F);
    return clamp_f(n_kc_pct + diffuse_boost_pct, 0.0F, 100.0F);
}

float MetCalculator::wind_chill_c(float temp_c, float wind_m_s) {
    const float v_kmh = wind_m_s * 3.6F;
    if (temp_c > 10.0F || v_kmh <= 4.8F) {
        return temp_c;
    }
    const float v_pow = std::pow(v_kmh, 0.16F);
    return 13.12F + 0.6215F * temp_c - 11.37F * v_pow + 0.3965F * temp_c * v_pow;
}

float MetCalculator::heat_index_c(float temp_c, float rh_percent) {
    if (temp_c < 26.7F || rh_percent < 40.0F) {
        return temp_c;
    }
    const float tf = temp_c * 1.8F + 32.0F;
    const float r = clamp_f(rh_percent, 0.0F, 100.0F);
    const float hi_f = -42.379F + 2.04901523F * tf + 10.14333127F * r -
                       0.22475541F * tf * r - 0.00683783F * tf * tf -
                       0.05481717F * r * r + 0.00122874F * tf * tf * r +
                       0.00085282F * tf * r * r - 0.00000199F * tf * tf * r * r;
    return (hi_f - 32.0F) / 1.8F;
}

float MetCalculator::feels_like_c(float temp_c, float rh_percent, float wind_m_s, float solar_w_m2) {
    if (temp_c <= 10.0F && wind_m_s * 3.6F > 4.8F) {
        return wind_chill_c(temp_c, wind_m_s);
    }
    if (temp_c >= 26.7F && rh_percent >= 40.0F && solar_w_m2 < 100.0F) {
        return heat_index_c(temp_c, rh_percent);
    }
    const float rh = clamp_f(rh_percent, 1.0F, 100.0F);
    const float e_hpa = (rh / 100.0F) * 6.105F * std::exp((17.27F * temp_c) / (237.7F + temp_c));
    const float q_solar = clamp_f(solar_w_m2, 0.0F, 1500.0F) * 0.0045F;
    return temp_c + 0.348F * e_hpa - 0.70F * std::max(0.0F, wind_m_s) + q_solar - 4.25F;
}

float MetCalculator::sea_level_pressure_hpa(float station_hpa, float temp_c, float altitude_m) {
    if (altitude_m <= 0.0F) {
        return station_hpa;
    }
    const float t_k = temp_c + 273.15F;
    const float exponent = 0.0341626F * altitude_m / (t_k + 0.0065F * altitude_m * 0.5F);
    return station_hpa * std::exp(exponent);
}

float MetCalculator::lux_from_ltr390_als(std::uint32_t als_counts, float gain, float integration_ms) {
    // Lite-On LTR-390UV-01 datasheet Section 6.1: Lux = (0.6 * ALS_DATA) / (Gain * (INT_ms / 100))
    const float denom = std::max(0.05F, gain * (integration_ms / 100.0F));
    return std::max(0.0F, (0.6F * static_cast<float>(als_counts)) / denom);
}

float MetCalculator::fuse_uv_hardened_lux(float veml7700_lux, float ltr390_als_lux) {
    // VEML7700 uses clear epoxy molding compound which can photo-yellow over multi-year UV exposure
    // under a JGS1 quartz dome, whereas LTR-390UV-01 uses a UV-stable optical window with a built-in
    // 20-bit ALS channel (0.01..157k lx). If LTR-390 ALS is present and VEML7700 reads >12% lower
    // due to epoxy UV aging, trust the UV-hardened LTR-390UV-01 ALS channel.
    if (ltr390_als_lux <= 0.0F) {
        return std::max(0.0F, veml7700_lux);
    }
    if (veml7700_lux < 0.88F * ltr390_als_lux) {
        return ltr390_als_lux;
    }
    return 0.5F * (veml7700_lux + ltr390_als_lux);
}

float MetCalculator::solar_irradiance_w_m2(float ambient_lux) {
    return std::max(0.0F, ambient_lux * 0.0079F);
}

float MetCalculator::uv_index_from_ltr390(std::uint32_t uvs_counts, float gain_factor, float integration_factor) {
    const float sensitivity = 2300.0F * (gain_factor / 18.0F) * integration_factor;
    if (sensitivity <= 1.0F) {
        return 0.0F;
    }
    return clamp_f(static_cast<float>(uvs_counts) / sensitivity, 0.0F, 20.0F);
}

float MetCalculator::absolute_humidity_g_m3(float temp_c, float rh_percent) {
    const float rh = clamp_f(rh_percent, 0.0F, 100.0F);
    const float es_hpa = 6.112F * std::exp((17.67F * temp_c) / (temp_c + 243.5F));
    return (es_hpa * rh * 2.1674F) / (273.15F + temp_c);
}

PressureTrend MetCalculator::classify_pressure_trend(float delta_3h_hpa) {
    if (delta_3h_hpa <= -3.0F) {
        return PressureTrend::FallingRapidly;
    }
    if (delta_3h_hpa <= -1.1F) {
        return PressureTrend::Falling;
    }
    if (delta_3h_hpa >= 3.0F) {
        return PressureTrend::RisingRapidly;
    }
    if (delta_3h_hpa >= 1.1F) {
        return PressureTrend::Rising;
    }
    return PressureTrend::Steady;
}

const char* MetCalculator::pressure_trend_name(PressureTrend trend) {
    switch (trend) {
        case PressureTrend::FallingRapidly:
            return "Falling Rapidly";
        case PressureTrend::Falling:
            return "Falling";
        case PressureTrend::Steady:
            return "Steady";
        case PressureTrend::Rising:
            return "Rising";
        case PressureTrend::RisingRapidly:
            return "Rising Rapidly";
    }
    return "Steady";
}

const char* MetCalculator::zambretti_forecast(float qnh_hpa, PressureTrend trend, float wind_dir_deg) {
    // Wind-direction barometric adjustment (southerly winds lower effective Zambretti pressure by 2 hPa)
    float p = clamp_f(qnh_hpa, 950.0F, 1050.0F);
    if (wind_dir_deg >= 135.0F && wind_dir_deg <= 225.0F) {
        p -= 2.0F;
    } else if (wind_dir_deg >= 315.0F || wind_dir_deg <= 45.0F) {
        p += 2.0F;
    }
    if (trend == PressureTrend::FallingRapidly || trend == PressureTrend::Falling) {
        if (p >= 1022.0F) return "Fairly Fine, Showery Later";
        if (p >= 1009.0F) return "Showery, Becoming Unsettled";
        if (p >= 996.0F) return "Unsettled, Rain at Times";
        return "Stormy, Heavy Rain & Gusts";
    }
    if (trend == PressureTrend::Rising || trend == PressureTrend::RisingRapidly) {
        if (p >= 1022.0F) return "Settled Fine";
        if (p >= 1008.0F) return "Fine, Improving";
        if (p >= 995.0F) return "Clearing, Cooler Breeze";
        return "Unsettled, Short Fine Intervals";
    }
    if (p >= 1022.0F) return "Settled Fine";
    if (p >= 1012.0F) return "Fine Weather";
    if (p >= 1000.0F) return "Changeable, Some Showers";
    return "Much Rain, Very Unsettled";
}

DerivedEnvironment MetCalculator::compute(const AtmosphereReading& in) {
    DerivedEnvironment out{};
    const float ltr_lux = (in.raw_als_counts > 0U) ? lux_from_ltr390_als(in.raw_als_counts) : 0.0F;
    const float true_lux = fuse_uv_hardened_lux(in.ambient_lux, ltr_lux);
    out.solar_irradiance_w_m2 = solar_irradiance_w_m2(true_lux);
    out.uv_index = uv_index_from_ltr390(in.raw_uv_counts);
    out.cloud_cover_pct = estimate_cloud_cover_pct(out.solar_irradiance_w_m2, out.uv_index);
    out.solar_heating_offset_c = solar_radiation_heating_error_c(out.solar_irradiance_w_m2, in.wind_speed_m_s);
    out.compensated_temp_c = in.temperature_c - out.solar_heating_offset_c;
    out.dew_point_c = dew_point_c(out.compensated_temp_c, in.humidity_rh);
    out.wet_bulb_c = wet_bulb_c(out.compensated_temp_c, in.humidity_rh);
    out.wbgt_c = wbgt_outdoor_c(out.compensated_temp_c, out.wet_bulb_c, out.solar_irradiance_w_m2, in.wind_speed_m_s);
    out.vpd_kpa = vapor_pressure_deficit_kpa(out.compensated_temp_c, in.humidity_rh);
    out.cloud_base_m = cloud_base_lcl_m(out.compensated_temp_c, out.dew_point_c);
    out.wind_chill_c = wind_chill_c(out.compensated_temp_c, in.wind_speed_m_s);
    out.heat_index_c = heat_index_c(out.compensated_temp_c, in.humidity_rh);
    out.feels_like_c = feels_like_c(out.compensated_temp_c, in.humidity_rh, in.wind_speed_m_s, out.solar_irradiance_w_m2);
    out.static_pressure_hpa = compensate_wind_dynamic_pressure_hpa(in.station_pressure_hpa, in.wind_speed_m_s, out.compensated_temp_c);
    out.dynamic_pressure_offset_hpa = out.static_pressure_hpa - in.station_pressure_hpa;
    out.sea_level_pressure_hpa = sea_level_pressure_hpa(out.static_pressure_hpa, out.compensated_temp_c, in.altitude_m);
    out.pressure_3h_delta_hpa = in.pressure_3h_delta_hpa;
    out.pressure_trend = classify_pressure_trend(in.pressure_3h_delta_hpa);
    out.zambretti_forecast = zambretti_forecast(out.sea_level_pressure_hpa, out.pressure_trend, in.wind_dir_deg);
    out.absolute_humidity_g_m3 = absolute_humidity_g_m3(out.compensated_temp_c, in.humidity_rh);
    out.frost_risk = (out.compensated_temp_c <= 3.0F && out.dew_point_c <= 0.5F);
    return out;
}

void BarographTracker::push_qnh(float qnh_hpa) {
    latest_ = qnh_hpa;
    history_[head_] = qnh_hpa;
    head_ = (head_ + 1) % kSlots;
    if (count_ < kSlots) {
        ++count_;
    }
}

float BarographTracker::delta_3h_hpa() const {
    if (count_ == 0) {
        return 0.0F;
    }
    const std::size_t oldest_idx = (count_ < kSlots) ? 0U : head_;
    return latest_ - history_[oldest_idx];
}

LongevityStatus EnvLongevityManager::tick_second(float ambient_temp_c, float rh_percent, bool minute_boundary) {
    status_.sht45_bakeout_pulse = false;

    if (ambient_temp_c < kScd41MinOperatingTempC) {
        status_.scd41_extreme_cold_pause = true;
    } else if (ambient_temp_c >= kScd41ResumeTempC) {
        status_.scd41_extreme_cold_pause = false;
    }

    if (status_.cooldown_seconds > 0) {
        --status_.cooldown_seconds;
        status_.sht45_reading_locked = (status_.cooldown_seconds > 0);
        return status_;
    }
    status_.sht45_reading_locked = false;

    if (minute_boundary) {
        if (rh_percent >= kCondensationRhThreshold && ambient_temp_c > -25.0F) {
            ++status_.high_rh_minutes;
            if (status_.high_rh_minutes >= kBakeoutTriggerMinutes) {
                status_.high_rh_minutes = 0;
                status_.sht45_bakeout_pulse = true;
                status_.sht45_reading_locked = true;
                status_.cooldown_seconds = kPostBakeoutCooldownS;
            }
        } else {
            status_.high_rh_minutes = 0;
        }
    }
    return status_;
}

}  // namespace environment
}  // namespace station
