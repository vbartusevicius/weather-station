#pragma once

#include <cstddef>
#include <cstdint>

#include "EspIdfHostStubs.hpp"

// Seeed XIAO ESP32-C6 pin assignment on AeroMet V1 main board:
//   D0  / GPIO0  = VIN_ADC    (ADC1_CH0: 12-32 V supply monitor via 100k/10k divider)
//   D1  / GPIO1  = NTC_SENSE  (ADC1_CH1: reflector 10k NTC thermistor + SW1 SERVICE button)
//   D2  / GPIO2  = CROWN_BOOT0 (crown STM32 BOOT0: high + reset = ROM I2C bootloader)
//   D3  / GPIO21 = HEAT_PWM   (LEDC PWM driving AO3400A reflector de-icing heater MOSFET)
//   D4  / GPIO22 = I2C_SDA    (crown STM32 0x2A, BMP585, LSM6DS3, ADS1115, PMSA003I, SHT40, SGP41, SCD41, LTR390,
//                              TSL25911, GUVB-C31SM, MMC5603NJ)
//   D5  / GPIO23 = I2C_SCL    (I2C clock, 100 kHz standard mode for every device)
//   D6  / GPIO16 = RS485_TX   (UART1 TX -> SP3485 DI)
//   D7  / GPIO17 = RS485_RX   (UART1 RX <- SP3485 RO)
//   D8  / GPIO19 = RS485_DE   (UART1 RTS in RS-485 half-duplex mode -> SP3485 DE + /RE)
//   D9  / GPIO20 = CROWN_IRQ  (crown data-ready / lightning interrupt, input)
//   D10 / GPIO18 = CROWN_NRST (crown STM32 reset, open-drain)
// Wind, rain and lightning are measured by the crown STM32 (shared/include/crown_protocol.hpp).
namespace station {
namespace firmware {
namespace board {

constexpr gpio_num_t fab_sw_pin = GPIO_NUM_9;

constexpr gpio_num_t vin_adc_pin = GPIO_NUM_0;
constexpr adc_channel_t vin_adc_channel = ADC_CHANNEL_0;

constexpr gpio_num_t ntc_sense_pin = GPIO_NUM_1;
constexpr adc_channel_t ntc_adc_channel = ADC_CHANNEL_1;

constexpr gpio_num_t crown_boot0_pin = GPIO_NUM_2;
constexpr gpio_num_t heat_pwm_pin = GPIO_NUM_21;
constexpr gpio_num_t i2c_sda_pin = GPIO_NUM_22;
constexpr gpio_num_t i2c_scl_pin = GPIO_NUM_23;
constexpr gpio_num_t rs485_tx_pin = GPIO_NUM_16;
constexpr gpio_num_t rs485_rx_pin = GPIO_NUM_17;
constexpr gpio_num_t rs485_de_pin = GPIO_NUM_19;
constexpr gpio_num_t crown_irq_pin = GPIO_NUM_20;
constexpr gpio_num_t crown_nrst_pin = GPIO_NUM_18;

// Seeed XIAO ESP32-C6 internal FM8625H SPDT RF switch pins (selects onboard U.FL / IPEX external antenna):
//   GPIO3  = RF_SW_PWR (LOW = enables FM8625H RF switch VDD)
//   GPIO14 = ANT_SEL   (HIGH = routes RF to external U.FL horizontal FPC dipole; LOW = internal chip antenna)
constexpr gpio_num_t rf_switch_power_pin = GPIO_NUM_3;
constexpr gpio_num_t rf_antenna_select_pin = GPIO_NUM_14;
constexpr std::int8_t wifi_max_tx_power_quarter_dbm = 82;  // 82 * 0.25 dBm = +20.5 dBm (112 mW max linear PA)

// I2C 7-bit addresses of all onboard & probe/crown/dome sensors
constexpr std::uint8_t i2c_addr_mmc5603 = 0x30;  // crown U6 MEMSIC MMC5603NJ 20-bit AMR compass
constexpr std::uint8_t i2c_addr_tsl25911 = 0x29;
constexpr std::uint8_t i2c_addr_guvb_c31sm = 0x39;  // dome U3 GenUV GUVB-C31SM AlGaN UV-B sensor
constexpr std::uint8_t i2c_addr_sht4x = 0x44;   // probe U3 SHT40-AD1F (SHT4x command set)
constexpr std::uint8_t i2c_addr_bmp581 = 0x47;  // probe U5 BMP585 (BMP581 register map, chip ID 0x51)
constexpr std::uint8_t i2c_addr_ltr390 = 0x53;
constexpr std::uint8_t i2c_addr_sgp41 = 0x59;   // probe U2
constexpr std::uint8_t i2c_addr_scd41 = 0x62;
constexpr std::uint8_t i2c_addr_pmsa003i = 0x12;  // Plantower PMSA003I in the base pod via main J4 (100 kHz)
constexpr std::uint8_t i2c_addr_lsm6ds3 = 0x6A;
constexpr std::uint8_t i2c_addr_ads1115 = 0x48;  // main U4 heater power monitor
constexpr float refl_shunt_ohm = 0.1F;    // main R16 (reflector heater return)
constexpr float probe_shunt_ohm = 1.0F;   // main R17 (probe heater return, CO2_HEAT_RTN)
constexpr float rail5_divider = 2.0F;     // main R24/R25 (+5V electronics rail), 10k/10k
constexpr float rail_heat_divider = 4.0F; // main R20/R21 (+12V_HEAT heater rail), 30k/10k

// 100 kHz for the whole bus: ~180 pF (12 devices + 260 / 60 mm cables) with 2.2k pull-ups rises in ~335 ns, over
// the 300 ns fast-mode limit but 3x inside the 1000 ns standard-mode one. Bus load ~35 % (IMU 200 Hz, BMP585 FIFO
// 100 Hz, crown 4 quads/s, slow sensors).
constexpr std::uint32_t i2c_speed_hz = 100000;

constexpr bool usb_pin(gpio_num_t pin) {
    return pin == GPIO_NUM_12 || pin == GPIO_NUM_13;
}

constexpr bool pins_distinct() {
    const gpio_num_t pins[] = {
        vin_adc_pin, ntc_sense_pin, crown_boot0_pin, heat_pwm_pin,
        i2c_sda_pin, i2c_scl_pin,   rs485_tx_pin,    rs485_rx_pin,
        rs485_de_pin, crown_irq_pin, crown_nrst_pin,
        rf_switch_power_pin,        rf_antenna_select_pin,
    };
    for (std::size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        if (usb_pin(pins[i])) {
            return false;
        }
        for (std::size_t j = i + 1; j < sizeof(pins) / sizeof(pins[0]); ++j) {
            if (pins[i] == pins[j]) {
                return false;
            }
        }
    }
    return true;
}

static_assert(pins_distinct(), "AeroMet V1 pins must be distinct and avoid USB D-/D+ (GPIO12/13)");

}  // namespace board
}  // namespace firmware
}  // namespace station
