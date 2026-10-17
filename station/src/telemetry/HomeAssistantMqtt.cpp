#include "telemetry/HomeAssistantMqtt.hpp"

#include <cstdio>

namespace station::telemetry {

std::string TelemetryFormatter::build_state_json(const StationSnapshot& s, const char* fw_version) {
    char buf[1536];
    std::snprintf(
        buf, sizeof(buf),
        "{\"fw\":\"%s\",\"uptime_s\":%lu,"
        "\"wind\":{\"speed_m_s\":%.2f,\"dir_deg\":%.1f,\"avg_1m_m_s\":%.2f,\"avg_10m_m_s\":%.2f,"
        "\"gust_m_s\":%.2f,\"lull_m_s\":%.2f,\"mean_dir_deg\":%.1f,\"beaufort\":%.0f,"
        "\"sound_m_s\":%.1f,\"acoustic_temp_c\":%.1f,\"tilt_deg\":%.2f,\"north_offset_deg\":%.1f},"
        "\"rain\":{\"state\":\"%s\",\"raining\":%s,\"wet\":%s,\"rate_mm_h\":%.2f,"
        "\"event_mm\":%.2f,\"hourly_mm\":%.2f,\"daily_mm\":%.2f,\"total_mm\":%.2f,"
        "\"drops\":%lu,\"hail_hits\":%lu,\"rejected_shake\":%lu},"
        "\"environment\":{\"temp_c\":%.2f,\"humidity_rh\":%.1f,\"dew_point_c\":%.2f,"
        "\"wet_bulb_c\":%.2f,\"feels_like_c\":%.2f,\"pressure_hpa\":%.2f,\"qnh_hpa\":%.2f,"
        "\"lux\":%.0f,\"solar_w_m2\":%.1f,\"uv_index\":%.1f,\"abs_humidity_g_m3\":%.2f},"
        "\"air_quality\":{\"aqi\":%u,\"eaqi\":%u,\"level\":\"%s\",\"dominant\":\"%s\","
        "\"pm1_0\":%.1f,\"pm2_5\":%.1f,\"pm4_0\":%.1f,\"pm10\":%.1f,"
        "\"voc_index\":%u,\"nox_index\":%u,\"co2_ppm\":%u},"
        "\"lightning\":{\"strikes\":%lu,\"distance_km\":%u,\"energy\":%lu},"
        "\"heater\":{\"state\":%u,\"vin_v\":%.2f,\"reflector_c\":%.1f,\"duty_pct\":%.1f,\"power_w\":%.2f}}",
        fw_version, static_cast<unsigned long>(s.uptime_s),
        static_cast<double>(s.wind.current_speed_m_s), static_cast<double>(s.wind.current_dir_deg),
        static_cast<double>(s.wind.avg_1min_m_s), static_cast<double>(s.wind.avg_10min_m_s),
        static_cast<double>(s.wind.gust_3s_m_s), static_cast<double>(s.wind.lull_m_s),
        static_cast<double>(s.wind.mean_dir_deg), static_cast<double>(s.wind.beaufort_scale),
        static_cast<double>(s.wind_raw.sound_speed_m_s), static_cast<double>(s.wind_raw.acoustic_temp_c),
        static_cast<double>(s.wind_raw.mast_tilt_deg), static_cast<double>(s.wind_raw.north_offset_deg),
        rain::precipitation_type_name(s.rain.state), s.rain.is_raining ? "true" : "false",
        s.rain.surface_wet ? "true" : "false", static_cast<double>(s.rain.rate_mm_h),
        static_cast<double>(s.rain.event_mm), static_cast<double>(s.rain.hourly_mm),
        static_cast<double>(s.rain.daily_mm), static_cast<double>(s.rain.total_mm),
        static_cast<unsigned long>(s.rain.drop_count), static_cast<unsigned long>(s.rain.hail_hits),
        static_cast<unsigned long>(s.rain.rejected_vibrations),
        static_cast<double>(s.atmo_raw.temperature_c), static_cast<double>(s.atmo_raw.humidity_rh),
        static_cast<double>(s.atmo.dew_point_c), static_cast<double>(s.atmo.wet_bulb_c),
        static_cast<double>(s.atmo.feels_like_c), static_cast<double>(s.atmo_raw.station_pressure_hpa),
        static_cast<double>(s.atmo.sea_level_pressure_hpa), static_cast<double>(s.atmo_raw.ambient_lux),
        static_cast<double>(s.atmo.solar_irradiance_w_m2), static_cast<double>(s.atmo.uv_index),
        static_cast<double>(s.atmo.absolute_humidity_g_m3),
        static_cast<unsigned>(s.aq.us_epa_aqi), static_cast<unsigned>(s.aq.eu_eaqi),
        airquality::AirQualityEvaluator::level_name(s.aq.level), s.aq.dominant_pollutant,
        static_cast<double>(s.aq_raw.pm1_0_ug_m3), static_cast<double>(s.aq_raw.pm2_5_ug_m3),
        static_cast<double>(s.aq_raw.pm4_0_ug_m3), static_cast<double>(s.aq_raw.pm10_ug_m3),
        static_cast<unsigned>(s.aq_raw.voc_index), static_cast<unsigned>(s.aq_raw.nox_index),
        static_cast<unsigned>(s.aq_raw.co2_ppm),
        static_cast<unsigned long>(s.lightning.strike_count),
        static_cast<unsigned>(s.lightning.closest_distance_km),
        static_cast<unsigned long>(s.lightning.last_energy),
        static_cast<unsigned>(s.heater.state), static_cast<double>(s.heater.supply_voltage_v),
        static_cast<double>(s.heater.reflector_temp_c), static_cast<double>(s.heater.pwm_duty_percent),
        static_cast<double>(s.heater.actual_power_w));
    return std::string(buf);
}

std::string TelemetryFormatter::build_ha_discovery_json(const char* node_id, const char* metric_id,
                                                        const char* name, const char* unit,
                                                        const char* device_class, const char* value_template) {
    char buf[640];
    const int n = std::snprintf(
        buf, sizeof(buf),
        "{\"name\":\"%s\",\"uniq_id\":\"aeromet_%s_%s\","
        "\"stat_t\":\"aeromet/%s/state\",\"val_tpl\":\"%s\"%s%s%s%s,"
        "\"dev\":{\"ids\":[\"aeromet_%s\"],\"name\":\"AeroMet Roof Weather Station\","
        "\"mdl\":\"AeroMet V1 Ultrasonic\",\"mf\":\"OpenHardware\"}}",
        name, node_id, metric_id, node_id, value_template,
        (unit && unit[0]) ? ",\"unit_of_meas\":\"" : "", (unit && unit[0]) ? unit : "",
        (unit && unit[0]) ? "\"" : "",
        (device_class && device_class[0]) ? (std::string(",\"dev_cla\":\"") + device_class + "\"").c_str() : "",
        node_id);
    return (n > 0) ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
}

}  // namespace station::telemetry
