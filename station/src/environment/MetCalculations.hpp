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

struct AtmosphereReading {
    float temperature_c{20.0F};
    float humidity_rh{50.0F};
    float station_pressure_hpa{1013.25F};
    float altitude_m{50.0F};
    float wind_speed_m_s{0.0F};
    float wind_dir_deg{180.0F};
    float ambient_lux{0.0F};          // Primary visible illuminance from TSL25911FN (0.00018..88k lx)
    std::uint16_t tsl2591_ch0_full{0};// TSL25911FN Ch0 (Visible + Near-IR 400-1100 nm)
    std::uint16_t tsl2591_ch1_nir{0}; // TSL25911FN Ch1 (Near-Infrared 700-1100 nm)
    std::uint32_t raw_als_counts{0};  // LTR-390UV-01 ALS channel (covers 88k..157k lx bright noon sun)
    std::uint32_t raw_uv_counts{0};   // LTR-390UV-01 UVS channel (300-350 nm)
    std::uint16_t raw_guvb_counts{0}; // Optional GenUV GUVB-C31SM AlGaN UV-B channel (240-320 nm, 0 = not populated)
    float pressure_3h_delta_hpa{0.0F};
    float wind_dir_3h_delta_deg{0.0F};// + = clockwise veering (clearing), - = counter-clockwise backing (deteriorating)
    std::uint8_t local_hour{12};      // Local solar hour (0..23) for semidiurnal atmospheric tide removal
    float bmp581_fifo_rms_pa{0.09F};  // 50 Hz BMP581 FIFO sub-Hz pressure ripple RMS (Pa)
    float thunder_delay_s{0.0F};      // Time delta (s) from AS3935 EM flash interrupt to BMP581/Piezo acoustic shockwave (0 = none)
    float thunder_peak_pa{0.0F};      // Peak N-wave acoustic overpressure (Pa) captured by BMP581 at 240 Hz
    float measured_sound_m_s{343.0F}; // Live speed of sound (m/s) measured by the 40 kHz ultrasonic anemometer
    bool raining{false};
};

struct DerivedEnvironment {
    float compensated_temp_c{20.0F};
    float solar_heating_offset_c{0.0F};
    float dew_point_c{9.3F};
    float wet_bulb_c{13.7F};
    float wbgt_c{16.5F};
    float feels_like_c{20.0F};
    float heat_index_c{20.0F};
    float wind_chill_c{20.0F};
    float vpd_kpa{1.17F};
    float cloud_base_m{1334.0F};
    float static_pressure_hpa{1013.25F};
    float dynamic_pressure_offset_hpa{0.0F};
    float sea_level_pressure_hpa{1013.25F};
    float pressure_3h_delta_hpa{0.0F};
    float synoptic_3h_delta_hpa{0.0F};
    float solar_tide_offset_hpa{0.0F};
    float microbarom_wave_pa{0.08F};
    float air_density_kg_m3{1.225F};
    float thunder_distance_km{0.0F};
    bool thunder_acoustic_locked{false};
    std::uint8_t rain_prob_pct{10};
    PressureTrend pressure_trend{PressureTrend::Steady};
    const char* zambretti_forecast{"Settled Fine"};
    const char* front_type{"Steady Air Mass"};
    const char* wind_shift{"Steady Bearing"};
    float solar_irradiance_w_m2{0.0F};
    float visible_w_m2{0.0F};
    float nir_w_m2{0.0F};
    float uva_w_m2{0.0F};
    float uvb_w_m2{0.0F};
    float uv_index{0.0F};
    std::uint16_t burn_time_min{0};
    bool vitamin_d_active{false};
    bool is_night{false};
    float sqm_mag_arcsec2{21.5F};
    std::uint8_t bortle_class{2};
    float cloud_cover_pct{0.0F};
    float absolute_humidity_g_m3{8.6F};
    bool frost_risk{false};
};

class MetCalculator {
public:
    static float solar_radiation_heating_error_c(float solar_w_m2, float wind_m_s);
    static float compensate_wind_dynamic_pressure_hpa(float raw_pressure_hpa, float wind_m_s, float temp_c);
    static float dew_point_c(float temp_c, float rh_percent);
    static float wet_bulb_c(float temp_c, float rh_percent);
    static float wbgt_outdoor_c(float temp_c, float wet_bulb_c, float solar_w_m2, float wind_m_s);
    static float vapor_pressure_deficit_kpa(float temp_c, float rh_percent);
    static float cloud_base_lcl_m(float temp_c, float dew_point_c);
    static float estimate_cloud_cover_pct(float solar_w_m2, float uv_index, float clear_sky_ref_w_m2 = 850.0F);
    static float sky_quality_mpsas(float ambient_lux);
    static std::uint8_t bortle_class_from_sqm(float sqm_mpsas);
    static float wind_chill_c(float temp_c, float wind_m_s);
    static float heat_index_c(float temp_c, float rh_percent);
    static float feels_like_c(float temp_c, float rh_percent, float wind_m_s, float solar_w_m2);
    static float sea_level_pressure_hpa(float station_hpa, float temp_c, float altitude_m);
    static float lux_from_ltr390_als(std::uint32_t als_counts, float gain = 3.0F, float integration_ms = 100.0F);
    static float fuse_uv_hardened_lux(float tsl25911_lux, float ltr390_als_lux);
    static float solar_irradiance_w_m2(float ambient_lux, std::uint16_t tsl2591_ch1_nir = 0);
    static float uv_index_from_ltr390(std::uint32_t uvs_counts, float gain_factor = 18.0F, float integration_factor = 1.0F);
    static float fuse_uv_index(float ltr390_uvi, std::uint16_t guvb_c31sm_counts);
    static float absolute_humidity_g_m3(float temp_c, float rh_percent);
    static PressureTrend classify_pressure_trend(float delta_3h_hpa);
    static const char* pressure_trend_name(PressureTrend trend);
    static const char* zambretti_forecast(float qnh_hpa, PressureTrend trend, float wind_dir_deg = 180.0F);
    static float diurnal_solar_tide_3h_delta_hpa(std::uint8_t local_hour, float solar_w_m2);
    static float microbarom_gravity_wave_pa(float bmp581_fifo_rms_pa, float wind_m_s);
    static float moist_air_density_kg_m3(float static_pressure_hpa, float temp_c, float rh_percent);
    static float thunder_exact_distance_km(float thunder_delay_s, float measured_sound_m_s, float wind_m_s = 0.0F);
    static const char* sager_front_type(float sea_level_hpa, float synoptic_3h_delta_hpa, float wind_dir_3h_delta_deg, float cloud_cover_pct);
    static std::uint8_t sager_rain_probability_pct(float sea_level_hpa, float synoptic_3h_delta_hpa, float wind_dir_3h_delta_deg, float cloud_cover_pct, bool raining);
    static DerivedEnvironment compute(const AtmosphereReading& in);
};

class BarographTracker {
public:
    static constexpr std::size_t kSlots = 12;  // 12 x 15-min slots = 3 hours

    void push_qnh(float qnh_hpa);
    float delta_3h_hpa() const;

private:
    std::array<float, kSlots> history_{};
    std::size_t head_{0};
    std::size_t count_{0};
    float latest_{1013.25F};
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
