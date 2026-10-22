#pragma once
// Still-air calibration of the ultrasonic anemometer (CALIBRATION.md, step "wind still air").
//
// With the air around the transducers held still (station under a closed box, or a calm indoor room), every path
// must read the same time of flight: t = L_eff / c(T, RH) + delay. From raw quads (nothing subtracted) and the
// probe's T / RH / pressure this gives
//   us_delay_us      = mean of all four ToFs - L_eff / c          (electronics + transducer + envelope-timing delay)
//   dt_ns_offset_us  = mean(t_NS - t_SN),  dt_ew_offset_us = mean(t_EW - t_WE)   (residual reciprocal asymmetry)
// using the same geometry as UltrasonicWindSolver, so the solver then reports 0 m/s and the true speed of sound.

#include <cstdint>

#include "UltrasonicWind.hpp"

namespace station {
namespace wind {

struct StillAirResult {
    bool ok{false};
    const char* reason{"no data"};
    float us_delay_us{0.0F};
    ZeroWindCalibration zero{};
    float dt_spread_us{0.0F};   // standard deviation of the reciprocal differences (still-air quality)
    std::uint32_t quads{0};
};

class StillAirCalibrator {
public:
    static constexpr std::uint32_t kMinQuads = 120;          // 30 s at 4 quads/s
    static constexpr float kMaxDtSpreadUs = 0.25F;           // ~0.15 m/s: more means the air is not still
    static constexpr float kMinDelayUs = -20.0F;
    static constexpr float kMaxDelayUs = 150.0F;

    void reset() { *this = StillAirCalibrator{}; }
    // `raw`: times of flight as delivered by the crown, before us_delay_us and the zero-wind offsets.
    void add(const TofQuad& raw);
    std::uint32_t count() const { return n_; }
    StillAirResult result(float air_temp_c, float rh_pct, float pressure_hpa) const;

    // Speed of sound in moist air and the solver's effective path length (shared with the tests).
    static float sound_speed_m_s(float air_temp_c, float rh_pct, float pressure_hpa);
    static float path_length_m();

private:
    std::uint32_t n_{0};
    double sum_t_{0.0};
    double sum_dns_{0.0}, sum_dns2_{0.0};
    double sum_dew_{0.0}, sum_dew2_{0.0};
};

}  // namespace wind
}  // namespace station
