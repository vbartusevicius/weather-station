#include "I2cBus.hpp"

namespace station {
namespace firmware {

namespace {
constexpr int kTimeoutMs = 50;
}

bool I2cBus::init(int sda_gpio, int scl_gpio, std::uint32_t speed_hz) {
    mutex_ = xSemaphoreCreateMutex();
    speed_hz_ = speed_hz;
    i2c_master_bus_config_t cfg{};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = static_cast<gpio_num_t>(sda_gpio);
    cfg.scl_io_num = static_cast<gpio_num_t>(scl_gpio);
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = 1;  // board has external 4.7k pull-ups; internal ones are harmless
    return mutex_ != nullptr && i2c_new_master_bus(&cfg, &bus_) == ESP_OK;
}

bool I2cBus::probe(std::uint8_t addr) {
    if (bus_ == nullptr) {
        return false;
    }
    Lock lock(mutex_);
    return i2c_master_probe(bus_, addr, kTimeoutMs) == ESP_OK;
}

I2cBus::Device I2cBus::add(std::uint8_t addr, std::uint32_t speed_hz) {
    Device d{};
    if (bus_ == nullptr) {
        return d;
    }
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = speed_hz != 0 ? speed_hz : speed_hz_;
    Lock lock(mutex_);
    if (i2c_master_bus_add_device(bus_, &cfg, &d.handle_) != ESP_OK) {
        d.handle_ = nullptr;
    }
    d.bus_ = this;
    return d;
}

bool I2cBus::Device::write(const std::uint8_t* data, std::size_t len) {
    if (handle_ == nullptr) return false;
    Lock lock(bus_->mutex_);
    return i2c_master_transmit(handle_, data, len, kTimeoutMs) == ESP_OK;
}

bool I2cBus::Device::read(std::uint8_t* data, std::size_t len) {
    if (handle_ == nullptr) return false;
    Lock lock(bus_->mutex_);
    return i2c_master_receive(handle_, data, len, kTimeoutMs) == ESP_OK;
}

bool I2cBus::Device::write_read(const std::uint8_t* w, std::size_t wlen, std::uint8_t* r, std::size_t rlen) {
    if (handle_ == nullptr) return false;
    Lock lock(bus_->mutex_);
    return i2c_master_transmit_receive(handle_, w, wlen, r, rlen, kTimeoutMs) == ESP_OK;
}

bool I2cBus::Device::write_reg(std::uint8_t reg, std::uint8_t value) {
    const std::uint8_t buf[2] = {reg, value};
    return write(buf, 2);
}

bool I2cBus::Device::read_regs(std::uint8_t reg, std::uint8_t* out, std::size_t len) {
    return write_read(&reg, 1, out, len);
}

std::uint8_t sensirion_crc8(const std::uint8_t* data, std::size_t len) {
    std::uint8_t crc = 0xFF;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x80U) ? static_cast<std::uint8_t>((crc << 1) ^ 0x31U) : static_cast<std::uint8_t>(crc << 1);
        }
    }
    return crc;
}

}  // namespace firmware
}  // namespace station
