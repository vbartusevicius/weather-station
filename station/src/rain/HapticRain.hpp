#pragma once

#include <cstdint>

namespace station::rain {

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
};

struct RainMetrics {
    PrecipitationType state{PrecipitationType::None};
    bool is_raining{false};
    bool surface_wet{false};
    float rate_mm_h{0.0F};
    float event_mm{0.0F};
    float hourly_mm{0.0F};
    float daily_mm{0.0F};
    float total_mm{0.0F};
    std::uint32_t drop_count{0};
    std::uint32_t hail_hits{0};
    std::uint32_t rejected_vibrations{0};
};

class HapticRainProcessor {
public:
    static constexpr float kCollectionAreaMm2 = 6361.7F;
    static constexpr std::uint16_t kDropThresholdMv = 165;
    static constexpr std::uint16_t kWetSurfaceBaselineMv = 140;
    static constexpr float kMaxMastShakeG = 0.18F;
    static constexpr float kNominalTerminalVelMs = 6.5F;

    bool process_impact(const RainImpactEvent& ev);
    void tick_minute();
    void reset_daily();
    const RainMetrics& metrics() const { return metrics_; }

private:
    RainMetrics metrics_{};
    float window_minute_mm_{0.0F};
    std::uint16_t dry_minutes_{0};
};

const char* precipitation_type_name(PrecipitationType type);

}  // namespace station::rain
