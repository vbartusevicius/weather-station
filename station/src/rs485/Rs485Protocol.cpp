#include "rs485/Rs485Protocol.hpp"

#include <cstdio>
#include <cstring>

namespace station::rs485 {

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

std::string Rs485Protocol::encode_telemetry_frame(const telemetry::StationSnapshot& s) {
    char payload[256];
    const int n = std::snprintf(
        payload, sizeof(payload),
        "$AM,%lu,%.2f,%.1f,%.2f,%.2f,%.2f,%.1f,%.2f,%.1f,%u",
        static_cast<unsigned long>(s.uptime_s),
        static_cast<double>(s.wind.current_speed_m_s),
        static_cast<double>(s.wind.current_dir_deg),
        static_cast<double>(s.rain.rate_mm_h),
        static_cast<double>(s.rain.daily_mm),
        static_cast<double>(s.atmo.compensated_temp_c),
        static_cast<double>(s.atmo_raw.humidity_rh),
        static_cast<double>(s.atmo.sea_level_pressure_hpa),
        static_cast<double>(s.atmo.uv_index),
        static_cast<unsigned>(s.aq.us_epa_aqi));
    if (n <= 0) {
        return {};
    }
    const std::uint16_t crc = crc16_modbus(payload, static_cast<std::size_t>(n));
    char frame[280];
    std::snprintf(frame, sizeof(frame), "%s*%04X\r\n", payload, static_cast<unsigned>(crc));
    return std::string(frame);
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

}  // namespace station::rs485
