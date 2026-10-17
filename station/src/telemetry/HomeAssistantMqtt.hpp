#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "airquality/AirQuality.hpp"
#include "environment/MetCalculations.hpp"
#include "heater/DeIcingHeater.hpp"
#include "rain/HapticRain.hpp"
#include "wind/UltrasonicWind.hpp"

namespace station::telemetry {

struct LightningMetrics {
    std::uint32_t strike_count{0};
    std::uint8_t closest_distance_km{0};
    std::uint32_t last_energy{0};
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
};

class TelemetryFormatter {
public:
    static std::string build_state_json(const StationSnapshot& snap, const char* fw_version = "26.10.16");
    static std::string build_ha_discovery_json(const char* node_id, const char* metric_id,
                                               const char* name, const char* unit,
                                               const char* device_class, const char* value_template);
};

}  // namespace station::telemetry
