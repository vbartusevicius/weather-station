#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/gpio.h>
#include <hal/adc_types.h>

// Seeed XIAO ESP32-C6 pin assignment on AeroMet V1 main board:
//   D0  / GPIO0  = VIN_ADC    (ADC1_CH0: 12-32 V supply monitor via 100k/10k divider)
//   D1  / GPIO1  = NTC_SENSE  (ADC1_CH1: reflector 10k NTC thermistor + SW1 SERVICE button)
//   D2  / GPIO2  = RAIN_ADC   (ADC1_CH2: hybrid piezo charge-amp & capacitive rain envelope)
//   D3  / GPIO21 = HEAT_PWM   (LEDC PWM driving AO3400A reflector de-icing heater MOSFET)
//   D4  / GPIO22 = I2C_SDA    (BMP581, LSM6DS3, AS3935, SEN55, SHT45, SGP41, SCD41, LTR390, VEML7700, LIS2MDL)
//   D5  / GPIO23 = I2C_SCL    (I2C clock, 400 kHz)
//   D6  / GPIO16 = US_SEL_A   (CD4052 ultrasonic transducer mux select A)
//   D7  / GPIO17 = US_SEL_B   (CD4052 ultrasonic transducer mux select B)
//   D8  / GPIO19 = US_TX      (RMT TX 40 kHz ultrasonic burst output)
//   D9  / GPIO20 = US_RX      (RMT RX / timer capture for ultrasonic echo zero-crossing)
//   D10 / GPIO18 = RS485_IO   (UART1 half-duplex TX/RX to auto-direction SP3485EN RS-485 transceiver)
namespace station::firmware::board {

constexpr gpio_num_t vin_adc_pin = GPIO_NUM_0;
constexpr adc_channel_t vin_adc_channel = ADC_CHANNEL_0;

constexpr gpio_num_t ntc_sense_pin = GPIO_NUM_1;
constexpr adc_channel_t ntc_adc_channel = ADC_CHANNEL_1;

constexpr gpio_num_t rain_adc_pin = GPIO_NUM_2;
constexpr adc_channel_t rain_adc_channel = ADC_CHANNEL_2;

constexpr gpio_num_t heat_pwm_pin = GPIO_NUM_21;
constexpr gpio_num_t i2c_sda_pin = GPIO_NUM_22;
constexpr gpio_num_t i2c_scl_pin = GPIO_NUM_23;
constexpr gpio_num_t us_sel_a_pin = GPIO_NUM_16;
constexpr gpio_num_t us_sel_b_pin = GPIO_NUM_17;
constexpr gpio_num_t us_tx_pin = GPIO_NUM_19;
constexpr gpio_num_t us_rx_pin = GPIO_NUM_20;
constexpr gpio_num_t rs485_io_pin = GPIO_NUM_18;

// Seeed XIAO ESP32-C6 internal FM8625H SPDT RF switch pins (selects onboard U.FL / IPEX external antenna):
//   GPIO3  = RF_SW_PWR (LOW = enables FM8625H RF switch VDD)
//   GPIO14 = ANT_SEL   (HIGH = routes RF to external U.FL horizontal FPC dipole; LOW = internal chip antenna)
constexpr gpio_num_t rf_switch_power_pin = GPIO_NUM_3;
constexpr gpio_num_t rf_antenna_select_pin = GPIO_NUM_14;
constexpr std::int8_t wifi_max_tx_power_quarter_dbm = 82;  // 82 * 0.25 dBm = +20.5 dBm (112 mW max linear PA)

// I2C 7-bit addresses of all onboard & probe/crown sensors
constexpr std::uint8_t i2c_addr_as3935 = 0x03;
constexpr std::uint8_t i2c_addr_veml7700 = 0x10;
constexpr std::uint8_t i2c_addr_lis2mdl = 0x1E;
constexpr std::uint8_t i2c_addr_sht45 = 0x44;
constexpr std::uint8_t i2c_addr_bmp581 = 0x47;
constexpr std::uint8_t i2c_addr_ltr390 = 0x53;
constexpr std::uint8_t i2c_addr_sgp41 = 0x59;
constexpr std::uint8_t i2c_addr_scd41 = 0x62;
constexpr std::uint8_t i2c_addr_sen55 = 0x69;
constexpr std::uint8_t i2c_addr_lsm6ds3 = 0x6A;

constexpr bool usb_pin(gpio_num_t pin) {
    return pin == GPIO_NUM_12 || pin == GPIO_NUM_13;
}

constexpr bool pins_distinct() {
    const gpio_num_t pins[] = {
        vin_adc_pin, ntc_sense_pin, rain_adc_pin, heat_pwm_pin,
        i2c_sda_pin, i2c_scl_pin,   us_sel_a_pin, us_sel_b_pin,
        us_tx_pin,   us_rx_pin,     rs485_io_pin,
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

}  // namespace station::firmware::board
