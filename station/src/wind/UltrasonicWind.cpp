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

void WmoWindAggregator::push_sample(float speed_m_s, float direction_deg) {
    const float clamped = std::max(0.0F, speed_m_s);
    const float dir = wrap_360(direction_deg);
    ring_[head_] = Entry{clamped, dir};
    head_ = (head_ + 1) % kWindowSize;
    if (count_ < kWindowSize) {
        ++count_;
    }
    last_speed_ = clamped;
    last_dir_ = dir;
}

WmoWindSummary WmoWindAggregator::summary() const {
    WmoWindSummary s{};
    if (count_ == 0) {
        return s;
    }
    s.current_speed_m_s = last_speed_;
    s.current_dir_deg = last_dir_;

    float sum_all = 0.0F;
    float sum_sq_all = 0.0F;
    float sum_1m = 0.0F;
    std::size_t n_1m = std::min<std::size_t>(count_, 20);
    float gust = 0.0F;
    float lull = ring_[(head_ + kWindowSize - 1) % kWindowSize].speed;
    float sin_sum = 0.0F;
    float cos_sum = 0.0F;
    std::array<std::uint16_t, 16> sector_counts{};

    for (std::size_t i = 0; i < count_; ++i) {
        const std::size_t idx = (head_ + kWindowSize - 1 - i) % kWindowSize;
        const Entry& e = ring_[idx];
        sum_all += e.speed;
        sum_sq_all += e.speed * e.speed;
        if (i < n_1m) {
            sum_1m += e.speed;
        }
        gust = std::max(gust, e.speed);
        lull = std::min(lull, e.speed);
        const float rad = e.dir_deg * kDegToRad;
        const float weight = std::max(0.1F, e.speed);
        sin_sum += weight * std::sin(rad);
        cos_sum += weight * std::cos(rad);
        const std::size_t sec = static_cast<std::size_t>(std::fmod(e.dir_deg + 11.25F, 360.0F) / 22.5F) % 16U;
        ++sector_counts[sec];
    }

    const float n_f = static_cast<float>(count_);
    s.avg_1min_m_s = sum_1m / static_cast<float>(n_1m);
    s.avg_10min_m_s = sum_all / n_f;
    s.gust_3s_m_s = gust;
    s.lull_m_s = lull;
    s.mean_dir_deg = wrap_360(std::atan2(sin_sum, cos_sum) * kRadToDeg);
    s.beaufort_scale = static_cast<float>(UltrasonicWindSolver::beaufort_number(s.avg_1min_m_s));
    const float variance = std::max(0.0F, (sum_sq_all / n_f) - (s.avg_10min_m_s * s.avg_10min_m_s));
    const float sigma_v = std::sqrt(variance);
    s.turbulence_intensity_pct = (s.avg_10min_m_s > 0.2F) ? std::min(100.0F, (sigma_v / s.avg_10min_m_s) * 100.0F) : 0.0F;
    s.gust_factor = (s.avg_10min_m_s > 0.2F) ? (s.gust_3s_m_s / s.avg_10min_m_s) : 1.0F;
    for (std::size_t k = 0; k < 16; ++k) {
        s.rose_freq_pct[k] = (static_cast<float>(sector_counts[k]) * 100.0F) / n_f;
    }
    return s;
}

void WmoWindAggregator::reset() {
    head_ = 0;
    count_ = 0;
    last_speed_ = 0.0F;
    last_dir_ = 0.0F;
}

}  // namespace wind
}  // namespace station
