#pragma once

#include <cstdint>

namespace station {
namespace heater {

enum class HeaterState : std::uint8_t {
    Off = 0,
    Heating = 1,
    UndervoltageLockout = 2,
    OvertempCutoff = 3,
    SensorFault = 4,  // reflector NTC open (reads colder than -45 C): never heat without temperature feedback
};

struct HeaterSensorInput {
    float rail_v{12.0F};              // +12V_HEAT heater supply (ADS1115 AIN2)
    std::uint16_t ntc_adc_mv{1650};   // ADC1_CH1 (10k pull-up + 10k B3950 NTC: 25 C -> 1650 mV)
    float ambient_temp_c{10.0F};
    bool icing_suspected{false};
};

struct HeaterControlOutput {
    HeaterState state{HeaterState::Off};
    float supply_voltage_v{12.0F};    // +12V_HEAT heater rail
    float reflector_temp_c{25.0F};
    float pwm_duty_percent{0.0F};
    float actual_power_w{0.0F};
    bool service_button_pressed{false};
};

class DeIcingHeaterController {
public:
    static constexpr float kHeaterResistanceOhm = 30.0F;  // 4x 120R 2512 2 W wired 100% in parallel, on +12V_HEAT
    static constexpr float kTargetPowerW = 4.5F;          // 4.8 W available at 12.0 V (100 % PWM), 3.6 W at 10.4 V
    static constexpr float kMinSupplyV = 9.0F;            // rail sagging (input far below 12 V / buck overloaded)
    static constexpr float kFreezeTriggerTempC = 2.5F;
    static constexpr float kReflectorTargetTempC = 6.5F;
    static constexpr float kReflectorCutoffTempC = 25.0F;
    static constexpr std::uint16_t kServiceBtnThresholdMv = 150;
    static constexpr std::uint16_t kNtcOpenThresholdMv = 3270;  // -40 C reads 3220 mV; an open NTC reads ~3300 mV

    static float decode_supply_voltage_v(std::uint16_t vin_adc_mv);
    static float decode_ntc_temperature_c(std::uint16_t ntc_adc_mv);
    static HeaterControlOutput update(const HeaterSensorInput& in);
    // `rail_v` is the +12V_HEAT supply: 12.0 V regulated, VIN - ~1 V when the input is below ~13.5 V.
    static HeaterControlOutput control(float rail_v, float reflector_c, float ambient_c, bool icing);
};

}  // namespace heater
}  // namespace station
