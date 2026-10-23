#include "Calibration.hpp"

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../astro/Astronomy.hpp"
#include "../wind/UltrasonicWind.hpp"

namespace station {
namespace calibration {

namespace {
constexpr float kPi = 3.14159265358979F;
constexpr float kG = 9.80665F;

const char* const kNames[] = {"none",     "compass",   "heading",      "car_run", "drop_test", "rain_gauge",
                              "pressure", "clear_sky", "uv_reference", "salt_rh", "ice_point", "co2_fresh_air"};

float mean_of(const float* v, int n) {
    double s = 0.0;
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (std::isfinite(v[i])) {
            s += v[i];
            ++k;
        }
    }
    return k ? static_cast<float>(s / k) : NAN;
}

float std_of(const float* v, int n) {
    const float m = mean_of(v, n);
    double s = 0.0;
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (std::isfinite(v[i])) {
            s += (v[i] - m) * (v[i] - m);
            ++k;
        }
    }
    return k > 1 ? static_cast<float>(std::sqrt(s / (k - 1))) : 0.0F;
}

int count_finite(const float* v, int n) {
    int k = 0;
    for (int i = 0; i < n; ++i) k += std::isfinite(v[i]) ? 1 : 0;
    return k;
}

float median_u16(const std::uint16_t* v, int n) {
    if (n <= 0) return NAN;
    std::uint16_t tmp[Calibrator::kMaxPeaks];
    std::copy(v, v + n, tmp);
    std::sort(tmp, tmp + n);
    return n % 2 ? tmp[n / 2] : 0.5F * (tmp[n / 2 - 1] + tmp[n / 2]);
}
}  // namespace

const char* kind_name(Kind k) {
    const auto i = static_cast<std::size_t>(k);
    return i < sizeof(kNames) / sizeof(kNames[0]) ? kNames[i] : "none";
}

Kind kind_from_name(const char* name) {
    for (std::size_t i = 1; name != nullptr && i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
        if (std::strcmp(name, kNames[i]) == 0) return static_cast<Kind>(i);
    }
    return Kind::None;
}

// ------------------------------------------------------------------------------------------------- pure functions
float wrap360(float deg) {
    float d = std::fmod(deg, 360.0F);
    return d < 0.0F ? d + 360.0F : d;
}

float wrap180(float deg) {
    const float d = wrap360(deg + 180.0F) - 180.0F;
    return d;
}

float circular_mean_deg(const float* deg, int n) {
    double s = 0.0, c = 0.0;
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(deg[i])) continue;
        s += std::sin(deg[i] * kPi / 180.0F);
        c += std::cos(deg[i] * kPi / 180.0F);
        ++k;
    }
    return k ? wrap360(static_cast<float>(std::atan2(s, c) * 180.0 / kPi)) : NAN;
}

float initial_bearing_deg(double lat1, double lon1, double lat2, double lon2) {
    const double p1 = lat1 * kPi / 180.0, p2 = lat2 * kPi / 180.0, dl = (lon2 - lon1) * kPi / 180.0;
    const double y = std::sin(dl) * std::cos(p2);
    const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    return wrap360(static_cast<float>(std::atan2(y, x) * 180.0 / kPi));
}

CompassFit fit_compass(const float* x, const float* y, int n) {
    CompassFit f{};
    if (n < 30) {
        f.reason = "too few magnetometer readings (rotate for the whole 90 s)";
        return f;
    }
    float x0 = x[0], x1 = x[0], y0 = y[0], y1 = y[0];
    for (int i = 1; i < n; ++i) {
        x0 = std::min(x0, x[i]);
        x1 = std::max(x1, x[i]);
        y0 = std::min(y0, y[i]);
        y1 = std::max(y1, y[i]);
    }
    f.offset_x = 0.5F * (x0 + x1);
    f.offset_y = 0.5F * (y0 + y1);
    const float rx = 0.5F * (x1 - x0), ry = 0.5F * (y1 - y0);
    f.radius_ut = 0.5F * (rx + ry);
    bool bins[12] = {};
    for (int i = 0; i < n; ++i) {
        const float a = wrap360(std::atan2(y[i] - f.offset_y, x[i] - f.offset_x) * 180.0F / kPi);
        bins[std::min(11, static_cast<int>(a / 30.0F))] = true;
    }
    for (bool b : bins) f.coverage_bins += b ? 1 : 0;
    if (f.radius_ut < 5.0F) {
        f.reason = "horizontal field below 5 uT: magnetometer not responding, or the station was not rotated";
    } else if (f.radius_ut > 150.0F) {
        f.reason = "field far above the Earth's: a magnet or steel is next to the station";
    } else if (f.coverage_bins < 10) {
        f.reason = "not all directions covered: rotate the station two full, slow turns";
    } else if (rx / ry < 0.6F || rx / ry > 1.67F) {
        f.reason = "very uneven field: steel nearby or the station was moved / tilted while turning";
    } else {
        f.ok = true;
        f.reason = "ok";
        f.scale_x = f.radius_ut / rx;
        f.scale_y = f.radius_ut / ry;
    }
    return f;
}

float drop_terminal_velocity_m_s(float diameter_mm) {
    return std::max(0.5F, 9.65F - 10.3F * std::exp(-0.6F * diameter_mm));   // Atlas et al. (1973)
}

float expected_drop_mv(float volume_mm3, float height_m) {
    if (volume_mm3 <= 0.0F || height_m <= 0.0F) return NAN;
    const float d_mm = std::cbrt(6.0F * volume_mm3 / kPi);
    const float vt = drop_terminal_velocity_m_s(d_mm);
    const float v = vt * std::sqrt(1.0F - std::exp(-2.0F * kG * height_m / (vt * vt)));   // quadratic drag
    // HapticRain: drop_volume_mm3 = 18.5 * (net_mV / 1000)^1.35 at terminal velocity, amplitude ~ kinetic energy
    const float v_term_volts = std::pow(volume_mm3 / 18.5F, 1.0F / 1.35F);
    return 1000.0F * v_term_volts * (v / vt) * (v / vt);
}

float elevation_from_qnh_m(float station_hpa, float qnh_hpa, float temp_c) {
    // inverse of MetCalculator::sea_level_pressure_hpa: qnh = p exp(0.0341626 h / (T + 0.00325 h))
    if (station_hpa <= 0.0F || qnh_hpa <= 0.0F) return NAN;
    const float l = std::log(qnh_hpa / station_hpa);
    const float t_k = temp_c + 273.15F;
    return t_k * l / (0.0341626F - 0.00325F * l);
}

float salt_rh_pct(float temp_c) {
    static const float kT[] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
    static const float kRh[] = {75.51F, 75.65F, 75.67F, 75.61F, 75.47F, 75.29F, 75.09F, 74.87F, 74.68F, 74.52F, 74.43F};
    const float t = std::min(50.0F, std::max(0.0F, temp_c));
    const int i = std::min(9, static_cast<int>(t / 5.0F));
    return kRh[i] + (kRh[i + 1] - kRh[i]) * (t - kT[i]) / 5.0F;
}

core::Vec3 corrected_mag(const core::Vec3& raw, const core::StationConfig& cfg) {
    core::Vec3 m{};
    m.x = (raw.x - cfg.mag_offset_ut[0]) * cfg.mag_scale[0];
    m.y = (raw.y - cfg.mag_offset_ut[1]) * cfg.mag_scale[1];
    m.z = raw.z - cfg.mag_offset_ut[2];
    return m;
}

Sample make_sample(const core::RawState& raw, const core::DerivedState& d, const core::StationConfig& cfg,
                   const core::Vec3* mag_raw) {
    Sample s{};
    const std::uint32_t now = raw.now_ms;
    s.t_ms = now;
    constexpr std::uint32_t kAge = 5000;
    if (mag_raw != nullptr) {
        s.mag_ok = true;
        s.mag_ut = *mag_raw;
        wind::ImuCompassReading imu{};
        const core::Vec3 m = corrected_mag(*mag_raw, cfg);
        imu.mag_x_ut = m.x;
        imu.mag_y_ut = m.y;
        imu.mag_z_ut = m.z;
        if (raw.imu.gravity_g.valid) {
            imu.accel_x_g = raw.imu.gravity_g.value.x;
            imu.accel_y_g = raw.imu.gravity_g.value.y;
            imu.accel_z_g = raw.imu.gravity_g.value.z;
        }
        imu.declination_deg = cfg.declination_deg + cfg.heading_offset_deg;
        s.heading_ok = true;
        s.heading_deg = wind::UltrasonicWindSolver::compute_north_heading_deg(imu);
    }
    if (raw.wind.tilt_deg.fresh(now, kAge)) {
        s.tilt_ok = true;
        s.tilt_deg = raw.wind.tilt_deg.value;
    }
    if (d.astro.location_set && d.astro.sun_alt_deg.ok && d.astro.sun_az_deg.ok) {
        s.sun_ok = true;
        s.sun_alt_deg = d.astro.sun_alt_deg.v;
        s.sun_az_deg = d.astro.sun_az_deg.v;
    }
    if (d.astro.sunrise_min.ok && d.astro.sunset_min.ok && d.astro.sunset_min.v > d.astro.sunrise_min.v) {
        s.solar_noon_ok = true;
        s.solar_noon_min = 0.5F * (d.astro.sunrise_min.v + d.astro.sunset_min.v);
    }
    if (raw.wind.second.fresh(now, kAge)) {
        s.wind_ok = true;
        s.wind_m_s = raw.wind.second.value.speed_mean_m_s;
        s.wind_raw_m_s = s.wind_m_s / (cfg.wind_speed_gain > 0.0F ? cfg.wind_speed_gain : 1.0F);
    }
    if (d.baro.station_hpa.ok) {
        s.pressure_ok = true;
        s.station_hpa = d.baro.station_hpa.v;
    }
    if (raw.thermo.temp_c.fresh(now, 10000)) {
        s.temp_ok = true;
        s.temp_c = raw.thermo.temp_c.value - cfg.temp_offset_c;
    }
    if (raw.thermo.rh_pct.fresh(now, 10000)) {
        s.rh_ok = true;
        s.rh_pct = raw.thermo.rh_pct.value - cfg.rh_offset_pct;
    }
    const float tr = cfg.optics_transmission > 0.0F ? cfg.optics_transmission : 1.0F;
    if (raw.optical.tsl_lux.fresh(now, kAge)) {
        s.lux_ok = true;
        s.lux_raw = raw.optical.tsl_lux.value * tr;
    } else if (raw.optical.ltr_lux.fresh(now, kAge)) {
        s.lux_ok = true;
        s.lux_raw = raw.optical.ltr_lux.value * tr;
    }
    if (raw.optical.ltr_uvi.fresh(now, kAge)) {
        s.ltr_uvi_ok = true;
        s.ltr_uvi_raw = raw.optical.ltr_uvi.value * tr;
    }
    if (raw.optical.guvb_counts.fresh(now, kAge)) {
        s.guvb_ok = true;
        s.guvb_counts = raw.optical.guvb_counts.value;
    }
    s.rain_ok = d.rain.sensor_ok;
    s.rain_total_mm = d.rain.total_mm;
    if (raw.air.co2_ppm.fresh(now, 15000)) {
        s.co2_ok = true;
        s.co2_ppm = raw.air.co2_ppm.value;
    }
    return s;
}

// ------------------------------------------------------------------------------------------------- sessions
std::uint32_t Calibrator::duration_s(Kind k) const {
    switch (k) {
        case Kind::Compass: return 90;
        case Kind::Heading: return 15;
        case Kind::Pressure: return 60;
        case Kind::ClearSky: return 120;
        case Kind::UvReference: return 60;
        case Kind::SaltRh: return 600;
        case Kind::IcePoint: return 300;
        case Kind::Co2FreshAir: return 180;
        default: return 0;   // open: finished by the user (car_run, drop_test, rain_gauge)
    }
}

void Calibrator::set_message(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(message_, sizeof message_, fmt, ap);
    va_end(ap);
}

void Calibrator::suggest(const char* key, float value) {
    if (n_sug_ < kMaxSuggest && std::isfinite(value)) {
        sug_key_[n_sug_] = key;
        sug_val_[n_sug_] = value;
        ++n_sug_;
    }
}

void Calibrator::cancel() {
    state_ = State::Idle;
    kind_ = Kind::None;
    ok_ = false;
    n_ = 0;
    n_peaks_ = 0;
    n_sug_ = 0;
    message_[0] = '\0';
}

bool Calibrator::start(Kind kind, float p1, float p2, float p3, const core::StationConfig& cfg, std::uint32_t now_ms) {
    if (kind == Kind::None || kind == Kind::Count) return false;
    cancel();
    kind_ = kind;
    p1_ = p1;
    p2_ = p2;
    p3_ = p3;
    start_ms_ = last_ms_ = now_ms;
    rain_seen_ = false;
    rain_acc_mm_ = 0.0F;
    state_ = State::Running;
    applied_ = false;
    auto refuse = [this](const char* why) {
        state_ = State::Done;
        ok_ = false;
        set_message("%s", why);
        return false;
    };
    switch (kind) {
        case Kind::Heading:
            if (p2_ == 1.0F) {   // sun shadow: the shadow of a stick at the S transducer crosses the N mark now
                if (!last_sun_ok_ || last_sun_alt_ < 3.0F) return refuse("the sun must be up (and the location set)");
                bearing_ = wrap360(last_sun_az_ + 180.0F);
            } else if (p2_ == 2.0F) {   // landmark coordinates in line with the N mark
                if (!cfg.location_set) return refuse("set the station location first (Setup tab)");
                bearing_ = initial_bearing_deg(cfg.latitude_deg, cfg.longitude_deg, p1_, p3_);
            } else {
                if (!(p1_ >= 0.0F && p1_ <= 360.0F)) return refuse("enter the N-mark bearing (0..360 deg)");
                bearing_ = wrap360(p1_);
            }
            if (fixed_) {   // fixed heading: nothing to measure
                compute(cfg);
                state_ = State::Done;
            }
            return true;
        case Kind::DropTest:
            if (!(p1_ >= 5.0F && p1_ <= 80.0F)) return refuse("drops per ml must be 5..80 (count them, see step 1)");
            if (!(p2_ >= 0.3F && p2_ <= 6.0F)) return refuse("drop height must be 0.3..6 m");
            return true;
        case Kind::Pressure:
            if (p2_ == 1.0F) {   // known elevation: ground elevation from a map (p1) + barometer height above it (p3)
                if (!(p1_ >= -430.0F && p1_ <= 5000.0F)) return refuse("enter the ground elevation in m (map)");
                if (!(p3_ >= 0.0F && p3_ <= 300.0F)) return refuse("enter the station height above the ground (0..300 m)");
                compute(cfg);
                state_ = State::Done;
                return true;
            }
            if (!(p1_ >= 900.0F && p1_ <= 1080.0F)) return refuse("enter the reference sea-level pressure in hPa (900..1080)");
            return true;
        case Kind::UvReference:
            if (p2_ != 1.0F && !(p1_ >= 0.5F && p1_ <= 16.0F)) return refuse("enter the reference UV index (0.5..16)");
            return true;
        case Kind::Co2FreshAir:
            if (p1_ == 0.0F) p1_ = 425.0F;
            if (!(p1_ >= 380.0F && p1_ <= 500.0F)) return refuse("fresh-air CO2 reference must be 380..500 ppm");
            return true;
        default:
            return true;
    }
}

bool Calibrator::finish(float p1, const core::StationConfig& cfg, std::uint32_t now_ms) {
    if (state_ != State::Running) return false;
    last_ms_ = now_ms;
    finish_p1_ = p1;
    compute(cfg);
    state_ = State::Done;
    return ok_;
}

void Calibrator::add_rain_peak(std::uint16_t raw_peak_mv) {
    if (state_ == State::Running && kind_ == Kind::DropTest && n_peaks_ < kMaxPeaks) {
        peaks_[n_peaks_++] = raw_peak_mv;
    }
}

void Calibrator::add_sample(const Sample& s, const core::StationConfig& cfg) {
    last_ = s;
    have_last_ = true;
    last_sun_ok_ = s.sun_ok;
    last_sun_az_ = s.sun_az_deg;
    last_sun_alt_ = s.sun_alt_deg;
    if (state_ != State::Running) return;
    last_ms_ = s.t_ms;
    auto push = [this](float a, float b, float c) {
        if (n_ < kMaxSamples) {
            a_[n_] = a;
            b_[n_] = b;
            c_[n_] = c;
            ++n_;
        }
    };
    switch (kind_) {
        case Kind::Compass:
            if (s.mag_ok) push(s.mag_ut.x, s.mag_ut.y, s.mag_ut.z);
            break;
        case Kind::Heading:
            if (s.heading_ok) push(s.heading_deg, 0.0F, 0.0F);
            break;
        case Kind::CarRun:
            if (s.wind_ok) push(s.wind_raw_m_s, 0.0F, 0.0F);
            break;
        case Kind::RainGauge:
            if (s.rain_ok) {
                if (rain_seen_ && s.rain_total_mm > rain_last_) rain_acc_mm_ += s.rain_total_mm - rain_last_;
                rain_last_ = s.rain_total_mm;
                rain_seen_ = true;
            }
            break;
        case Kind::Pressure:
            if (s.pressure_ok && s.temp_ok) push(s.station_hpa, s.temp_c, s.wind_ok ? s.wind_m_s : 0.0F);
            break;
        case Kind::ClearSky:
            if (s.lux_ok && s.sun_ok) push(s.lux_raw, s.sun_alt_deg, 0.0F);
            break;
        case Kind::UvReference:
            push(s.guvb_ok ? s.guvb_counts : NAN, s.ltr_uvi_ok ? s.ltr_uvi_raw : NAN, 0.0F);
            break;
        case Kind::SaltRh:
            if (s.rh_ok && s.temp_ok) push(s.rh_pct, s.temp_c, 0.0F);
            break;
        case Kind::IcePoint:
            if (s.temp_ok) push(s.temp_c, 0.0F, 0.0F);
            break;
        case Kind::Co2FreshAir:
            if (s.co2_ok) push(s.co2_ppm, 0.0F, 0.0F);
            break;
        default:
            break;
    }
    const std::uint32_t dur = duration_s(kind_);
    const std::uint32_t elapsed = (s.t_ms - start_ms_) / 1000U;
    if ((dur > 0 && elapsed >= dur) || (kind_ == Kind::DropTest && elapsed >= 900U)) {
        compute(cfg);
        state_ = State::Done;
    }
}

void Calibrator::compute(const core::StationConfig& cfg) {
    ok_ = false;
    n_sug_ = 0;
    switch (kind_) {
        case Kind::Compass: {
            const CompassFit f = fit_compass(a_, b_, n_);
            if (!f.ok) {
                set_message("Compass: %s (%d of 12 directions, field %.1f uT).", f.reason, f.coverage_bins,
                            static_cast<double>(f.radius_ut));
                return;
            }
            ok_ = true;
            suggest("mag_offset_x", f.offset_x);
            suggest("mag_offset_y", f.offset_y);
            suggest("mag_scale_x", f.scale_x);
            suggest("mag_scale_y", f.scale_y);
            set_message("Compass fitted: offset %.1f / %.1f uT, scale %.3f / %.3f, horizontal field %.1f uT, %d of 12 "
                        "directions. Apply, then mount the station and do the North reference.",
                        static_cast<double>(f.offset_x), static_cast<double>(f.offset_y), static_cast<double>(f.scale_x),
                        static_cast<double>(f.scale_y), static_cast<double>(f.radius_ut), f.coverage_bins);
            return;
        }
        case Kind::Heading: {
            if (fixed_) {
                ok_ = true;
                suggest("heading_mode", 1.0F);
                suggest("heading_fixed_deg", bearing_);
                set_message("Fixed heading: the N mark points to %.1f deg; the magnetometer will be ignored.",
                            static_cast<double>(bearing_));
                return;
            }
            if (count_finite(a_, n_) < 8) {
                set_message("North reference: no magnetometer heading (is the crown connected?).");
                return;
            }
            const float mean = circular_mean_deg(a_, n_);
            const float delta = wrap180(bearing_ - mean);
            ok_ = true;
            suggest("heading_mode", 0.0F);
            suggest("heading_offset_deg", wrap180(cfg.heading_offset_deg + delta));
            set_message("North reference: N mark at %.1f deg, compass said %.1f deg -> correction %+.1f deg.%s",
                        static_cast<double>(bearing_), static_cast<double>(mean), static_cast<double>(delta),
                        std::fabs(delta) > 30.0F ? " Large: do the compass spin first, or use a fixed heading if the "
                                                   "station never turns on its mast." : "");
            return;
        }
        case Kind::CarRun: {
            const int k = count_finite(a_, n_);
            const float station = mean_of(a_, n_);
            if (k < 20 || finish_p1_ < 2.0F) {
                set_message("Run discarded: needs >= 20 s of wind data and a reference speed >= 2 m/s (7 km/h).");
                return;
            }
            if (n_runs_ < kMaxRuns) runs_[n_runs_++] = CarRunRecord{finish_p1_, station, static_cast<std::uint32_t>(k)};
            double sr = 0.0, ss = 0.0;
            for (int i = 0; i < n_runs_; ++i) {
                sr += runs_[i].ref_m_s;
                ss += runs_[i].station_raw_m_s;
            }
            const float gain = ss > 0.0 ? static_cast<float>(sr / ss) : NAN;
            if (n_runs_ < 2) {
                set_message("Run 1 saved: reference %.1f m/s, station %.2f m/s (before gain). Now the same in the "
                            "opposite direction (or a second anemometer reading).", static_cast<double>(finish_p1_),
                            static_cast<double>(station));
                return;
            }
            if (!(gain >= 0.5F && gain <= 2.0F)) {
                set_message("Gain %.3f is outside 0.5..2: check the mounting height above the roof and the crown "
                            "calibration (still air) first.", static_cast<double>(gain));
                return;
            }
            ok_ = true;
            suggest("wind_speed_gain", gain);
            set_message("%d runs: wind speed gain %.3f (reference / station). Apply, or add more runs.", n_runs_,
                        static_cast<double>(gain));
            return;
        }
        case Kind::DropTest: {
            const float med = median_u16(peaks_, n_peaks_);
            const float vol = 1000.0F / p1_;
            const float expected = expected_drop_mv(vol, p2_);
            if (n_peaks_ < 8) {
                set_message("Drop test: only %d impacts registered (need >= 8). Drop closer to the lid centre, or "
                            "from higher up.", n_peaks_);
                return;
            }
            const float scale = expected / med;
            if (!(scale >= 0.1F && scale <= 10.0F)) {
                set_message("Drop test: scale %.2f is implausible (median %.0f mV, expected %.0f mV). Re-count the "
                            "drops per ml.", static_cast<double>(scale), static_cast<double>(med),
                            static_cast<double>(expected));
                return;
            }
            ok_ = true;
            suggest("rain_impact_scale", scale);
            set_message("Drop test: %d impacts, median %.0f mV; a %.1f mm drop from %.2f m should give %.0f mV -> "
                        "rain impact scale %.2f.", n_peaks_, static_cast<double>(med),
                        static_cast<double>(std::cbrt(6.0F * vol / kPi)), static_cast<double>(p2_),
                        static_cast<double>(expected), static_cast<double>(scale));
            return;
        }
        case Kind::RainGauge: {
            const float ref = finish_p1_;
            if (rain_acc_mm_ < 1.0F || ref < 1.0F) {
                set_message("Rain comparison: station %.2f mm, gauge %.2f mm; both need >= 1 mm. Keep it running "
                            "through more rain.", static_cast<double>(rain_acc_mm_), static_cast<double>(ref));
                return;
            }
            const float gain = cfg.rain_gain * ref / rain_acc_mm_;
            if (!(gain >= 0.2F && gain <= 5.0F)) {
                set_message("Rain gain %.2f is implausible: do the drop test first.", static_cast<double>(gain));
                return;
            }
            ok_ = true;
            suggest("rain_gain", gain);
            set_message("Rain: gauge %.1f mm, station %.1f mm -> rain gain %.3f (was %.3f).", static_cast<double>(ref),
                        static_cast<double>(rain_acc_mm_), static_cast<double>(gain), static_cast<double>(cfg.rain_gain));
            return;
        }
        case Kind::Pressure: {
            if (p2_ == 1.0F) {
                ok_ = true;
                suggest("altitude_m", p1_ + p3_);
                set_message("Elevation: ground %.1f m + %.1f m above it = %.1f m (was %.1f m).", static_cast<double>(p1_),
                            static_cast<double>(p3_), static_cast<double>(p1_ + p3_), static_cast<double>(cfg.altitude_m));
                return;
            }
            if (n_ < 30) {
                set_message("Pressure: needs >= 30 s of barometer and temperature data.");
                return;
            }
            if (mean_of(c_, n_) > 5.0F) {
                set_message("Pressure: too windy (%.1f m/s); repeat in calm weather.", static_cast<double>(mean_of(c_, n_)));
                return;
            }
            const float p = mean_of(a_, n_), t = mean_of(b_, n_);
            const float h = elevation_from_qnh_m(p, p1_, t);
            if (!(h > -100.0F && h < 3000.0F)) {
                set_message("Pressure: elevation %.0f m is implausible; check the QNH (hPa, not inHg).", static_cast<double>(h));
                return;
            }
            ok_ = true;
            suggest("altitude_m", h);
            set_message("Pressure: station %.2f hPa at %.1f C, reference %.1f hPa at sea level -> elevation %.1f m (was %.1f m).",
                        static_cast<double>(p), static_cast<double>(t), static_cast<double>(p1_), static_cast<double>(h),
                        static_cast<double>(cfg.altitude_m));
            return;
        }
        case Kind::ClearSky: {
            int k = 0;
            for (int i = 0; i < n_; ++i) k += b_[i] >= 25.0F ? 1 : 0;
            if (k < 60) {
                set_message("Clear sky: the sun must be >= 25 deg high for the whole 2 minutes (location set).");
                return;
            }
            const float lux = mean_of(a_, n_), alt = mean_of(b_, n_);
            const float cv = lux > 0.0F ? std_of(a_, n_) / lux : 1.0F;
            if (cv > 0.04F) {
                set_message("Clear sky: the light varied by %.0f %% (clouds or haze moving through); retry under a "
                            "completely clear sky.", static_cast<double>(100.0F * cv));
                return;
            }
            const float expected = astro::Astronomy::clear_sky_ghi_w_m2(alt) * kLuxPerWm2;
            const float tr = lux / expected;
            if (!(tr >= 0.02F && tr <= 1.0F)) {
                set_message("Clear sky: transmission %.3f is implausible (%.0f lx behind the cover, %.0f lx expected).",
                            static_cast<double>(tr), static_cast<double>(lux), static_cast<double>(expected));
                return;
            }
            ok_ = true;
            suggest("optics_transmission", tr);
            set_message("Clear sky at %.0f deg sun: %.0f lx behind the cover, %.0f lx expected -> cover transmission "
                        "%.3f (was %.3f). The clear-sky model is good to about 10-15 %%.", static_cast<double>(alt),
                        static_cast<double>(lux), static_cast<double>(expected), static_cast<double>(tr),
                        static_cast<double>(cfg.optics_transmission));
            return;
        }
        case Kind::UvReference: {
            const int kg = count_finite(a_, n_);
            if (kg < 30) {
                set_message("UV: no GUVB-C31SM readings (sensor fitted? dome board connected?).");
                return;
            }
            const float counts = mean_of(a_, n_);
            const float ltr = mean_of(b_, n_);
            if (counts <= 0.0F) {
                set_message("UV: the GUVB-C31SM reads no UV; repeat in sunshine.");
                return;
            }
            if (p2_ == 1.0F) {   // self-reference: LTR-390 factory UVI behind the cover, divided by its transmission
                const float tr = cfg.optics_transmission > 0.0F ? cfg.optics_transmission : 1.0F;
                const float ref = std::isfinite(ltr) ? ltr / tr : NAN;
                if (count_finite(b_, n_) < 30 || !(ref >= 1.0F)) {
                    set_message("UV self-reference: the LTR-390 sees UV index %.1f; needs >= 1 (sunny day near noon).",
                                static_cast<double>(std::isfinite(ref) ? ref : 0.0F));
                    return;
                }
                ok_ = true;
                suggest("guvb_counts_per_uvi", counts / ref);
                set_message("UV self-reference: LTR-390 UV index %.2f (through the cover, transmission %.3f) -> GUVB "
                            "%.2f counts per UVI.%s", static_cast<double>(ref), static_cast<double>(tr),
                            static_cast<double>(counts / ref),
                            cfg.cal_unix[static_cast<int>(Kind::ClearSky)] == 0 ? " Do the Light cover step first: "
                                                                                  "this result depends on it." : "");
                return;
            }
            ok_ = true;
            suggest("guvb_counts_per_uvi", counts / p1_);
            if (std::isfinite(ltr)) {
                set_message("UV index %.1f: GUVB %.1f counts -> %.2f counts per UVI. LTR-390 behind the cover read "
                            "%.2f (its UV transmission %.3f).", static_cast<double>(p1_), static_cast<double>(counts),
                            static_cast<double>(counts / p1_), static_cast<double>(ltr), static_cast<double>(ltr / p1_));
            } else {
                set_message("UV index %.1f: GUVB %.1f counts -> %.2f counts per UVI.", static_cast<double>(p1_),
                            static_cast<double>(counts), static_cast<double>(counts / p1_));
            }
            return;
        }
        case Kind::SaltRh: {
            if (n_ < 300) {
                set_message("Salt test: needs >= 5 minutes of readings.");
                return;
            }
            const int w = std::min(60, n_ / 3);
            const float drift = mean_of(a_ + n_ - w, w) - mean_of(a_, w);
            if (std::fabs(drift) > 1.0F) {
                set_message("Salt test: RH still moving (%+.1f %% in 10 min); leave the jar closed longer (6-12 h) "
                            "and start again.", static_cast<double>(drift));
                return;
            }
            const float rh = mean_of(a_ + n_ - w, w), t = mean_of(b_, n_);
            const float expected = salt_rh_pct(t);
            const float off = expected - rh;
            if (std::fabs(off) > 8.0F) {
                set_message("Salt test: %.1f %% read vs %.1f %% expected: too far off; is the jar sealed and the salt "
                            "wet?", static_cast<double>(rh), static_cast<double>(expected));
                return;
            }
            ok_ = true;
            suggest("rh_offset_pct", off);
            set_message("Salt test at %.1f C: sensor %.1f %% RH, saturated salt %.1f %% -> RH offset %+.1f %%.%s",
                        static_cast<double>(t), static_cast<double>(rh), static_cast<double>(expected),
                        static_cast<double>(off), std::fabs(off) <= 1.8F ? " Within the SHT40 spec (+/-1.8 %)." : "");
            return;
        }
        case Kind::IcePoint: {
            if (n_ < 120) {
                set_message("Ice point: needs >= 2 minutes of readings.");
                return;
            }
            const float sd = std_of(a_ + n_ / 2, n_ - n_ / 2);
            const float t = mean_of(a_ + n_ / 2, n_ - n_ / 2);
            if (sd > 0.15F || std::fabs(t) > 2.0F) {
                set_message("Ice point: %.2f C +/- %.2f: not settled at 0 C (more ice, stir, keep the bag submerged, "
                            "wait 20 min).", static_cast<double>(t), static_cast<double>(sd));
                return;
            }
            ok_ = true;
            suggest("temp_offset_c", -t);
            set_message("Ice point: sensor %.2f C in melting ice -> temperature offset %+.2f C.%s", static_cast<double>(t),
                        static_cast<double>(-t), std::fabs(t) <= 0.2F ? " Within the SHT40 spec (+/-0.2 C)." : "");
            return;
        }
        case Kind::Co2FreshAir: {
            if (n_ < 120) {
                set_message("Fresh air: needs >= 2 minutes of CO2 readings (one every 5 s).");
                return;
            }
            const float m = mean_of(a_, n_), sd = std_of(a_, n_);
            if (sd > 20.0F) {
                set_message("Fresh air: CO2 varies by +/-%.0f ppm (people / cars / chimneys nearby?); retry.",
                            static_cast<double>(sd));
                return;
            }
            if (std::fabs(m - p1_) > 250.0F) {
                set_message("Fresh air: %.0f ppm is too far from %.0f ppm for outdoor air.", static_cast<double>(m),
                            static_cast<double>(p1_));
                return;
            }
            ok_ = true;
            set_message("Fresh air: SCD41 reads %.0f +/- %.0f ppm. Apply recalibrates it to %.0f ppm.", static_cast<double>(m),
                        static_cast<double>(sd), static_cast<double>(p1_));
            return;
        }
        default:
            set_message("unknown procedure");
            return;
    }
}

bool Calibrator::apply(core::StationConfig& cfg, std::int64_t unix_s) {
    if (state_ != State::Done || !ok_ || applied_) return false;
    for (int i = 0; i < n_sug_; ++i) {
        const char* k = sug_key_[i];
        const float v = sug_val_[i];
        if (std::strcmp(k, "mag_offset_x") == 0) cfg.mag_offset_ut[0] = v;
        else if (std::strcmp(k, "mag_offset_y") == 0) cfg.mag_offset_ut[1] = v;
        else if (std::strcmp(k, "mag_scale_x") == 0) cfg.mag_scale[0] = v;
        else if (std::strcmp(k, "mag_scale_y") == 0) cfg.mag_scale[1] = v;
        else if (std::strcmp(k, "heading_mode") == 0) cfg.heading_mode = static_cast<std::uint8_t>(v);
        else if (std::strcmp(k, "heading_fixed_deg") == 0) cfg.heading_fixed_deg = v;
        else if (std::strcmp(k, "heading_offset_deg") == 0) cfg.heading_offset_deg = v;
        else if (std::strcmp(k, "wind_speed_gain") == 0) cfg.wind_speed_gain = v;
        else if (std::strcmp(k, "rain_impact_scale") == 0) cfg.rain_impact_scale = v;
        else if (std::strcmp(k, "rain_gain") == 0) cfg.rain_gain = v;
        else if (std::strcmp(k, "altitude_m") == 0) cfg.altitude_m = v;
        else if (std::strcmp(k, "optics_transmission") == 0) cfg.optics_transmission = v;
        else if (std::strcmp(k, "guvb_counts_per_uvi") == 0) cfg.guvb_counts_per_uvi = v;
        else if (std::strcmp(k, "rh_offset_pct") == 0) cfg.rh_offset_pct = v;
        else if (std::strcmp(k, "temp_offset_c") == 0) cfg.temp_offset_c = v;
    }
    if (kind_ == Kind::Co2FreshAir) {
        frc_pending_ = true;
        frc_ppm_ = p1_;
        std::snprintf(frc_msg_, sizeof frc_msg_, "pending (%.0f ppm)", static_cast<double>(p1_));
    }
    if (kind_ == Kind::CarRun) n_runs_ = 0;
    const auto idx = static_cast<std::size_t>(kind_);
    if (idx < sizeof(cfg.cal_unix) / sizeof(cfg.cal_unix[0])) {
        cfg.cal_unix[idx] = unix_s > 0 ? static_cast<std::uint32_t>(unix_s) : 1U;
    }
    applied_ = true;
    return true;
}

bool Calibrator::take_frc_request(float& ppm) {
    if (!frc_pending_) return false;
    frc_pending_ = false;
    ppm = frc_ppm_;
    return true;
}

void Calibrator::report_frc(bool ok, int correction_ppm) {
    if (ok) {
        std::snprintf(frc_msg_, sizeof frc_msg_, "done: correction %+d ppm", correction_ppm);
    } else {
        std::snprintf(frc_msg_, sizeof frc_msg_, "failed (SCD41 must run >= 3 min first)");
    }
}

// ------------------------------------------------------------------------------------------------- JSON
std::string Calibrator::status_json(const core::StationConfig& cfg, std::uint32_t now_ms) const {
    JsonDocument doc;
    doc["kind"] = kind_name(kind_);
    doc["state"] = state_ == State::Running ? "running" : (state_ == State::Done ? "done" : "idle");
    doc["ok"] = ok_;
    doc["applied"] = applied_;
    const std::uint32_t ref_ms = state_ == State::Running ? now_ms : last_ms_;
    doc["elapsed_s"] = state_ == State::Idle ? 0U : (ref_ms - start_ms_) / 1000U;
    doc["duration_s"] = duration_s(kind_);
    doc["samples"] = n_;
    doc["message"] = message_;
    JsonObject sug = doc["suggest"].to<JsonObject>();
    for (int i = 0; i < n_sug_; ++i) sug[sug_key_[i]] = sug_val_[i];

    JsonObject live = doc["live"].to<JsonObject>();
    auto put = [&live](const char* k, float v) {
        if (std::isfinite(v)) live[k] = std::round(v * 1000.0F) / 1000.0F;
    };
    switch (kind_) {
        case Kind::Compass: {
            const CompassFit f = fit_compass(a_, b_, n_);
            live["coverage_bins"] = f.coverage_bins;
            put("field_ut", n_ >= 30 ? f.radius_ut : NAN);
            break;
        }
        case Kind::Heading:
            put("bearing_deg", bearing_);
            put("compass_deg", circular_mean_deg(a_, n_));
            break;
        case Kind::CarRun:
            put("station_raw_m_s", mean_of(a_, n_));
            live["seconds"] = count_finite(a_, n_);
            break;
        case Kind::DropTest:
            live["impacts"] = n_peaks_;
            put("median_mv", median_u16(peaks_, n_peaks_));
            put("expected_mv", expected_drop_mv(1000.0F / std::max(1.0F, p1_), p2_));
            break;
        case Kind::RainGauge:
            put("station_mm", rain_acc_mm_);
            break;
        case Kind::Pressure:
            put("station_hpa", mean_of(a_, n_));
            put("elevation_m", elevation_from_qnh_m(mean_of(a_, n_), p1_, mean_of(b_, n_)));
            break;
        case Kind::ClearSky: {
            const float alt = mean_of(b_, n_);
            put("lux_behind_cover", mean_of(a_, n_));
            put("expected_lux", std::isfinite(alt) ? astro::Astronomy::clear_sky_ghi_w_m2(alt) * kLuxPerWm2 : NAN);
            put("sun_alt_deg", alt);
            put("variation_pct", n_ > 1 ? 100.0F * std_of(a_, n_) / mean_of(a_, n_) : NAN);
            break;
        }
        case Kind::UvReference:
            put("guvb_counts", mean_of(a_, n_));
            put("ltr_uvi_behind_cover", mean_of(b_, n_));
            break;
        case Kind::SaltRh: {
            const int w = std::min(60, n_);
            put("rh_pct", w ? mean_of(a_ + n_ - w, w) : NAN);
            put("expected_pct", n_ ? salt_rh_pct(mean_of(b_, n_)) : NAN);
            break;
        }
        case Kind::IcePoint: {
            const int w = std::min(60, n_);
            put("temp_c", w ? mean_of(a_ + n_ - w, w) : NAN);
            break;
        }
        case Kind::Co2FreshAir:
            put("co2_ppm", mean_of(a_, n_));
            put("co2_std_ppm", n_ > 1 ? std_of(a_, n_) : NAN);
            break;
        default:
            break;
    }
    JsonArray runs = doc["runs"].to<JsonArray>();
    for (int i = 0; i < n_runs_; ++i) {
        JsonObject r = runs.add<JsonObject>();
        r["ref_m_s"] = runs_[i].ref_m_s;
        r["station_raw_m_s"] = runs_[i].station_raw_m_s;
        r["seconds"] = runs_[i].seconds;
    }
    doc["frc"] = frc_msg_;

    JsonObject now = doc["now"].to<JsonObject>();
    if (have_last_) {
        auto putn = [&now](const char* k, bool ok, float v) {
            if (ok && std::isfinite(v)) now[k] = std::round(v * 100.0F) / 100.0F;
        };
        putn("heading_deg", last_.heading_ok, last_.heading_deg);
        putn("tilt_deg", last_.tilt_ok, last_.tilt_deg);
        putn("sun_az_deg", last_.sun_ok, last_.sun_az_deg);
        putn("sun_alt_deg", last_.sun_ok, last_.sun_alt_deg);
        putn("shadow_deg", last_.sun_ok, wrap360(last_.sun_az_deg + 180.0F));
        putn("solar_noon_min", last_.solar_noon_ok, last_.solar_noon_min);
        putn("wind_m_s", last_.wind_ok, last_.wind_m_s);
        putn("station_hpa", last_.pressure_ok, last_.station_hpa);
        putn("temp_raw_c", last_.temp_ok, last_.temp_c);
        putn("rh_raw_pct", last_.rh_ok, last_.rh_pct);
        putn("lux_behind_cover", last_.lux_ok, last_.lux_raw);
        putn("co2_ppm", last_.co2_ok, last_.co2_ppm);
        putn("rain_total_mm", last_.rain_ok, last_.rain_total_mm);
        now["mag_ok"] = last_.mag_ok;
    }

    JsonObject cur = doc["config"].to<JsonObject>();
    cur["heading_mode"] = cfg.heading_mode;
    cur["heading_fixed_deg"] = cfg.heading_fixed_deg;
    cur["heading_offset_deg"] = cfg.heading_offset_deg;
    cur["declination_deg"] = cfg.declination_deg;
    cur["mag_offset_x"] = cfg.mag_offset_ut[0];
    cur["mag_offset_y"] = cfg.mag_offset_ut[1];
    cur["mag_scale_x"] = cfg.mag_scale[0];
    cur["mag_scale_y"] = cfg.mag_scale[1];
    cur["wind_speed_gain"] = cfg.wind_speed_gain;
    cur["us_delay_us"] = cfg.us_delay_us;
    cur["rain_impact_scale"] = cfg.rain_impact_scale;
    cur["rain_gain"] = cfg.rain_gain;
    cur["altitude_m"] = cfg.altitude_m;
    cur["optics_transmission"] = cfg.optics_transmission;
    cur["guvb_counts_per_uvi"] = cfg.guvb_counts_per_uvi;
    cur["temp_offset_c"] = cfg.temp_offset_c;
    cur["rh_offset_pct"] = cfg.rh_offset_pct;
    cur["location_set"] = cfg.location_set;
    JsonObject stamps = doc["stamps"].to<JsonObject>();
    for (std::size_t i = 1; i < static_cast<std::size_t>(Kind::Count); ++i) {
        stamps[kNames[i]] = cfg.cal_unix[i];
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string Calibrator::handle(const char* body, core::StationConfig& cfg, std::int64_t unix_s, std::uint32_t now_ms,
                               bool& cfg_changed) {
    cfg_changed = false;
    JsonDocument in;
    if (body != nullptr && body[0] != '\0' && deserializeJson(in, body) == DeserializationError::Ok) {
        const char* cmd = in["cmd"] | "";
        const float p1 = in["p1"] | 0.0F, p2 = in["p2"] | 0.0F, p3 = in["p3"] | 0.0F;
        if (std::strcmp(cmd, "start") == 0) {
            fixed_ = in["fixed"] | false;
            start(kind_from_name(in["kind"] | ""), p1, p2, p3, cfg, now_ms);
        } else if (std::strcmp(cmd, "finish") == 0) {
            finish(p1, cfg, now_ms);
        } else if (std::strcmp(cmd, "apply") == 0) {
            cfg_changed = apply(cfg, unix_s);
        } else if (std::strcmp(cmd, "cancel") == 0) {
            cancel();
        } else if (std::strcmp(cmd, "clear_runs") == 0) {
            clear_runs();
        }
    }
    return status_json(cfg, now_ms);
}

}  // namespace calibration
}  // namespace station
