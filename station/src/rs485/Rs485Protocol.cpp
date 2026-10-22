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
