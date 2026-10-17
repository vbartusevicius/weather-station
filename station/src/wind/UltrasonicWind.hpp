#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace station::wind {

struct TofQuad {
    // Fine zero-crossing timestamps from LM393 Comparator A (microseconds)
    float t_ns_us{321.0F};  // North -> South zero-crossing ToF
    float t_sn_us{321.0F};  // South -> North zero-crossing ToF
    float t_ew_us{321.0F};  // East  -> West  zero-crossing ToF
    float t_we_us{321.0F};  // West  -> East  zero-crossing ToF

    // Optional coarse envelope arrival timestamps from LM393 Comparator B (0 = same cycle as t_*_us)
    float env_ns_us{0.0F};
    float env_sn_us{0.0F};
    float env_ew_us{0.0F};
    float env_we_us{0.0F};
};

struct ZeroWindCalibration {
    float dt_ns_offset_us{0.0F};  // Residual t_NS - t_SN offset at v = 0 (microseconds)
    float dt_ew_offset_us{0.0F};  // Residual t_EW - t_WE offset at v = 0 (microseconds)
};

struct ImuCompassReading {
    float accel_x_g{0.0F};
    float accel_y_g{0.0F};
    float accel_z_g{1.0F};
    float mag_x_ut{0.0F};
    float mag_y_ut{20.0F};
    float mag_z_ut{-40.0F};
    float declination_deg{0.0F};
};

struct WindSample {
    bool valid{false};
    float speed_m_s{0.0F};
    float direction_deg{0.0F};
    float v_east_m_s{0.0F};
    float v_north_m_s{0.0F};
    float sound_speed_m_s{343.2F};
    float acoustic_temp_c{20.0F};
    float mast_tilt_deg{0.0F};
    float north_offset_deg{0.0F};
};

struct WmoWindSummary {
    float current_speed_m_s{0.0F};
    float current_dir_deg{0.0F};
    float avg_1min_m_s{0.0F};
    float avg_10min_m_s{0.0F};
    float gust_3s_m_s{0.0F};
    float lull_m_s{0.0F};
    float mean_dir_deg{0.0F};
    float beaufort_scale{0.0F};
};

class UltrasonicWindSolver {
public:
    // AeroMet V1 upgraded geometry: 4 transducers at r=27 mm (separation D=54 mm), reflector H=48 mm
    static constexpr float kTransducerSeparationM = 0.054F;
    static constexpr float kReflectorGapM = 0.048F;
    static constexpr float kCarrierPeriodUs = 25.0F;  // 40 kHz ultrasonic period (25.0 us)
    static constexpr float kMinDetectableSpeedMs = 0.05F;
    static constexpr float kMaxValidSpeedMs = 65.0F;

    static float resolve_cycle_slip(float zc_us, float env_us);
    static WindSample solve(const TofQuad& tof,
                            const ImuCompassReading& orientation = {},
                            const ZeroWindCalibration& cal = {});
    static float compute_north_heading_deg(const ImuCompassReading& imu);
    static float compute_mast_tilt_deg(const ImuCompassReading& imu);
    static std::uint8_t beaufort_number(float speed_m_s);
};

class WmoWindAggregator {
public:
    static constexpr std::size_t kWindowSize = 60;

    void push_sample(float speed_m_s, float direction_deg);
    WmoWindSummary summary() const;
    void reset();

private:
    struct Entry {
        float speed{0.0F};
        float dir_deg{0.0F};
    };
    std::array<Entry, kWindowSize> ring_{};
    std::size_t head_{0};
    std::size_t count_{0};
    float last_speed_{0.0F};
    float last_dir_{0.0F};
};

}  // namespace station::wind
