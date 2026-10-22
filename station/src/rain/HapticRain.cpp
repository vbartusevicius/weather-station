#include "HapticRain.hpp"

#include <algorithm>
#include <cmath>

namespace station {
namespace rain {

namespace {
float clamp_f(float v, float lo, float hi) {
    return std::min(hi, std::max(lo, v));
}
}  // namespace

float HapticRainProcessor::estimate_hail_diameter_mm(int net_mv, std::uint16_t pulse_width_us) {
    if (net_mv < 1400 || pulse_width_us > 140) {
        return 0.0F;
    }
    // Hertzian ice-sphere impact scaling on 20 mm PZT diaphragm:
    // 1400 mV -> ~5.2 mm graupel/pea hail; 2500 mV -> ~12.5 mm marble hail; 3300 mV -> ~18 mm hail
    const float excess_v = static_cast<float>(net_mv - 1100) / 1000.0F;
    const float pulse_factor = clamp_f(120.0F / static_cast<float>( std::max<std::uint16_t>(60, pulse_width_us) ), 0.85F, 1.35F);
    return clamp_f(8.2F * std::pow(excess_v, 0.68F) * pulse_factor, 4.0F, 35.0F);
}

float HapticRainProcessor::process_impact(const RainImpactEvent& ev, float gain) {
    // baseline_mv == 0: the V1 crown charge amplifier has no capacitive wetness channel; the surface then counts as
    // wet while a rain event is running (tick_second), and only the long-pulse rule rejects mast vibration.
    const bool has_baseline = ev.baseline_mv != 0;
    if (has_baseline) {
        metrics_.surface_wet = (ev.baseline_mv >= kWetSurfaceBaselineMv);
    } else {
        activity_wetness_ = true;
    }

    const int net_mv = static_cast<int>(ev.peak_mv) - static_cast<int>(ev.baseline_mv);
    if (net_mv < static_cast<int>(kDropThresholdMv)) {
        return 0.0F;
    }

    if (ev.imu_vibration_g > kMaxMastShakeG && ((has_baseline && !metrics_.surface_wet) || ev.pulse_width_us > 850)) {
        ++metrics_.rejected_vibrations;
        return 0.0F;
    }

    const bool is_hail = (net_mv >= 1400 && ev.pulse_width_us <= 140);
    float hail_lwe_mm = 0.0F;
    if (is_hail) {
        ++metrics_.hail_hits;
        const float diam_mm = estimate_hail_diameter_mm(net_mv, ev.pulse_width_us);
        metrics_.max_hail_diameter_mm = std::max(metrics_.max_hail_diameter_mm, diam_mm);
        // Converted Liquid-Water Equivalent (LWE) depth of an ice sphere (rho_ice = 0.917 g/cm3):
        const float hail_water_vol_mm3 = 0.917F * 0.5235988F * diam_mm * diam_mm * diam_mm;
        hail_lwe_mm = clamp_f(hail_water_vol_mm3 / kCollectionAreaMm2, 0.005F, 1.50F);
        sec_hail_mm_ += hail_lwe_mm;
    }

    // 1. Normalize out horizontal wind kinetic-energy boost: E_total = E_vert * (1 + 0.5*(v_wind / v_term)^2)
    const float wind_ratio = clamp_f(ev.wind_speed_m_s / kNominalTerminalVelMs, 0.0F, 5.0F);
    const float wind_ke_factor = 1.0F + 0.5F * wind_ratio * wind_ratio;
    // 2. Compensate Murata 7BB-20-6L0 PZT capacitance/charge sensitivity thermal drift (-0.18% / C from 20 C):
    const float temp_factor = clamp_f(1.0F + 0.0018F * (ev.dome_temp_c - 20.0F), 0.85F, 1.15F);
    const float norm_energy = ((static_cast<float>(net_mv) / 1000.0F) / wind_ke_factor) / temp_factor;
    const float drop_volume_mm3 = 18.5F * std::pow(norm_energy, 1.35F);
    const float depth_mm = gain * (is_hail ? hail_lwe_mm : clamp_f(drop_volume_mm3 / kCollectionAreaMm2, 0.0005F, 0.25F));

    ++metrics_.drop_count;
    sec_mm_ += depth_mm;
    metrics_.event_mm += depth_mm;
    metrics_.daily_mm += depth_mm;
    metrics_.total_mm += depth_mm;
    return depth_mm;
}

float HapticRainProcessor::tick_second() {
    const float closed_mm = sec_mm_;
    last_hail_mm_ = sec_hail_mm_;
    ring_mm_[head_] = sec_mm_;
    ring_hail_[head_] = sec_hail_mm_;
    head_ = static_cast<std::uint8_t>((head_ + 1) % 60);
    sec_mm_ = 0.0F;
    sec_hail_mm_ = 0.0F;

    float sum = 0.0F;
    float hail = 0.0F;
    for (int i = 0; i < 60; ++i) {
        sum += ring_mm_[i];
        hail += ring_hail_[i];
    }
    metrics_.rate_mm_h = sum * 60.0F;
    metrics_.hail_rate_mm_h = hail * 60.0F;

    if (closed_mm > 0.0F) {
        dry_seconds_ = 0;
        metrics_.is_raining = true;
    } else if (dry_seconds_ < 65535U) {
        ++dry_seconds_;
    }
    if (dry_seconds_ >= kDryMinutesToEndEvent * 60U) {
        metrics_.is_raining = false;
        metrics_.event_mm = 0.0F;
    }
    if (activity_wetness_) {
        metrics_.surface_wet = metrics_.is_raining;
    }

    if (metrics_.hail_rate_mm_h > 0.0F) {
        metrics_.state = PrecipitationType::Hail;
    } else if (metrics_.rate_mm_h >= 50.0F) {
        metrics_.state = PrecipitationType::ViolentRain;
    } else if (metrics_.rate_mm_h >= 10.0F) {
        metrics_.state = PrecipitationType::HeavyRain;
    } else if (metrics_.rate_mm_h >= 2.5F) {
        metrics_.state = PrecipitationType::ModerateRain;
    } else if (metrics_.rate_mm_h >= 0.5F) {
        metrics_.state = PrecipitationType::LightRain;
    } else if (metrics_.rate_mm_h > 0.0F) {
        metrics_.state = PrecipitationType::Drizzle;
    } else {
        metrics_.state = PrecipitationType::None;
    }
    return closed_mm;
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

}  // namespace rain
}  // namespace station
