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

// Rain impact -> core::RainImpact. The crown sends the charge-amplifier deviation; the station's HapticRain
// expects peak and baseline, so the deviation sits on the nominal dry baseline (kRainBaselineMv).
constexpr std::uint16_t kRainBaselineMv = 206;
core::RainImpact to_rain_impact(const crown::RainEvent& e, std::uint32_t now_ms);

// Lightning strike (only kind 1 = lightning is a strike; disturbers / noise are diagnostics).
bool to_strike(const crown::StrikeEvent& e, std::uint32_t now_ms, core::LightningStrike& out);

}  // namespace crown_link
}  // namespace station
