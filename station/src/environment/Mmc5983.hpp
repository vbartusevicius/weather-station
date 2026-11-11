#pragma once
// MEMSIC MMC5983MA output decoding (pure, host-tested): registers 0x00..0x06 -> field in microtesla.
#include <cstdint>

#include "core/Reading.hpp"

namespace station {
namespace environment {

// b[0..5] = X[17:10], X[9:2], Y[17:10], Y[9:2], Z[17:10], Z[9:2]; b[6] = X[1:0] (bits 7:6), Y[1:0] (5:4), Z[1:0] (3:2).
// 18 bit, null field 131072, 16384 counts per gauss (1 G = 100 uT).
inline core::Vec3 mmc5983_field_ut(const std::uint8_t* b) {
    auto axis = [](std::uint8_t hi, std::uint8_t mid, std::uint8_t lo2) {
        const std::uint32_t raw = (static_cast<std::uint32_t>(hi) << 10) | (static_cast<std::uint32_t>(mid) << 2) | (lo2 & 0x3U);
        return (static_cast<float>(raw) - 131072.0F) / 16384.0F * 100.0F;
    };
    core::Vec3 f{};
    f.x = axis(b[0], b[1], static_cast<std::uint8_t>(b[6] >> 6));
    f.y = axis(b[2], b[3], static_cast<std::uint8_t>(b[6] >> 4));
    f.z = axis(b[4], b[5], static_cast<std::uint8_t>(b[6] >> 2));
    return f;
}

}  // namespace environment
}  // namespace station
