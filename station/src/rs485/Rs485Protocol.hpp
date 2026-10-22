#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "../core/DerivedState.hpp"

namespace station {
namespace rs485 {

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
    // Formats a deterministic NMEA/MODBUS-CRC16 telemetry frame (empty field = no valid value):
    //   $AM[<addr>],<uptime_s>,<wind_2m_ms>,<wind_dir_10m>,<rain_mm_h>,<rain_day_mm>,<temp_c>,<rh>,<qnh_hpa>,<uv>,<aqi>*<CRC16>\r\n
    // `address` 0 = single station ("$AM,..."), 1..247 = "$AM<addr>,..." so a master can tell stations apart.
    static std::string encode_telemetry_frame(const core::DerivedState& d, std::uint8_t address = 0);
    static std::string encode_pong(std::uint8_t address = 0);   // "$AM[<addr>],PONG*<CRC16>\r\n"
    // Multi-drop addressing: "<addr>:CMD" is answered only by the station with that address; a bare "CMD" only by a
    // station left at address 0 (single station on the bus). Address 0 also accepts "0:CMD".
    static CommandType parse_command(const char* line, std::size_t len, std::uint8_t my_address);
    static CommandType parse_command_frame(const char* line, std::size_t len) { return parse_command(line, len, 0); }
};

}  // namespace rs485
}  // namespace station
