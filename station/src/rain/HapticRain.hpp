#pragma once

#include <cstdint>

namespace station {
namespace rain {

enum class PrecipitationType : std::uint8_t {
    None = 0,
    Drizzle = 1,
    LightRain = 2,
    ModerateRain = 3,
    HeavyRain = 4,
    ViolentRain = 5,
    Hail = 6,
};

struct RainImpactEvent {
    std::uint16_t peak_mv{0};
    std::uint16_t baseline_mv{206};  // 206 mV resting DC bias from 100k pull-up on RAIN_ADC
    std::uint16_t pulse_width_us{200};
    float imu_vibration_g{0.0F};
    float wind_speed_m_s{0.0F};      // Ultrasonic wind speed for oblique-impact kinetic energy normalization
    float dome_temp_c{20.0F};        // PZT ceramic temperature sensitivity drift compensation (-0.18% / C)
};

struct RainMetrics {
    PrecipitationType state{PrecipitationType::None};
    bool is_raining{false};
    bool surface_wet{false};
    float rate_mm_h{0.0F};        // rolling 60 s accumulation x 60
    float hail_rate_mm_h{0.0F};   // rolling 60 s hail liquid-water equivalent x 60
    float event_mm{0.0F};         // current shower; resets after 15 dry minutes
    float daily_mm{0.0F};         // since local midnight or manual reset
    float total_mm{0.0F};
    std::uint32_t drop_count{0};
    std::uint32_t hail_hits{0};
    float max_hail_diameter_mm{0.0F};
    std::uint32_t rejected_vibrations{0};
};

class HapticRainProcessor {
public:
    static constexpr float kCollectionAreaMm2 = 6361.7F;
    static constexpr std::uint16_t kDropThresholdMv = 165;
    static constexpr std::uint16_t kWetSurfaceBaselineMv = 140;
    static constexpr float kMaxMastShakeG = 0.18F;
    static constexpr float kNominalTerminalVelMs = 6.5F;
    static constexpr std::uint16_t kDryMinutesToEndEvent = 15;

    static float estimate_hail_diameter_mm(int net_mv, std::uint16_t pulse_width_us);
    // Returns the liquid depth (mm) attributed to the impact, 0 when rejected.
    float process_impact(const RainImpactEvent& ev, float gain = 1.0F);
    // Once per second: closes the 1-s bucket, updates rolling rates and the precipitation state.
    // Returns the depth (mm) accumulated during the closing second.
    float tick_second();
    void new_day() { metrics_.daily_mm = 0.0F; metrics_.hail_hits = 0; metrics_.max_hail_diameter_mm = 0.0F; }
    void reset_daily() { new_day(); }
    float last_second_hail_mm() const { return last_hail_mm_; }
    const RainMetrics& metrics() const { return metrics_; }

private:
    RainMetrics metrics_{};
    float sec_mm_{0.0F};
    float sec_hail_mm_{0.0F};
    float last_hail_mm_{0.0F};
    float ring_mm_[60]{};
    float ring_hail_[60]{};
    std::uint8_t head_{0};
    std::uint16_t dry_seconds_{0};
    bool activity_wetness_{false};   // no capacitive baseline (crown v2): wet = rain event running
};

const char* precipitation_type_name(PrecipitationType type);

}  // namespace rain
}  // namespace station
