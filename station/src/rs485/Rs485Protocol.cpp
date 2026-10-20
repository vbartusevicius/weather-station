#include "Rs485Protocol.hpp"

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

std::string Rs485Protocol::encode_telemetry_frame(const core::DerivedState& d) {
    char head[24];
    std::snprintf(head, sizeof(head), "$AM,%lu", static_cast<unsigned long>(d.uptime_s));
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
    const std::uint16_t crc = crc16_modbus(payload.c_str(), payload.size());
    char tail[12];
    std::snprintf(tail, sizeof(tail), "*%04X\r\n", static_cast<unsigned>(crc));
    return payload + tail;
}

CommandType Rs485Protocol::parse_command_frame(const char* line, std::size_t len) {
    if (!line || len < 4) {
        return CommandType::None;
    }
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
        --len;
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
