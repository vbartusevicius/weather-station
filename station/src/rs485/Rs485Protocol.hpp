#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "telemetry/HomeAssistantMqtt.hpp"

namespace station::rs485 {

enum class CommandType : std::uint8_t {
    None = 0,
    Ping = 1,
    RequestState = 2,
    ResetDailyRain = 3,
    ForceHeaterCheck = 4,
};

class Rs485Protocol {
public:
    static constexpr std::uint32_t kDefaultBaudRate = 115200;

    static std::uint16_t crc16_modbus(const char* data, std::size_t len);
    // Formats a deterministic NMEA/MODBUS-CRC16 telemetry frame:
    //   $AM,<uptime_s>,<wind_ms>,<wind_dir>,<rain_mm_h>,<rain_day_mm>,<temp_c>,<rh>,<qnh_hpa>,<uv>,<aqi>*<CRC16>\r\n
    static std::string encode_telemetry_frame(const telemetry::StationSnapshot& snap);
    static CommandType parse_command_frame(const char* line, std::size_t len);
};

}  // namespace station::rs485
