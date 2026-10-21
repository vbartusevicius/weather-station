#pragma once

#include <cstdint>

#include "../../core/Reading.hpp"
#include "../../environment/MetCalculations.hpp"
#include "I2cBus.hpp"

namespace station {
namespace firmware {

// ---------------------------------------------------------------------------------------------
// I2C sensor drivers. Each driver only talks to its chip and converts to engineering units; all
// physics (cross-sensor corrections, statistics) lives in the host-tested core.
// ---------------------------------------------------------------------------------------------

// Sensirion SHT40-AD1F (probe tip): temperature / humidity, on-chip heater for condensation bake-out.
class Sht4x {
public:
    bool begin(I2cBus& bus, std::uint8_t addr);
    bool measure(float& temp_c, float& rh_pct);          // high repeatability, ~9 ms
    bool heater_pulse(float& temp_c, float& rh_pct);     // 200 mW for 1 s, then measures (~1.1 s)
    bool present() const { return present_; }

private:
    bool read_result(std::uint8_t cmd, std::uint32_t wait_ms, float& temp_c, float& rh_pct);
    I2cBus::Device dev_;
    bool present_{false};
};

// Bosch BMP581: 100 Hz pressure FIFO (thunder + 1-s mean), temperature register.
class Bmp581 {
public:
    bool begin(I2cBus& bus, std::uint8_t addr);
    // Reads all FIFO frames; returns the number of pressure samples (Pa) written to `out`.
    int read_fifo(float* out, int max_samples);
    bool read_temperature(float& temp_c);
    bool present() const { return present_; }

private:
    I2cBus::Device dev_;
    bool present_{false};
};

// Sensirion SCD41: photoacoustic CO2 (5 s periodic mode), in-sensor ambient pressure compensation.
class Scd41 {
public:
    bool begin(I2cBus& bus, std::uint8_t addr);
    bool data_ready();
    bool read(float& co2_ppm, float& temp_c, float& rh_pct);
    bool set_ambient_pressure(float hpa);
    bool start();
    bool stop();
    bool running() const { return running_; }
    bool present() const { return present_; }

private:
    I2cBus::Device dev_;
    bool present_{false};
    bool running_{false};
};

struct PmData {
    float pm1{0.0F}, pm25{0.0F}, pm10{0.0F};  // ug/m3, "atmospheric environment" values (outdoor)
};

// Plantower PMSA003I laser particle counter (I2C 0x12, 100 kHz, 5 V fan) in the sealed base pod: PM1.0 / PM2.5 / PM10.
// One 32-byte frame per read: 0x42 0x4D, length 28, 13 data words, checksum = sum of the first 30 bytes.
class Pmsa003i {
public:
    static constexpr std::uint32_t kWarmupMs = 30000;  // datasheet: stable data >= 30 s after fan start

    bool begin(I2cBus& bus, std::uint8_t addr, std::uint32_t now_ms);
    bool read(std::uint32_t now_ms, PmData& out);
    bool present() const { return present_; }

private:
    I2cBus::Device dev_;
    bool present_{false};
    std::uint32_t started_ms_{0};
};

// TI ADS1115 (main U4, I2C 0x48): heater power monitor. Single-shot, single-ended conversions at 16 SPS: the
// delta-sigma conversion integrates over 62.5 ms, averaging the 1 kHz reflector-heater PWM (with the R/C filters).
class Ads1115 {
public:
    enum class Range : std::uint8_t { Fs256mV = 0b101, Fs4096mV = 0b001 };

    bool begin(I2cBus& bus, std::uint8_t addr);
    bool start(std::uint8_t channel, Range range);   // AIN0..AIN3 against GND
    bool read(float& volts);                         // result of the last start() (>= 63 ms later)
    bool present() const { return present_; }

private:
    I2cBus::Device dev_;
    bool present_{false};
    float fs_v_{0.256F};
};

// Sensirion SGP41 on the probe: raw VOC / NOx MOx ticks at 1 Hz, humidity/temperature compensated
// in-sensor with the neighbouring SHT40 reading. The gas index algorithm runs in the core pipeline.
class Sgp41 {
public:
    static constexpr std::uint8_t kConditioningSeconds = 10;  // Sensirion: NOx conditioning, max 10 s

    bool begin(I2cBus& bus, std::uint8_t addr);
    // Call exactly once per second. During conditioning only SRAW_VOC is produced (nox_ok = false).
    bool measure(bool comp_ok, float rh_pct, float temp_c, std::uint16_t& sraw_voc, std::uint16_t& sraw_nox, bool& nox_ok);
    bool present() const { return present_; }

private:
    I2cBus::Device dev_;
    bool present_{false};
    std::uint8_t conditioned_s_{0};
};

// ST LSM6DS3TR-C accelerometer (416 Hz ODR, polled at 200 Hz) for tilt and ground vibration.
class Lsm6ds3 {
public:
    bool begin(I2cBus& bus, std::uint8_t addr);
    bool read_accel(core::Vec3& g);
    bool present() const { return present_; }

private:
    I2cBus::Device dev_;
    bool present_{false};
};

// MEMSIC MMC5603NJ 20-bit AMR compass on the crown (heading for the ultrasonic wind rotation).
class Mmc5603 {
public:
    bool begin(I2cBus& bus, std::uint8_t addr);
    bool read(core::Vec3& field_ut);
    bool present() const { return present_; }

private:
    I2cBus::Device dev_;
    bool present_{false};
    std::uint16_t reads_{0};
};

// Lite-On LTR-390UV-01: alternates UVS (UV index) and ALS (lux up to 157 klx) every 500 ms.
class Ltr390 {
public:
    bool begin(I2cBus& bus, std::uint8_t addr);
    // Call every 500 ms. Reads the finished conversion and switches mode.
    // Returns true when a value was produced; `is_uvs` tells which.
    bool step(bool& is_uvs, float& value);
    bool present() const { return present_; }

private:
    bool configure(bool uvs);
    I2cBus::Device dev_;
    bool present_{false};
    bool uvs_mode_{true};
};

// ams TSL25911FN: visible + NIR with auto-gain (1x .. 9876x) down to ~188 uLux.
class Tsl2591 {
public:
    bool begin(I2cBus& bus, std::uint8_t addr);
    // Reads the last 100 ms integration; false when saturated or not ready.
    bool read(float& lux, float& nir_ratio);
    bool present() const { return present_; }

private:
    bool set_gain(environment::Tsl2591Gain g);
    I2cBus::Device dev_;
    bool present_{false};
    environment::DomeOpticalScheduler sched_{};
    environment::Tsl2591Gain gain_{environment::Tsl2591Gain::Med25x};
};

// GenUV GUVB-C31SM (dome U3): AlGaN UV-B photodiode (240-320 nm), 16-bit ADC, I2C 0x39.
// Register map per the Genicom preliminary datasheet (Rev 1, 2015): CHIPID 0x00 = 0x62, MODE 0x01,
// RES_UV 0x04, RANGE_UVB 0x07, SOFT_RESET 0x0B, UVB 0x17/0x18, NVM read 0x30..0x32 (offset, B_Scale).
class GuvbC31sm {
public:
    bool begin(I2cBus& bus, std::uint8_t addr);
    // Reads the last 100 ms conversion with auto-ranging (x1..x128). `counts` = counts normalised to the
    // power-on default range (x8) minus the factory dark offset. `uvi` = counts / scale, where scale is
    // `counts_per_uvi` (field calibration) or else the factory B_Scale. False when not ready / re-ranging.
    bool read(float counts_per_uvi, float& counts, float& uvi, bool& uvi_ok);
    bool present() const { return present_; }
    std::uint16_t factory_b_scale() const { return b_scale_; }

private:
    bool reg_read(std::uint8_t reg, std::uint8_t* out, std::size_t len);
    I2cBus::Device dev_;
    bool present_{false};
    std::uint8_t offset_{0};
    std::uint16_t b_scale_{0};
    std::uint8_t range_{3};      // RANGE_UVB code: 0 = x1 .. 7 = x128 (default 3 = x8)
    bool skip_next_{false};
};

}  // namespace firmware
}  // namespace station
