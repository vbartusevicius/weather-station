#pragma once

#include <cstdint>

namespace station {
namespace core {

// A single sensor value with its acquisition time (monotonic ms) and validity.
// Drivers set() it after unit conversion and calibration; processors check fresh() before use,
// so a disconnected or failing sensor produces "no value" instead of a silent default.
template <typename T>
struct Reading {
    T value{};
    std::uint32_t t_ms{0};
    bool valid{false};

    void set(T v, std::uint32_t now_ms) {
        value = v;
        t_ms = now_ms;
        valid = true;
    }
    void invalidate() { valid = false; }
    bool fresh(std::uint32_t now_ms, std::uint32_t max_age_ms) const {
        return valid && (now_ms - t_ms) <= max_age_ms;
    }
};

// Derived value with validity: invalid values are published as JSON null.
template <typename T>
struct Value {
    T v{};
    bool ok{false};

    void set(T x) {
        v = x;
        ok = true;
    }
    void clear() { ok = false; }
};

struct Vec3 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

}  // namespace core
}  // namespace station
