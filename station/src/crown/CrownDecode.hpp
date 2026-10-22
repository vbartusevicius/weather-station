#pragma once
// Crown (STM32G431) register blocks -> station pipeline types. Host-tested; the ESP-IDF I2C transport is
// firmware/drivers/CrownLink.

#include <cstdint>

#include "../core/RawState.hpp"
#include "../wind/UltrasonicWind.hpp"
#include "crown_protocol.hpp"

namespace station {
namespace crown_link {

struct WindQuality {
    bool usable{false};
    std::uint8_t min_snr_db{0};
    std::uint8_t min_corr_pct{0};
    std::uint8_t bad_paths{0};   // paths flagged no-echo / clipped / low-correlation / early
};

// Times of flight (ps) -> wind::TofQuad (us). Returns false (and leaves `out` untouched) when any path is unusable.
bool to_tof_quad(const crown::WindBlock& w, wind::TofQuad& out, WindQuality& q);

// Rain impact -> core::RainImpact. The crown sends the charge-amplifier deviation; HapticRain works on
// peak - baseline, so the impact carries baseline 0 (= no capacitive wetness channel) and the deviation scaled by
// `mv_scale` (StationConfig::rain_impact_scale, calibrated so its drop / hail thresholds match, see CALIBRATION.md).
core::RainImpact to_rain_impact(const crown::RainEvent& e, std::uint32_t now_ms, float mv_scale = 1.0F);

// Lightning strike (only kind 1 = lightning is a strike; disturbers / noise are diagnostics).
bool to_strike(const crown::StrikeEvent& e, std::uint32_t now_ms, core::LightningStrike& out);

}  // namespace crown_link
}  // namespace station
