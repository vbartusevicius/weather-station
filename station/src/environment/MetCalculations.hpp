#pragma once

#include <cstdint>

namespace station::environment {

struct AtmosphereReading {
    float temperature_c{20.0F};
    float humidity_rh{50.0F};
    float station_pressure_hpa{1013.25F};
    float altitude_m{50.0F};
    float wind_speed_m_s{0.0F};
    float ambient_lux{0.0F};
    std::uint32_t raw_uv_counts{0};
};

struct DerivedEnvironment {
    float compensated_temp_c{20.0F};
    float solar_heating_offset_c{0.0F};
    float dew_point_c{9.3F};
    float wet_bulb_c{13.7F};
    float feels_like_c{20.0F};
    float heat_index_c{20.0F};
    float wind_chill_c{20.0F};
    float static_pressure_hpa{1013.25F};
    float sea_level_pressure_hpa{1013.25F};
    float solar_irradiance_w_m2{0.0F};
    float uv_index{0.0F};
    float absolute_humidity_g_m3{8.6F};
};

class MetCalculator {
public:
    static float solar_radiation_heating_error_c(float solar_w_m2, float wind_m_s);
    static float compensate_wind_dynamic_pressure_hpa(float raw_pressure_hpa, float wind_m_s, float temp_c);
    static float dew_point_c(float temp_c, float rh_percent);
    static float wet_bulb_c(float temp_c, float rh_percent);
    static float wind_chill_c(float temp_c, float wind_m_s);
    static float heat_index_c(float temp_c, float rh_percent);
    static float feels_like_c(float temp_c, float rh_percent, float wind_m_s, float solar_w_m2);
    static float sea_level_pressure_hpa(float station_hpa, float temp_c, float altitude_m);
    static float solar_irradiance_w_m2(float ambient_lux);
    static float uv_index_from_ltr390(std::uint32_t uvs_counts, float gain_factor = 18.0F, float integration_factor = 1.0F);
    static float absolute_humidity_g_m3(float temp_c, float rh_percent);
    static DerivedEnvironment compute(const AtmosphereReading& in);
};

struct LongevityStatus {
    bool sht45_bakeout_pulse{false};     // Trigger 1s 200 mW on-chip SHT45 heater to vaporize condensed fog
    bool sht45_reading_locked{false};    // Suppress SHT45 Temp/RH updates during post-heater thermal cooldown
    bool scd41_extreme_cold_pause{false};// Pause SCD41 photoacoustic pulsing below -10 C (protect down to -40 C)
    std::uint16_t high_rh_minutes{0};
    std::uint8_t cooldown_seconds{0};
};

class EnvLongevityManager {
public:
    static constexpr float kCondensationRhThreshold = 96.0F;
    static constexpr std::uint16_t kBakeoutTriggerMinutes = 30;
    static constexpr std::uint8_t kPostBakeoutCooldownS = 15;
    static constexpr float kScd41MinOperatingTempC = -10.0F;
    static constexpr float kScd41ResumeTempC = -8.0F;

    LongevityStatus tick_second(float ambient_temp_c, float rh_percent, bool minute_boundary);
    const LongevityStatus& status() const { return status_; }

private:
    LongevityStatus status_{};
};

}  // namespace station::environment
