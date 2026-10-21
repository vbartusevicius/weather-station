#pragma once
// Piezo rain impacts from the crown charge amplifier (ADC2, continuous). Host-testable.
//
// The charge amplifier idles at VREF (1.65 V); a drop or hail stone hitting the lid diaphragm produces a short
// bipolar charge pulse. An impact starts when |x - baseline| exceeds the trigger, its size is the largest
// deviation, and it ends after `kQuietUs` below half the trigger. The baseline follows slow drift (0.2 s).

#include <cstdint>

namespace crown {

struct RainImpact {
    std::uint32_t t_us{0};      // start of the impact
    std::uint16_t peak_mv{0};   // largest deviation from the baseline
    std::uint16_t width_us{0};  // time above the trigger
    bool clipped{false};
};

class RainDetector {
public:
    static constexpr float kMvPerCount = 3300.0F / 4096.0F;
    static constexpr std::uint32_t kQuietUs = 400;

    explicit RainDetector(float fs_hz = 50000.0F, float trigger_mv = 40.0F) : fs_hz_(fs_hz), trigger_mv_(trigger_mv) {}
    void set_trigger_mv(float mv) { trigger_mv_ = mv; }

    // Feed one raw 12-bit sample taken at `t_us`. Returns true when an impact has just ended (`out` filled).
    bool add(std::uint16_t raw, std::uint32_t t_us, RainImpact& out);
    float baseline_mv() const { return baseline_; }

private:
    float fs_hz_;
    float trigger_mv_;
    float baseline_{1650.0F};
    bool init_{false};
    bool in_pulse_{false};
    float peak_{0.0F};
    bool clipped_{false};
    std::uint32_t start_us_{0};
    std::uint32_t last_above_us_{0};
};

// Fixed-size FIFO for events waiting to be read over I2C (drops the oldest when full).
template <typename T, unsigned N>
class EventFifo {
public:
    bool push(const T& v) {
        const bool dropped = count_ == N;
        if (dropped) {
            head_ = (head_ + 1) % N;
            --count_;
        }
        buf_[(head_ + count_) % N] = v;
        ++count_;
        return !dropped;
    }
    bool pop(T& v) {
        if (count_ == 0) {
            return false;
        }
        v = buf_[head_];
        head_ = (head_ + 1) % N;
        --count_;
        return true;
    }
    unsigned size() const { return count_; }
    void clear() { head_ = count_ = 0; }

private:
    T buf_[N]{};
    unsigned head_{0};
    unsigned count_{0};
};

}  // namespace crown
