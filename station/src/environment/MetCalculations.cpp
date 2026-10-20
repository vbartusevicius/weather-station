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

float MetCalculator::compensate_wind_dynamic_pressure_hpa(float raw_pressure_hpa, float wind_m_s, float temp_c, float port_coef) {
    if (wind_m_s <= 0.5F) {
        return raw_pressure_hpa;
    }
    const float t_k = std::max(200.0F, temp_c + 273.15F);
    const float rho = (raw_pressure_hpa * 100.0F) / (287.058F * t_k);
    const float dyn_hpa = port_coef * 0.5F * rho * wind_m_s * wind_m_s * 0.01F;
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

float MetCalculator::estimate_cloud_cover_pct(float solar_w_m2, float clear_sky_w_m2) {
    // Kasten & Czeplak (1980): G / G_clear = 1 - 0.75 (N/8)^3.4  =>  N/8 = ((1 - kc) / 0.75)^(1/3.4)
    if (clear_sky_w_m2 < 50.0F) {
        return 0.0F;
    }
    const float kc = clamp_f(solar_w_m2 / clear_sky_w_m2, 0.0F, 1.0F);
    const float attenuation = clamp_f((1.0F - kc) / 0.75F, 0.0F, 1.0F);
    return 100.0F * std::pow(attenuation, 1.0F / 3.4F);
}

float MetCalculator::sky_quality_mpsas(float ambient_lux) {
    // Uniform-sky approximation: L[cd/m^2] = 1.08e5 * 10^(-0.4 m) and E = pi * L
    //   => m = -2.5 log10(E) + 2.5 log10(pi * 1.08e5) = 13.82 - 2.5 log10(E).
    // An estimate only: the dome sees the whole hemisphere incl. horizon light pollution.
    const float lx = clamp_f(ambient_lux, 0.00018F, 10.0F);
    return clamp_f(13.82F - 2.5F * std::log10(lx), 12.0F, 22.5F);
}

std::uint8_t MetCalculator::bortle_class_from_sqm(float sqm_mpsas) {
    if (sqm_mpsas >= 21.75F) return 1;
    if (sqm_mpsas >= 21.50F) return 2;
    if (sqm_mpsas >= 21.20F) return 3;
    if (sqm_mpsas >= 20.40F) return 4;
    if (sqm_mpsas >= 19.50F) return 5;
    if (sqm_mpsas >= 18.90F) return 6;
    if (sqm_mpsas >= 18.30F) return 7;
    if (sqm_mpsas >= 17.50F) return 8;
    return 9;
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

float MetCalculator::feels_like_c(float temp_c, float rh_percent, float wind_m_s) {
    // Wind chill (Environment Canada / NWS) in the cold, NWS heat index in the heat, otherwise the
    // Steadman / Australian BoM shade apparent temperature AT = Ta + 0.33 e - 0.70 ws - 4.00.
    if (temp_c <= 10.0F && wind_m_s * 3.6F > 4.8F) {
        return wind_chill_c(temp_c, wind_m_s);
    }
    if (temp_c >= 26.7F && rh_percent >= 40.0F) {
        return heat_index_c(temp_c, rh_percent);
    }
    const float rh = clamp_f(rh_percent, 1.0F, 100.0F);
    const float e_hpa = (rh / 100.0F) * 6.105F * std::exp((17.27F * temp_c) / (237.7F + temp_c));
    return temp_c + 0.33F * e_hpa - 0.70F * std::max(0.0F, wind_m_s) - 4.00F;
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

float MetCalculator::fuse_uv_hardened_lux(float tsl25911_lux, float ltr390_als_lux) {
    // TSL25911FN resolves down to 0.00018 lx (nighttime starlight/moonlight) up to 88,000 lx,
    // while LTR-390UV-01's ALS channel covers high-noon sunlight up to 157,000 lx when TSL25911FN saturates.
    if (ltr390_als_lux <= 0.0F) {
        return std::max(0.0F, tsl25911_lux);
    }
    if (tsl25911_lux >= 80000.0F || tsl25911_lux < 0.88F * ltr390_als_lux) {
        return ltr390_als_lux;
    }
    return 0.5F * (tsl25911_lux + ltr390_als_lux);
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

float MetCalculator::moist_air_density_kg_m3(float static_pressure_hpa, float temp_c, float rh_percent) {
    // CIPM-2007 / ideal-gas moist air density: rho = (p_d / (R_d * T)) + (e / (R_v * T))
    const float tk = temp_c + 273.15F;
    const float es_hpa = 6.1078F * std::exp((17.27F * temp_c) / (temp_c + 237.3F));
    const float e_pa = clamp_f(rh_percent, 0.0F, 100.0F) * 0.01F * es_hpa * 100.0F;
    const float pd_pa = std::max(0.0F, static_pressure_hpa * 100.0F - e_pa);
    return clamp_f((pd_pa / (287.058F * tk)) + (e_pa / (461.495F * tk)), 0.80F, 1.50F);
}

float MetCalculator::thunder_distance_km(float thunder_delay_s, float sound_speed_m_s) {
    // Flash (AS3935, speed of light) to thunder onset (BMP581) delay x the speed of sound measured
    // by the ultrasonic anemometer. Wind along the unknown propagation path is not corrected.
    if (thunder_delay_s <= 0.05F || thunder_delay_s > 60.0F) {
        return 0.0F;
    }
    return clamp_f(sound_speed_m_s, 315.0F, 365.0F) * thunder_delay_s * 0.001F;
}

const char* MetCalculator::classify_vibration_source(float pga_mg, float dominant_hz, float gust_m_s) {
    if (pga_mg < 1.5F) {
        return "Ambient";
    }
    if (dominant_hz < 8.0F && gust_m_s >= 8.0F) {
        return "Wind Sway";
    }
    if (dominant_hz >= 10.0F && dominant_hz <= 60.0F && pga_mg < 15.0F) {
        return "Heavy Truck / Train";
    }
    if (dominant_hz < 10.0F) {
        return pga_mg >= 14.0F ? "Earthquake" : "Minor Tremor";
    }
    return "Machinery / Impact";
}

const char* MetCalculator::mmi_from_pga(float pga_mg) {
    // Wald et al. (1999) PGA bands (1 mg = 0.1 %g)
    if (pga_mg < 1.7F) return "MMI I";
    if (pga_mg < 14.0F) return "MMI II-III";
    if (pga_mg < 39.0F) return "MMI IV";
    if (pga_mg < 92.0F) return "MMI V";
    if (pga_mg < 180.0F) return "MMI VI";
    return "MMI VII+";
}

const char* MetCalculator::sager_front_type(
    float sea_level_hpa, float synoptic_3h_delta_hpa, float wind_dir_3h_delta_deg, float cloud_cover_pct) {
    if (synoptic_3h_delta_hpa <= -2.5F && wind_dir_3h_delta_deg < -10.0F) {
        return "Squall / Cold Front";
    }
    if (synoptic_3h_delta_hpa <= -1.2F && cloud_cover_pct >= 65.0F) {
        return "Warm Front Approaching";
    }
    if (synoptic_3h_delta_hpa >= 1.5F && wind_dir_3h_delta_deg > 10.0F) {
        return "Cold Front Clearing";
    }
    if (sea_level_hpa >= 1018.0F && std::fabs(synoptic_3h_delta_hpa) < 1.2F) {
        return "High-Pressure Ridge";
    }
    if (sea_level_hpa < 1004.0F) {
        return "Low-Pressure Trough";
    }
    return "Steady Air Mass";
}

std::uint8_t MetCalculator::sager_rain_probability_pct(
    float sea_level_hpa, float synoptic_3h_delta_hpa, float wind_dir_3h_delta_deg, float cloud_cover_pct, bool raining) {
    if (raining) {
        return 96U;
    }
    float p = clamp_f((1018.0F - sea_level_hpa) * 2.2F, 4.0F, 65.0F);
    if (synoptic_3h_delta_hpa < -0.5F) {
        p += std::min(32.0F, -synoptic_3h_delta_hpa * 7.5F);
    } else if (synoptic_3h_delta_hpa > 0.8F) {
        p -= std::min(25.0F, synoptic_3h_delta_hpa * 5.0F);
    }
    if (wind_dir_3h_delta_deg < -10.0F) {
        p += 12.0F;
    } else if (wind_dir_3h_delta_deg > 12.0F) {
        p -= 10.0F;
    }
    p += (cloud_cover_pct - 40.0F) * 0.25F;
    return static_cast<std::uint8_t>(clamp_f(p, 5.0F, 95.0F));
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

float DomeOpticalScheduler::gain_multiplier(Tsl2591Gain g) {
    switch (g) {
        case Tsl2591Gain::Low1x:
            return 1.0F;
        case Tsl2591Gain::Med25x:
            return 25.0F;
        case Tsl2591Gain::High428x:
            return 428.0F;
        case Tsl2591Gain::Max9876x:
            return 9876.0F;
    }
    return 25.0F;
}

DomeOpticalCommand DomeOpticalScheduler::tick_500ms(std::uint16_t tsl_ch0_counts) {
    DomeOpticalCommand cmd{};
    if (in_uvs_phase_) {
        // UVS 100 ms integration just finished: read UVS_DATA (0x10..0x12) and switch U1 to ALS mode (0x02)
        cmd.read_ltr390_uvs = true;
        cmd.ltr390_main_ctrl_reg = 0x02U;
        in_uvs_phase_ = false;
    } else {
        // ALS 100 ms integration just finished: read ALS_DATA (0x0D..0x0F) and switch U1 to UVS mode (0x0A)
        cmd.read_ltr390_als = true;
        cmd.ltr390_main_ctrl_reg = 0x0AU;
        in_uvs_phase_ = true;
    }

    // Auto-range U2 (TSL25911FN) across 1x / 25x / 428x / 9876x so it never saturates in day and resolves 188 uLux at night
    Tsl2591Gain next = gain_;
    if (tsl_ch0_counts > 52000U) {
        if (gain_ == Tsl2591Gain::Max9876x) next = Tsl2591Gain::High428x;
        else if (gain_ == Tsl2591Gain::High428x) next = Tsl2591Gain::Med25x;
        else if (gain_ == Tsl2591Gain::Med25x) next = Tsl2591Gain::Low1x;
    } else if (tsl_ch0_counts < 450U) {
        if (gain_ == Tsl2591Gain::Low1x) next = Tsl2591Gain::Med25x;
        else if (gain_ == Tsl2591Gain::Med25x) next = Tsl2591Gain::High428x;
        else if (gain_ == Tsl2591Gain::High428x) next = Tsl2591Gain::Max9876x;
    }
    cmd.tsl2591_gain_changed = (next != gain_);
    cmd.tsl2591_next_gain = next;
    gain_ = next;
    return cmd;
}

}  // namespace environment
}  // namespace station
