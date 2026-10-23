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

// One field of the $AM telemetry frame (after the talker), in frame order: used to document and decode it.
struct FrameField {
    const char* key;
    const char* label;
    const char* unit;
};

// Bus activity seen by the station (Web UI Setup -> RS-485).
struct BusStats {
    std::uint32_t lines{0};          // complete lines received
    std::uint32_t ignored{0};        // other address / unknown command
    std::uint32_t pings{0}, states{0}, resets{0}, heat_checks{0};
    std::uint32_t replies{0};
    std::uint32_t last_ms{0};        // monotonic time of the last recognised command (0 = none yet)
    char last_cmd[24]{""};
    void note(CommandType c, const char* line, std::size_t len, std::uint32_t now_ms);
};

class Rs485Protocol {
public:
    static constexpr std::size_t kFrameFieldCount = 33;
    static const FrameField* frame_fields();   // kFrameFieldCount entries
    static constexpr std::uint32_t kDefaultBaudRate = 115200;

    static std::uint16_t crc16_modbus(const char* data, std::size_t len);
    // Formats a deterministic NMEA/MODBUS-CRC16 telemetry frame (empty field = no valid value):
    //   $AM[<addr>],<uptime_s>,<wind_2m_ms>,<wind_dir_10m>,<rain_mm_h>,<rain_day_mm>,<temp_c>,<rh>,<qnh_hpa>,<uv>,<aqi>,
    //   <the 23 further fields of frame_fields(), in order>*<CRC16>\r\n   (every published sensor, ~200 characters)
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
