#include "Rs485Protocol.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace station {
namespace rs485 {

std::uint16_t Rs485Protocol::crc16_modbus(const char* data, std::size_t len) {
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<std::uint8_t>(data[i]);
        for (int b = 0; b < 8; ++b) {
            if ((crc & 0x0001U) != 0U) {
                crc = static_cast<std::uint16_t>((crc >> 1U) ^ 0xA001U);
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

namespace {
void field(std::string& out, const core::Value<float>& v, const char* fmt) {
    out += ',';
    if (v.ok) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), fmt, static_cast<double>(v.v));
        out += buf;
    }
}
}  // namespace

namespace {
std::string talker(std::uint8_t address) {
    char t[8];
    std::snprintf(t, sizeof(t), address ? "$AM%u" : "$AM", static_cast<unsigned>(address));
    return t;
}

std::string seal(const std::string& payload) {
    char tail[12];
    std::snprintf(tail, sizeof(tail), "*%04X\r\n", static_cast<unsigned>(Rs485Protocol::crc16_modbus(payload.c_str(), payload.size())));
    return payload + tail;
}
}  // namespace

std::string Rs485Protocol::encode_pong(std::uint8_t address) {
    return seal(talker(address) + ",PONG");
}

namespace {
constexpr FrameField kFields[Rs485Protocol::kFrameFieldCount] = {
    {"uptime_s", "Uptime", "s"},
    {"wind_2m_m_s", "Wind, 2-min mean", "m/s"},
    {"wind_dir_10m_deg", "Wind direction, 10-min", "\u00b0"},
    {"rain_rate_mm_h", "Rain rate", "mm/h"},
    {"rain_today_mm", "Rain today", "mm"},
    {"temp_c", "Temperature", "\u00b0C"},
    {"rh_pct", "Humidity", "%"},
    {"qnh_hpa", "Sea-level pressure", "hPa"},
    {"uv_index", "UV index", ""},
    {"aqi", "Air quality index (US AQI)", ""},
    // appended after the original ten fields: masters that parse by position keep working
    {"wind_gust_m_s", "Wind gust, 10-min", "m/s"},
    {"wind_m_s", "Wind, 3-s", "m/s"},
    {"wind_dir_deg", "Wind direction, 3-s", "\u00b0"},
    {"dew_point_c", "Dew point", "\u00b0C"},
    {"feels_like_c", "Feels like", "\u00b0C"},
    {"pressure_hpa", "Station pressure", "hPa"},
    {"pressure_3h_hpa", "Pressure change, 3 h", "hPa"},
    {"irradiance_w_m2", "Solar irradiance", "W/m\u00b2"},
    {"lux", "Illuminance", "lx"},
    {"cloud_cover_pct", "Cloud cover", "%"},
    {"rain_total_mm", "Rain total", "mm"},
    {"hail_mm", "Largest hail today", "mm"},
    {"pm1_ug_m3", "PM1.0", "\u00b5g/m\u00b3"},
    {"pm25_ug_m3", "PM2.5", "\u00b5g/m\u00b3"},
    {"pm10_ug_m3", "PM10", "\u00b5g/m\u00b3"},
    {"co2_ppm", "CO\u2082", "ppm"},
    {"voc_index", "VOC index", ""},
    {"nox_index", "NOx index", ""},
    {"strikes_today", "Lightning strikes today", ""},
    {"strike_km", "Last strike distance", "km"},
    {"ppv_mm_s", "Ground vibration (PPV)", "mm/s"},
    {"supply_v", "Supply voltage", "V"},
    {"health", "Station health (0 ok, 1 warn, 2 fail, 3 off)", ""},
};
}  // namespace

const FrameField* Rs485Protocol::frame_fields() {
    return kFields;
}

void BusStats::note(CommandType c, const char* line, std::size_t len, std::uint32_t now_ms) {
    ++lines;
    switch (c) {
        case CommandType::Ping: ++pings; break;
        case CommandType::RequestState: ++states; break;
        case CommandType::ResetDailyRain: ++resets; break;
        case CommandType::ForceHeaterCheck: ++heat_checks; break;
        default: ++ignored; return;
    }
    last_ms = now_ms;
    std::size_t n = std::min(len, sizeof(last_cmd) - 1);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n' || line[n - 1] == ' ')) --n;
    std::memcpy(last_cmd, line, n);
    last_cmd[n] = '\0';
}

std::string Rs485Protocol::encode_telemetry_frame(const core::DerivedState& d, std::uint8_t address) {
    char head[24];
    std::snprintf(head, sizeof(head), "%s,%lu", talker(address).c_str(), static_cast<unsigned long>(d.uptime_s));
    std::string payload = head;
    field(payload, d.wind.avg_2m_m_s, "%.2f");
    field(payload, d.wind.dir_10m_deg, "%.1f");
    core::Value<float> rate;
    rate.set(d.rain.rate_mm_h);
    core::Value<float> day;
    day.set(d.rain.today_mm);
    field(payload, rate, "%.2f");
    field(payload, day, "%.2f");
    field(payload, d.thermo.temp_c, "%.2f");
    field(payload, d.thermo.rh_pct, "%.1f");
    field(payload, d.baro.qnh_hpa, "%.2f");
    field(payload, d.solar.uv_index, "%.1f");
    field(payload, d.air.aqi, "%.0f");
    auto num = [](float v) {
        core::Value<float> x;
        x.set(v);
        return x;
    };
    field(payload, d.wind.gust_m_s, "%.2f");
    field(payload, d.wind.speed_m_s, "%.2f");
    field(payload, d.wind.dir_deg, "%.0f");
    field(payload, d.thermo.dew_point_c, "%.2f");
    field(payload, d.thermo.feels_like_c, "%.1f");
    field(payload, d.baro.station_hpa, "%.2f");
    field(payload, d.baro.change_3h_hpa, "%.2f");
    field(payload, d.solar.irradiance_w_m2, "%.0f");
    field(payload, d.solar.lux, "%.0f");
    field(payload, d.solar.cloud_cover_pct, "%.0f");
    field(payload, num(d.rain.total_mm), "%.2f");
    field(payload, num(d.rain.max_hail_mm), "%.1f");
    field(payload, d.air.pm1_ug_m3, "%.1f");
    field(payload, d.air.pm25_ug_m3, "%.1f");
    field(payload, d.air.pm10_ug_m3, "%.1f");
    field(payload, d.air.co2_ppm, "%.0f");
    field(payload, d.air.voc_index, "%.0f");
    field(payload, d.air.nox_index, "%.0f");
    field(payload, num(static_cast<float>(d.lightning.strikes_today)), "%.0f");
    field(payload, d.lightning.distance_km, "%.1f");
    field(payload, d.seismic.ppv_mm_s, "%.3f");
    field(payload, d.heater.vin_v, "%.2f");
    field(payload, num(static_cast<float>(static_cast<int>(d.quality.summary))), "%.0f");
    return seal(payload);
}

CommandType Rs485Protocol::parse_command(const char* line, std::size_t len, std::uint8_t my_address) {
    if (!line || len < 4) {
        return CommandType::None;
    }
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
        --len;
    }
    // optional "<addr>:" prefix
    std::size_t i = 0;
    unsigned addr = 0;
    while (i < len && i < 3 && line[i] >= '0' && line[i] <= '9') {
        addr = addr * 10U + static_cast<unsigned>(line[i] - '0');
        ++i;
    }
    if (i > 0 && i < len && line[i] == ':') {
        if (addr != my_address) {
            return CommandType::None;
        }
        line += i + 1;
        len -= i + 1;
    } else if (my_address != 0) {
        return CommandType::None;   // addressed station ignores bare commands (another master target)
    }
    if (len == 4 && std::strncmp(line, "PING", 4) == 0) {
        return CommandType::Ping;
    }
    if (len == 6 && std::strncmp(line, "STATE?", 6) == 0) {
        return CommandType::RequestState;
    }
    if (len == 10 && std::strncmp(line, "RESET_RAIN", 10) == 0) {
        return CommandType::ResetDailyRain;
    }
    if (len == 10 && std::strncmp(line, "HEAT_CHECK", 10) == 0) {
        return CommandType::ForceHeaterCheck;
    }
    return CommandType::None;
}

}  // namespace rs485
}  // namespace station
