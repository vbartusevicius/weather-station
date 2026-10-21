#pragma once

#include <array>
#include <cstdint>

#include "Reading.hpp"

namespace station {
namespace core {

// ---------------------------------------------------------------------------------------------
// DerivedState: everything the Web UI, Home Assistant and RS-485 publish. Written only by the
// pipeline processors once per second; publishers only read it. Value<T>.ok == false is
// serialised as JSON null ("—" in the UI).
// ---------------------------------------------------------------------------------------------

struct WindDerived {
    Value<float> speed_m_s;        // 3-s running mean (WMO "instantaneous")
    Value<float> dir_deg;          // direction the wind blows FROM, 3-s vector mean
    Value<float> avg_2m_m_s;
    Value<float> avg_10m_m_s;
    Value<float> dir_10m_deg;      // 10-min vector-mean direction
    Value<float> gust_m_s;         // max 3-s mean over 10 min (WMO gust)
    Value<float> lull_m_s;         // min 3-s mean over 10 min
    Value<float> beaufort;
    Value<float> turbulence_pct;
    Value<float> trend_m_s_per_h;  // change of the 10-min mean over the last hour
    Value<float> dir_change_3h_deg;// + veering (clockwise), - backing
    Value<float> load_kg_m2;       // dynamic pressure q = 1/2 rho v^2 of the 3-s wind, as kg-force per m^2
    Value<float> load_min_1h_kg_m2;
    Value<float> load_max_1h_kg_m2;
    Value<float> sound_m_s;
    Value<float> acoustic_temp_c;
    Value<float> tilt_deg;
    std::array<float, 16> rose_pct{};
};

struct ThermoDerived {
    Value<float> temp_c;            // solar-heating corrected air temperature
    Value<float> solar_offset_c;
    Value<float> rh_pct;
    Value<float> dew_point_c;
    Value<float> wet_bulb_c;
    Value<float> feels_like_c;
    Value<float> wind_chill_c;
    Value<float> heat_index_c;
    Value<float> abs_humidity_g_m3;
    Value<float> vpd_kpa;
    Value<float> cloud_base_m;
    Value<float> temp_min_24h_c;
    Value<float> temp_max_24h_c;
    Value<float> trend_c_per_h;
    bool frost_risk{false};
    const char* comfort{"—"};
};

struct BaroDerived {
    Value<float> station_hpa;       // wind-compensated static pressure
    Value<float> wind_corr_hpa;     // port suction correction added to the raw BMP581 value
    Value<float> qnh_hpa;           // reduced to sea level
    Value<float> change_3h_hpa;     // measured QNH change over exactly 3 h
    Value<float> tide_3h_hpa;       // expected semidiurnal solar-tide part of that change
    Value<float> synoptic_3h_hpa;   // change_3h - tide_3h: the weather-system signal
    Value<float> qnh_min_24h_hpa;
    Value<float> qnh_max_24h_hpa;
    Value<float> air_density_kg_m3;
    Value<float> ripple_pa;
    const char* trend{"—"};
    const char* forecast{"—"};
    const char* front{"—"};
    const char* wind_shift{"—"};
    Value<float> rain_prob_pct;
};

struct SolarDerived {
    Value<float> lux;
    Value<float> irradiance_w_m2;
    Value<float> visible_w_m2;
    Value<float> nir_w_m2;
    Value<float> uv_index;          // fused: 80 % GUVB-C31SM (true UV-B) + 20 % LTR-390, or LTR-390 alone
    Value<float> uv_index_guvb;
    Value<float> uv_index_ltr;
    const char* uv_source{"—"};
    Value<float> uva_w_m2;
    Value<float> uvb_w_m2;           // estimate from the UV index (not a band-integrated measurement)
    Value<float> burn_time_min;      // fair skin (type II) time to 1 MED; invalid when UVI < 1
    Value<float> clear_sky_w_m2;
    Value<float> cloud_cover_pct;    // daytime only (sun > 10 deg)
    Value<float> sqm_mag_arcsec2;    // night only
    Value<float> bortle;             // night only
};

struct AstroDerived {
    bool location_set{false};
    bool is_night{false};            // sun below -6 deg (civil twilight) or, without location, dark sky
    Value<float> sun_alt_deg;
    Value<float> sun_az_deg;
    Value<float> sunrise_min;        // local minutes after midnight
    Value<float> sunset_min;
    Value<float> daylight_min;
    Value<float> moon_alt_deg;
    Value<float> moon_illum_pct;
    Value<float> moon_age_days;
    Value<float> moonrise_min;
    Value<float> moonset_min;
    const char* moon_phase{"—"};
};

struct RainDerived {
    const char* state{"Dry"};
    bool raining{false};
    bool surface_wet{false};
    float rate_mm_h{0.0F};          // last 60 s x 60
    float hail_rate_mm_h{0.0F};
    float event_mm{0.0F};           // current shower (resets after 15 dry minutes)
    float last_hour_mm{0.0F};       // rolling 60 min
    float today_mm{0.0F};           // since local midnight (or manual reset)
    float total_mm{0.0F};
    std::uint32_t drops{0};
    std::uint32_t hail_hits{0};
    float max_hail_mm{0.0F};
    std::uint32_t rejected_shake{0};
    bool sensor_ok{false};
};

struct AirDerived {
    Value<float> pm1_ug_m3;
    Value<float> pm25_ug_m3;         // humidity-corrected ("dry")
    Value<float> pm25_raw_ug_m3;     // as reported by the PMSA003I
    Value<float> pm10_ug_m3;         // humidity-corrected
    Value<float> coarse_ug_m3;       // PM10 - PM2.5 (pollen, spores, dust)
    Value<float> voc_index;          // SGP41 + Sensirion gas index algorithm (1..500, 100 = typical for this site)
    Value<float> nox_index;          // (1..500, 1 = clean baseline)
    bool gas_learning{false};        // gas index algorithm still in its initial learning phase
    Value<float> co2_ppm;
    Value<float> aqi;                // US EPA AQI from NowCast PM2.5 / PM10
    Value<float> eaqi;               // European AQI band 1..6 from 24-h PM means
    const char* level{"—"};
    const char* dominant{"—"};
};

struct LightningDerived {
    std::uint32_t strikes_total{0};
    std::uint32_t strikes_1h{0};
    std::uint32_t strikes_today{0};
    Value<float> last_strike_age_s;
    Value<float> distance_km;        // most recent strike
    Value<float> closest_1h_km;
    Value<float> thunder_delay_s;    // RF-to-acoustic delay of the most recent strike (when heard)
    const char* method{"—"};         // "acoustic" (thunder delay x measured sound speed) or "rf" (AS3935 estimate)
};

struct SeismicDerived {
    Value<float> ppv_mm_s;
    Value<float> pga_cm_s2;
    Value<float> dominant_hz;
    const char* source{"—"};
    const char* mmi{"—"};
};

struct HeaterDerived {
    std::uint8_t state{0};
    Value<float> vin_v;
    Value<float> rail_heat_v;        // +5V_HEAT (heaters)
    Value<float> rail_logic_v;       // +5V (electronics)
    Value<float> reflector_c;
    float duty_pct{0.0F};
    float power_w{0.0F};             // reflector heater, set point: duty x V5^2 / 6.75 ohm
    Value<float> probe_c;            // probe heater zone (SCD41 on-chip temperature; analog +8 C thermostat)
    Value<float> probe_rise_k;       // probe heater zone minus air temperature
    Value<float> reflector_w;        // measured: +5V x mean shunt current
    Value<float> probe_w;            // measured: +5V x shunt current
    bool probe_on{false};            // probe thermostat is heating (current > 20 mA)
    const char* reflector_status{"—"};  // ok / open / stuck-on / check-running / check-pass / check-fail
    const char* probe_status{"—"};      // ok / heating / stuck-on / not-heating
};

// Commands from processors back to the acquisition layer.
struct Controls {
    float heater_duty_pct{0.0F};
    bool heater_check{false};        // HEAT_CHECK self-test running (fixed 2 W reflector pulse)
    bool sht_bakeout{false};         // run the SHT40 200 mW heater pulse now
    bool scd41_pause{false};         // stop SCD41 periodic measurement (extreme cold)
    bool thermo_locked{false};       // ignore SHT40 values (post-bake-out cool-down)
    float ambient_pressure_hpa{0.0F};// sent to SCD41 for in-sensor CO2 pressure compensation
};

struct DerivedState {
    std::uint32_t uptime_s{0};
    bool clock_valid{false};
    std::int64_t unix_s{0};
    std::int32_t utc_offset_s{0};
    WindDerived wind;
    ThermoDerived thermo;
    BaroDerived baro;
    SolarDerived solar;
    AstroDerived astro;
    RainDerived rain;
    AirDerived air;
    LightningDerived lightning;
    SeismicDerived seismic;
    HeaterDerived heater;
    Controls controls;
};

}  // namespace core
}  // namespace station
