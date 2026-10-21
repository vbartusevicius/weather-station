#include "RegisterBank.hpp"

#include <algorithm>
#include <cstring>

namespace crown {

namespace {
template <typename T>
std::size_t copy_out(const T& v, std::uint8_t* out, std::size_t max) {
    const std::size_t n = std::min(sizeof(T), max);
    std::memcpy(out, &v, n);
    return n;
}
}  // namespace

void RegisterBank::defaults() {
    config_ = ConfigBlock{};
    config_.quads_per_s = 4;
    config_.burst_cycles = 8;
    config_.as3935_outdoor = 1;
    config_.as3935_min_strikes = 1;
    config_.rain_trigger_mv = 40;
    seal(config_);
    seal(wind_);
    seal(diag_);
}

void RegisterBank::publish_wind(const WindBlock& w) {
    wind_ = w;
    seal(wind_);
    status_ |= kStWindNew;
}

void RegisterBank::push_rain(const RainImpact& r) {
    rain_.push({r, now_ms_});
    status_ |= kStRainPending;
}

void RegisterBank::push_strike(std::uint8_t kind, std::uint8_t distance_km, std::uint32_t energy, bool during_burst,
                               std::uint32_t t_ms) {
    StampedStrike s{};
    s.e.kind = kind;
    s.e.distance_km = distance_km;
    s.e.energy = energy;
    s.e.during_burst = during_burst ? 1 : 0;
    s.t_ms = t_ms;
    strikes_.push(s);
    status_ |= kStStrikePending;
}

void RegisterBank::publish_diag(const DiagBlock& d) {
    diag_ = d;
    seal(diag_);
}

void RegisterBank::set_status_bit(StatusBits bit, bool on) {
    status_ = static_cast<std::uint16_t>(on ? (status_ | bit) : (status_ & ~bit));
}

std::size_t RegisterBank::read(std::uint8_t reg, std::uint8_t* out, std::size_t max) {
    switch (reg) {
        case kWhoAmI:
            return copy_out(kWhoAmIValue, out, max);
        case kVersion: {
            const std::uint8_t v[4] = {kProtocolVersion, version_.major, version_.minor, version_.patch};
            const std::size_t n = std::min<std::size_t>(4, max);
            std::memcpy(out, v, n);
            return n;
        }
        case kStatus:
            return copy_out(status_, out, max);
        case kWind:
            status_ &= static_cast<std::uint16_t>(~kStWindNew);
            return copy_out(wind_, out, max);
        case kRainCount:
            return copy_out(static_cast<std::uint8_t>(rain_.size()), out, max);
        case kRainPop: {
            StampedRain s{};
            RainEvent e{};
            if (rain_.pop(s)) {
                e.age_ms = now_ms_ - s.t_ms;
                e.peak_mv = s.r.peak_mv;
                e.width_us = s.r.width_us;
                e.flags = s.r.clipped ? 1 : 0;
            }
            if (rain_.size() == 0) {
                status_ &= static_cast<std::uint16_t>(~kStRainPending);
            }
            seal(e);
            return copy_out(e, out, max);
        }
        case kStrikeCount:
            return copy_out(static_cast<std::uint8_t>(strikes_.size()), out, max);
        case kStrikePop: {
            StampedStrike s{};
            StrikeEvent e{};
            if (strikes_.pop(s)) {
                e = s.e;
                e.age_ms = now_ms_ - s.t_ms;
            }
            if (strikes_.size() == 0) {
                status_ &= static_cast<std::uint16_t>(~kStStrikePending);
            }
            seal(e);
            return copy_out(e, out, max);
        }
        case kDiag:
            return copy_out(diag_, out, max);
        case kConfig:
            return copy_out(config_, out, max);
        default:
            return 0;
    }
}

void RegisterBank::write(const std::uint8_t* data, std::size_t len) {
    if (len < 2) {
        return;
    }
    const std::uint8_t reg = data[0];
    if (reg == kCommand) {
        command_ = static_cast<Command>(data[1]);
        if (command_ == kCmdClearFifos) {
            rain_.clear();
            strikes_.clear();
            status_ &= static_cast<std::uint16_t>(~(kStRainPending | kStStrikePending));
        }
    } else if (reg == kConfig && len - 1 == sizeof(ConfigBlock)) {
        ConfigBlock c{};
        std::memcpy(&c, data + 1, sizeof(c));
        if (valid(c) && c.quads_per_s >= 1 && c.quads_per_s <= 10 && c.burst_cycles >= 4 && c.burst_cycles <= 16) {
            config_ = c;
        }
    }
}

Command RegisterBank::take_command() {
    const Command c = command_;
    command_ = kCmdNone;
    return c;
}

bool RegisterBank::irq_level() const {
    return (status_ & (kStWindNew | kStRainPending | kStStrikePending)) != 0;
}

}  // namespace crown
