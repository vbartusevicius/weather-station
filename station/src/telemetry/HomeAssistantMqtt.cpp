#include "HomeAssistantMqtt.hpp"

#include <ArduinoJson.h>

#include <cmath>
#include <cstring>

namespace station {
namespace telemetry {

namespace {
using core::Value;

double round_to(float v, int decimals) {
    const double p = std::pow(10.0, decimals);
    return std::round(static_cast<double>(v) * p) / p;
}

void put(JsonObject o, const char* key, const Value<float>& v, int decimals) {
    if (v.ok && std::isfinite(v.v)) {
        o[key] = round_to(v.v, decimals);
    } else {
        o[key] = nullptr;
    }
}

void putf(JsonObject o, const char* key, float v, int decimals) {
    o[key] = round_to(v, decimals);
}

struct HaSensor {
    const char* id;
    const char* name;
    const char* unit;
    const char* device_class;
    const char* state_class;
    const char* tpl;
};

// Home Assistant records long-term statistics itself; the station only publishes current values.
constexpr HaSensor kHaSensors[] = {
    {"temperature", "Temperature", "°C", "temperature", "measurement", "{{ value_json.environment.temp_c }}"},
    {"humidity", "Humidity", "%", "humidity", "measurement", "{{ value_json.environment.humidity_rh }}"},
    {"dew_point", "Dew Point", "°C", "temperature", "measurement", "{{ value_json.environment.dew_point_c }}"},
    {"feels_like", "Feels Like", "°C", "temperature", "measurement", "{{ value_json.environment.feels_like_c }}"},
    {"pressure", "Sea-Level Pressure", "hPa", "atmospheric_pressure", "measurement", "{{ value_json.environment.qnh_hpa }}"},
    {"pressure_trend_3h", "Pressure Change 3h", "hPa", "", "measurement", "{{ value_json.environment.pressure_3h_delta_hpa }}"},
    {"wind_speed", "Wind Speed", "m/s", "wind_speed", "measurement", "{{ value_json.wind.avg_2m_m_s }}"},
    {"wind_gust", "Wind Gust", "m/s", "wind_speed", "measurement", "{{ value_json.wind.gust_m_s }}"},
    {"wind_direction", "Wind Direction", "°", "", "measurement", "{{ value_json.wind.dir_10m_deg }}"},
    {"rain_rate", "Rain Rate", "mm/h", "precipitation_intensity", "measurement", "{{ value_json.rain.rate_mm_h }}"},
    {"rain_today", "Rain Today", "mm", "precipitation", "total_increasing", "{{ value_json.rain.today_mm }}"},
    {"uv_index", "UV Index", "", "", "measurement", "{{ value_json.solar.uv_index }}"},
    {"irradiance", "Solar Irradiance", "W/m²", "irradiance", "measurement", "{{ value_json.solar.irradiance_w_m2 }}"},
    {"illuminance", "Illuminance", "lx", "illuminance", "measurement", "{{ value_json.solar.lux }}"},
    {"cloud_cover", "Cloud Cover", "%", "", "measurement", "{{ value_json.solar.cloud_cover_pct }}"},
    {"pm25", "PM2.5", "µg/m³", "pm25", "measurement", "{{ value_json.air_quality.pm2_5 }}"},
    {"pm10", "PM10", "µg/m³", "pm10", "measurement", "{{ value_json.air_quality.pm10 }}"},
    {"pm1", "PM1.0", "µg/m³", "pm1", "measurement", "{{ value_json.air_quality.pm1_0 }}"},
    {"co2", "CO₂", "ppm", "carbon_dioxide", "measurement", "{{ value_json.air_quality.co2_ppm }}"},
    {"voc_index", "VOC Index", "", "", "measurement", "{{ value_json.air_quality.voc_index }}"},
    {"nox_index", "NOx Index", "", "", "measurement", "{{ value_json.air_quality.nox_index }}"},
    {"aqi", "Air Quality Index", "", "aqi", "measurement", "{{ value_json.air_quality.aqi }}"},
    {"lightning_distance", "Lightning Distance", "km", "distance", "measurement", "{{ value_json.lightning.distance_km }}"},
    {"lightning_strikes", "Lightning Strikes", "", "", "total_increasing", "{{ value_json.lightning.strikes_total }}"},
    {"vibration", "Ground Vibration", "mm/s", "", "measurement", "{{ value_json.seismic.ppv_mm_s }}"},
    {"reflector_heater_power", "Reflector Heater Power", "W", "power", "measurement", "{{ value_json.heater.reflector_w }}"},
    {"reflector_heater_temp", "Reflector Heater Temperature", "°C", "temperature", "measurement", "{{ value_json.heater.reflector_c }}"},
    {"probe_heater_power", "Probe Heater Power", "W", "power", "measurement", "{{ value_json.heater.probe_w }}"},
    {"probe_heater_temp", "Probe Heater Zone Temperature", "°C", "temperature", "measurement", "{{ value_json.heater.probe_c }}"},
};
constexpr std::size_t kHaSensorCount = sizeof(kHaSensors) / sizeof(kHaSensors[0]);

void add_series(JsonObject series, const history::TieredHistory& h, history::Metric m, history::Tier tier) {
    const auto& info = history::metric_info(m);
    JsonObject o = series[info.key].to<JsonObject>();
    o["unit"] = info.unit;
    JsonArray v = o["v"].to<JsonArray>();
    const bool range = info.agg == history::Agg::Mean;
    JsonArray mn;
    JsonArray mx;
    if (range) {
        mn = o["min"].to<JsonArray>();
        mx = o["max"].to<JsonArray>();
    }
    const std::size_t n = history::TieredHistory::size(tier);
    for (std::size_t i = 0; i <= n; ++i) {
        const history::Slot& s = (i < n) ? h.slot(m, tier, i) : h.current(m, tier);
        float x = 0.0F;
        if (s.value(info.agg, x)) {
            v.add(round_to(x, 2));
        } else {
            v.add(nullptr);
        }
        if (range) {
            if (s.min(info.agg, x)) mn.add(round_to(x, 2)); else mn.add(nullptr);
            if (s.max(info.agg, x)) mx.add(round_to(x, 2)); else mx.add(nullptr);
        }
    }
}
}  // namespace

bool TelemetryFormatter::select_external_ufl_antenna(std::int8_t ufl_rssi_dbm, std::int8_t internal_rssi_dbm) {
    return (static_cast<int>(internal_rssi_dbm) - static_cast<int>(ufl_rssi_dbm)) <= 8;
}

std::string TelemetryFormatter::build_state_json(const core::DerivedState& d, const char* fw_version, const WifiInfo& wifi) {
    JsonDocument doc;
    doc["fw"] = fw_version;
    doc["uptime_s"] = d.uptime_s;
    doc["clock_valid"] = d.clock_valid;
    if (d.clock_valid) {
        doc["unix_s"] = d.unix_s;
    } else {
        doc["unix_s"] = nullptr;
    }
    doc["utc_offset_s"] = d.utc_offset_s;
    doc["location_set"] = d.astro.location_set;

    JsonObject wf = doc["wifi"].to<JsonObject>();
    wf["connected"] = wifi.connected;
    wf["ap_mode"] = wifi.ap_mode;
    wf["external_ufl"] = wifi.external_ufl;
    wf["tx_power_dbm"] = wifi.tx_power_dbm;
    wf["rssi_dbm"] = wifi.rssi_dbm;
    wf["ip"] = wifi.ip;

    const core::WindDerived& w = d.wind;
    JsonObject wind = doc["wind"].to<JsonObject>();
    put(wind, "speed_m_s", w.speed_m_s, 2);
    put(wind, "dir_deg", w.dir_deg, 0);
    put(wind, "avg_2m_m_s", w.avg_2m_m_s, 2);
    put(wind, "avg_10m_m_s", w.avg_10m_m_s, 2);
    put(wind, "dir_10m_deg", w.dir_10m_deg, 0);
    put(wind, "gust_m_s", w.gust_m_s, 2);
    put(wind, "lull_m_s", w.lull_m_s, 2);
    put(wind, "beaufort", w.beaufort, 0);
    put(wind, "turbulence_pct", w.turbulence_pct, 0);
    put(wind, "trend_m_s_per_h", w.trend_m_s_per_h, 2);
    put(wind, "dir_change_3h_deg", w.dir_change_3h_deg, 0);
    put(wind, "load_kg_m2", w.load_kg_m2, 2);
    put(wind, "load_min_1h_kg_m2", w.load_min_1h_kg_m2, 2);
    put(wind, "load_max_1h_kg_m2", w.load_max_1h_kg_m2, 2);
    put(wind, "sound_m_s", w.sound_m_s, 1);
    put(wind, "acoustic_temp_c", w.acoustic_temp_c, 1);
    put(wind, "tilt_deg", w.tilt_deg, 2);
    JsonArray rose = wind["rose"].to<JsonArray>();
    for (float p : w.rose_pct) {
        rose.add(round_to(p, 1));
    }

    const core::ThermoDerived& t = d.thermo;
    const core::BaroDerived& b = d.baro;
    JsonObject env = doc["environment"].to<JsonObject>();
    put(env, "temp_c", t.temp_c, 2);
    put(env, "solar_offset_c", t.solar_offset_c, 2);
    put(env, "humidity_rh", t.rh_pct, 1);
    put(env, "dew_point_c", t.dew_point_c, 2);
    put(env, "wet_bulb_c", t.wet_bulb_c, 2);
    put(env, "feels_like_c", t.feels_like_c, 2);
    put(env, "wind_chill_c", t.wind_chill_c, 2);
    put(env, "heat_index_c", t.heat_index_c, 2);
    put(env, "abs_humidity_g_m3", t.abs_humidity_g_m3, 2);
    put(env, "vpd_kpa", t.vpd_kpa, 2);
    put(env, "cloud_base_m", t.cloud_base_m, 0);
    put(env, "temp_min_24h_c", t.temp_min_24h_c, 2);
    put(env, "temp_max_24h_c", t.temp_max_24h_c, 2);
    put(env, "temp_trend_c_per_h", t.trend_c_per_h, 2);
    env["frost_risk"] = t.frost_risk;
    env["comfort"] = t.comfort;
    put(env, "pressure_hpa", b.station_hpa, 2);
    put(env, "pressure_wind_corr_hpa", b.wind_corr_hpa, 3);
    put(env, "qnh_hpa", b.qnh_hpa, 2);
    put(env, "pressure_3h_delta_hpa", b.change_3h_hpa, 2);
    put(env, "solar_tide_hpa", b.tide_3h_hpa, 2);
    put(env, "synoptic_3h_delta_hpa", b.synoptic_3h_hpa, 2);
    put(env, "qnh_min_24h_hpa", b.qnh_min_24h_hpa, 2);
    put(env, "qnh_max_24h_hpa", b.qnh_max_24h_hpa, 2);
    put(env, "air_density_kg_m3", b.air_density_kg_m3, 4);
    put(env, "pressure_ripple_pa", b.ripple_pa, 2);
    env["pressure_trend"] = b.trend;
    env["forecast"] = b.forecast;
    env["front_type"] = b.front;
    env["wind_shift"] = b.wind_shift;
    put(env, "rain_prob_pct", b.rain_prob_pct, 0);

    const core::SolarDerived& s = d.solar;
    JsonObject sol = doc["solar"].to<JsonObject>();
    put(sol, "lux", s.lux, 4);
    put(sol, "irradiance_w_m2", s.irradiance_w_m2, 1);
    put(sol, "visible_w_m2", s.visible_w_m2, 1);
    put(sol, "nir_w_m2", s.nir_w_m2, 1);
    put(sol, "uv_index", s.uv_index, 1);
    put(sol, "uv_index_guvb", s.uv_index_guvb, 1);
    put(sol, "uv_index_ltr", s.uv_index_ltr, 1);
    sol["uv_source"] = s.uv_source;
    put(sol, "uva_w_m2", s.uva_w_m2, 1);
    put(sol, "uvb_w_m2", s.uvb_w_m2, 2);
    put(sol, "burn_time_min", s.burn_time_min, 0);
    put(sol, "clear_sky_w_m2", s.clear_sky_w_m2, 0);
    put(sol, "cloud_cover_pct", s.cloud_cover_pct, 0);
    put(sol, "sqm", s.sqm_mag_arcsec2, 2);
    put(sol, "bortle", s.bortle, 0);

    const core::AstroDerived& a = d.astro;
    JsonObject ast = doc["astronomy"].to<JsonObject>();
    ast["is_night"] = a.is_night;
    put(ast, "sun_alt_deg", a.sun_alt_deg, 1);
    put(ast, "sun_az_deg", a.sun_az_deg, 0);
    put(ast, "sunrise_min", a.sunrise_min, 0);
    put(ast, "sunset_min", a.sunset_min, 0);
    put(ast, "daylight_min", a.daylight_min, 0);
    put(ast, "moon_alt_deg", a.moon_alt_deg, 1);
    put(ast, "moon_illum_pct", a.moon_illum_pct, 0);
    put(ast, "moon_age_days", a.moon_age_days, 1);
    put(ast, "moonrise_min", a.moonrise_min, 0);
    put(ast, "moonset_min", a.moonset_min, 0);
    ast["moon_phase"] = a.moon_phase;

    const core::RainDerived& r = d.rain;
    JsonObject rain = doc["rain"].to<JsonObject>();
    rain["sensor_ok"] = r.sensor_ok;
    rain["state"] = r.state;
    rain["raining"] = r.raining;
    rain["wet"] = r.surface_wet;
    putf(rain, "rate_mm_h", r.rate_mm_h, 2);
    putf(rain, "hail_rate_mm_h", r.hail_rate_mm_h, 2);
    putf(rain, "event_mm", r.event_mm, 2);
    putf(rain, "last_hour_mm", r.last_hour_mm, 2);
    putf(rain, "today_mm", r.today_mm, 2);
    putf(rain, "total_mm", r.total_mm, 1);
    rain["drops"] = r.drops;
    rain["hail_hits"] = r.hail_hits;
    putf(rain, "hail_diam_mm", r.max_hail_mm, 1);
    rain["rejected_shake"] = r.rejected_shake;

    const core::AirDerived& q = d.air;
    JsonObject aq = doc["air_quality"].to<JsonObject>();
    put(aq, "pm1_0", q.pm1_ug_m3, 1);
    put(aq, "pm2_5", q.pm25_ug_m3, 1);
    put(aq, "pm2_5_raw", q.pm25_raw_ug_m3, 1);
    put(aq, "pm10", q.pm10_ug_m3, 1);
    put(aq, "coarse", q.coarse_ug_m3, 1);
    put(aq, "voc_index", q.voc_index, 0);
    put(aq, "nox_index", q.nox_index, 0);
    aq["gas_learning"] = q.gas_learning;
    put(aq, "co2_ppm", q.co2_ppm, 0);
    put(aq, "aqi", q.aqi, 0);
    put(aq, "eaqi", q.eaqi, 0);
    aq["level"] = q.level;
    aq["dominant"] = q.dominant;

    const core::LightningDerived& l = d.lightning;
    JsonObject lt = doc["lightning"].to<JsonObject>();
    lt["strikes_total"] = l.strikes_total;
    lt["strikes_1h"] = l.strikes_1h;
    lt["strikes_today"] = l.strikes_today;
    put(lt, "last_strike_age_s", l.last_strike_age_s, 0);
    put(lt, "distance_km", l.distance_km, 2);
    put(lt, "closest_1h_km", l.closest_1h_km, 1);
    put(lt, "thunder_delay_s", l.thunder_delay_s, 1);
    lt["method"] = l.method;

    const core::SeismicDerived& z = d.seismic;
    JsonObject seis = doc["seismic"].to<JsonObject>();
    put(seis, "ppv_mm_s", z.ppv_mm_s, 3);
    put(seis, "pga_cm_s2", z.pga_cm_s2, 2);
    put(seis, "dominant_hz", z.dominant_hz, 0);
    seis["source"] = z.source;
    seis["mmi"] = z.mmi;

    const core::HeaterDerived& h = d.heater;
    JsonObject ht = doc["heater"].to<JsonObject>();
    ht["state"] = h.state;
    put(ht, "vin_v", h.vin_v, 2);
    put(ht, "rail_heat_v", h.rail_heat_v, 2);
    put(ht, "rail_logic_v", h.rail_logic_v, 2);
    put(ht, "reflector_c", h.reflector_c, 1);
    putf(ht, "duty_pct", h.duty_pct, 1);
    putf(ht, "power_w", h.power_w, 2);
    put(ht, "probe_c", h.probe_c, 1);
    put(ht, "probe_rise_k", h.probe_rise_k, 1);
    put(ht, "reflector_w", h.reflector_w, 2);
    put(ht, "probe_w", h.probe_w, 2);
    ht["probe_on"] = h.probe_on;
    ht["reflector_status"] = h.reflector_status;
    ht["probe_status"] = h.probe_status;

    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string TelemetryFormatter::build_minute_history_json(const history::TieredHistory& h) {
    using history::Metric;
    using history::Tier;
    JsonDocument doc;
    JsonArray rain = doc["rain_mm_h"].to<JsonArray>();
    JsonArray hail = doc["hail_mm_h"].to<JsonArray>();
    JsonArray strikes = doc["strikes"].to<JsonArray>();
    JsonArray ppv = doc["ppv_mm_s"].to<JsonArray>();
    const std::size_t n = history::TieredHistory::kMinutes;
    auto value_at = [&h](Metric m, std::size_t i, std::size_t total, float& out) {
        const history::Slot& s = (i < total) ? h.slot(m, Tier::Minute, i) : h.current(m, Tier::Minute);
        return s.value(history::metric_info(m).agg, out);
    };
    // n - 1 closed minutes + the running minute = 60 bars ending "now".
    for (std::size_t i = 1; i <= n; ++i) {
        float v = 0.0F;
        if (value_at(Metric::RainMm, i, n, v)) rain.add(round_to(v * 60.0F, 2)); else rain.add(nullptr);
        if (value_at(Metric::HailMm, i, n, v)) hail.add(round_to(v * 60.0F, 2)); else hail.add(nullptr);
        if (value_at(Metric::Strikes, i, n, v)) strikes.add(static_cast<int>(std::lround(v))); else strikes.add(nullptr);
        if (value_at(Metric::PpvMmS, i, n, v)) ppv.add(round_to(v, 3)); else ppv.add(nullptr);
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string TelemetryFormatter::build_history_json(const history::TieredHistory& h, history::Tier tier, const char* metrics,
                                                   std::int64_t now_unix) {
    JsonDocument doc;
    doc["tier"] = tier == history::Tier::Minute ? "minute" : (tier == history::Tier::Hour ? "hour" : "day");
    doc["step_s"] = tier == history::Tier::Minute ? 60 : (tier == history::Tier::Hour ? 3600 : 86400);
    doc["end_unix"] = now_unix;
    JsonObject series = doc["series"].to<JsonObject>();
    if (metrics == nullptr || metrics[0] == '\0') {
        for (std::size_t i = 0; i < history::kMetricCount; ++i) {
            add_series(series, h, static_cast<history::Metric>(i), tier);
        }
    } else {
        char key[32];
        const char* p = metrics;
        while (*p != '\0') {
            std::size_t len = 0;
            while (p[len] != '\0' && p[len] != ',' && len < sizeof(key) - 1) {
                key[len] = p[len];
                ++len;
            }
            key[len] = '\0';
            history::Metric m{};
            if (history::metric_from_key(key, m)) {
                add_series(series, h, m, tier);
            }
            p += len;
            while (*p == ',') {
                ++p;
            }
            if (len == sizeof(key) - 1) {
                break;
            }
        }
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::size_t TelemetryFormatter::ha_sensor_count() {
    return kHaSensorCount;
}

std::string TelemetryFormatter::ha_state_topic(const char* node_id) {
    return std::string("aeromet/") + node_id + "/state";
}

bool TelemetryFormatter::build_ha_discovery(std::size_t i, const char* node_id, const char* ha_prefix, std::string& topic,
                                            std::string& payload) {
    if (i >= kHaSensorCount) {
        return false;
    }
    const HaSensor& s = kHaSensors[i];
    topic = std::string(ha_prefix) + "/sensor/aeromet_" + node_id + "/" + s.id + "/config";
    JsonDocument doc;
    doc["name"] = s.name;
    doc["uniq_id"] = std::string("aeromet_") + node_id + "_" + s.id;
    doc["stat_t"] = ha_state_topic(node_id);
    doc["val_tpl"] = s.tpl;
    if (s.unit[0] != '\0') doc["unit_of_meas"] = s.unit;
    if (s.device_class[0] != '\0') doc["dev_cla"] = s.device_class;
    if (s.state_class[0] != '\0') doc["stat_cla"] = s.state_class;
    JsonObject dev = doc["dev"].to<JsonObject>();
    JsonArray ids = dev["ids"].to<JsonArray>();
    ids.add(std::string("aeromet_") + node_id);
    dev["name"] = "AeroMet Roof Weather Station";
    dev["mdl"] = "AeroMet V1 Ultrasonic";
    dev["mf"] = "OpenHardware";
    payload.clear();
    serializeJson(doc, payload);
    return true;
}

}  // namespace telemetry
}  // namespace station
