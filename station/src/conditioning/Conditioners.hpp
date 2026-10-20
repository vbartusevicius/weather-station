#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../core/RawState.hpp"

namespace station {
namespace conditioning {

// Accumulates ultrasonic samples (typ. 4 Hz) into a 1-s vector + scalar mean.
class WindSecondAccumulator {
public:
    void add(float v_east, float v_north, float speed, float sound_m_s, float virtual_temp_c);
    // Returns false when no valid sample arrived during the second.
    bool take(core::WindSecond& out);

private:
    float se_{0.0F}, sn_{0.0F}, ss_{0.0F}, sc_{0.0F}, st_{0.0F};
    std::uint8_t n_{0};
};

// BMP581 100 Hz pressure stream -> 1-s mean, 10-s ripple RMS and thunder impulse onsets.
class PressureAnalyzer {
public:
    static constexpr float kSampleHz = 100.0F;

    // Feed one pressure sample (Pa). Returns true if a thunder impulse onset was detected.
    bool add(float pressure_pa, std::uint32_t t_ms, core::ThunderArrival& onset);
    // 1-s summary; false when no samples.
    bool take_second(float& mean_hpa, float& ripple_pa);

private:
    double sum_pa_{0.0};
    std::uint16_t n_{0};
    float baseline_pa_{0.0F};   // slow (0.05 Hz) low-pass baseline
    bool init_{false};
    float rms_sq_{0.0F};        // ~10 s exponential mean of the high-passed signal squared
    std::uint32_t last_onset_ms_{0};
};

// LSM6DS3 200 Hz accelerometer -> gravity (tilt), and per-second vibration PGA / PPV / frequency.
class VibrationAnalyzer {
public:
    static constexpr float kSampleHz = 200.0F;

    void add(const core::Vec3& accel_g);
    bool take_second(core::Vec3& gravity_g, core::VibrationSecond& out);

private:
    core::Vec3 lp_{0.0F, 0.0F, 1.0F};
    bool init_{false};
    core::Vec3 vel_{};          // band-limited integrated velocity (m/s)
    core::Vec3 prev_hp_{};
    float pga_mg_{0.0F};
    float ppv_mm_s_{0.0F};
    std::array<std::uint16_t, 3> crossings_{};
    std::array<float, 3> energy_{};
    std::uint16_t n_{0};
};

// RAIN_ADC (~16 kHz) piezo envelope -> discrete impacts (peak, baseline, width).
class RainPulseDetector {
public:
    static constexpr float kSampleHz = 16000.0F;
    static constexpr std::uint16_t kTriggerMv = 60;   // above baseline to start a pulse

    // Returns true when a pulse just ended; `out` then holds the impact.
    bool add(std::uint16_t mv, std::uint32_t t_ms, core::RainImpact& out);
    std::uint16_t baseline_mv() const { return static_cast<std::uint16_t>(baseline_); }

private:
    float baseline_{206.0F};
    bool init_{false};
    bool in_pulse_{false};
    std::uint16_t peak_{0};
    std::uint32_t samples_{0};
    std::uint32_t start_ms_{0};
};

}  // namespace conditioning
}  // namespace station
