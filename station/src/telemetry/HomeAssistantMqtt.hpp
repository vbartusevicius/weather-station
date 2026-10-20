#pragma once

#include <algorithm>
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

constexpr std::size_t kMinuteBins = 60;

// Rolling 60 x 1-minute series for the Web UI charts. bins[0] = -59 min (oldest), bins[59] = current
// (still-filling) minute. Call roll() once at each minute boundary, then update_max()/add() every sample.
template <typename T>
struct MinuteSeries {
    std::array<T, kMinuteBins> bins{};

    void roll() {
        std::copy(bins.begin() + 1, bins.end(), bins.begin());
        bins.back() = T{};
    }
    void update_max(T v) {
        if (v > bins.back()) {
            bins.back() = v;
        }
    }
    void add(T v) { bins.back() = static_cast<T>(bins.back() + v); }
};

struct LightningMetrics {
    std::uint32_t strike_count{0};
    std::uint8_t closest_distance_km{0};
    std::uint32_t last_energy{0};
    MinuteSeries<std::uint16_t> timeline_60m{};  // strikes per minute (-60m .. Now)
};

struct MinuteHistory {
    MinuteSeries<float> rain_mm_h{};  // peak liquid precipitation rate per minute
    MinuteSeries<float> hail_mm_h{};  // peak hail liquid-water-equivalent rate per minute
    MinuteSeries<float> ppv_mm_s{};   // peak ground vibration velocity per minute
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
    MinuteHistory history_60m{};
    WifiRfMetrics wifi_rf{};
};

class TelemetryFormatter {
public:
    // Selects external U.FL antenna by default; falls back to internal chip antenna only if external
    // U.FL is unplugged on the bench (internal RSSI exceeds external RSSI by > 8 dB)
    static bool select_external_ufl_antenna(std::int8_t ufl_rssi_dbm, std::int8_t internal_rssi_dbm);
    // Current-value state for the Web UI WebSocket and Home Assistant MQTT (HA records its own history)
    static std::string build_state_json(const StationSnapshot& snap, const char* fw_version = "26.10.16");
    // 60 x 1-minute chart series served only to the station's own Web UI (/api/history/minutes)
    static std::string build_minute_history_json(const StationSnapshot& snap);
    static std::string build_ha_discovery_json(const char* node_id, const char* metric_id,
                                               const char* name, const char* unit,
                                               const char* device_class, const char* value_template);
};

}  // namespace telemetry
}  // namespace station
