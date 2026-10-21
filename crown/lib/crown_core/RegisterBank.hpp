#pragma once
// Crown I2C register bank (shared/include/crown_protocol.hpp): owns the latest wind quad, the rain and strike FIFOs,
// diagnostics and configuration. The STM32 I2C2 interrupt handler calls read() / write(); the acquisition loop calls
// the publish_* / push_* methods. Host-testable; on the MCU the calls are made with I2C interrupts masked.

#include <cstddef>
#include <cstdint>

#include "RainDetector.hpp"
#include "crown_protocol.hpp"

namespace crown {

struct FirmwareVersion {
    std::uint8_t major{0}, minor{1}, patch{0};
};

class RegisterBank {
public:
    explicit RegisterBank(FirmwareVersion v = {}) : version_(v) { defaults(); }

    // ---- acquisition side ----
    void publish_wind(const WindBlock& w);
    void push_rain(const RainImpact& r);
    void push_strike(std::uint8_t kind, std::uint8_t distance_km, std::uint32_t energy, bool during_burst,
                     std::uint32_t t_ms);
    void publish_diag(const DiagBlock& d);
    void set_status_bit(StatusBits bit, bool on);
    void set_now_ms(std::uint32_t t) { now_ms_ = t; }

    // ---- I2C side ----
    // Bytes for a read starting at `reg` (pop registers remove the event). Returns the byte count (0 = unknown reg).
    std::size_t read(std::uint8_t reg, std::uint8_t* out, std::size_t max);
    // A master write: data[0] = register, data[1..] = payload.
    void write(const std::uint8_t* data, std::size_t len);

    Command take_command();          // pending command from kCommand (cleared on read)
    const ConfigBlock& config() const { return config_; }
    bool irq_level() const;          // CROWN_IRQ: unread wind / rain / strike data
    std::uint16_t status() const { return status_; }

private:
    void defaults();
    struct StampedRain {
        RainImpact r;
        std::uint32_t t_ms;
    };
    struct StampedStrike {
        StrikeEvent e;
        std::uint32_t t_ms;
    };

    FirmwareVersion version_;
    WindBlock wind_{};
    DiagBlock diag_{};
    ConfigBlock config_{};
    EventFifo<StampedRain, 32> rain_;
    EventFifo<StampedStrike, 16> strikes_;
    std::uint16_t status_{0};
    std::uint32_t now_ms_{0};
    Command command_{kCmdNone};
};

}  // namespace crown
