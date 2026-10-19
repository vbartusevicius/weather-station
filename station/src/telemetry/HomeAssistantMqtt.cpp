#include "HomeAssistantMqtt.hpp"

#include <ArduinoJson.h>

namespace station {
namespace telemetry {

bool TelemetryFormatter::select_external_ufl_antenna(std::int8_t ufl_rssi_dbm, std::int8_t internal_rssi_dbm) {
    return (static_cast<int>(internal_rssi_dbm) - static_cast<int>(ufl_rssi_dbm)) <= 8;
}

std::string TelemetryFormatter::build_state_json(const StationSnapshot& s, const char* fw_version) {
    JsonDocument doc;
    doc["fw"] = fw_version;
    doc["uptime_s"] = s.uptime_s;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["external_ufl"] = s.wifi_rf.external_ufl_active;
    wifi["tx_power_dbm"] = s.wifi_rf.tx_power_dbm;
    wifi["rssi_dbm"] = s.wifi_rf.rssi_dbm;

    JsonObject wind = doc["wind"].to<JsonObject>();
    wind["speed_m_s"] = s.wind.current_speed_m_s;
    wind["dir_deg"] = s.wind.current_dir_deg;
    wind["avg_1m_m_s"] = s.wind.avg_1min_m_s;
    wind["avg_10m_m_s"] = s.wind.avg_10min_m_s;
    wind["gust_m_s"] = s.wind.gust_3s_m_s;
    wind["lull_m_s"] = s.wind.lull_m_s;
    wind["mean_dir_deg"] = s.wind.mean_dir_deg;
    wind["beaufort"] = s.wind.beaufort_scale;
    wind["turbulence_pct"] = s.wind.turbulence_intensity_pct;
    wind["gust_factor"] = s.wind.gust_factor;
    wind["crosswind_corr_pct"] = s.wind_raw.crosswind_correction_pct;
    wind["sound_m_s"] = s.wind_raw.sound_speed_m_s;
    wind["acoustic_temp_c"] = s.wind_raw.acoustic_temp_c;
    wind["tilt_deg"] = s.wind_raw.mast_tilt_deg;
    wind["north_offset_deg"] = s.wind_raw.north_offset_deg;
    JsonArray rose = wind["rose"].to<JsonArray>();
    for (float pct : s.wind.rose_freq_pct) {
        rose.add(pct);
    }

    JsonObject rain = doc["rain"].to<JsonObject>();
    rain["state"] = rain::precipitation_type_name(s.rain.state);
    rain["raining"] = s.rain.is_raining;
    rain["wet"] = s.rain.surface_wet;
    rain["rate_mm_h"] = s.rain.rate_mm_h;
    rain["hail_lwe_rate"] = s.rain.hail_rate_mm_h;
    rain["event_mm"] = s.rain.event_mm;
    rain["hourly_mm"] = s.rain.hourly_mm;
    rain["daily_mm"] = s.rain.daily_mm;
    rain["total_mm"] = s.rain.total_mm;
    rain["drops"] = s.rain.drop_count;
    rain["hail_hits"] = s.rain.hail_hits;
    rain["hail_diam_mm"] = s.rain.max_hail_diameter_mm;
    rain["rejected_shake"] = s.rain.rejected_vibrations;

    JsonObject env = doc["environment"].to<JsonObject>();
    env["temp_c"] = s.atmo.compensated_temp_c;
    env["raw_temp_c"] = s.atmo_raw.temperature_c;
    env["solar_offset_c"] = s.atmo.solar_heating_offset_c;
    env["humidity_rh"] = s.atmo_raw.humidity_rh;
    env["dew_point_c"] = s.atmo.dew_point_c;
    env["wet_bulb_c"] = s.atmo.wet_bulb_c;
    env["wbgt_c"] = s.atmo.wbgt_c;
    env["feels_like_c"] = s.atmo.feels_like_c;
    env["wind_chill_c"] = s.atmo.wind_chill_c;
    env["heat_index_c"] = s.atmo.heat_index_c;
    env["vpd_kpa"] = s.atmo.vpd_kpa;
    env["cloud_base_m"] = s.atmo.cloud_base_m;
    env["frost_risk"] = s.atmo.frost_risk;
    env["pressure_hpa"] = s.atmo.static_pressure_hpa;
    env["dyn_offset_hpa"] = s.atmo.dynamic_pressure_offset_hpa;
    env["qnh_hpa"] = s.atmo.sea_level_pressure_hpa;
    env["pressure_3h_delta_hpa"] = s.atmo.pressure_3h_delta_hpa;
    env["pressure_trend"] = environment::MetCalculator::pressure_trend_name(s.atmo.pressure_trend);
    env["forecast"] = s.atmo.zambretti_forecast;
    env["lux"] = s.atmo_raw.ambient_lux;
    env["solar_w_m2"] = s.atmo.solar_irradiance_w_m2;
    env["uv_index"] = s.atmo.uv_index;
    env["cloud_cover_pct"] = s.atmo.cloud_cover_pct;
    env["abs_humidity_g_m3"] = s.atmo.absolute_humidity_g_m3;

    JsonObject aq = doc["air_quality"].to<JsonObject>();
    aq["aqi"] = s.aq.us_epa_aqi;
    aq["eaqi"] = s.aq.eu_eaqi;
    aq["level"] = airquality::AirQualityEvaluator::level_name(s.aq.level);
    aq["dominant"] = s.aq.dominant_pollutant;
    aq["pm1_0"] = s.aq_raw.pm1_0_ug_m3;
    aq["pm2_5"] = s.aq_raw.pm2_5_ug_m3;
    aq["dry_pm2_5"] = s.aq.dry_pm2_5_ug_m3;
    aq["pm4_0"] = s.aq_raw.pm4_0_ug_m3;
    aq["pm10"] = s.aq_raw.pm10_ug_m3;
    aq["dry_pm10"] = s.aq.dry_pm10_ug_m3;
    aq["voc_index"] = s.aq_raw.voc_index;
    aq["nox_index"] = s.aq_raw.nox_index;
    aq["co2_ppm"] = s.aq_raw.co2_ppm;
    aq["comp_co2_ppm"] = s.aq.pressure_comp_co2_ppm;

    JsonObject lt = doc["lightning"].to<JsonObject>();
    lt["strikes"] = s.lightning.strike_count;
    lt["distance_km"] = s.lightning.closest_distance_km;
    lt["energy"] = s.lightning.last_energy;
    JsonArray tl = lt["timeline_60m"].to<JsonArray>();
    for (std::uint16_t cnt : s.lightning.timeline_60m) {
        tl.add(cnt);
    }

    JsonObject ht = doc["heater"].to<JsonObject>();
    ht["state"] = static_cast<unsigned>(s.heater.state);
    ht["vin_v"] = s.heater.supply_voltage_v;
    ht["reflector_c"] = s.heater.reflector_temp_c;
    ht["duty_pct"] = s.heater.pwm_duty_percent;
    ht["power_w"] = s.heater.actual_power_w;

    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string TelemetryFormatter::build_ha_discovery_json(const char* node_id, const char* metric_id,
                                                        const char* name, const char* unit,
                                                        const char* device_class, const char* value_template) {
    JsonDocument doc;
    doc["name"] = name;
    doc["uniq_id"] = std::string("aeromet_") + node_id + "_" + metric_id;
    doc["stat_t"] = std::string("aeromet/") + node_id + "/state";
    doc["val_tpl"] = value_template;
    if (unit != nullptr && unit[0] != '\0') {
        doc["unit_of_meas"] = unit;
    }
    if (device_class != nullptr && device_class[0] != '\0') {
        doc["dev_cla"] = device_class;
    }
    JsonObject dev = doc["dev"].to<JsonObject>();
    JsonArray ids = dev["ids"].to<JsonArray>();
    ids.add(std::string("aeromet_") + node_id);
    dev["name"] = "AeroMet Roof Weather Station";
    dev["mdl"] = "AeroMet V1 Ultrasonic";
    dev["mf"] = "OpenHardware";

    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace telemetry
}  // namespace station
