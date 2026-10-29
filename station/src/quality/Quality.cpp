#include "Quality.hpp"

#include "../calibration/Calibration.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace station {
namespace quality {

using core::HealthStatus;
using core::QcFlag;
using core::QcMetric;

namespace {
constexpr std::uint32_t kSpikeHoldS = 600;
constexpr int kCompassKind = static_cast<int>(calibration::Kind::Compass);   // cal_unix index

// Range / one-minute step / persistence limits. Ranges are the sensors' and the atmosphere's; steps are about a third
// of the Oklahoma Mesonet 5-min values; persistence windows are long enough for calm nights and fog.
const Limits kLimits[] = {
    {-50.0F, 60.0F, 3.0F, 0.02F, 3U * 3600U},      // TempC        (SHT40 -40..125; air)
    {0.0F, 100.5F, 25.0F, 0.05F, 6U * 3600U},     // RhPct        (skipped at >= 99 %: fog)
    {500.0F, 1100.0F, 3.0F, 0.02F, 3U * 3600U},   // PressureHpa  (station pressure)
    {0.0F, 75.0F, 0.0F, 0.005F, 1800U},           // WindSpeed    (gusts: no step test; skipped below 0.5 m/s)
    {0.0F, 360.0F, 0.0F, 0.5F, 1800U},            // WindDir      (persistence only above 2 m/s)
    {0.0F, 500.0F, 0.0F, 0.0F, 0U},               // RainRate
    {0.0F, 1500.0F, 0.0F, 0.0F, 0U},              // Irradiance   (+ clear-sky / night consistency)
    {0.0F, 20.0F, 0.0F, 0.0F, 0U},                // UvIndex      (+ night consistency)
    {0.0F, 1000.0F, 300.0F, 0.1F, 6U * 3600U},    // Pm25         (persistence skipped below 2 ug/m3)
    {250.0F, 10000.0F, 500.0F, 1.0F, 2U * 3600U}, // Co2
};

const char* const kKeys[] = {"temp_c", "rh_pct", "pressure_hpa", "wind_speed", "wind_dir", "rain_rate",
                             "irradiance", "uv_index", "pm2_5", "co2_ppm"};

HealthStatus worse(HealthStatus a, HealthStatus b) {
    auto rank = [](HealthStatus s) { return s == HealthStatus::Fail ? 3 : (s == HealthStatus::Warn ? 2 : (s == HealthStatus::Ok ? 1 : 0)); };
    return rank(a) >= rank(b) ? a : b;
}
}  // namespace

const char* flag_name(QcFlag f) {
    switch (f) {
        case QcFlag::Good: return "good";
        case QcFlag::Suspect: return "suspect";
        case QcFlag::Fail: return "fail";
        default: return "missing";
    }
}

const char* status_name(HealthStatus s) {
    switch (s) {
        case HealthStatus::Ok: return "ok";
        case HealthStatus::Warn: return "warn";
        case HealthStatus::Fail: return "fail";
        default: return "off";
    }
}

const char* metric_key(QcMetric m) {
    const auto i = static_cast<std::size_t>(m);
    return i < sizeof(kKeys) / sizeof(kKeys[0]) ? kKeys[i] : "";
}

const Limits& limits(QcMetric m) {
    return kLimits[static_cast<std::size_t>(m)];
}

core::QcValue QualityControl::check(QcMetric m, bool ok, float v, bool persistence_applies) {
    Track& t = tracks_[static_cast<int>(m)];
    const Limits& L = limits(m);
    if (!ok || !std::isfinite(v)) {
        t = Track{};   // a gap restarts the step and persistence tests
        return {QcFlag::Missing, "no data"};
    }
    if (v < L.lo || v > L.hi) {
        return {QcFlag::Fail, "out of range"};
    }
    auto diff = [m](float a, float b) {
        const float d = std::fabs(a - b);
        return m == QcMetric::WindDir ? std::min(d, 360.0F - d) : d;
    };
    if (L.step_per_min > 0.0F) {
        if (!t.has_ref) {
            t.has_ref = true;
            t.ref = v;
            t.ref_s = t_s_;
        } else if (t_s_ - t.ref_s >= 60U) {
            if (diff(v, t.ref) > L.step_per_min) {
                t.suspect_until_s = t_s_ + kSpikeHoldS;
                t.suspect_reason = "sudden jump (spike)";
            }
            t.ref = v;
            t.ref_s = t_s_;
        }
    }
    if (L.stuck_s > 0U) {
        if (!persistence_applies || !t.has_stuck || diff(v, t.stuck_ref) > L.stuck_eps) {
            t.has_stuck = true;
            t.stuck_ref = v;
            t.stuck_since_s = t_s_;
        } else if (t_s_ - t.stuck_since_s >= L.stuck_s) {
            return {QcFlag::Suspect, "no change for hours (stuck)"};
        }
    }
    if (t_s_ < t.suspect_until_s) {
        return {QcFlag::Suspect, t.suspect_reason};
    }
    return {QcFlag::Good, ""};
}

void QualityControl::step(const core::RawState& raw, const core::StationConfig& cfg, core::DerivedState& d) {
    ++t_s_;
    auto& q = d.quality.qc;
    auto at = [&q](QcMetric m) -> core::QcValue& { return q[static_cast<std::size_t>(m)]; };
    auto suspect = [&at](QcMetric m, const char* why) {
        if (at(m).flag == QcFlag::Good) at(m) = {QcFlag::Suspect, why};
    };
    const auto& th = d.thermo;
    const auto& w = d.wind;
    const auto& s = d.solar;
    at(QcMetric::TempC) = check(QcMetric::TempC, th.temp_c.ok, th.temp_c.v, true);
    at(QcMetric::RhPct) = check(QcMetric::RhPct, th.rh_pct.ok, th.rh_pct.v, th.rh_pct.v < 99.0F);
    at(QcMetric::PressureHpa) = check(QcMetric::PressureHpa, d.baro.station_hpa.ok, d.baro.station_hpa.v, true);
    at(QcMetric::WindSpeed) = check(QcMetric::WindSpeed, w.speed_m_s.ok, w.speed_m_s.v, w.speed_m_s.v >= 0.5F);
    at(QcMetric::WindDir) = check(QcMetric::WindDir, w.dir_deg.ok, w.dir_deg.v, w.speed_m_s.ok && w.speed_m_s.v > 2.0F);
    at(QcMetric::RainRate) = check(QcMetric::RainRate, d.rain.sensor_ok, d.rain.rate_mm_h, false);
    at(QcMetric::Irradiance) = check(QcMetric::Irradiance, s.irradiance_w_m2.ok, s.irradiance_w_m2.v, false);
    at(QcMetric::UvIndex) = check(QcMetric::UvIndex, s.uv_index.ok, s.uv_index.v, false);
    at(QcMetric::Pm25) = check(QcMetric::Pm25, d.air.pm25_ug_m3.ok, d.air.pm25_ug_m3.v, d.air.pm25_ug_m3.v >= 2.0F);
    at(QcMetric::Co2) = check(QcMetric::Co2, d.air.co2_ppm.ok, d.air.co2_ppm.v, true);

    // consistency checks
    const bool sun = d.astro.location_set && d.astro.sun_alt_deg.ok;
    if (sun && d.astro.sun_alt_deg.v < -3.0F) {
        if (s.irradiance_w_m2.ok && s.irradiance_w_m2.v > 15.0F) suspect(QcMetric::Irradiance, "sunlight at night");
        if (s.uv_index.ok && s.uv_index.v > 0.3F) suspect(QcMetric::UvIndex, "UV at night");
    }
    if (s.irradiance_w_m2.ok && s.clear_sky_w_m2.ok && s.irradiance_w_m2.v > 1.5F * s.clear_sky_w_m2.v + 150.0F) {
        suspect(QcMetric::Irradiance, "above the clear-sky limit (cover transmission?)");
    }
    if (d.rain.rate_mm_h > 0.5F && th.rh_pct.ok && th.rh_pct.v < 40.0F) {
        suspect(QcMetric::RainRate, "rain at low humidity (vibration / insects?)");
    }
    if (w.acoustic_temp_c.ok && th.temp_c.ok && std::fabs(w.acoustic_temp_c.v - th.temp_c.v) > 6.0F) {
        suspect(QcMetric::WindSpeed, "sonic temperature far from the air temperature (transducers?)");
    }
    if (d.air.co2_ppm.ok && d.air.co2_ppm.v < 350.0F) {
        suspect(QcMetric::Co2, "below the outdoor background");
    }
    health(raw, cfg, d);
}

void QualityControl::health(const core::RawState& raw, const core::StationConfig& cfg, core::DerivedState& d) {
    auto& Q = d.quality;
    Q.n_health = 0;
    Q.summary = HealthStatus::Off;
    auto add = [&Q](const char* id, const char* label, HealthStatus st, const char* fmt, ...) {
        if (Q.n_health >= core::QualityDerived::kMaxHealth) return;
        core::HealthEntry& e = Q.health[Q.n_health++];
        e.id = id;
        e.label = label;
        e.status = st;
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(e.detail, sizeof e.detail, fmt, ap);
        va_end(ap);
        Q.summary = worse(Q.summary, st);
    };
    auto qc = [&Q](QcMetric m) { return Q.qc[static_cast<std::size_t>(m)]; };
    auto qc_status = [](const core::QcValue& v) {
        return v.flag == QcFlag::Good ? HealthStatus::Ok : (v.flag == QcFlag::Suspect ? HealthStatus::Warn : HealthStatus::Fail);
    };
    const std::uint32_t now = raw.now_ms;
    const auto& h = d.heater;

    // power
    if (!h.vin_v.ok) {
        add("supply", "Supply", HealthStatus::Fail, "no reading");
    } else {
        const float v = h.vin_v.v;
        add("supply", "Supply", v < 10.5F ? HealthStatus::Fail : ((v < 11.5F || v > 32.5F) ? HealthStatus::Warn : HealthStatus::Ok),
            "%s", v < 11.5F ? "low input voltage" : (v > 32.5F ? "above the 32 V rating" : ""));
    }
    if (!h.rail_logic_v.ok) {
        add("rail_5v", "+5V rail", HealthStatus::Fail, "no reading (ADS1115?)");
    } else {
        const float v = h.rail_logic_v.v;
        add("rail_5v", "+5V rail", (v < 4.6F || v > 5.4F) ? HealthStatus::Fail : ((v < 4.8F || v > 5.25F) ? HealthStatus::Warn : HealthStatus::Ok),
            "%s", (v < 4.8F || v > 5.25F) ? "outside 4.8-5.25 V" : "");
    }
    if (h.rail_heat_v.ok) {
        const float v = h.rail_heat_v.v;
        const float expect = h.vin_v.ok ? std::min(11.5F, h.vin_v.v - 1.5F) : 11.5F;
        add("rail_heat", "+12V_HEAT rail", v < 9.0F ? HealthStatus::Fail : (v < expect ? HealthStatus::Warn : HealthStatus::Ok),
            "%s", v < 9.0F ? "undervoltage: heaters off" : (v < expect ? "low for this input voltage" : ""));
    } else {
        add("rail_heat", "+12V_HEAT rail", HealthStatus::Fail, "no reading");
    }

    // sensors
    if (raw.wind.second.fresh(now, 5000)) {
        const core::QcValue ws = qc(QcMetric::WindSpeed);
        add("wind", "Ultrasonic wind (crown)", ws.flag == QcFlag::Good || ws.flag == QcFlag::Missing ? HealthStatus::Ok : qc_status(ws),
            "%s", ws.flag == QcFlag::Good || ws.flag == QcFlag::Missing ? "" : ws.reason);
    } else {
        add("wind", "Ultrasonic wind (crown)", HealthStatus::Fail, "no data from the crown");
    }
    add("rain", "Rain sensor (crown)", d.rain.sensor_ok ? qc_status(qc(QcMetric::RainRate)) : HealthStatus::Fail, "%s",
        !d.rain.sensor_ok ? "no rain data" : (qc(QcMetric::RainRate).flag == QcFlag::Good ? "" : qc(QcMetric::RainRate).reason));
    // lightning: antenna resonance from the crown diagnostics, disturber (interference) rate per 10 min
    if (!win_started_ || raw.crown.disturbers < win_start_count_) {
        win_started_ = true;
        win_start_s_ = t_s_;
        win_start_count_ = raw.crown.disturbers;
    } else if (t_s_ - win_start_s_ >= 600U) {
        disturbers_10min_ = raw.crown.disturbers - win_start_count_;
        have_rate_ = true;
        win_start_s_ = t_s_;
        win_start_count_ = raw.crown.disturbers;
    }
    if (have_rate_) Q.disturbers_10min.set(static_cast<float>(disturbers_10min_));
    if (!raw.crown.as3935_lco_hz.fresh(now, 30000)) {
        add("lightning", "Lightning (AS3935)", HealthStatus::Fail, "no crown diagnostics");
    } else {
        const float khz = static_cast<float>(raw.crown.as3935_lco_hz.value) / 1000.0F;
        Q.as3935_antenna_khz.set(khz);
        if (khz < 482.5F || khz > 517.5F) {
            add("lightning", "Lightning (AS3935)", HealthStatus::Warn, "antenna tuning off (500 kHz +/- 3.5 %%)");
        } else if (have_rate_ && disturbers_10min_ > 50U) {
            add("lightning", "Lightning (AS3935)", HealthStatus::Warn, "interference: %u disturbers in 10 min",
                static_cast<unsigned>(disturbers_10min_));
        } else {
            add("lightning", "Lightning (AS3935)", HealthStatus::Ok, "%s", "");
        }
    }
    if (!raw.thermo.temp_c.fresh(now, 10000)) {
        add("thermo", "Temperature / RH (SHT40)", HealthStatus::Fail, "no data");
    } else {
        const core::QcValue t = qc(QcMetric::TempC), r = qc(QcMetric::RhPct);
        const core::QcValue& bad = t.flag != QcFlag::Good ? t : r;
        add("thermo", "Temperature / RH (SHT40)", worse(qc_status(t), qc_status(r)), "%s",
            d.controls.thermo_locked ? "sensor heater bake-out" : (bad.flag == QcFlag::Good ? "" : bad.reason));
    }
    if (!raw.baro.pressure_hpa.fresh(now, 5000)) {
        add("baro", "Pressure (BMP585)", HealthStatus::Fail, "no data");
    } else {
        const core::QcValue p = qc(QcMetric::PressureHpa);
        add("baro", "Pressure (BMP585)", qc_status(p), "%s", p.flag == QcFlag::Good ? "" : p.reason);
    }
    if (d.controls.scd41_pause) {
        add("co2", "CO\u2082 (SCD41)", HealthStatus::Off, "paused: too cold");
    } else if (!raw.air.co2_ppm.fresh(now, 30000)) {
        add("co2", "CO\u2082 (SCD41)", HealthStatus::Fail, "no data");
    } else {
        const core::QcValue c = qc(QcMetric::Co2);
        add("co2", "CO\u2082 (SCD41)", qc_status(c), "%s", c.flag == QcFlag::Good ? "" : c.reason);
    }
    add("voc", "VOC / NOx (SGP41)", raw.air.sgp_sraw_voc.fresh(now, 10000) ? HealthStatus::Ok : HealthStatus::Fail, "%s",
        !raw.air.sgp_sraw_voc.fresh(now, 10000) ? "no data" : (d.air.gas_learning ? "learning the site baseline" : ""));
    if (!raw.air.pm25_ug_m3.fresh(now, 10000)) {
        add("pm", "Particulates (PMSA003I)", HealthStatus::Fail, "no data");
    } else {
        const core::QcValue p = qc(QcMetric::Pm25);
        add("pm", "Particulates (PMSA003I)", qc_status(p), "%s", p.flag == QcFlag::Good ? "" : p.reason);
    }
    if (!raw.imu.gravity_g.fresh(now, 5000)) {
        add("imu", "Tilt (LSM6DS3)", HealthStatus::Fail, "no data");
    } else {
        const float tilt = d.wind.tilt_deg.ok ? d.wind.tilt_deg.v : 0.0F;
        add("imu", "Tilt (LSM6DS3)", tilt > 3.0F ? HealthStatus::Warn : HealthStatus::Ok, "%s", tilt > 3.0F ? "tilted more than 3\u00b0" : "");
    }
    add("seismic", "Vibration / seismic (LSM6DS3)", raw.imu.vibration.fresh(now, 5000) ? HealthStatus::Ok : HealthStatus::Fail,
        "%s", raw.imu.vibration.fresh(now, 5000) ? "" : "no accelerometer data");
    if (cfg.heading_mode == 1) {
        add("compass", "Compass (MMC5603)", HealthStatus::Off, "not used: fixed heading");
    } else if (raw.wind.heading_deg.fresh(now, 10000)) {
        add("compass", "Compass (MMC5603)", cfg.cal_unix[kCompassKind] == 0 ? HealthStatus::Warn : HealthStatus::Ok, "%s",
            cfg.cal_unix[kCompassKind] == 0 ? "not calibrated" : "");
    } else {
        add("compass", "Compass (MMC5603)", HealthStatus::Fail, "no data");
    }
    // TSL25911 saturates above ~88 klx behind a clear (JGS1) cover; the LTR-390 then measures (not a fault)
    const float tr = cfg.optics_transmission > 0.0F ? cfg.optics_transmission : 1.0F;
    // photodiode dome: UV-A channel instead of the LTR-390; the TSL saturates when the UV-A says full sun
    const bool pd_uva = raw.optical.pd_uva_w_m2.fresh(now, 5000);
    const bool tsl_saturated = (raw.optical.ltr_lux.fresh(now, 5000) && raw.optical.ltr_lux.value * tr > 60000.0F) ||
                               (pd_uva && raw.optical.pd_uva_w_m2.value * tr > 20.0F);
    const int optics = (raw.optical.tsl_lux.fresh(now, 5000) || tsl_saturated ? 1 : 0) + (raw.optical.ltr_lux.fresh(now, 5000) || raw.optical.ltr_uvi.fresh(now, 5000) || pd_uva ? 1 : 0) +
                       (raw.optical.guvb_counts.fresh(now, 5000) ? 1 : 0);
    {
        const core::QcValue ir = qc(QcMetric::Irradiance), uv = qc(QcMetric::UvIndex);
        const core::QcValue& bad = ir.flag == QcFlag::Suspect ? ir : uv;
        const HealthStatus st = optics == 0 ? HealthStatus::Fail
                                : (optics < 3 || bad.flag == QcFlag::Suspect ? HealthStatus::Warn : HealthStatus::Ok);
        if (bad.flag == QcFlag::Suspect && optics > 0) {
            add("optics", "Light / UV (dome)", st, "%s", bad.reason);
        } else {
            add("optics", "Light / UV (dome)", st, "%s", optics == 3 ? "" : (optics == 0 ? "no dome sensors" : (optics == 1 ? "1 of 3 sensors" : "2 of 3 sensors")));
        }
    }

    // heaters
    const char* rs = h.reflector_status;
    const bool r_fail = std::strcmp(rs, "open") == 0 || std::strcmp(rs, "stuck-on") == 0 || std::strcmp(rs, "check-fail") == 0;
    const HealthStatus r_st = std::strcmp(rs, "\u2014") == 0 ? HealthStatus::Fail : (r_fail ? HealthStatus::Fail : HealthStatus::Ok);
    if (!h.reflector_c.ok) {
        add("reflector_heater", "De-icing heater (reflector)", HealthStatus::Fail, "no NTC reading");
    } else {
        add("reflector_heater", "De-icing heater (reflector)", r_st, "%s", std::strcmp(rs, "ok") == 0 ? "" : rs);
    }
    const char* ps = h.probe_status;
    add("probe_heater", "Probe heater (SCD41)",
        std::strcmp(ps, "stuck-on") == 0 || std::strcmp(ps, "not-heating") == 0 ? HealthStatus::Warn : HealthStatus::Ok, "%s",
        std::strcmp(ps, "ok") == 0 || std::strcmp(ps, "heating") == 0 ? "" : ps);
}

}  // namespace quality
}  // namespace station
