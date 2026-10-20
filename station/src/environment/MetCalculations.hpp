#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace station {
namespace environment {

enum class PressureTrend : std::uint8_t {
    FallingRapidly = 0,
    Falling = 1,
    Steady = 2,
    Rising = 3,
    RisingRapidly = 4,
};

class MetCalculator {
public:
    static float solar_radiation_heating_error_c(float solar_w_m2, float wind_m_s);
    static float compensate_wind_dynamic_pressure_hpa(float raw_pressure_hpa, float wind_m_s, float temp_c, float port_coef = 0.22F);
    static float dew_point_c(float temp_c, float rh_percent);
    static float wet_bulb_c(float temp_c, float rh_percent);
    static float wbgt_outdoor_c(float temp_c, float wet_bulb_c, float solar_w_m2, float wind_m_s);
    static float vapor_pressure_deficit_kpa(float temp_c, float rh_percent);
    static float cloud_base_lcl_m(float temp_c, float dew_point_c);
    static float estimate_cloud_cover_pct(float solar_w_m2, float clear_sky_w_m2);
    static float sky_quality_mpsas(float ambient_lux);
    static std::uint8_t bortle_class_from_sqm(float sqm_mpsas);
    static float wind_chill_c(float temp_c, float wind_m_s);
    static float heat_index_c(float temp_c, float rh_percent);
    static float feels_like_c(float temp_c, float rh_percent, float wind_m_s);
    static float sea_level_pressure_hpa(float station_hpa, float temp_c, float altitude_m);
    static float lux_from_ltr390_als(std::uint32_t als_counts, float gain = 3.0F, float integration_ms = 100.0F);
    static float fuse_uv_hardened_lux(float tsl25911_lux, float ltr390_als_lux);
    static float uv_index_from_ltr390(std::uint32_t uvs_counts, float gain_factor = 18.0F, float integration_factor = 1.0F);
    static float absolute_humidity_g_m3(float temp_c, float rh_percent);
    static PressureTrend classify_pressure_trend(float delta_3h_hpa);
    static const char* pressure_trend_name(PressureTrend trend);
    static const char* zambretti_forecast(float qnh_hpa, PressureTrend trend, float wind_dir_deg = 180.0F);
    static float moist_air_density_kg_m3(float static_pressure_hpa, float temp_c, float rh_percent);
    static float thunder_distance_km(float thunder_delay_s, float sound_speed_m_s);
    static const char* classify_vibration_source(float pga_mg, float dominant_hz, float gust_m_s);
    static const char* mmi_from_pga(float pga_mg);
    static const char* sager_front_type(float sea_level_hpa, float synoptic_3h_delta_hpa, float wind_dir_3h_delta_deg, float cloud_cover_pct);
    static std::uint8_t sager_rain_probability_pct(float sea_level_hpa, float synoptic_3h_delta_hpa, float wind_dir_3h_delta_deg, float cloud_cover_pct, bool raining);
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

enum class Tsl2591Gain : std::uint8_t {
    Low1x = 0x00,     // 1x gain (bright sunlight up to 88,000 lx)
    Med25x = 0x10,    // 25x gain (overcast / indoor / shade)
    High428x = 0x20,  // 428x gain (twilight / civil dusk)
    Max9876x = 0x30,  // 9876x gain (188 uLux night sky / moonlight / starlight)
};

struct DomeOpticalCommand {
    std::uint8_t ltr390_main_ctrl_reg{0x0A};  // 0x0A = UVS mode, 0x02 = ALS mode (alternates every 500 ms)
    bool read_ltr390_uvs{false};              // True on odd 500 ms ticks after UVS integration completes
    bool read_ltr390_als{false};              // True on even 500 ms ticks after ALS integration completes
    Tsl2591Gain tsl2591_next_gain{Tsl2591Gain::Med25x};
    bool tsl2591_gain_changed{false};
};

class DomeOpticalScheduler {
public:
    // Called every 500 ms (2 Hz): alternates U1 (LTR-390UV-01) between UVS and ALS mode
    // and auto-ranges U2 (TSL25911FN) across 9 orders of magnitude (188 uLux .. 88,000 lx).
    DomeOpticalCommand tick_500ms(std::uint16_t tsl_ch0_counts);
    Tsl2591Gain current_gain() const { return gain_; }
    static float gain_multiplier(Tsl2591Gain g);

private:
    bool in_uvs_phase_{true};
    Tsl2591Gain gain_{Tsl2591Gain::Med25x};
};

}  // namespace environment
}  // namespace station
