#include "RainDetector.hpp"

#include <cmath>

namespace crown {

bool RainDetector::add(std::uint16_t raw, std::uint32_t t_us, RainImpact& out) {
    const float mv = static_cast<float>(raw) * kMvPerCount;
    if (!init_) {
        baseline_ = mv;
        init_ = true;
    }
    const float dev = std::fabs(mv - baseline_);
    const bool rail = raw < 16 || raw > 4079;
    if (!in_pulse_) {
        baseline_ += (mv - baseline_) / (0.2F * fs_hz_);   // ~0.2 s drift tracking, frozen during impacts
        if (dev >= trigger_mv_) {
            in_pulse_ = true;
            peak_ = dev;
            clipped_ = rail;
            start_us_ = last_above_us_ = t_us;
        }
        return false;
    }
    if (dev > peak_) {
        peak_ = dev;
    }
    clipped_ = clipped_ || rail;
    if (dev >= 0.5F * trigger_mv_) {
        last_above_us_ = t_us;
        return false;
    }
    if (t_us - last_above_us_ < kQuietUs) {
        return false;
    }
    in_pulse_ = false;
    out.t_us = start_us_;
    out.peak_mv = static_cast<std::uint16_t>(std::lround(peak_));
    const std::uint32_t w = last_above_us_ - start_us_ + static_cast<std::uint32_t>(1e6F / fs_hz_);
    out.width_us = static_cast<std::uint16_t>(w > 65535U ? 65535U : w);
    out.clipped = clipped_;
    return true;
}

}  // namespace crown
