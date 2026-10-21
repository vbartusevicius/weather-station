#pragma once

#include <cstdint>

namespace station {
namespace heater {

enum class HeaterState : std::uint8_t {
    Off = 0,
    Heating = 1,
    UndervoltageLockout = 2,
    OvertempCutoff = 3,
};

struct HeaterSensorInput {
    float rail_v{5.0F};               // regulated +5V heater supply (ADS1115 AIN2)
    std::uint16_t ntc_adc_mv{1650};   // ADC1_CH1 (10k pull-up + 10k B3950 NTC: 25 C -> 1650 mV)
    float ambient_temp_c{10.0F};
    bool icing_suspected{false};
};

struct HeaterControlOutput {
    HeaterState state{HeaterState::Off};
    float supply_voltage_v{5.0F};     // +5V heater rail
    float reflector_temp_c{25.0F};
    float pwm_duty_percent{0.0F};
    float actual_power_w{0.0F};
    bool service_button_pressed{false};
};

class DeIcingHeaterController {
public:
    static constexpr float kHeaterResistanceOhm = 6.75F;  // 4x 27R 2512 wired 100% in parallel on reflector PCB, on +5V
    static constexpr float kTargetPowerW = 3.5F;          // 3.7 W available at 5.0 V (100 % PWM)
    static constexpr float kMinSupplyV = 4.5F;            // +5V rail sagging (buck overloaded / input below UVLO)
    static constexpr float kFreezeTriggerTempC = 2.5F;
    static constexpr float kReflectorTargetTempC = 6.5F;
    static constexpr float kReflectorCutoffTempC = 25.0F;
    static constexpr std::uint16_t kServiceBtnThresholdMv = 150;

    static float decode_supply_voltage_v(std::uint16_t vin_adc_mv);
    static float decode_ntc_temperature_c(std::uint16_t ntc_adc_mv);
    static HeaterControlOutput update(const HeaterSensorInput& in);
    // `rail_v` is the +5V heater supply; the heater no longer depends on the 12-32 V input.
    static HeaterControlOutput control(float rail_v, float reflector_c, float ambient_c, bool icing);
};

}  // namespace heater
}  // namespace station
