#include "CrownLink.hpp"

#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace station {
namespace firmware {

bool CrownLink::begin(I2cBus& bus, gpio_num_t irq_pin, gpio_num_t nrst_pin, gpio_num_t boot0_pin) {
    bus_ = &bus;
    irq_ = irq_pin;
    nrst_ = nrst_pin;
    boot0_ = boot0_pin;
    transport_.link = this;
    gpio_config_t in{};
    in.pin_bit_mask = 1ULL << irq_;
    in.mode = GPIO_MODE_INPUT;
    in.pull_down_en = GPIO_PULLDOWN_ENABLE;   // crown absent / in reset -> no data
    gpio_config(&in);
    gpio_config_t od{};
    od.pin_bit_mask = 1ULL << nrst_;
    od.mode = GPIO_MODE_INPUT_OUTPUT_OD;      // the STM32 drives NRST itself on internal resets
    gpio_config(&od);
    gpio_set_level(nrst_, 1);
    gpio_config_t pp{};
    pp.pin_bit_mask = 1ULL << boot0_;
    pp.mode = GPIO_MODE_OUTPUT;
    gpio_config(&pp);
    gpio_set_level(boot0_, 0);

    dev_ = bus.add(crown::kI2cAddress, 100000);
    boot_a_ = bus.add(crown::kBootloaderAddrA, 100000);
    boot_b_ = bus.add(crown::kBootloaderAddrB, 100000);
    std::uint8_t who = 0;
    present_ = dev_.read_regs(crown::kWhoAmI, &who, 1) && who == crown::kWhoAmIValue;
    return present_;
}

bool CrownLink::irq() const {
    return irq_ != GPIO_NUM_NC && gpio_get_level(irq_) == 1;
}

bool CrownLink::read_status(std::uint16_t& status) {
    if (flashing()) return false;
    std::uint8_t b[2];
    if (!dev_.read_regs(crown::kStatus, b, 2)) return false;
    status = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
    return true;
}

bool CrownLink::read_wind(crown::WindBlock& w) {
    if (flashing()) return false;
    std::uint8_t raw[sizeof(crown::WindBlock)];
    return dev_.read_regs(crown::kWind, raw, sizeof raw) && crown::decode(raw, sizeof raw, w);
}

int CrownLink::pop_rain(crown::RainEvent* out, int max) {
    if (flashing()) return 0;
    std::uint8_t count = 0;
    if (!dev_.read_regs(crown::kRainCount, &count, 1)) return 0;
    int n = 0;
    for (; n < max && n < count; ++n) {
        std::uint8_t raw[sizeof(crown::RainEvent)];
        if (!dev_.read_regs(crown::kRainPop, raw, sizeof raw) || !crown::decode(raw, sizeof raw, out[n])) break;
    }
    return n;
}

int CrownLink::pop_strikes(crown::StrikeEvent* out, int max) {
    if (flashing()) return 0;
    std::uint8_t count = 0;
    if (!dev_.read_regs(crown::kStrikeCount, &count, 1)) return 0;
    int n = 0;
    for (; n < max && n < count; ++n) {
        std::uint8_t raw[sizeof(crown::StrikeEvent)];
        if (!dev_.read_regs(crown::kStrikePop, raw, sizeof raw) || !crown::decode(raw, sizeof raw, out[n])) break;
    }
    return n;
}

bool CrownLink::read_diag(crown::DiagBlock& d) {
    if (flashing()) return false;
    std::uint8_t raw[sizeof(crown::DiagBlock)];
    return dev_.read_regs(crown::kDiag, raw, sizeof raw) && crown::decode(raw, sizeof raw, d);
}

bool CrownLink::send_config(const crown::ConfigBlock& c) {
    if (flashing()) return false;
    crown::ConfigBlock sealed = c;
    crown::seal(sealed);
    std::uint8_t buf[1 + sizeof(crown::ConfigBlock)] = {crown::kConfig};
    std::memcpy(buf + 1, &sealed, sizeof sealed);
    return dev_.write(buf, sizeof buf);
}

bool CrownLink::command(crown::Command c) {
    if (flashing()) return false;
    return dev_.write_reg(crown::kCommand, c);
}

bool CrownLink::version(std::uint8_t out[4]) {
    return !flashing() && dev_.read_regs(crown::kVersion, out, 4);
}

void CrownLink::reset() {
    transport_.reset_pulse();
}

crown_link::StmI2cBootloader::Result CrownLink::flash(const std::uint8_t* image, std::size_t len) {
    flashing_ = true;
    crown_link::StmI2cBootloader bl(transport_);
    const auto r = bl.flash(image, len);
    vTaskDelay(pdMS_TO_TICKS(300));   // application start-up
    std::uint8_t who = 0;
    present_ = dev_.read_regs(crown::kWhoAmI, &who, 1) && who == crown::kWhoAmIValue;
    flashing_ = false;
    return r;
}

I2cBus::Device* CrownLink::boot_device(std::uint8_t addr) {
    if (addr == crown::kBootloaderAddrA) return &boot_a_;
    if (addr == crown::kBootloaderAddrB) return &boot_b_;
    return nullptr;
}

bool CrownLink::Transport::write(std::uint8_t addr, const std::uint8_t* data, std::size_t len) {
    auto* d = link->boot_device(addr);
    return d && d->write(data, len);
}

bool CrownLink::Transport::read(std::uint8_t addr, std::uint8_t* data, std::size_t len) {
    auto* d = link->boot_device(addr);
    return d && d->read(data, len);
}

void CrownLink::Transport::delay_ms(std::uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}

void CrownLink::Transport::set_boot0(bool high) {
    gpio_set_level(link->boot0_, high ? 1 : 0);
}

void CrownLink::Transport::reset_pulse() {
    gpio_set_level(link->nrst_, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(link->nrst_, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

}  // namespace firmware
}  // namespace station
