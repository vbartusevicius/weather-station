#pragma once

#include <cstdint>

// Top-level header of the PlatformIO library (wraps the C algorithm header in extern "C").
#include <SensirionGasIndexAlgorithm.h>

namespace station {
namespace airquality {

// SGP41 raw ticks -> Sensirion VOC Index (1..500, 100 = this site's typical air over the last ~24 h)
// and NOx Index (1..500, 1 = clean baseline) using Sensirion's Gas Index Algorithm 3.2.3 (BSD-3).
// The algorithm assumes exactly one sample per second.
class GasIndexPair {
public:
    GasIndexPair();
    void reset();

    // One 1-s sample. `nox_ok` is false while the SGP41 is conditioning (first 10 s).
    void process(std::uint16_t sraw_voc, bool nox_ok, std::uint16_t sraw_nox);

    // Indices are 0 during the algorithm's 45 s blackout; that is reported as not ok.
    bool voc(float& out) const;
    bool nox(float& out) const;
    // True until both estimators have finished their initial learning (VOC ~1.5 h, NOx ~5.7 h),
    // unless the VOC state was restored after a short interruption.
    bool learning() const;

    // VOC state persistence (Sensirion: only after >= 3 h of operation, restore only after an
    // interruption shorter than 10 minutes; not supported for NOx).
    bool voc_state(float& state0, float& state1) const;
    void restore_voc_state(float state0, float state1);

    float uptime_s() const { return voc_.mUptime; }

private:
    GasIndexAlgorithmParams voc_{};
    GasIndexAlgorithmParams nox_{};
    std::int32_t voc_index_{0};
    std::int32_t nox_index_{0};
    std::uint32_t seconds_{0};
    bool restored_{false};
};

}  // namespace airquality
}  // namespace station
