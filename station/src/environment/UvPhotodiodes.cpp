#include "environment/UvPhotodiodes.hpp"

#include <algorithm>
#include <cmath>

#include "core/Config.hpp"

namespace station {
namespace environment {
namespace uvpd {

std::uint8_t next_range(std::uint8_t current, float volts) {
    const float v = std::fabs(volts);
    if (current < 3 && v > 0.9F * kFullScaleV[current]) {
        return static_cast<std::uint8_t>(current + 1);
    }
    if (current > 0 && v < 0.4F * kFullScaleV[current - 1]) {
        return static_cast<std::uint8_t>(current - 1);
    }
    return current;
}

void DarkZero::update(float na, bool night) {
    if (!night) {
        return;
    }
    offset_na_ = learnt_ ? 0.98F * offset_na_ + 0.02F * na : na;
    learnt_ = true;
}

float DarkZero::apply(float na) const { return std::max(0.0F, na - offset_na_); }

float uv_index(float uvb_na, float transmission, float uvb_na_per_uvi) {
    return uvb_na_per_uvi > 0.0F ? uvb_na / uvb_na_per_uvi : core::cover_corrected(uvb_na, transmission) / kUvbNaPerUvi;
}

float uvb_w_m2(float uvb_na, float transmission) { return core::cover_corrected(uvb_na, transmission) / kUvbNaPerWm2; }

float uva_w_m2(float uva_na, float transmission) { return core::cover_corrected(uva_na, transmission) / kUvaNaPerWm2; }

}  // namespace uvpd
}  // namespace environment
}  // namespace station
