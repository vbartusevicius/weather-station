#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "../core/DerivedState.hpp"
#include "../history/TieredHistory.hpp"

namespace station {
namespace telemetry {

struct WifiInfo {
    bool connected{false};
    bool external_ufl{true};      // GPIO3=0, GPIO14=1 -> external U.FL horizontal FPC dipole
    float tx_power_dbm{20.5F};
    std::int8_t rssi_dbm{0};
    char ip[16]{""};
    bool ap_mode{false};          // provisioning access point active
};

// Publishers: read DerivedState / history only, never sensors.
class TelemetryFormatter {
public:
    // Falls back to the internal chip antenna only when the external U.FL is clearly worse (> 8 dB).
    static bool select_external_ufl_antenna(std::int8_t ufl_rssi_dbm, std::int8_t internal_rssi_dbm);

    // Current values for the Web UI WebSocket, /api/state and the Home Assistant MQTT state topic.
    static std::string build_state_json(const core::DerivedState& d, const char* fw_version, const WifiInfo& wifi);

    // 60 x 1-minute chart series for the Web UI cards (oldest .. newest, current minute last).
    static std::string build_minute_history_json(const history::TieredHistory& h);

    // Generic tier export: {"tier":"hour","step_s":3600,"end_unix":..,"series":{"temp_c":{"v":[..],"min":[..],"max":[..]}}}
    // `metrics` is a comma-separated list of metric keys (empty = all).
    static std::string build_history_json(const history::TieredHistory& h, history::Tier tier, const char* metrics,
                                          std::int64_t now_unix);

    // Home Assistant MQTT discovery for sensor index `i` (0 .. ha_sensor_count()-1).
    static std::size_t ha_sensor_count();
    // `state_topic` (nullptr / "" = ha_state_topic(node_id)) is where the station publishes its state JSON.
    static bool build_ha_discovery(std::size_t i, const char* node_id, const char* ha_prefix, std::string& topic,
                                   std::string& payload, const char* state_topic = nullptr);
    // State topic: `<base>/state` for a configured base topic, otherwise "aeromet/<node_id>/state".
    static std::string ha_state_topic(const char* node_id, const char* base_topic = nullptr);
};

}  // namespace telemetry
}  // namespace station
