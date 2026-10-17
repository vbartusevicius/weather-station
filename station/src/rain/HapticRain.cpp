#include "rain/HapticRain.hpp"

#include <algorithm>
#include <cmath>

namespace station::rain {

bool HapticRainProcessor::process_impact(const RainImpactEvent& ev) {
    metrics_.surface_wet = (ev.baseline_mv >= kWetSurfaceBaselineMv);

    const int net_mv = static_cast<int>(ev.peak_mv) - static_cast<int>(ev.baseline_mv);
    if (net_mv < static_cast<int>(kDropThresholdMv)) {
        return false;
    }

    if (ev.imu_vibration_g > kMaxMastShakeG && (!metrics_.surface_wet || ev.pulse_width_us > 850)) {
        ++metrics_.rejected_vibrations;
        return false;
    }

    const bool is_hail = (net_mv >= 1400 && ev.pulse_width_us <= 140);
    if (is_hail) {
        ++metrics_.hail_hits;
    }

    // Normalize out horizontal wind kinetic-energy boost: E_total = E_vert * (1 + 0.5*(v_wind / v_term)^2)
    const float wind_ratio = std::clamp(ev.wind_speed_m_s / kNominalTerminalVelMs, 0.0F, 5.0F);
    const float wind_ke_factor = 1.0F + 0.5F * wind_ratio * wind_ratio;
    const float norm_energy = (static_cast<float>(net_mv) / 1000.0F) / wind_ke_factor;
    const float drop_volume_mm3 = 18.5F * std::pow(norm_energy, 1.35F);
    const float depth_mm = std::clamp(drop_volume_mm3 / kCollectionAreaMm2, 0.0005F, 0.25F);

    metrics_.is_raining = true;
    dry_minutes_ = 0;
    ++metrics_.drop_count;
    window_minute_mm_ += depth_mm;
    metrics_.event_mm += depth_mm;
    metrics_.hourly_mm += depth_mm;
    metrics_.daily_mm += depth_mm;
    metrics_.total_mm += depth_mm;

    metrics_.rate_mm_h = std::max(metrics_.rate_mm_h, window_minute_mm_ * 60.0F);
    if (is_hail) {
        metrics_.state = PrecipitationType::Hail;
    } else if (metrics_.rate_mm_h >= 50.0F) {
        metrics_.state = PrecipitationType::ViolentRain;
    } else if (metrics_.rate_mm_h >= 10.0F) {
        metrics_.state = PrecipitationType::HeavyRain;
    } else if (metrics_.rate_mm_h >= 2.5F) {
        metrics_.state = PrecipitationType::ModerateRain;
    } else if (metrics_.rate_mm_h >= 0.5F) {
        metrics_.state = PrecipitationType::LightRain;
    } else {
        metrics_.state = PrecipitationType::Drizzle;
    }
    return true;
}

void HapticRainProcessor::tick_minute() {
    metrics_.rate_mm_h = 0.65F * (window_minute_mm_ * 60.0F) + 0.35F * metrics_.rate_mm_h;
    if (window_minute_mm_ < 1e-4F) {
        ++dry_minutes_;
        if (dry_minutes_ >= 15) {
            metrics_.is_raining = false;
            metrics_.state = PrecipitationType::None;
            metrics_.rate_mm_h = 0.0F;
            metrics_.event_mm = 0.0F;
        }
    }
    window_minute_mm_ = 0.0F;
    metrics_.hourly_mm *= (59.0F / 60.0F);
}

void HapticRainProcessor::reset_daily() {
    metrics_.daily_mm = 0.0F;
}

const char* precipitation_type_name(PrecipitationType type) {
    switch (type) {
        case PrecipitationType::None:
            return "Dry";
        case PrecipitationType::Drizzle:
            return "Drizzle";
        case PrecipitationType::LightRain:
            return "Light Rain";
        case PrecipitationType::ModerateRain:
            return "Moderate Rain";
        case PrecipitationType::HeavyRain:
            return "Heavy Rain";
        case PrecipitationType::ViolentRain:
            return "Violent Downpour";
        case PrecipitationType::Hail:
            return "Hail";
    }
    return "Dry";
}

}  // namespace station::rain
