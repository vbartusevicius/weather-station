// Host simulator: runs the REAL firmware pipeline (src/core, src/history, processors, publishers)
// fed with physically modelled raw sensor values for a demo scenario.
//
// Line protocol on stdin, one JSON line per reply on stdout:
//   scenario <name>              reset + warm up 31 days of history, reply {"ok":true}
//   tick                         advance one second, reply = state JSON (/api/state)
//   minutes                      reply = /api/history/minutes JSON
//   history <tier> [metrics]     reply = /api/history JSON (tier = minute|hour|day)
//   config <lat> <lon> <alt_m>   set the location, reply {"ok":true}
//   action reset_rain            reply {"ok":true}
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "astro/Astronomy.hpp"
#include "core/Pipeline.hpp"
#include "rain/HapticRain.hpp"
#include "telemetry/HomeAssistantMqtt.hpp"

using namespace station;

namespace {
constexpr double kPi = 3.14159265358979323846;

// ---- deterministic noise ------------------------------------------------------------------------
double hash01(std::uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<double>(x & 0xFFFFFFFFULL) / 4294967295.0;
}
double noise(std::int64_t t, std::uint64_t channel) { return hash01(static_cast<std::uint64_t>(t) * 1315423911ULL + channel) * 2.0 - 1.0; }
// smooth value noise with period `p` seconds
double smooth(std::int64_t t, double p, std::uint64_t channel) {
    const double x = static_cast<double>(t) / p;
    const auto i = static_cast<std::int64_t>(std::floor(x));
    const double f = x - static_cast<double>(i);
    const double u = f * f * (3.0 - 2.0 * f);
    return noise(i, channel) * (1.0 - u) + noise(i + 1, channel) * u;
}

// ---- Europe/Vilnius offset (EET / EEST, EU rule) ------------------------------------------------
std::int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}
std::int64_t last_sunday_utc_1am(int year, unsigned month) {
    std::int64_t day = days_from_civil(year, month, 31);
    while (((day + 4) % 7 + 7) % 7 != 0) {  // 1970-01-01 was a Thursday
        --day;
    }
    return day * 86400 + 3600;
}
int year_of(std::int64_t unix_s) {
    int y = 1970 + static_cast<int>(unix_s / 31556952);
    while (days_from_civil(y + 1, 1, 1) * 86400 <= unix_s) ++y;
    while (days_from_civil(y, 1, 1) * 86400 > unix_s) --y;
    return y;
}
std::int32_t vilnius_offset(std::int64_t unix_s) {
    const int y = year_of(unix_s);
    const bool dst = unix_s >= last_sunday_utc_1am(y, 3) && unix_s < last_sunday_utc_1am(y, 10);
    return dst ? 3 * 3600 : 2 * 3600;
}
std::int64_t local_to_unix(int y, unsigned m, unsigned d, int hh, int mm) {
    const std::int64_t guess = days_from_civil(y, m, d) * 86400 + hh * 3600 + mm * 60;
    return guess - vilnius_offset(guess - 2 * 3600);
}

// ---- scenarios ------------------------------------------------------------------------------------
struct Scenario {
    const char* name;
    std::int64_t end_unix;       // "now" at the end of the warm-up
    double temp_c, temp_amp, rh, qnh, qnh_trend_per_h;
    double wind_ms, gustiness, wind_dir;
    double cloud_pct;            // daytime cloud cover
    double night_lux;            // sky illuminance when the sun is down
    double pm25, pm10, co2;
    double rain_mm_h;            // rain rate during the active window
    std::int64_t rain_window_s;  // rain for the last N seconds
    double hail_mm_h;
    std::int64_t hail_window_s;
    int strikes_per_10min;       // lightning during the rain window
    double storm_km;             // storm distance at "now" (approaching)
    bool train;
    double ripple_pa;
    double sgp_voc_event;      // SGP41 raw VOC tick change during the event window (negative = more VOC)
    double sgp_nox_event;      // SGP41 raw NOx tick change (positive = more NOx)
    std::int64_t gas_event_s;  // event window before "now"
};

Scenario make(const std::string& n) {
    Scenario s{};
    s.name = "breeze";
    s.end_unix = local_to_unix(2025, 4, 22, 13, 0);
    s.temp_c = 15.5; s.temp_amp = 4.0; s.rh = 56; s.qnh = 1016.5; s.qnh_trend_per_h = 0.05;
    s.wind_ms = 4.2; s.gustiness = 0.35; s.wind_dir = 225;
    s.cloud_pct = 25; s.night_lux = 0.004;
    s.pm25 = 6.2; s.pm10 = 12.8; s.co2 = 418;
    s.ripple_pa = 0.12;
    if (n == "storm") {
        s.name = "storm";
        s.end_unix = local_to_unix(2025, 7, 15, 16, 30);
        s.temp_c = 22.0; s.temp_amp = 3.0; s.rh = 86; s.qnh = 1002.0; s.qnh_trend_per_h = -1.1;
        s.wind_ms = 9.5; s.gustiness = 0.55; s.wind_dir = 250;
        s.cloud_pct = 96;
        s.pm25 = 4.0; s.pm10 = 9.0; s.co2 = 425;
        s.rain_mm_h = 32.0; s.rain_window_s = 50 * 60; s.hail_mm_h = 6.0; s.hail_window_s = 12 * 60;
        s.strikes_per_10min = 9; s.storm_km = 4.5; s.ripple_pa = 0.6;
    } else if (n == "freeze") {
        s.name = "freeze";
        s.end_unix = local_to_unix(2025, 1, 15, 10, 0);
        s.temp_c = -4.0; s.temp_amp = 1.2; s.rh = 97; s.qnh = 1029.0; s.qnh_trend_per_h = 0.4;
        s.wind_ms = 1.6; s.gustiness = 0.25; s.wind_dir = 90;
        s.cloud_pct = 92;
        s.pm25 = 14.0; s.pm10 = 22.0; s.co2 = 432;
    } else if (n == "heatwave") {
        s.name = "heatwave";
        s.end_unix = local_to_unix(2025, 7, 20, 14, 0);
        s.temp_c = 29.0; s.temp_amp = 5.5; s.rh = 34; s.qnh = 1012.0; s.qnh_trend_per_h = -0.15;
        s.wind_ms = 2.6; s.gustiness = 0.4; s.wind_dir = 180;
        s.cloud_pct = 4;
        s.pm25 = 11.0; s.pm10 = 24.0; s.co2 = 410;
    } else if (n == "plume") {
        s.name = "plume";
        s.end_unix = local_to_unix(2025, 11, 5, 12, 0);
        s.temp_c = 6.0; s.temp_amp = 1.5; s.rh = 80; s.qnh = 1024.0; s.qnh_trend_per_h = 0.0;
        s.wind_ms = 0.9; s.gustiness = 0.3; s.wind_dir = 135;
        s.cloud_pct = 70;
        s.pm25 = 46.0; s.pm10 = 68.0; s.co2 = 515;
        s.sgp_voc_event = -330.0; s.sgp_nox_event = 4600.0; s.gas_event_s = 45 * 60;
    } else if (n == "train") {
        s.name = "train";
        s.end_unix = local_to_unix(2025, 5, 10, 11, 0);
        s.temp_c = 16.0; s.temp_amp = 4.0; s.rh = 58; s.qnh = 1014.6; s.qnh_trend_per_h = 0.1;
        s.wind_ms = 2.2; s.gustiness = 0.3; s.wind_dir = 180;
        s.cloud_pct = 35;
        s.pm25 = 9.4; s.pm10 = 24.0; s.co2 = 422;
        s.train = true;
    } else if (n == "night_dark") {
        s.name = "night_dark";
        s.end_unix = local_to_unix(2025, 10, 21, 23, 30);  // new moon
        s.temp_c = 5.0; s.temp_amp = 3.0; s.rh = 84; s.qnh = 1021.0; s.qnh_trend_per_h = 0.1;
        s.wind_ms = 1.1; s.gustiness = 0.3; s.wind_dir = 315;
        s.cloud_pct = 10; s.night_lux = 0.0011;
        s.pm25 = 5.0; s.pm10 = 9.0; s.co2 = 421;
    } else if (n == "night_moon") {
        s.name = "night_moon";
        s.end_unix = local_to_unix(2025, 10, 7, 0, 30);  // full moon
        s.temp_c = 9.0; s.temp_amp = 3.0; s.rh = 90; s.qnh = 1009.0; s.qnh_trend_per_h = -0.4;
        s.wind_ms = 2.0; s.gustiness = 0.35; s.wind_dir = 200;
        s.cloud_pct = 60; s.night_lux = 0.12;
        s.pm25 = 7.0; s.pm10 = 13.0; s.co2 = 424;
    }
    return s;
}

// ---- simulator ------------------------------------------------------------------------------------
class Sim {
public:
    void reset(const std::string& name) {
        sc_ = make(name);
        pipeline_ = core::Pipeline{};
        events_ = core::EventQueues{};
        raw_ = core::RawState{};
        derived_ = core::DerivedState{};
        // calibrate one rain impact's attributed depth through the real processor
        rain::HapticRainProcessor probe;
        drop_mm_ = probe.process_impact({626, 206, 220, 0.01F, static_cast<float>(sc_.wind_ms), static_cast<float>(sc_.temp_c)});
        rain::HapticRainProcessor hp;
        hail_mm_ = hp.process_impact({1850, 180, 110, 0.01F, static_cast<float>(sc_.wind_ms), static_cast<float>(sc_.temp_c)});
        drop_acc_ = hail_acc_ = strike_acc_ = 0.0;
        // Warm-up: 31 days at 60-s steps (fills day / hour tiers), then the last 20 min at 1 s.
        const std::int64_t fine = 20 * 60;
        t_ = sc_.end_unix - 31 * 86400;
        mono_ms_ = 1000;
        while (t_ < sc_.end_unix - fine) {
            step(60);
        }
        while (t_ < sc_.end_unix) {
            step(1);
        }
    }

    void step(int dt) {
        t_ += dt;
        mono_ms_ += static_cast<std::uint32_t>(dt) * 1000U;
        fill_raw(dt);
        pipeline_.step(raw_, events_, cfg_, derived_);
    }

    std::string state() const {
        telemetry::WifiInfo w{};
        w.connected = true;
        w.rssi_dbm = -56;
        std::strncpy(w.ip, "127.0.0.1", sizeof(w.ip) - 1);
        return telemetry::TelemetryFormatter::build_state_json(derived_, "sim", w);
    }
    std::string minutes() const { return telemetry::TelemetryFormatter::build_minute_history_json(pipeline_.history()); }
    std::string history(history::Tier tier, const std::string& metrics) const {
        return telemetry::TelemetryFormatter::build_history_json(pipeline_.history(), tier, metrics.c_str(), t_);
    }
    void set_location(double lat, double lon, float alt) {
        cfg_.location_set = true;
        cfg_.latitude_deg = lat;
        cfg_.longitude_deg = lon;
        cfg_.altitude_m = alt;
    }
    void reset_rain() { pipeline_.reset_rain_today(); }
    void clear_location() { cfg_.location_set = false; }

private:
    void fill_raw(int dt) {
        const std::uint32_t now = mono_ms_;
        const std::int64_t t = t_;
        const std::int64_t age = sc_.end_unix - t;  // seconds before "now" (negative after)
        raw_.now_ms = now;
        raw_.clock.valid = true;
        raw_.clock.unix_s = t;
        raw_.clock.utc_offset_s = vilnius_offset(t);

        const double local_h = std::fmod(static_cast<double>(t + raw_.clock.utc_offset_s) / 3600.0, 24.0);
        const bool raining = sc_.rain_mm_h > 0 && age >= 0 && age < sc_.rain_window_s;
        // ---- thermo (diurnal cycle peaks at 15:00, storm cools the air) ----
        double temp = sc_.temp_c + sc_.temp_amp * std::sin(2.0 * kPi * (local_h - 9.0) / 24.0) + 0.3 * smooth(t, 900, 1);
        if (raining) temp -= 4.0 * (1.0 - static_cast<double>(age) / static_cast<double>(sc_.rain_window_s));
        double rh = sc_.rh - 1.8 * (temp - sc_.temp_c) + 2.0 * smooth(t, 1200, 2);
        if (raining) rh = std::max(rh, 93.0);
        rh = std::min(100.0, std::max(5.0, rh));
        raw_.thermo.temp_c.set(static_cast<float>(temp), now);
        raw_.thermo.rh_pct.set(static_cast<float>(rh), now);

        // ---- barometer: station pressure from QNH at 112 m, linear tendency + S2 tide + noise ----
        const double trend_age_h = std::min(6.0, std::max(0.0, static_cast<double>(age) / 3600.0));  // tendency over the last 6 h
        const double qnh = sc_.qnh - sc_.qnh_trend_per_h * trend_age_h +
                           0.6 * std::cos(2.0 * kPi * (local_h - 10.0) / 12.0) * 0.21 + 0.05 * smooth(t, 600, 3);
        raw_.baro.pressure_hpa.set(static_cast<float>(qnh / 1.0134), now);
        raw_.baro.ripple_pa.set(static_cast<float>(sc_.ripple_pa * (1.0 + 0.3 * smooth(t, 60, 4))), now);
        raw_.baro.sensor_temp_c.set(static_cast<float>(temp + 2.0), now);

        // ---- wind: gusty speed, slowly veering direction ----
        double speed = sc_.wind_ms * (1.0 + sc_.gustiness * 0.6 * smooth(t, 9, 5) + sc_.gustiness * 0.4 * smooth(t, 2.5, 6));
        if (raining) speed *= 1.25;
        speed = std::max(0.0, speed);
        const double dir = sc_.wind_dir + 18.0 * smooth(t, 300, 7) + 8.0 * smooth(t, 6, 8);
        core::WindSecond w{};
        w.v_east_m_s = static_cast<float>(-speed * std::sin(dir * kPi / 180.0));
        w.v_north_m_s = static_cast<float>(-speed * std::cos(dir * kPi / 180.0));
        w.speed_mean_m_s = static_cast<float>(speed);
        w.sound_speed_m_s = static_cast<float>(331.3 * std::sqrt(1.0 + temp / 273.15) + 0.0124 * rh * 0.1);
        w.virtual_temp_c = static_cast<float>(temp + 0.0006 * rh * (temp + 20.0));
        w.samples = 4;
        raw_.wind.second.set(w, now);
        raw_.wind.tilt_deg.set(0.38F, now);

        // ---- optics from the real sun position + Kasten-Czeplak cloud attenuation ----
        const auto sun = astro::Astronomy::sun(t, cfg_.latitude_deg, cfg_.longitude_deg);
        double cloud = sc_.cloud_pct + 6.0 * smooth(t, 1800, 9);
        cloud = std::min(100.0, std::max(0.0, cloud));
        double lux = sc_.night_lux * (1.0 + 0.1 * smooth(t, 600, 10));
        double uvi = 0.0;
        if (sun.altitude_deg > -6.0F) {
            const double clear = astro::Astronomy::clear_sky_ghi_w_m2(sun.altitude_deg);
            const double g = clear * (1.0 - 0.75 * std::pow(cloud / 100.0, 3.4));
            const double twilight = sun.altitude_deg < 0 ? 400.0 * std::pow(10.0, sun.altitude_deg / 2.0) : 0.0;
            lux = std::max(lux, g * 122.0 + twilight);
            const double cz = std::sin(std::max(0.0F, sun.altitude_deg) * kPi / 180.0);
            uvi = 12.5 * std::pow(cz, 2.42) * (1.0 - 0.56 * std::pow(cloud / 100.0, 2.0));
        }
        if (lux < 80000.0) {
            raw_.optical.tsl_lux.set(static_cast<float>(lux), now);
            raw_.optical.tsl_nir_ratio.set(static_cast<float>(cloud > 70 ? 0.22 : 0.27), now);
        } else {
            raw_.optical.tsl_lux.invalidate();  // TSL25911 saturates; LTR-390 ALS takes over
        }
        raw_.optical.ltr_lux.set(static_cast<float>(lux * (1.0 + 0.02 * smooth(t, 30, 11))), now);
        // LTR-390 UVS leans toward UV-A (reads ~8 % high vs. the erythemal UVI); GUVB-C31SM tracks UV-B.
        raw_.optical.ltr_uvi.set(static_cast<float>(std::max(0.0, uvi * 1.08)), now);
        raw_.optical.guvb_uvi.set(static_cast<float>(std::max(0.0, uvi * (1.0 + 0.03 * smooth(t, 120, 20)))), now);
        raw_.optical.guvb_counts.set(static_cast<float>(std::max(0.0, uvi) * 410.0), now);

        // ---- air ----
        const double aq = 1.0 + 0.15 * smooth(t, 3600, 12) + 0.05 * smooth(t, 120, 13);
        raw_.air.pm1_ug_m3.set(static_cast<float>(sc_.pm25 * 0.6 * aq), now);
        raw_.air.pm25_ug_m3.set(static_cast<float>(sc_.pm25 * aq), now);
        raw_.air.pm10_ug_m3.set(static_cast<float>(sc_.pm10 * aq), now);
        raw_.air.co2_ppm.set(static_cast<float>(sc_.co2 + 6.0 * smooth(t, 600, 16)), now);
        // SCD41 sits on the probe heater zone: the analog thermostat holds it at +8 C in the cold, else ~0.4 K self-heating
        raw_.air.scd41_temp_c.set(static_cast<float>(std::max(8.0 + 0.3 * smooth(t, 300, 25), temp + 0.4)), now);

        // SGP41 raw ticks: sensor baseline with realistic noise/drift; events only near "now" (the gas
        // index is relative to the site's own last ~24 h, so a permanent level would read as normal).
        const bool gas_event = sc_.gas_event_s > 0 && age >= 0 && age < sc_.gas_event_s;
        const double voc_ticks = 30000.0 + 60.0 * smooth(t, 1800, 21) + 25.0 * noise(t, 22) + (gas_event ? sc_.sgp_voc_event : 0.0);
        const double nox_ticks = 16000.0 + 30.0 * smooth(t, 1800, 23) + 15.0 * noise(t, 24) + (gas_event ? sc_.sgp_nox_event : 0.0);
        raw_.air.sgp_sraw_voc.set(static_cast<std::uint16_t>(voc_ticks), now);
        raw_.air.sgp_sraw_nox.set(static_cast<std::uint16_t>(nox_ticks), now);

        // ---- IMU: ambient floor, wind sway in storms, freight trains every 14 min ----
        core::VibrationSecond v{};
        v.pga_mg = static_cast<float>(0.45 + 0.05 * std::fabs(noise(t, 17)));
        v.ppv_mm_s = static_cast<float>(0.035 + 0.008 * std::fabs(noise(t, 18)));
        v.dominant_hz = 18.0F;
        if (speed > 12.0) {
            v.pga_mg = static_cast<float>(0.02 * speed * speed);
            v.ppv_mm_s = static_cast<float>(v.pga_mg * 9.81 / (2 * kPi * 3.0));
            v.dominant_hz = 3.0F;
        }
        if (sc_.train && ((t % 840) < 25)) {
            const double env = std::sin(kPi * static_cast<double>(t % 840) / 25.0);
            v.pga_mg = static_cast<float>(5.4 * env + 0.5);
            v.ppv_mm_s = static_cast<float>(0.35 * env + 0.03);
            v.dominant_hz = 24.0F;
        }
        raw_.imu.vibration.set(v, now);
        raw_.imu.gravity_g.set({0.0F, 0.0066F, 1.0F}, now);

        // ---- power / heater ----
        raw_.power.vin_v.set(24.1F, now);
        raw_.power.reflector_c.set(static_cast<float>(temp + (derived_.heater.power_w > 0.0F ? 5.0 : 1.0)), now);
        raw_.power.rain_adc_ok = true;
        // Heater power monitor (ADS1115): reflector mean current = duty x 4.95 V / 6.75 ohm; probe heater 83 mA below +8 C
        raw_.power.refl_heater_a.set(static_cast<float>(derived_.heater.duty_pct / 100.0 * 4.95 / 6.75), now);
        raw_.power.probe_heater_a.set(temp < 8.0 ? 0.0833F : 0.0F, now);
        raw_.power.rail_5v.set(4.95F, now);
        raw_.power.rail_5v_logic.set(5.01F, now);

        // ---- rain & hail impacts (deterministic Poisson-like arrivals) ----
        if (raining) {
            drop_acc_ += sc_.rain_mm_h / 3600.0 * dt / std::max(1e-6F, drop_mm_);
            if (age < sc_.hail_window_s) {
                hail_acc_ += sc_.hail_mm_h / 3600.0 * dt / std::max(1e-6F, hail_mm_);
            }
        }
        while (drop_acc_ >= 1.0) {
            events_.rain.push({now, 626, 206, 220});
            drop_acc_ -= 1.0;
        }
        while (hail_acc_ >= 1.0) {
            events_.rain.push({now, 1850, 180, 110});
            hail_acc_ -= 1.0;
        }

        // ---- lightning (approaching storm) + thunder heard within 15 km ----
        if (sc_.strikes_per_10min > 0 && age >= 0 && age < sc_.rain_window_s + 20 * 60) {
            strike_acc_ += sc_.strikes_per_10min / 600.0 * dt;
            if (strike_acc_ >= 1.0) {
                strike_acc_ -= 1.0;
                const double km = sc_.storm_km + static_cast<double>(age) / 60.0 * 0.35 + 1.5 * std::fabs(noise(t, 19));
                const auto rf = static_cast<std::uint8_t>(std::min(40.0, std::max(1.0, std::round(km))));
                events_.lightning.push({now, rf, 90000});
                if (dt == 1 && km < 15.0) {
                    pending_thunder_ms_ = now + static_cast<std::uint32_t>(km * 1000.0 / w.sound_speed_m_s * 1000.0);
                }
            }
        }
        if (pending_thunder_ms_ != 0 && now >= pending_thunder_ms_) {
            events_.thunder.push({pending_thunder_ms_, 2.5F});
            pending_thunder_ms_ = 0;
        }
    }

    Scenario sc_{};
    core::StationConfig cfg_ = [] {
        core::StationConfig c{};
        c.location_set = true;
        c.latitude_deg = 54.6872;
        c.longitude_deg = 25.2797;
        c.altitude_m = 112.0F;
        return c;
    }();
    core::Pipeline pipeline_{};
    core::EventQueues events_{};
    core::RawState raw_{};
    core::DerivedState derived_{};
    std::int64_t t_{0};
    std::uint32_t mono_ms_{0};
    float drop_mm_{0.0F};
    float hail_mm_{0.0F};
    double drop_acc_{0.0}, hail_acc_{0.0}, strike_acc_{0.0};
    std::uint32_t pending_thunder_ms_{0};
};
}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    static Sim sim;
    sim.reset("breeze");
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        std::string reply = "{\"ok\":false}";
        if (cmd == "scenario") {
            std::string name;
            in >> name;
            sim.reset(name);
            reply = "{\"ok\":true}";
        } else if (cmd == "tick") {
            sim.step(1);
            reply = sim.state();
        } else if (cmd == "state") {
            reply = sim.state();
        } else if (cmd == "minutes") {
            reply = sim.minutes();
        } else if (cmd == "history") {
            std::string tier, metrics;
            in >> tier >> metrics;
            const auto t = tier == "minute" ? history::Tier::Minute : (tier == "day" ? history::Tier::Day : history::Tier::Hour);
            reply = sim.history(t, metrics);
        } else if (cmd == "config") {
            double lat = 0, lon = 0;
            float alt = 0;
            if (in >> lat >> lon >> alt) {
                sim.set_location(lat, lon, alt);
                reply = "{\"ok\":true}";
            }
        } else if (cmd == "action") {
            std::string a;
            in >> a;
            if (a == "reset_rain") {
                sim.reset_rain();
                reply = "{\"ok\":true}";
            }
        }
        std::cout << reply << '\n' << std::flush;
    }
    return 0;
}
