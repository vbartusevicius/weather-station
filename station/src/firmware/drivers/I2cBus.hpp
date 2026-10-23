#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace station {
namespace firmware {

// Thin, mutex-protected wrapper around the ESP-IDF 5.x i2c_master driver shared by all sensors.
class I2cBus {
public:
    bool init(int sda_gpio, int scl_gpio, std::uint32_t speed_hz);
    bool probe(std::uint8_t addr);

    class Device {
    public:
        bool valid() const { return handle_ != nullptr; }
        bool write(const std::uint8_t* data, std::size_t len);
        bool read(std::uint8_t* data, std::size_t len);
        bool write_read(const std::uint8_t* w, std::size_t wlen, std::uint8_t* r, std::size_t rlen);
        bool write_reg(std::uint8_t reg, std::uint8_t value);
        bool read_regs(std::uint8_t reg, std::uint8_t* out, std::size_t len);

    private:
        friend class I2cBus;
        I2cBus* bus_{nullptr};
        i2c_master_dev_handle_t handle_{nullptr};
    };

    Device add(std::uint8_t addr, std::uint32_t speed_hz = 0);  // 0 = bus speed

private:
    friend class Device;
    struct Lock {
        explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
        ~Lock() { xSemaphoreGive(m_); }
        SemaphoreHandle_t m_;
    };
    i2c_master_bus_handle_t bus_{nullptr};
    SemaphoreHandle_t mutex_{nullptr};
    std::uint32_t speed_hz_{100000};
};

// Sensirion CRC-8 (poly 0x31, init 0xFF) used by SHT4x, SCD4x, SGP4x.
std::uint8_t sensirion_crc8(const std::uint8_t* data, std::size_t len);

}  // namespace firmware
}  // namespace station
