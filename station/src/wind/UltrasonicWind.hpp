#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace station {
namespace wind {

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
    float crosswind_correction_pct{0.0F};
};

// WMO-No. 8 wind statistics from 1-s vector means (ultrasonic samples already averaged per second).
struct WmoWindSummary {
    bool valid{false};             // at least 3 s of data
    float speed_3s_m_s{0.0F};      // 3-s running mean ("instantaneous" wind)
    float dir_3s_deg{0.0F};
    float avg_2m_m_s{0.0F};
    float avg_10m_m_s{0.0F};
    float dir_10m_deg{0.0F};       // vector-mean direction over 10 min
    bool dir_valid{false};         // false when calm (< 0.5 m/s)
    float gust_m_s{0.0F};          // max 3-s mean over 10 min
    float lull_m_s{0.0F};          // min 3-s mean over 10 min
    float beaufort_scale{0.0F};    // from the 10-min mean
    float turbulence_intensity_pct{0.0F};
    std::array<float, 16> rose_freq_pct{};  // share of non-calm seconds per 22.5 deg sector (10 min)
    std::uint16_t seconds{0};      // valid seconds in the 10-min window
};

class UltrasonicWindSolver {
public:
    static constexpr float kTransducerSeparationM = 0.054F;
    static constexpr float kReflectorGapM = 0.048F;
    static constexpr float kCarrierPeriodUs = 25.0F;  // 40 kHz ultrasonic period (25.0 us)
    static constexpr float kMinDetectableSpeedMs = 0.05F;
    static constexpr float kMaxValidSpeedMs = 65.0F;

    static float resolve_cycle_slip(float zc_us, float env_us);
    // Converts moist-air acoustic virtual temperature Tv (C) to true thermodynamic temperature T (C)
    static float correct_acoustic_temp_for_humidity(float virtual_temp_c,
                                                    float rh_percent,
                                                    float pressure_hpa = 1013.25F);
    static WindSample solve(const TofQuad& tof,
                            const ImuCompassReading& orientation = {},
                            const ZeroWindCalibration& cal = {});
    static float compute_north_heading_deg(const ImuCompassReading& imu);
    static float compute_mast_tilt_deg(const ImuCompassReading& imu);
    static std::uint8_t beaufort_number(float speed_m_s);
};

class WmoWindAggregator {
public:
    static constexpr std::size_t kWindowSize = 600;  // 10 min of 1-s values
    static constexpr float kCalmMs = 0.5F;

    // One call per second. `valid == false` records a missing second (sensor fault).
    void push_second(bool valid, float v_east_m_s, float v_north_m_s, float speed_mean_m_s);
    WmoWindSummary summary() const;
    void reset();

private:
    struct Entry {
        float u{0.0F};      // east
        float v{0.0F};      // north
        float speed{0.0F};  // 1-s scalar mean
        float s3{0.0F};     // 3-s running mean ending at this second
        bool valid{false};
        bool s3_valid{false};
    };
    const Entry& back(std::size_t i) const { return ring_[(head_ + kWindowSize - 1 - i) % kWindowSize]; }
    std::array<Entry, kWindowSize> ring_{};
    std::size_t head_{0};
    std::size_t count_{0};
};

// Direction the wind blows FROM (meteorological convention) for an east/north flow vector.
float wind_from_deg(float v_east_m_s, float v_north_m_s);

}  // namespace wind
}  // namespace station
