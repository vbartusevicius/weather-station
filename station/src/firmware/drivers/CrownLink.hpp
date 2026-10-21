#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <driver/gpio.h>

#include "../../crown/StmI2cBootloader.hpp"
#include "I2cBus.hpp"
#include "crown_protocol.hpp"

namespace station {
namespace firmware {

// Main <-> crown STM32G431 link (shared/include/crown_protocol.hpp): register reads over the shared I2C bus, the
// CROWN_IRQ data-ready line, and CROWN_NRST (open-drain) / CROWN_BOOT0 for resets and ROM-bootloader flashing.
class CrownLink {
public:
    bool begin(I2cBus& bus, gpio_num_t irq_pin, gpio_num_t nrst_pin, gpio_num_t boot0_pin);
    bool present() const { return present_; }
    bool irq() const;

    bool read_status(std::uint16_t& status);
    bool read_wind(crown::WindBlock& w);              // CRC-checked
    int pop_rain(crown::RainEvent* out, int max);
    int pop_strikes(crown::StrikeEvent* out, int max);
    bool read_diag(crown::DiagBlock& d);
    bool send_config(const crown::ConfigBlock& c);
    bool command(crown::Command c);
    bool version(std::uint8_t out[4]);

    // Reflash the crown (blocks ~10-20 s for a 32 KB image). Other tasks must not use the link meanwhile:
    // flashing() turns true for the duration.
    crown_link::StmI2cBootloader::Result flash(const std::uint8_t* image, std::size_t len);
    bool flashing() const { return flashing_.load(); }
    void reset();

private:
    class Transport : public crown_link::BootTransport {
    public:
        CrownLink* link{nullptr};
        bool write(std::uint8_t addr, const std::uint8_t* data, std::size_t len) override;
        bool read(std::uint8_t addr, std::uint8_t* data, std::size_t len) override;
        void delay_ms(std::uint32_t ms) override;
        void set_boot0(bool high) override;
        void reset_pulse() override;
    };
    I2cBus::Device* boot_device(std::uint8_t addr);

    I2cBus* bus_{nullptr};
    I2cBus::Device dev_{};
    I2cBus::Device boot_a_{}, boot_b_{};
    gpio_num_t irq_{GPIO_NUM_NC}, nrst_{GPIO_NUM_NC}, boot0_{GPIO_NUM_NC};
    bool present_{false};
    std::atomic<bool> flashing_{false};
    Transport transport_{};
};

}  // namespace firmware
}  // namespace station
