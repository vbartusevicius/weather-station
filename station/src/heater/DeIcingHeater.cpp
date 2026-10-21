#include "DeIcingHeater.hpp"

#include <algorithm>
#include <cmath>

namespace station {
namespace heater {

namespace {
float clamp_f(float v, float lo, float hi) {
    return std::min(hi, std::max(lo, v));
}
}  // namespace

float DeIcingHeaterController::decode_supply_voltage_v(std::uint16_t vin_adc_mv) {
    // 100k / 10k voltage divider: V_in = V_adc * (100 + 10) / 10 = 11.0 * V_adc
    return (static_cast<float>(vin_adc_mv) * 11.0F) / 1000.0F;
}

float DeIcingHeaterController::decode_ntc_temperature_c(std::uint16_t ntc_adc_mv) {
    // 10k pull-up to 3300 mV + 10k B=3950K NTC to GND
    const float v_mv = clamp_f(static_cast<float>(ntc_adc_mv), 160.0F, 3220.0F);
    const float r_ntc = 10000.0F * v_mv / (3300.0F - v_mv);
    constexpr float kBeta = 3950.0F;
    constexpr float kT0 = 298.15F;  // 25 C in Kelvin
    constexpr float kR0 = 10000.0F;
    const float inv_t = (1.0F / kT0) + (1.0F / kBeta) * std::log(r_ntc / kR0);
    return (1.0F / inv_t) - 273.15F;
}

HeaterControlOutput DeIcingHeaterController::update(const HeaterSensorInput& in) {
    const bool button = (in.ntc_adc_mv < kServiceBtnThresholdMv);
    const float reflector_c = button ? in.ambient_temp_c : decode_ntc_temperature_c(in.ntc_adc_mv);
    HeaterControlOutput out{};
    if (!button && in.ntc_adc_mv > kNtcOpenThresholdMv) {
        out.state = HeaterState::SensorFault;
        out.supply_voltage_v = in.rail_v;
        out.reflector_temp_c = reflector_c;
        return out;
    }
    out = control(in.rail_v, reflector_c, in.ambient_temp_c, in.icing_suspected);
    out.service_button_pressed = button;
    return out;
}

HeaterControlOutput DeIcingHeaterController::control(float rail_v, float reflector_c, float ambient_c, bool icing) {
    HeaterControlOutput out{};
    out.supply_voltage_v = rail_v;
    out.reflector_temp_c = reflector_c;
    if (out.supply_voltage_v < kMinSupplyV) {
        out.state = HeaterState::UndervoltageLockout;
        return out;
    }
    if (out.reflector_temp_c >= kReflectorCutoffTempC) {
        out.state = HeaterState::OvertempCutoff;
        return out;
    }

    const bool need_heat = (ambient_c <= kFreezeTriggerTempC || icing) && (out.reflector_temp_c < kReflectorTargetTempC);
    if (!need_heat) {
        out.state = HeaterState::Off;
        return out;
    }

    // Proportional temperature error scaling (0..1 across 4 C band)
    const float temp_err = clamp_f((kReflectorTargetTempC - out.reflector_temp_c) / 4.0F, 0.25F, 1.0F);
    const float desired_power_w = kTargetPowerW * temp_err;

    // Constant-wattage duty cycle on the +12V_HEAT rail (compensates the pass-through range and load droop):
    // P_full(V) = V^2 / R_heat => duty = desired_power_w / P_full(V)
    const float p_full_w = (out.supply_voltage_v * out.supply_voltage_v) / kHeaterResistanceOhm;
    const float duty = clamp_f(desired_power_w / std::max(0.5F, p_full_w), 0.0F, 1.0F);

    out.state = HeaterState::Heating;
    out.pwm_duty_percent = duty * 100.0F;
    out.actual_power_w = p_full_w * duty;
    return out;
}

}  // namespace heater
}  // namespace station
