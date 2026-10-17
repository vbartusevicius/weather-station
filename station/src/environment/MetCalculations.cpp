#include "environment/MetCalculations.hpp"

#include <algorithm>
#include <cmath>

namespace station::environment {

float MetCalculator::solar_radiation_heating_error_c(float solar_w_m2, float wind_m_s) {
    // WMO / van der Meulen empirical passive radiation-shield heating model:
    // ΔT_rad = k_rad * I_solar / max(v_wind, 0.25)^0.65
    if (solar_w_m2 <= 10.0F) {
        return 0.0F;
    }
    const float v_eff = std::max(0.25F, wind_m_s);
    const float dt = (0.0006F * std::clamp(solar_w_m2, 0.0F, 1500.0F)) / std::pow(v_eff, 0.65F);
    return std::clamp(dt, 0.0F, 2.5F);
}

float MetCalculator::compensate_wind_dynamic_pressure_hpa(float raw_pressure_hpa, float wind_m_s, float temp_c) {
    // Bernoulli dynamic wind suction compensation across the quad-vented cylindrical base (Cp = 0.22):
    // P_static = P_measured + Cp * 0.5 * rho * v^2
    if (wind_m_s <= 0.5F) {
        return raw_pressure_hpa;
    }
    const float t_k = std::max(200.0F, temp_c + 273.15F);
    const float rho = (raw_pressure_hpa * 100.0F) / (287.058F * t_k);
    const float dyn_hpa = 0.22F * 0.5F * rho * wind_m_s * wind_m_s * 0.01F;
    return raw_pressure_hpa + dyn_hpa;
}

float MetCalculator::dew_point_c(float temp_c, float rh_percent) {
    const float rh = std::clamp(rh_percent, 1.0F, 100.0F);
    constexpr float kA = 17.625F;
    constexpr float kB = 243.04F;
    const float gamma = std::log(rh / 100.0F) + (kA * temp_c) / (kB + temp_c);
    return (kB * gamma) / (kA - gamma);
}

float MetCalculator::wet_bulb_c(float temp_c, float rh_percent) {
    const float rh = std::clamp(rh_percent, 1.0F, 100.0F);
    return temp_c * std::atan(0.151977F * std::sqrt(rh + 8.313659F)) +
           std::atan(temp_c + rh) - std::atan(rh - 1.676331F) +
           0.00391838F * std::pow(rh, 1.5F) * std::atan(0.023101F * rh) - 4.686035F;
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
    const float r = std::clamp(rh_percent, 0.0F, 100.0F);
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
    const float rh = std::clamp(rh_percent, 1.0F, 100.0F);
    const float e_hpa = (rh / 100.0F) * 6.105F * std::exp((17.27F * temp_c) / (237.7F + temp_c));
    const float q_solar = std::clamp(solar_w_m2, 0.0F, 1500.0F) * 0.0045F;
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

float MetCalculator::solar_irradiance_w_m2(float ambient_lux) {
    return std::max(0.0F, ambient_lux * 0.0079F);
}

float MetCalculator::uv_index_from_ltr390(std::uint32_t uvs_counts, float gain_factor, float integration_factor) {
    const float sensitivity = 2300.0F * (gain_factor / 18.0F) * integration_factor;
    if (sensitivity <= 1.0F) {
        return 0.0F;
    }
    return std::clamp(static_cast<float>(uvs_counts) / sensitivity, 0.0F, 20.0F);
}

float MetCalculator::absolute_humidity_g_m3(float temp_c, float rh_percent) {
    const float rh = std::clamp(rh_percent, 0.0F, 100.0F);
    const float es_hpa = 6.112F * std::exp((17.67F * temp_c) / (temp_c + 243.5F));
    return (es_hpa * rh * 2.1674F) / (273.15F + temp_c);
}

DerivedEnvironment MetCalculator::compute(const AtmosphereReading& in) {
    DerivedEnvironment out{};
    out.solar_irradiance_w_m2 = solar_irradiance_w_m2(in.ambient_lux);
    out.uv_index = uv_index_from_ltr390(in.raw_uv_counts);
    out.solar_heating_offset_c = solar_radiation_heating_error_c(out.solar_irradiance_w_m2, in.wind_speed_m_s);
    out.compensated_temp_c = in.temperature_c - out.solar_heating_offset_c;
    out.dew_point_c = dew_point_c(out.compensated_temp_c, in.humidity_rh);
    out.wet_bulb_c = wet_bulb_c(out.compensated_temp_c, in.humidity_rh);
    out.wind_chill_c = wind_chill_c(out.compensated_temp_c, in.wind_speed_m_s);
    out.heat_index_c = heat_index_c(out.compensated_temp_c, in.humidity_rh);
    out.feels_like_c = feels_like_c(out.compensated_temp_c, in.humidity_rh, in.wind_speed_m_s, out.solar_irradiance_w_m2);
    out.static_pressure_hpa = compensate_wind_dynamic_pressure_hpa(in.station_pressure_hpa, in.wind_speed_m_s, out.compensated_temp_c);
    out.sea_level_pressure_hpa = sea_level_pressure_hpa(out.static_pressure_hpa, out.compensated_temp_c, in.altitude_m);
    out.absolute_humidity_g_m3 = absolute_humidity_g_m3(out.compensated_temp_c, in.humidity_rh);
    return out;
}

LongevityStatus EnvLongevityManager::tick_second(float ambient_temp_c, float rh_percent, bool minute_boundary) {
    status_.sht45_bakeout_pulse = false;

    // 1. SCD41 extreme-cold (-35 C) photoacoustic transducer protection with 2 C hysteresis
    if (ambient_temp_c < kScd41MinOperatingTempC) {
        status_.scd41_extreme_cold_pause = true;
    } else if (ambient_temp_c >= kScd41ResumeTempC) {
        status_.scd41_extreme_cold_pause = false;
    }

    // 2. Post-bakeout thermal recovery cooldown (15 s lockout so self-heating never corrupts ambient T/RH)
    if (status_.cooldown_seconds > 0) {
        --status_.cooldown_seconds;
        status_.sht45_reading_locked = (status_.cooldown_seconds > 0);
        return status_;
    }
    status_.sht45_reading_locked = false;

    // 3. SHT45 polymer anti-condensation bake-out after 30 minutes at >= 96% RH (freezing fog / rain)
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

}  // namespace station::environment
