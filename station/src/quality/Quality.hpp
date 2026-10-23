#pragma once
// Automatic data quality control and station health (Web UI Calibration tab, Now-card badges, MQTT).
//
// QC follows the usual automatic-weather-station practice (WMO-No. 8 Vol. III ch. 1, Oklahoma Mesonet / Shafer et
// al. 2000): every published measurement gets a flag that supplements, never alters, the value:
//   range        physically impossible / outside the sensor range          -> Fail
//   step         change over one minute larger than the atmosphere allows   -> Suspect for 10 min ("spike")
//   persistence  no change at all for hours while the sensor still reports  -> Suspect ("stuck")
//   consistency  cross-checks: light at night, irradiance above the clear-sky limit (BSRN-style), rain at low
//                humidity, sonic temperature far from the SHT40, CO2 below the outdoor background
// The health list is the station's "supervisor message" (cf. Vaisala aR5, Tempest device_status): one entry per
// subsystem with ok / warn / fail / off and a reason or note ("" when normal); the UI shows the live readings.

#include <cstdint>

#include "../core/Config.hpp"
#include "../core/DerivedState.hpp"
#include "../core/RawState.hpp"

namespace station {
namespace quality {

const char* flag_name(core::QcFlag f);
const char* metric_key(core::QcMetric m);
const char* status_name(core::HealthStatus s);

struct Limits {
    float lo, hi;            // range (outside: Fail)
    float step_per_min;      // larger one-minute change: Suspect (<= 0: no step test)
    float stuck_eps;         // smaller change ...
    std::uint32_t stuck_s;   // ... for this long: Suspect (0: no persistence test)
};
const Limits& limits(core::QcMetric m);

class QualityControl {
public:
    // Runs once per second at the end of Pipeline::step and fills d.quality.
    void step(const core::RawState& raw, const core::StationConfig& cfg, core::DerivedState& d);

private:
    struct Track {
        bool has_ref{false};
        float ref{0.0F};
        std::uint32_t ref_s{0};
        bool has_stuck{false};
        float stuck_ref{0.0F};
        std::uint32_t stuck_since_s{0};
        std::uint32_t suspect_until_s{0};
        const char* suspect_reason{""};
    };
    core::QcValue check(core::QcMetric m, bool ok, float v, bool persistence_applies);
    void health(const core::RawState& raw, const core::StationConfig& cfg, core::DerivedState& d);

    Track tracks_[static_cast<int>(core::QcMetric::Count)]{};
    std::uint32_t t_s_{0};
    // AS3935 disturber rate over 10-minute windows
    std::uint32_t win_start_s_{0};
    std::uint32_t win_start_count_{0};
    bool win_started_{false};
    bool have_rate_{false};
    std::uint32_t disturbers_10min_{0};
};

}  // namespace quality
}  // namespace station
