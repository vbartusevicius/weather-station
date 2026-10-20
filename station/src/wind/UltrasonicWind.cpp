#include "UltrasonicWind.hpp"

#include <algorithm>
#include <cmath>

namespace station {
namespace wind {

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kRadToDeg = 180.0F / kPi;
constexpr float kDegToRad = kPi / 180.0F;
constexpr float kGammaRd = 1.4F * 287.058F;  // gamma * R_d for dry air (m^2 / (s^2 * K))

float clamp_f(float v, float lo, float hi) {
    return std::min(hi, std::max(lo, v));
}

float wrap_360(float deg) {
    float r = std::fmod(deg, 360.0F);
    return (r < 0.0F) ? (r + 360.0F) : r;
}
}  // namespace

float UltrasonicWindSolver::resolve_cycle_slip(float zc_us, float env_us) {
    if (env_us <= 0.0F) {
        return zc_us;
    }
    const float k_cycles = std::round((env_us - zc_us) / kCarrierPeriodUs);
    return zc_us + k_cycles * kCarrierPeriodUs;
}

float UltrasonicWindSolver::correct_acoustic_temp_for_humidity(float virtual_temp_c,
                                                               float rh_percent,
                                                               float pressure_hpa) {
    // Moist air has lower molar mass than dry air: Tv_K = T_K * (1 + 0.32 * e / P)
    const float rh = clamp_f(rh_percent, 0.0F, 100.0F);
    const float p_hpa = clamp_f(pressure_hpa, 500.0F, 1100.0F);
    const float es_hpa = 6.112F * std::exp((17.67F * virtual_temp_c) / (virtual_temp_c + 243.5F));
    const float e_hpa = (rh / 100.0F) * es_hpa;
    const float tv_k = virtual_temp_c + 273.15F;
    const float t_true_k = tv_k / (1.0F + 0.32F * (e_hpa / p_hpa));
    return t_true_k - 273.15F;
}

float UltrasonicWindSolver::compute_mast_tilt_deg(const ImuCompassReading& imu) {
    const float norm = std::sqrt(imu.accel_x_g * imu.accel_x_g +
                                 imu.accel_y_g * imu.accel_y_g +
                                 imu.accel_z_g * imu.accel_z_g);
    if (norm < 1e-3F) {
        return 0.0F;
    }
    const float cos_tilt = clamp_f(std::fabs(imu.accel_z_g) / norm, 0.0F, 1.0F);
    return std::acos(cos_tilt) * kRadToDeg;
}

float UltrasonicWindSolver::compute_north_heading_deg(const ImuCompassReading& imu) {
    const float norm_a = std::sqrt(imu.accel_x_g * imu.accel_x_g +
                                   imu.accel_y_g * imu.accel_y_g +
                                   imu.accel_z_g * imu.accel_z_g);
    float mx = imu.mag_x_ut;
    float my = imu.mag_y_ut;
    if (norm_a > 1e-3F) {
        const float ax = imu.accel_x_g / norm_a;
        const float ay = imu.accel_y_g / norm_a;
        const float pitch = std::asin(clamp_f(-ax, -1.0F, 1.0F));
        const float roll = std::asin(clamp_f(ay / std::max(0.1F, std::cos(pitch)), -1.0F, 1.0F));
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        const float cr = std::cos(roll);
        const float sr = std::sin(roll);
        mx = imu.mag_x_ut * cp + imu.mag_z_ut * sp;
        my = imu.mag_x_ut * sr * sp + imu.mag_y_ut * cr - imu.mag_z_ut * sr * cp;
    }
    if (std::fabs(mx) < 1e-4F && std::fabs(my) < 1e-4F) {
        return wrap_360(imu.declination_deg);
    }
    const float mag_heading = std::atan2(mx, my) * kRadToDeg;
    return wrap_360(mag_heading + imu.declination_deg);
}

WindSample UltrasonicWindSolver::solve(const TofQuad& tof,
                                       const ImuCompassReading& orientation,
                                       const ZeroWindCalibration& cal) {
    WindSample out{};
    const float t_ns = resolve_cycle_slip(tof.t_ns_us, tof.env_ns_us) - 0.5F * cal.dt_ns_offset_us;
    const float t_sn = resolve_cycle_slip(tof.t_sn_us, tof.env_sn_us) + 0.5F * cal.dt_ns_offset_us;
    const float t_ew = resolve_cycle_slip(tof.t_ew_us, tof.env_ew_us) - 0.5F * cal.dt_ew_offset_us;
    const float t_we = resolve_cycle_slip(tof.t_we_us, tof.env_we_us) + 0.5F * cal.dt_ew_offset_us;

    if (t_ns < 150.0F || t_sn < 150.0F || t_ew < 150.0F || t_we < 150.0F ||
        t_ns > 600.0F || t_sn > 600.0F || t_ew > 600.0F || t_we > 600.0F) {
        return out;
    }

    const float half_d = 0.5F * kTransducerSeparationM;
    const float leg = std::sqrt(half_d * half_d + kReflectorGapM * kReflectorGapM);
    const float l_eff = 2.0F * leg;
    const float cos_alpha = half_d / leg;

    const float inv_ns = 1e6F / t_ns;
    const float inv_sn = 1e6F / t_sn;
    const float inv_ew = 1e6F / t_ew;
    const float inv_we = 1e6F / t_we;

    // 1. First-order reciprocal inverse-ToF flow components (c cancels out):
    const float raw_flow_north = (l_eff / (2.0F * cos_alpha)) * (inv_sn - inv_ns);
    const float raw_flow_east = (l_eff / (2.0F * cos_alpha)) * (inv_we - inv_ew);

    // 2. Speed of sound & Second-Order Crosswind Acoustic Beam-Deflection (Mach^2) Correction:
    const float c_raw = (l_eff * 0.25F) * (inv_ns + inv_sn + inv_ew + inv_we);
    const float c2 = std::max(50000.0F, c_raw * c_raw);
    const float mach_perp_ns = clamp_f((raw_flow_east * raw_flow_east) / c2, 0.0F, 0.05F);
    const float mach_perp_ew = clamp_f((raw_flow_north * raw_flow_north) / c2, 0.0F, 0.05F);
    const float flow_north = raw_flow_north / (1.0F - 0.5F * mach_perp_ns);
    const float flow_east = raw_flow_east / (1.0F - 0.5F * mach_perp_ew);
    const float c_mean = c_raw / std::sqrt(1.0F - 0.5F * (mach_perp_ns + mach_perp_ew));

    const float tilt_deg = compute_mast_tilt_deg(orientation);
    const float tilt_cos = std::max(0.7F, std::cos(tilt_deg * kDegToRad));
    const float north_offset = compute_north_heading_deg(orientation);
    const float rot_rad = north_offset * kDegToRad;

    const float v_e = (flow_east * std::cos(rot_rad) + flow_north * std::sin(rot_rad)) / tilt_cos;
    const float v_n = (-flow_east * std::sin(rot_rad) + flow_north * std::cos(rot_rad)) / tilt_cos;

    const float raw_speed = std::sqrt(v_e * v_e + v_n * v_n);
    const float speed = (raw_speed < kMinDetectableSpeedMs) ? 0.0F : raw_speed;
    const float from_deg = (speed >= kMinDetectableSpeedMs) ? wrap_360(std::atan2(-v_e, -v_n) * kRadToDeg) : 0.0F;
    const float t_virtual_c = (c_mean * c_mean) / kGammaRd - 273.15F;

    out.valid = (raw_speed <= kMaxValidSpeedMs);
    out.speed_m_s = out.valid ? speed : 0.0F;
    out.direction_deg = from_deg;
    out.v_east_m_s = v_e;
    out.v_north_m_s = v_n;
    out.sound_speed_m_s = c_mean;
    out.acoustic_temp_c = t_virtual_c;
    out.mast_tilt_deg = tilt_deg;
    out.north_offset_deg = north_offset;
    out.crosswind_correction_pct = 50.0F * std::max(mach_perp_ns, mach_perp_ew);
    return out;
}

std::uint8_t UltrasonicWindSolver::beaufort_number(float speed_m_s) {
    static constexpr float kThresholds[12] = {
        0.3F, 1.6F, 3.4F, 5.5F, 8.0F, 10.8F, 13.9F, 17.2F, 20.8F, 24.5F, 28.5F, 32.7F,
    };
    for (std::uint8_t b = 0; b < 12; ++b) {
        if (speed_m_s < kThresholds[b]) {
            return b;
        }
    }
    return 12;
}

float wind_from_deg(float v_east_m_s, float v_north_m_s) {
    return wrap_360(std::atan2(-v_east_m_s, -v_north_m_s) * kRadToDeg);
}

void WmoWindAggregator::push_second(bool valid, float v_east_m_s, float v_north_m_s, float speed_mean_m_s) {
    Entry e{};
    e.valid = valid;
    if (valid) {
        e.u = v_east_m_s;
        e.v = v_north_m_s;
        e.speed = std::max(0.0F, speed_mean_m_s);
    }
    ring_[head_] = e;
    head_ = (head_ + 1) % kWindowSize;
    if (count_ < kWindowSize) {
        ++count_;
    }
    // 3-s running mean ending at this second (requires all 3 seconds present).
    if (count_ >= 3 && back(0).valid && back(1).valid && back(2).valid) {
        Entry& cur = ring_[(head_ + kWindowSize - 1) % kWindowSize];
        cur.s3 = (back(0).speed + back(1).speed + back(2).speed) / 3.0F;
        cur.s3_valid = true;
    }
}

WmoWindSummary WmoWindAggregator::summary() const {
    WmoWindSummary s{};
    if (count_ == 0 || !back(0).s3_valid) {
        return s;
    }
    s.valid = true;
    s.speed_3s_m_s = back(0).s3;
    float u3 = 0.0F;
    float v3 = 0.0F;
    for (std::size_t i = 0; i < 3; ++i) {
        u3 += back(i).u;
        v3 += back(i).v;
    }
    s.dir_3s_deg = wind_from_deg(u3, v3);

    float sum10 = 0.0F, sq10 = 0.0F, sum2 = 0.0F, su = 0.0F, sv = 0.0F;
    std::uint16_t n10 = 0, n2 = 0, n_dir = 0;
    float gust = 0.0F;
    float lull = s.speed_3s_m_s;
    std::array<std::uint16_t, 16> sectors{};
    for (std::size_t i = 0; i < count_; ++i) {
        const Entry& e = back(i);
        if (!e.valid) {
            continue;
        }
        sum10 += e.speed;
        sq10 += e.speed * e.speed;
        ++n10;
        if (i < 120) {
            sum2 += e.speed;
            ++n2;
        }
        su += e.u;
        sv += e.v;
        if (e.s3_valid) {
            gust = std::max(gust, e.s3);
            lull = std::min(lull, e.s3);
        }
        if (e.speed >= kCalmMs) {
            const float d = wind_from_deg(e.u, e.v);
            ++sectors[static_cast<std::size_t>(std::fmod(d + 11.25F, 360.0F) / 22.5F) % 16U];
            ++n_dir;
        }
    }
    s.seconds = n10;
    s.avg_10m_m_s = sum10 / static_cast<float>(n10);
    s.avg_2m_m_s = (n2 > 0) ? sum2 / static_cast<float>(n2) : s.avg_10m_m_s;
    s.gust_m_s = gust;
    s.lull_m_s = lull;
    s.dir_valid = s.avg_10m_m_s >= kCalmMs && (std::fabs(su) + std::fabs(sv)) > 1e-3F;
    s.dir_10m_deg = s.dir_valid ? wind_from_deg(su, sv) : 0.0F;
    s.beaufort_scale = static_cast<float>(UltrasonicWindSolver::beaufort_number(s.avg_10m_m_s));
    const float var = std::max(0.0F, sq10 / static_cast<float>(n10) - s.avg_10m_m_s * s.avg_10m_m_s);
    s.turbulence_intensity_pct = (s.avg_10m_m_s > 0.5F) ? std::min(100.0F, 100.0F * std::sqrt(var) / s.avg_10m_m_s) : 0.0F;
    for (std::size_t k = 0; k < 16; ++k) {
        s.rose_freq_pct[k] = (n_dir > 0) ? 100.0F * static_cast<float>(sectors[k]) / static_cast<float>(n_dir) : 0.0F;
    }
    return s;
}

void WmoWindAggregator::reset() {
    ring_ = {};
    head_ = 0;
    count_ = 0;
}

}  // namespace wind
}  // namespace station
