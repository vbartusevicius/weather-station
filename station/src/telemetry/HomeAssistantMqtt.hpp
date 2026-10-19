#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "../airquality/AirQuality.hpp"
#include "../environment/MetCalculations.hpp"
#include "../heater/DeIcingHeater.hpp"
#include "../rain/HapticRain.hpp"
#include "../wind/UltrasonicWind.hpp"

namespace station {
namespace telemetry {

struct LightningMetrics {
    std::uint32_t strike_count{0};
    std::uint8_t closest_distance_km{0};
    std::uint32_t last_energy{0};
    std::array<std::uint16_t, 12> timeline_60m{};  // 12 x 5-minute strike buckets (-60m .. Now)
};

struct WifiRfMetrics {
    bool external_ufl_active{true};  // GPIO3=0, GPIO14=1 -> external U.FL horizontal FPC dipole
    float tx_power_dbm{20.5F};       // +20.5 dBm (82 quarter-dBm)
    std::int8_t rssi_dbm{-58};
};

struct StationSnapshot {
    std::uint32_t uptime_s{0};
    wind::WindSample wind_raw{};
    wind::WmoWindSummary wind{};
    rain::RainMetrics rain{};
    environment::AtmosphereReading atmo_raw{};
    environment::DerivedEnvironment atmo{};
    airquality::AirQualityInput aq_raw{};
    airquality::AirQualityReport aq{};
    heater::HeaterControlOutput heater{};
    LightningMetrics lightning{};
    WifiRfMetrics wifi_rf{};
};

class TelemetryFormatter {
public:
    // Selects external U.FL antenna by default; falls back to internal chip antenna only if external
    // U.FL is unplugged on the bench (internal RSSI exceeds external RSSI by > 8 dB)
    static bool select_external_ufl_antenna(std::int8_t ufl_rssi_dbm, std::int8_t internal_rssi_dbm);
    static std::string build_state_json(const StationSnapshot& snap, const char* fw_version = "26.10.16");
    static std::string build_ha_discovery_json(const char* node_id, const char* metric_id,
                                               const char* name, const char* unit,
                                               const char* device_class, const char* value_template);
};

}  // namespace telemetry
}  // namespace station
