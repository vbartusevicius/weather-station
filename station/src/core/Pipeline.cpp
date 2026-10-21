#include "Pipeline.hpp"

#include <algorithm>
#include <cmath>

#include "../airquality/AirQuality.hpp"
#include "../astro/Astronomy.hpp"
#include "../heater/DeIcingHeater.hpp"

namespace station {
namespace core {

namespace {
using environment::MetCalculator;
using history::Metric;
using history::Tier;

// Maximum ages before a reading is treated as missing.
constexpr std::uint32_t kWindMaxAgeMs = 3000;
constexpr std::uint32_t kBaroMaxAgeMs = 5000;
constexpr std::uint32_t kThermoMaxAgeMs = 15000;
constexpr std::uint32_t kOpticalMaxAgeMs = 5000;
constexpr std::uint32_t kAirMaxAgeMs = 60000;
constexpr std::uint32_t kImuMaxAgeMs = 3000;
constexpr std::uint32_t kPowerMaxAgeMs = 5000;
constexpr float kReflectorOhm = heater::DeIcingHeaterController::kHeaterResistanceOhm;  // 4x 120 ohm on +12V_HEAT
constexpr float kHeaterCheckW = 2.0F;
constexpr float kProbeHeaterOnA = 0.015F;   // probe heater draws 35 mA from +12V_HEAT when on
constexpr float kProbeSetpointC = 8.0F;

constexpr float kG = 9.80665F;
constexpr float kLuxPerWm2 = 122.0F;     // luminous efficacy of global daylight (lm/W)
constexpr float kUvFraction = 0.045F;    // UV share of global irradiance
constexpr float kTypicalNirRatio = 0.27F;// TSL25911 ch1/ch0 in clear midday sunlight

float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

float wrap180(float d) {
    while (d > 180.0F) d -= 360.0F;
    while (d < -180.0F) d += 360.0F;
    return d;
}

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

const char* comfort_label(const ThermoDerived& t) {
    if (!t.temp_c.ok) return "—";
    if (t.frost_risk) return "Frost Risk";
    const float feels = t.feels_like_c.ok ? t.feels_like_c.v : t.temp_c.v;
    if (feels >= 32.0F) return "Heat Stress";
    if (feels <= -10.0F) return "Cold Stress";
    if (t.temp_c.v <= 0.0F) return "Freezing";
    if (t.dew_point_c.ok && t.dew_point_c.v >= 18.0F) return "Humid";
    return "Comfortable";
}
}  // namespace

// ------------------------------------------------------------------------------------------------
void FiveMinuteRing::accumulate(bool qnh_ok, float qnh, bool wind_ok, float u, float v) {
    if (qnh_ok) {
        sq_ += qnh;
        ++nq_;
    }
    if (wind_ok) {
        su_ += u;
        sv_ += v;
        ++nw_;
    }
}

void FiveMinuteRing::close() {
    Entry e{};
    e.qnh_ok = nq_ > 0;
    e.qnh = e.qnh_ok ? static_cast<float>(sq_ / nq_) : 0.0F;
    e.wind_ok = nw_ > 0;
    e.u = e.wind_ok ? static_cast<float>(su_ / nw_) : 0.0F;
    e.v = e.wind_ok ? static_cast<float>(sv_ / nw_) : 0.0F;
    ring_[head_] = e;
    head_ = (head_ + 1) % kSlots;
    count_ = std::min(count_ + 1, kSlots);
    sq_ = su_ = sv_ = 0.0;
    nq_ = nw_ = 0;
}

bool FiveMinuteRing::change_3h_qnh(float& out) const {
    if (count_ < kSlots) {
        return false;
    }
    const Entry& oldest = ring_[head_];  // full ring: head points at the oldest entry
    const Entry& newest = ring_[(head_ + kSlots - 1) % kSlots];
    if (!oldest.qnh_ok || !newest.qnh_ok) {
        return false;
    }
    out = newest.qnh - oldest.qnh;
    return true;
}

bool FiveMinuteRing::dir_change_3h_deg(float& out) const {
    if (count_ < kSlots) {
        return false;
    }
    const Entry& oldest = ring_[head_];
    const Entry& newest = ring_[(head_ + kSlots - 1) % kSlots];
    auto ok = [](const Entry& e) { return e.wind_ok && std::hypot(e.u, e.v) >= 1.0F; };
    if (!ok(oldest) || !ok(newest)) {
        return false;
    }
    out = wrap180(wind::wind_from_deg(newest.u, newest.v) - wind::wind_from_deg(oldest.u, oldest.v));
    return true;
}

// ------------------------------------------------------------------------------------------------
void Pipeline::step(const RawState& raw, EventQueues& events, const StationConfig& cfg, DerivedState& out) {
    DerivedState d{};
    ++seconds_;
    d.uptime_s = raw.now_ms / 1000U;
    d.clock_valid = raw.clock.valid;
    d.unix_s = raw.clock.unix_s;
    d.utc_offset_s = raw.clock.utc_offset_s;

    // ---- history interval bookkeeping (wall-clock aligned once the clock is valid) ----
    std::int64_t m_idx = 0, h_idx = 0, day_idx = 0;
    if (raw.clock.valid) {
        const std::int64_t local = raw.clock.unix_s + raw.clock.utc_offset_s;
        m_idx = floor_div(raw.clock.unix_s, 60);
        h_idx = floor_div(local, 3600);
        day_idx = floor_div(local, 86400);
    } else {
        const std::int64_t up = raw.now_ms / 1000U;
        m_idx = up / 60;
        h_idx = up / 3600;
        day_idx = up / 86400;
    }
    if (raw.clock.valid != clock_was_valid_) {
        hist_.rebase(m_idx, h_idx, day_idx);  // time base switch: keep data, don't insert gaps
        clock_was_valid_ = raw.clock.valid;
        last_day_idx_ = day_idx;
        five_min_idx_ = m_idx / 5;
    }
    const bool new_minute = (hist_.minute_idx() >= 0 && m_idx != hist_.minute_idx());
    hist_.advance(m_idx, h_idx, day_idx);
    if (last_day_idx_ >= 0 && day_idx != last_day_idx_ && raw.clock.valid) {
        rain_.new_day();
    }
    last_day_idx_ = day_idx;
    if (five_min_idx_ >= 0 && m_idx / 5 != five_min_idx_) {
        five_min_.close();
    }
    five_min_idx_ = m_idx / 5;

    // ---- processors (dependency order) ----
    process_wind(raw, d);
    process_astro(raw, cfg, d);
    process_solar(raw, d);

    // Bake-out / cold-pause controller acts on the raw SHT40 values.
    if (raw.thermo.temp_c.fresh(raw.now_ms, kThermoMaxAgeMs) && raw.thermo.rh_pct.fresh(raw.now_ms, kThermoMaxAgeMs)) {
        const auto st = longevity_.tick_second(raw.thermo.temp_c.value, raw.thermo.rh_pct.value, new_minute);
        d.controls.sht_bakeout = st.sht45_bakeout_pulse;
        d.controls.thermo_locked = st.sht45_reading_locked;
        d.controls.scd41_pause = st.scd41_extreme_cold_pause;
    }
    process_thermo(raw, d);

    const std::uint32_t strikes_before = strikes_total_;
    process_rain(raw, events, cfg, d);
    process_baro(raw, cfg, d);
    process_wind_load(d);
    process_air(raw, d);
    process_lightning(raw, events, d);
    process_seismic(raw, d);
    process_heater(raw, d);

    d.thermo.comfort = comfort_label(d.thermo);
    d.controls.ambient_pressure_hpa = d.baro.station_hpa.ok ? d.baro.station_hpa.v : 0.0F;

    record_history(d, last_rain_mm_, static_cast<std::uint16_t>(strikes_total_ - strikes_before));
    out = d;
}

bool Pipeline::minute_trend(Metric m, float& per_hour) const {
    // Mean of the 5 newest closed minutes vs. the 5 oldest (55 min apart), scaled to 1 h.
    const auto agg = history::metric_info(m).agg;
    float recent = 0.0F, old = 0.0F;
    int nr = 0, no = 0;
    for (std::size_t i = 0; i < 5; ++i) {
        float v = 0.0F;
        if (hist_.slot(m, Tier::Minute, history::TieredHistory::kMinutes - 1 - i).value(agg, v)) {
            recent += v;
            ++nr;
        }
        if (hist_.slot(m, Tier::Minute, i).value(agg, v)) {
            old += v;
            ++no;
        }
    }
    if (nr < 3 || no < 3) {
        return false;
    }
    per_hour = (recent / static_cast<float>(nr) - old / static_cast<float>(no)) * (60.0F / 55.0F);
    return true;
}

void Pipeline::process_wind(const RawState& raw, DerivedState& d) {
    const bool fresh = raw.wind.second.fresh(raw.now_ms, kWindMaxAgeMs);
    const WindSecond& w = raw.wind.second.value;
    wind_.push_second(fresh, w.v_east_m_s, w.v_north_m_s, w.speed_mean_m_s);
    const wind::WmoWindSummary s = wind_.summary();
    WindDerived& o = d.wind;
    if (s.valid) {
        o.speed_m_s.set(s.speed_3s_m_s);
        if (s.speed_3s_m_s >= wind::WmoWindAggregator::kCalmMs) {
            o.dir_deg.set(s.dir_3s_deg);
        }
        o.avg_2m_m_s.set(s.avg_2m_m_s);
        o.avg_10m_m_s.set(s.avg_10m_m_s);
        if (s.dir_valid) {
            o.dir_10m_deg.set(s.dir_10m_deg);
        }
        o.gust_m_s.set(s.gust_m_s);
        o.lull_m_s.set(s.lull_m_s);
        o.beaufort.set(s.beaufort_scale);
        o.turbulence_pct.set(s.turbulence_intensity_pct);
        o.rose_pct = s.rose_freq_pct;
    }
    if (fresh) {
        o.sound_m_s.set(w.sound_speed_m_s);
    }
    if (raw.wind.tilt_deg.fresh(raw.now_ms, 60000)) {
        o.tilt_deg.set(raw.wind.tilt_deg.value);
    }
    float tr = 0.0F;
    if (minute_trend(Metric::WindMs, tr)) {
        o.trend_m_s_per_h.set(tr);
    }
    five_min_.accumulate(false, 0.0F, fresh, w.v_east_m_s, w.v_north_m_s);
    float dc = 0.0F;
    if (five_min_.dir_change_3h_deg(dc)) {
        o.dir_change_3h_deg.set(dc);
    }
}

void Pipeline::process_astro(const RawState& raw, const StationConfig& cfg, DerivedState& d) {
    AstroDerived& a = d.astro;
    a.location_set = cfg.location_set;
    if (!cfg.location_set || !raw.clock.valid) {
        return;
    }
    const auto sun = astro::Astronomy::sun(raw.clock.unix_s, cfg.latitude_deg, cfg.longitude_deg);
    a.sun_alt_deg.set(sun.altitude_deg);
    a.sun_az_deg.set(sun.azimuth_deg);
    a.is_night = sun.altitude_deg < -6.0F;

    const auto moon = astro::Astronomy::moon(raw.clock.unix_s, cfg.latitude_deg, cfg.longitude_deg);
    a.moon_alt_deg.set(moon.altitude_deg);
    a.moon_illum_pct.set(moon.illumination * 100.0F);
    a.moon_age_days.set(moon.age_days);
    a.moon_phase = astro::Astronomy::moon_phase_name(moon);

    // Rise/set times change slowly: recompute once per local hour.
    const std::int64_t key = floor_div(raw.clock.unix_s + raw.clock.utc_offset_s, 3600);
    if (key != riseset_key_) {
        riseset_key_ = key;
        riseset_ = {};
        const auto srs = astro::Astronomy::sun_rise_set(raw.clock.unix_s, raw.clock.utc_offset_s, cfg.latitude_deg, cfg.longitude_deg);
        if (srs.has_rise) riseset_.sunrise.set(srs.rise_min);
        if (srs.has_set) riseset_.sunset.set(srs.set_min);
        if (srs.has_rise && srs.has_set) {
            riseset_.daylight.set(std::fmod(srs.set_min - srs.rise_min + 1440.0F, 1440.0F));
        } else if (srs.always_up) {
            riseset_.daylight.set(1440.0F);
        } else if (srs.always_down) {
            riseset_.daylight.set(0.0F);
        }
        const auto mrs = astro::Astronomy::moon_rise_set(raw.clock.unix_s, raw.clock.utc_offset_s, cfg.latitude_deg, cfg.longitude_deg);
        if (mrs.has_rise) riseset_.moonrise.set(mrs.rise_min);
        if (mrs.has_set) riseset_.moonset.set(mrs.set_min);
    }
    a.sunrise_min = riseset_.sunrise;
    a.sunset_min = riseset_.sunset;
    a.daylight_min = riseset_.daylight;
    a.moonrise_min = riseset_.moonrise;
    a.moonset_min = riseset_.moonset;
}

void Pipeline::process_solar(const RawState& raw, DerivedState& d) {
    SolarDerived& s = d.solar;
    const OpticalRaw& o = raw.optical;
    const bool tsl = o.tsl_lux.fresh(raw.now_ms, kOpticalMaxAgeMs);
    const bool ltr = o.ltr_lux.fresh(raw.now_ms, kOpticalMaxAgeMs);
    if (tsl || ltr) {
        const float lux = MetCalculator::fuse_uv_hardened_lux(tsl ? o.tsl_lux.value : 0.0F, ltr ? o.ltr_lux.value : 0.0F);
        s.lux.set(lux);
        const float g = lux / kLuxPerWm2;
        s.irradiance_w_m2.set(g);
        float nir_share = 0.52F;
        if (o.tsl_nir_ratio.fresh(raw.now_ms, kOpticalMaxAgeMs) && lux > 50.0F) {
            nir_share = clampf(0.52F * o.tsl_nir_ratio.value / kTypicalNirRatio, 0.30F, 0.70F);
        }
        s.nir_w_m2.set(g * nir_share);
        s.visible_w_m2.set(g * std::max(0.0F, 1.0F - kUvFraction - nir_share));
        if (!d.astro.location_set || !d.astro.sun_alt_deg.ok) {
            d.astro.is_night = lux < 2.0F;  // fallback without a location
        }
    }
    // UV index: the erythemal (sunburn) weighting is dominated by UV-B, which the GUVB-C31SM measures
    // directly; the LTR-390 UVS channel leans toward UV-A. Fuse 80/20 when both are available.
    const bool ltr_uv = o.ltr_uvi.fresh(raw.now_ms, kOpticalMaxAgeMs);
    const bool guvb_uv = o.guvb_uvi.fresh(raw.now_ms, kOpticalMaxAgeMs);
    if (ltr_uv) s.uv_index_ltr.set(std::max(0.0F, o.ltr_uvi.value));
    if (guvb_uv) s.uv_index_guvb.set(std::max(0.0F, o.guvb_uvi.value));
    if (ltr_uv || guvb_uv) {
        float uvi = 0.0F;
        if (ltr_uv && guvb_uv) {
            uvi = 0.8F * s.uv_index_guvb.v + 0.2F * s.uv_index_ltr.v;
            s.uv_source = "GUVB-C31SM + LTR-390";
        } else if (guvb_uv) {
            uvi = s.uv_index_guvb.v;
            s.uv_source = "GUVB-C31SM";
        } else {
            uvi = s.uv_index_ltr.v;
            s.uv_source = "LTR-390";
        }
        s.uv_index.set(uvi);
        s.uvb_w_m2.set(uvi * 0.18F);  // typical ground-level UV-B (280-315 nm) per UV index unit
        s.uva_w_m2.set(uvi * 5.0F);
        if (uvi >= 1.0F) {
            // 1 MED (skin type II) = 250 J/m^2 erythemal; erythemal irradiance = UVI / 40 W/m^2.
            s.burn_time_min.set(250.0F / (uvi / 40.0F) / 60.0F);
        }
    }
    if (d.astro.sun_alt_deg.ok) {
        const float clear = astro::Astronomy::clear_sky_ghi_w_m2(d.astro.sun_alt_deg.v);
        s.clear_sky_w_m2.set(clear);
        if (d.astro.sun_alt_deg.v >= 10.0F && s.irradiance_w_m2.ok) {
            const float c = MetCalculator::estimate_cloud_cover_pct(s.irradiance_w_m2.v, clear);
            cloud_ema_ = (cloud_ema_ < 0.0F) ? c : cloud_ema_ + (c - cloud_ema_) / 300.0F;  // ~5 min
            s.cloud_cover_pct.set(cloud_ema_);
        } else {
            cloud_ema_ = -1.0F;
        }
    }
    const bool dark = d.astro.sun_alt_deg.ok ? d.astro.sun_alt_deg.v < -12.0F : (s.lux.ok && s.lux.v < 0.05F);
    if (dark && s.lux.ok) {
        const float sqm = MetCalculator::sky_quality_mpsas(s.lux.v);
        s.sqm_mag_arcsec2.set(sqm);
        s.bortle.set(static_cast<float>(MetCalculator::bortle_class_from_sqm(sqm)));
    }
}

void Pipeline::process_thermo(const RawState& raw, DerivedState& d) {
    ThermoDerived& t = d.thermo;
    const bool fresh = raw.thermo.temp_c.fresh(raw.now_ms, kThermoMaxAgeMs) && raw.thermo.rh_pct.fresh(raw.now_ms, kThermoMaxAgeMs);
    if (fresh && !d.controls.thermo_locked) {
        thermo_hold_c_ = raw.thermo.temp_c.value;
        thermo_hold_rh_ = clampf(raw.thermo.rh_pct.value, 0.0F, 100.0F);
        thermo_hold_ok_ = true;
    } else if (!fresh) {
        thermo_hold_ok_ = false;
    }
    if (thermo_hold_ok_) {
        const float wind_ref = d.wind.avg_2m_m_s.ok ? d.wind.avg_2m_m_s.v : 1.0F;
        const float solar = d.solar.irradiance_w_m2.ok ? d.solar.irradiance_w_m2.v : 0.0F;
        const float offset = MetCalculator::solar_radiation_heating_error_c(solar, wind_ref);
        const float tc = thermo_hold_c_ - offset;
        const float rh = thermo_hold_rh_;
        const float wind10 = d.wind.avg_10m_m_s.ok ? d.wind.avg_10m_m_s.v : 0.0F;
        t.temp_c.set(tc);
        t.solar_offset_c.set(offset);
        t.rh_pct.set(rh);
        t.dew_point_c.set(MetCalculator::dew_point_c(tc, rh));
        t.wet_bulb_c.set(MetCalculator::wet_bulb_c(tc, rh));
        t.feels_like_c.set(MetCalculator::feels_like_c(tc, rh, wind10));
        t.wind_chill_c.set(MetCalculator::wind_chill_c(tc, wind10));
        t.heat_index_c.set(MetCalculator::heat_index_c(tc, rh));
        t.abs_humidity_g_m3.set(MetCalculator::absolute_humidity_g_m3(tc, rh));
        t.vpd_kpa.set(MetCalculator::vapor_pressure_deficit_kpa(tc, rh));
        t.cloud_base_m.set(MetCalculator::cloud_base_lcl_m(tc, t.dew_point_c.v));
        t.frost_risk = tc <= 3.0F && t.dew_point_c.v <= 0.5F;
        if (raw.wind.second.fresh(raw.now_ms, kWindMaxAgeMs)) {
            const float p = raw.baro.pressure_hpa.fresh(raw.now_ms, kBaroMaxAgeMs) ? raw.baro.pressure_hpa.value : 1013.25F;
            d.wind.acoustic_temp_c.set(
                wind::UltrasonicWindSolver::correct_acoustic_temp_for_humidity(raw.wind.second.value.virtual_temp_c, rh, p));
        }
    }
    float v = 0.0F;
    if (hist_.window_min(Metric::TempC, Tier::Hour, 23, v)) {
        t.temp_min_24h_c.set(v);
    }
    if (hist_.window_max(Metric::TempC, Tier::Hour, 23, v)) {
        t.temp_max_24h_c.set(v);
    }
    if (minute_trend(Metric::TempC, v)) {
        t.trend_c_per_h.set(v);
    }
}

void Pipeline::process_baro(const RawState& raw, const StationConfig& cfg, DerivedState& d) {
    BaroDerived& b = d.baro;
    const bool fresh = raw.baro.pressure_hpa.fresh(raw.now_ms, kBaroMaxAgeMs);
    const float temp = d.thermo.temp_c.ok ? d.thermo.temp_c.v : (raw.baro.sensor_temp_c.valid ? raw.baro.sensor_temp_c.value : 15.0F);
    if (fresh) {
        const float wind = d.wind.speed_m_s.ok ? d.wind.speed_m_s.v : 0.0F;
        const float stat = MetCalculator::compensate_wind_dynamic_pressure_hpa(raw.baro.pressure_hpa.value, wind, temp, cfg.pressure_wind_coef);
        // WMO practice: reduce with the air-column temperature (mean of now and 12 h ago) so the
        // diurnal heating cycle does not fake a pressure tendency.
        float t_col = temp;
        float t12 = 0.0F;
        if (hist_.slot(Metric::TempC, Tier::Hour, history::TieredHistory::kHours - 12).value(history::Agg::Mean, t12)) {
            t_col = 0.5F * (temp + t12);
        }
        const float qnh = MetCalculator::sea_level_pressure_hpa(stat, t_col, cfg.altitude_m);
        b.station_hpa.set(stat);
        b.wind_corr_hpa.set(stat - raw.baro.pressure_hpa.value);
        b.qnh_hpa.set(qnh);
        b.ripple_pa.set(raw.baro.ripple_pa.valid ? raw.baro.ripple_pa.value : 0.0F);
        const float rh = d.thermo.rh_pct.ok ? d.thermo.rh_pct.v : 50.0F;
        b.air_density_kg_m3.set(MetCalculator::moist_air_density_kg_m3(stat, temp, rh));
        five_min_.accumulate(true, qnh, false, 0.0F, 0.0F);

        float change = 0.0F;
        if (five_min_.change_3h_qnh(change)) {
            b.change_3h_hpa.set(change);
            float tide = 0.0F;
            if (cfg.location_set && raw.clock.valid) {
                tide = astro::Astronomy::solar_tide_3h_hpa(raw.clock.unix_s, cfg.latitude_deg, cfg.longitude_deg);
                b.tide_3h_hpa.set(tide);
            }
            b.synoptic_3h_hpa.set(change - tide);
        }
        const float syn = b.synoptic_3h_hpa.ok ? b.synoptic_3h_hpa.v : 0.0F;
        const auto trend = MetCalculator::classify_pressure_trend(syn);
        b.trend = b.synoptic_3h_hpa.ok ? MetCalculator::pressure_trend_name(trend) : "—";
        const float dir = d.wind.dir_10m_deg.ok ? d.wind.dir_10m_deg.v : 180.0F;
        b.forecast = MetCalculator::zambretti_forecast(qnh, trend, dir);
        const float dir_change = d.wind.dir_change_3h_deg.ok ? d.wind.dir_change_3h_deg.v : 0.0F;
        const float cloud = d.solar.cloud_cover_pct.ok ? d.solar.cloud_cover_pct.v : 40.0F;
        b.front = MetCalculator::sager_front_type(qnh, syn, dir_change, cloud);
        b.rain_prob_pct.set(static_cast<float>(MetCalculator::sager_rain_probability_pct(qnh, syn, dir_change, cloud, d.rain.raining)));
        b.wind_shift = !d.wind.dir_change_3h_deg.ok ? "—"
                       : (dir_change < -10.0F ? "Backing" : (dir_change > 10.0F ? "Veering" : "Steady"));
    }
    float v = 0.0F;
    if (hist_.window_min(Metric::QnhHpa, Tier::Hour, 23, v)) {
        b.qnh_min_24h_hpa.set(v);
    }
    if (hist_.window_max(Metric::QnhHpa, Tier::Hour, 23, v)) {
        b.qnh_max_24h_hpa.set(v);
    }
}

void Pipeline::process_wind_load(DerivedState& d) {
    WindDerived& w = d.wind;
    const float rho = d.baro.air_density_kg_m3.ok ? d.baro.air_density_kg_m3.v : 1.225F;
    auto load = [rho](float v) { return 0.5F * rho * v * v / kG; };  // kgf/m^2
    if (w.speed_m_s.ok) {
        w.load_kg_m2.set(load(w.speed_m_s.v));
    }
    float lo = 0.0F, hi = 0.0F;
    if (hist_.window_min(Metric::WindMs, Tier::Minute, history::TieredHistory::kMinutes, lo)) {
        w.load_min_1h_kg_m2.set(load(lo));
    }
    if (hist_.window_max(Metric::WindMs, Tier::Minute, history::TieredHistory::kMinutes, hi)) {
        w.load_max_1h_kg_m2.set(load(hi));
    }
}

void Pipeline::process_rain(const RawState& raw, EventQueues& events, const StationConfig& cfg, DerivedState& d) {
    RainImpact imp{};
    const float vib_g = raw.imu.vibration.fresh(raw.now_ms, kImuMaxAgeMs) ? raw.imu.vibration.value.pga_mg / 1000.0F : 0.0F;
    const float wind = d.wind.speed_m_s.ok ? d.wind.speed_m_s.v : 0.0F;
    const float dome_t = d.thermo.temp_c.ok ? d.thermo.temp_c.v : 20.0F;
    while (events.rain.pop(imp)) {
        rain::RainImpactEvent ev{};
        ev.peak_mv = imp.peak_mv;
        ev.baseline_mv = imp.baseline_mv;
        ev.pulse_width_us = imp.width_us;
        ev.imu_vibration_g = vib_g;
        ev.wind_speed_m_s = wind;
        ev.dome_temp_c = dome_t;
        rain_.process_impact(ev, cfg.rain_gain);
    }
    last_rain_mm_ = rain_.tick_second();
    const rain::RainMetrics& m = rain_.metrics();
    RainDerived& r = d.rain;
    r.sensor_ok = raw.power.rain_adc_ok;
    r.state = rain::precipitation_type_name(m.state);
    r.raining = m.is_raining;
    r.surface_wet = m.surface_wet;
    r.rate_mm_h = m.rate_mm_h;
    r.hail_rate_mm_h = m.hail_rate_mm_h;
    r.event_mm = m.event_mm;
    r.today_mm = m.daily_mm;
    r.total_mm = m.total_mm;
    r.drops = m.drop_count;
    r.hail_hits = m.hail_hits;
    r.max_hail_mm = m.max_hail_diameter_mm;
    r.rejected_shake = m.rejected_vibrations;
    float hour = 0.0F;
    hist_.window_value(Metric::RainMm, Tier::Minute, history::TieredHistory::kMinutes, hour);
    r.last_hour_mm = hour + last_rain_mm_;
}

void Pipeline::process_air(const RawState& raw, DerivedState& d) {
    AirDerived& a = d.air;
    const AirRaw& in = raw.air;
    const std::uint32_t now = raw.now_ms;
    const float rh = d.thermo.rh_pct.ok ? d.thermo.rh_pct.v : 50.0F;
    using airquality::AirQualityEvaluator;
    if (in.pm1_ug_m3.fresh(now, kAirMaxAgeMs)) a.pm1_ug_m3.set(in.pm1_ug_m3.value);
    if (in.pm25_ug_m3.fresh(now, kAirMaxAgeMs)) {
        a.pm25_raw_ug_m3.set(in.pm25_ug_m3.value);
        a.pm25_ug_m3.set(AirQualityEvaluator::compensate_pm_hygroscopic_growth(in.pm25_ug_m3.value, rh));
    }
    if (in.pm10_ug_m3.fresh(now, kAirMaxAgeMs)) {
        a.pm10_ug_m3.set(AirQualityEvaluator::compensate_pm_hygroscopic_growth(in.pm10_ug_m3.value, rh));
    }
    if (a.pm25_ug_m3.ok && a.pm10_ug_m3.ok) {
        a.coarse_ug_m3.set(std::max(0.0F, a.pm10_ug_m3.v - a.pm25_ug_m3.v));
    }
    // VOC / NOx: SGP41 raw ticks through Sensirion's gas index algorithm, exactly one new sample per second.
    if (in.sgp_sraw_voc.fresh(now, 3000)) {
        const std::uint32_t t = in.sgp_sraw_voc.t_ms;
        if (sgp_seen_ && t - last_sgp_ms_ > 600000U) {
            gas_.reset();  // > 10 min gap: Sensirion recommends starting the learning again
        }
        if (!sgp_seen_ || t != last_sgp_ms_) {
            const bool nox_ok = in.sgp_sraw_nox.valid && in.sgp_sraw_nox.t_ms == t;
            gas_.process(in.sgp_sraw_voc.value, nox_ok, in.sgp_sraw_nox.value);
            last_sgp_ms_ = t;
            sgp_seen_ = true;
        }
        float v = 0.0F;
        if (gas_.voc(v)) a.voc_index.set(v);
        if (gas_.nox(v)) a.nox_index.set(v);
        a.gas_learning = gas_.learning();
    }
    if (in.co2_ppm.fresh(now, kAirMaxAgeMs)) a.co2_ppm.set(in.co2_ppm.value);

    // US EPA AQI from NowCast (12 h) PM2.5 / PM10; hour 0 = the running hour.
    auto nowcast = [this](Metric m, float fallback, bool fallback_ok, float& out) {
        float vals[12]{};
        bool present[12]{};
        const auto agg = history::metric_info(m).agg;
        present[0] = hist_.current(m, Tier::Hour).value(agg, vals[0]);
        for (std::size_t i = 1; i < 12; ++i) {
            present[i] = hist_.slot(m, Tier::Hour, history::TieredHistory::kHours - i).value(agg, vals[i]);
        }
        if (AirQualityEvaluator::nowcast(vals, present, 12, out)) {
            return true;
        }
        out = fallback;  // first hours after boot: current value
        return fallback_ok;
    };
    float c25 = 0.0F, c10 = 0.0F;
    const bool ok25 = nowcast(Metric::Pm25, a.pm25_ug_m3.v, a.pm25_ug_m3.ok, c25);
    const bool ok10 = nowcast(Metric::Pm10, a.pm10_ug_m3.v, a.pm10_ug_m3.ok, c10);
    if (ok25 || ok10) {
        const std::uint16_t i25 = ok25 ? AirQualityEvaluator::compute_pm25_aqi(c25) : 0;
        const std::uint16_t i10 = ok10 ? AirQualityEvaluator::compute_pm10_aqi(c10) : 0;
        const std::uint16_t aqi = std::max(i25, i10);
        a.aqi.set(static_cast<float>(aqi));
        a.level = AirQualityEvaluator::level_name(AirQualityEvaluator::level_for_aqi(aqi));
        a.dominant = (i10 > i25) ? "PM10" : "PM2.5";
    }
    float m25 = 0.0F, m10 = 0.0F;
    if (hist_.window_value(Metric::Pm25, Tier::Hour, 23, m25) && hist_.window_value(Metric::Pm10, Tier::Hour, 23, m10)) {
        a.eaqi.set(static_cast<float>(AirQualityEvaluator::eaqi_band(m25, m10)));
    }
}

void Pipeline::process_lightning(const RawState& raw, EventQueues& events, DerivedState& d) {
    LightningStrike s{};
    while (events.lightning.pop(s)) {
        ++strikes_total_;
        have_strike_ = true;
        last_strike_ms_ = s.t_ms;
        last_rf_in_range_ = s.distance_km > 0 && s.distance_km < 63;
        last_rf_km_ = static_cast<float>(s.distance_km == 1 ? 0 : s.distance_km);
        last_acoustic_ = false;
        if (last_rf_in_range_) {
            hist_.add(Metric::LightningKm, last_rf_km_);
        }
    }
    ThunderArrival th{};
    while (events.thunder.pop(th)) {
        if (!have_strike_ || last_acoustic_) {
            continue;
        }
        const std::uint32_t dt_ms = th.t_ms - last_strike_ms_;
        if (dt_ms < 300U || dt_ms > 60000U) {
            continue;
        }
        const float c = d.wind.sound_m_s.ok ? d.wind.sound_m_s.v
                                            : 331.3F * std::sqrt(1.0F + (d.thermo.temp_c.ok ? d.thermo.temp_c.v : 15.0F) / 273.15F);
        const float delay_s = static_cast<float>(dt_ms) / 1000.0F;
        const float km = MetCalculator::thunder_distance_km(delay_s, c);
        // The AS3935 reports a coarse storm-front estimate; accept the acoustic range when compatible.
        if (!last_rf_in_range_ || std::fabs(km - last_rf_km_) <= std::max(5.0F, 0.6F * last_rf_km_)) {
            last_acoustic_ = true;
            last_delay_s_ = delay_s;
            last_acoustic_km_ = km;
            hist_.add(Metric::LightningKm, km);
        }
    }

    LightningDerived& l = d.lightning;
    l.strikes_total = strikes_total_;
    float sum = 0.0F;
    if (hist_.window_value(Metric::Strikes, Tier::Minute, history::TieredHistory::kMinutes, sum)) {
        l.strikes_1h = static_cast<std::uint32_t>(std::lround(sum));
    }
    float today = 0.0F;
    if (hist_.current(Metric::Strikes, Tier::Day).value(history::Agg::Sum, today)) {
        l.strikes_today = static_cast<std::uint32_t>(std::lround(today));
    }
    if (have_strike_) {
        l.last_strike_age_s.set(static_cast<float>(raw.now_ms - last_strike_ms_) / 1000.0F);
        if (last_acoustic_) {
            l.distance_km.set(last_acoustic_km_);
            l.thunder_delay_s.set(last_delay_s_);
            l.method = "acoustic";
        } else if (last_rf_in_range_) {
            l.distance_km.set(last_rf_km_);
            l.method = "rf";
        }
    }
    float closest = 0.0F;
    if (hist_.window_min(Metric::LightningKm, Tier::Minute, history::TieredHistory::kMinutes, closest)) {
        l.closest_1h_km.set(closest);
    }
}

void Pipeline::process_seismic(const RawState& raw, DerivedState& d) {
    if (!raw.imu.vibration.fresh(raw.now_ms, kImuMaxAgeMs)) {
        return;
    }
    const VibrationSecond& v = raw.imu.vibration.value;
    SeismicDerived& s = d.seismic;
    s.ppv_mm_s.set(v.ppv_mm_s);
    s.pga_cm_s2.set(v.pga_mg * kG / 10.0F);  // 1 mg = 0.980665 cm/s^2
    s.dominant_hz.set(v.dominant_hz);
    s.source = MetCalculator::classify_vibration_source(v.pga_mg, v.dominant_hz, d.wind.gust_m_s.ok ? d.wind.gust_m_s.v : 0.0F);
    s.mmi = MetCalculator::mmi_from_pga(v.pga_mg);
}

void Pipeline::process_heater(const RawState& raw, DerivedState& d) {
    HeaterDerived& h = d.heater;
    const bool vin_ok = raw.power.vin_v.fresh(raw.now_ms, kPowerMaxAgeMs);
    const bool ntc_ok = raw.power.reflector_c.fresh(raw.now_ms, kPowerMaxAgeMs);
    if (vin_ok) h.vin_v.set(raw.power.vin_v.value);
    if (ntc_ok) h.reflector_c.set(raw.power.reflector_c.value);
    if (raw.air.scd41_temp_c.fresh(raw.now_ms, kAirMaxAgeMs)) {
        h.probe_c.set(raw.air.scd41_temp_c.value);
        if (d.thermo.temp_c.ok) h.probe_rise_k.set(raw.air.scd41_temp_c.value - d.thermo.temp_c.v);
    }
    // Both heaters run from the separate +12V_HEAT rail (ADS1115 AIN2; nominal 12.0 V if the monitor is missing).
    const float v5 = raw.power.rail_heat.fresh(raw.now_ms, kPowerMaxAgeMs) ? raw.power.rail_heat.value : 12.0F;
    if (raw.power.rail_heat.fresh(raw.now_ms, kPowerMaxAgeMs)) h.rail_heat_v.set(raw.power.rail_heat.value);
    if (raw.power.rail_5v_logic.fresh(raw.now_ms, kPowerMaxAgeMs)) h.rail_logic_v.set(raw.power.rail_5v_logic.value);
    // Probe heater (analog +8 C thermostat): measured through its 1 ohm return shunt.
    const bool probe_i_ok = raw.power.probe_heater_a.fresh(raw.now_ms, kPowerMaxAgeMs);
    if (probe_i_ok) {
        const float ia = std::max(0.0F, raw.power.probe_heater_a.value);
        h.probe_w.set(v5 * ia);
        h.probe_on = ia > kProbeHeaterOnA;
        if (h.probe_c.ok) {
            const bool warm = h.probe_c.v > kProbeSetpointC + 4.0F;   // well above the +8 C setpoint
            const bool cold = h.probe_c.v < kProbeSetpointC - 3.0F;   // heater should be full on
            h.probe_status = h.probe_on ? (warm ? "stuck-on" : "heating") : (cold ? "not-heating" : "ok");
        }
    }
    if (!ntc_ok || !d.thermo.temp_c.ok) {
        return;  // heater stays off without reflector or ambient temperature
    }
    const float vin = v5;  // reflector heater supply = +12V_HEAT rail (12 V, or VIN - ~1 V on a weak 12 V supply)
    const bool icing = d.thermo.temp_c.v <= 2.5F && (d.thermo.rh_pct.v >= 95.0F || d.rain.raining);
    auto out = heater::DeIcingHeaterController::control(vin, raw.power.reflector_c.value, d.thermo.temp_c.v, icing);
    if (heater_check_s_ > 0) {
        // Self-test pulse: fixed 2 W, independent of the thermostat logic (still capped below the 4 W design power)
        out.pwm_duty_percent = std::min(100.0F, 100.0F * kHeaterCheckW * kReflectorOhm / (vin * vin));
        out.actual_power_w = kHeaterCheckW;
    }
    h.state = static_cast<std::uint8_t>(out.state);
    h.duty_pct = out.pwm_duty_percent;
    h.power_w = out.actual_power_w;
    d.controls.heater_duty_pct = out.pwm_duty_percent;
    d.controls.heater_check = heater_check_s_ > 0;

    // Reflector heater: measured mean current vs. the commanded duty (duty x V / 30 ohm).
    if (raw.power.refl_heater_a.fresh(raw.now_ms, kPowerMaxAgeMs)) {
        const float ia = std::max(0.0F, raw.power.refl_heater_a.value);
        h.reflector_w.set(vin * ia);
        const float expected_a = (h.duty_pct / 100.0F) * vin / kReflectorOhm;
        const char* status = "ok";
        if (expected_a > 0.03F && ia < 0.3F * expected_a) {
            status = "open";
        } else if (expected_a < 0.005F && ia > 0.03F) {
            status = "stuck-on";
        }
        if (heater_check_s_ > 0) {
            status = "check-running";
            if (--heater_check_s_ == 0) {   // judge on the last second of the pulse (filter settled)
                const float ratio = expected_a > 0.0F ? ia / expected_a : 0.0F;
                heater_check_result_ = (ratio > 0.7F && ratio < 1.3F) ? "check-pass" : "check-fail";
                heater_check_hold_s_ = 60;
            }
        }
        if (heater_check_hold_s_ > 0) {
            status = heater_check_result_;
            --heater_check_hold_s_;
        }
        h.reflector_status = status;
    }
}

void Pipeline::record_history(const DerivedState& d, float rain_mm, std::uint16_t strikes) {
    auto put = [this](Metric m, const Value<float>& v) {
        if (v.ok) hist_.add(m, v.v);
    };
    put(Metric::TempC, d.thermo.temp_c);
    put(Metric::RhPct, d.thermo.rh_pct);
    put(Metric::DewPointC, d.thermo.dew_point_c);
    put(Metric::QnhHpa, d.baro.qnh_hpa);
    put(Metric::WindMs, d.wind.speed_m_s);
    put(Metric::WindDirDeg, d.wind.dir_deg);
    if (d.rain.sensor_ok) {
        hist_.add(Metric::RainMm, rain_mm);
        hist_.add(Metric::RainRateMmH, d.rain.rate_mm_h);
        hist_.add(Metric::HailMm, rain_.last_second_hail_mm());
        hist_.add(Metric::HailRateMmH, d.rain.hail_rate_mm_h);
    }
    hist_.add(Metric::Strikes, static_cast<float>(strikes));
    put(Metric::SolarWm2, d.solar.irradiance_w_m2);
    put(Metric::UvIndex, d.solar.uv_index);
    put(Metric::Lux, d.solar.lux);
    put(Metric::CloudPct, d.solar.cloud_cover_pct);
    put(Metric::Pm1, d.air.pm1_ug_m3);
    put(Metric::Pm25, d.air.pm25_ug_m3);
    put(Metric::Pm10, d.air.pm10_ug_m3);
    put(Metric::Co2Ppm, d.air.co2_ppm);
    put(Metric::VocIndex, d.air.voc_index);
    put(Metric::NoxIndex, d.air.nox_index);
    put(Metric::PpvMmS, d.seismic.ppv_mm_s);
    put(Metric::PgaCmS2, d.seismic.pga_cm_s2);
    hist_.add(Metric::HeaterW, d.heater.power_w);
    put(Metric::VinV, d.heater.vin_v);
}

}  // namespace core
}  // namespace station
