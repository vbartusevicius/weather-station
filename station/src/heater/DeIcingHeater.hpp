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
    std::uint16_t vin_adc_mv{2182};   // ADC1_CH0 (100k/10k divider: 24.0 V -> 2182 mV)
    std::uint16_t ntc_adc_mv{1650};   // ADC1_CH1 (10k pull-up + 10k B3950 NTC: 25 C -> 1650 mV)
    float ambient_temp_c{10.0F};
    bool icing_suspected{false};
};

struct HeaterControlOutput {
    HeaterState state{HeaterState::Off};
    float supply_voltage_v{24.0F};
    float reflector_temp_c{25.0F};
    float pwm_duty_percent{0.0F};
    float actual_power_w{0.0F};
    bool service_button_pressed{false};
};

class DeIcingHeaterController {
public:
    static constexpr float kHeaterResistanceOhm = 25.0F;  // 4x 100R 2512 wired 100% in parallel on reflector PCB
    static constexpr float kTargetPowerW = 4.0F;
    static constexpr float kMinSupplyV = 10.8F;
    static constexpr float kFreezeTriggerTempC = 2.5F;
    static constexpr float kReflectorTargetTempC = 6.5F;
    static constexpr float kReflectorCutoffTempC = 25.0F;
    static constexpr std::uint16_t kServiceBtnThresholdMv = 150;

    static float decode_supply_voltage_v(std::uint16_t vin_adc_mv);
    static float decode_ntc_temperature_c(std::uint16_t ntc_adc_mv);
    static HeaterControlOutput update(const HeaterSensorInput& in);
};

}  // namespace heater
}  // namespace station
