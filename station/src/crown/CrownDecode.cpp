#include "CrownDecode.hpp"

#include <algorithm>

namespace station {
namespace crown_link {

bool to_tof_quad(const crown::WindBlock& w, wind::TofQuad& out, WindQuality& q) {
    q = WindQuality{};
    q.min_snr_db = *std::min_element(w.snr_db, w.snr_db + 4);
    q.min_corr_pct = std::min(w.corr_pct[0], w.corr_pct[1]);
    for (int i = 0; i < 4; ++i) {
        if (w.flags[i] != crown::kPathOk || w.tof_ps[i] == 0) {
            ++q.bad_paths;
        }
    }
    q.usable = q.bad_paths == 0;
    if (!q.usable) {
        return false;
    }
    out.t_ns_us = static_cast<float>(w.tof_ps[crown::kNS]) * 1e-6F;
    out.t_sn_us = static_cast<float>(w.tof_ps[crown::kSN]) * 1e-6F;
    out.t_ew_us = static_cast<float>(w.tof_ps[crown::kEW]) * 1e-6F;
    out.t_we_us = static_cast<float>(w.tof_ps[crown::kWE]) * 1e-6F;
    out.env_ns_us = out.env_sn_us = out.env_ew_us = out.env_we_us = 0.0F;
    return true;
}

core::RainImpact to_rain_impact(const crown::RainEvent& e, std::uint32_t now_ms) {
    core::RainImpact r{};
    r.t_ms = now_ms - e.age_ms;
    r.baseline_mv = kRainBaselineMv;
    r.peak_mv = static_cast<std::uint16_t>(std::min<std::uint32_t>(65535U, kRainBaselineMv + e.peak_mv));
    r.width_us = e.width_us;
    return r;
}

bool to_strike(const crown::StrikeEvent& e, std::uint32_t now_ms, core::LightningStrike& out) {
    if (e.kind != 1) {
        return false;
    }
    out.t_ms = now_ms - e.age_ms;
    out.distance_km = e.distance_km;
    out.energy = e.energy;
    return true;
}

}  // namespace crown_link
}  // namespace station
