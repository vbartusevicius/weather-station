#include "Conditioners.hpp"

#include <algorithm>
#include <cmath>

namespace station {
namespace conditioning {

void WindSecondAccumulator::add(float v_east, float v_north, float speed, float sound_m_s, float virtual_temp_c) {
    se_ += v_east;
    sn_ += v_north;
    ss_ += speed;
    sc_ += sound_m_s;
    st_ += virtual_temp_c;
    if (n_ < 255) {
        ++n_;
    }
}

bool WindSecondAccumulator::take(core::WindSecond& out) {
    if (n_ == 0) {
        return false;
    }
    const float inv = 1.0F / static_cast<float>(n_);
    out.v_east_m_s = se_ * inv;
    out.v_north_m_s = sn_ * inv;
    out.speed_mean_m_s = ss_ * inv;
    out.sound_speed_m_s = sc_ * inv;
    out.virtual_temp_c = st_ * inv;
    out.samples = n_;
    se_ = sn_ = ss_ = sc_ = st_ = 0.0F;
    n_ = 0;
    return true;
}

bool PressureAnalyzer::add(float pressure_pa, std::uint32_t t_ms, core::ThunderArrival& onset) {
    if (!init_) {
        baseline_pa_ = pressure_pa;
        init_ = true;
    }
    // 1st-order low-pass at 0.05 Hz isolates the slow barometric level; the residual is the
    // acoustic / gust band (0.05 .. 50 Hz).
    constexpr float kAlpha = 2.0F * 3.14159265F * 0.05F / kSampleHz;
    baseline_pa_ += kAlpha * (pressure_pa - baseline_pa_);
    const float hp = pressure_pa - baseline_pa_;
    const float sq = hp * hp;

    sum_pa_ += pressure_pa;
    ++n_;

    // Thunder: a sudden impulse far above the running ripple level (and >= 0.5 Pa absolute).
    const float rms = std::sqrt(std::max(rms_sq_, 1e-4F));
    const bool impulse = std::fabs(hp) > std::max(0.5F, 6.0F * rms) && (t_ms - last_onset_ms_) > 2000U;
    constexpr float kRmsAlpha = 1.0F / (10.0F * kSampleHz);  // ~10 s
    rms_sq_ += kRmsAlpha * (sq - rms_sq_);
    if (impulse) {
        last_onset_ms_ = t_ms;
        onset.t_ms = t_ms;
        onset.peak_pa = std::fabs(hp);
        return true;
    }
    return false;
}

bool PressureAnalyzer::take_second(float& mean_hpa, float& ripple_pa) {
    if (n_ == 0) {
        return false;
    }
    mean_hpa = static_cast<float>(sum_pa_ / n_) / 100.0F;
    ripple_pa = std::sqrt(std::max(0.0F, rms_sq_));
    sum_pa_ = 0.0;
    n_ = 0;
    return true;
}

void VibrationAnalyzer::add(const core::Vec3& a) {
    if (!init_) {
        lp_ = a;
        init_ = true;
    }
    // Gravity / tilt: 0.5 Hz low-pass. Vibration: residual (high-passed > 0.5 Hz).
    constexpr float kAlpha = 2.0F * 3.14159265F * 0.5F / kSampleHz;
    lp_.x += kAlpha * (a.x - lp_.x);
    lp_.y += kAlpha * (a.y - lp_.y);
    lp_.z += kAlpha * (a.z - lp_.z);
    const core::Vec3 hp{a.x - lp_.x, a.y - lp_.y, a.z - lp_.z};

    // Integrate to velocity (m/s) with a leaky integrator (0.5 Hz) to suppress drift.
    constexpr float kG = 9.80665F;
    constexpr float kDt = 1.0F / kSampleHz;
    constexpr float kLeak = 1.0F - 2.0F * 3.14159265F * 0.5F / kSampleHz;
    vel_.x = vel_.x * kLeak + hp.x * kG * kDt;
    vel_.y = vel_.y * kLeak + hp.y * kG * kDt;
    vel_.z = vel_.z * kLeak + hp.z * kG * kDt;

    const float a_mg = 1000.0F * std::sqrt(hp.x * hp.x + hp.y * hp.y + hp.z * hp.z);
    const float v_mm = 1000.0F * std::sqrt(vel_.x * vel_.x + vel_.y * vel_.y + vel_.z * vel_.z);
    pga_mg_ = std::max(pga_mg_, a_mg);
    ppv_mm_s_ = std::max(ppv_mm_s_, v_mm);

    const float cur[3] = {hp.x, hp.y, hp.z};
    const float prev[3] = {prev_hp_.x, prev_hp_.y, prev_hp_.z};
    for (std::size_t i = 0; i < 3; ++i) {
        if ((cur[i] >= 0.0F) != (prev[i] >= 0.0F)) {
            ++crossings_[i];
        }
        energy_[i] += cur[i] * cur[i];
    }
    prev_hp_ = hp;
    ++n_;
}

bool VibrationAnalyzer::take_second(core::Vec3& gravity_g, core::VibrationSecond& out) {
    if (n_ == 0) {
        return false;
    }
    gravity_g = lp_;
    std::size_t dom = 0;
    for (std::size_t i = 1; i < 3; ++i) {
        if (energy_[i] > energy_[dom]) {
            dom = i;
        }
    }
    const float seconds = static_cast<float>(n_) / kSampleHz;
    out.pga_mg = pga_mg_;
    out.ppv_mm_s = ppv_mm_s_;
    out.dominant_hz = static_cast<float>(crossings_[dom]) / (2.0F * seconds);
    pga_mg_ = 0.0F;
    ppv_mm_s_ = 0.0F;
    crossings_ = {};
    energy_ = {};
    n_ = 0;
    return true;
}

bool RainPulseDetector::add(std::uint16_t mv, std::uint32_t t_ms, core::RainImpact& out) {
    if (!init_) {
        baseline_ = static_cast<float>(mv);
        init_ = true;
    }
    const float level = static_cast<float>(mv);
    if (!in_pulse_) {
        if (level > baseline_ + kTriggerMv) {
            in_pulse_ = true;
            peak_ = mv;
            samples_ = 1;
            start_ms_ = t_ms;
            return false;
        }
        // Track the resting (wet/dry) DC bias with a ~2 s time constant while idle.
        baseline_ += (level - baseline_) / (2.0F * kSampleHz);
        return false;
    }
    peak_ = std::max(peak_, mv);
    ++samples_;
    const bool ended = level < baseline_ + kTriggerMv * 0.5F;
    const bool too_long = samples_ > static_cast<std::uint32_t>(0.02F * kSampleHz);  // 20 ms: not a drop
    if (!ended && !too_long) {
        return false;
    }
    in_pulse_ = false;
    out.t_ms = start_ms_;
    out.peak_mv = peak_;
    out.baseline_mv = static_cast<std::uint16_t>(baseline_);
    out.width_us = static_cast<std::uint16_t>(std::min<std::uint32_t>(65535U, samples_ * 1000000U / static_cast<std::uint32_t>(kSampleHz)));
    return true;
}

}  // namespace conditioning
}  // namespace station
