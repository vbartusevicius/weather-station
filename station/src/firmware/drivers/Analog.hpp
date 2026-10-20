#pragma once

#include <cstdint>

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_continuous.h>

namespace station {
namespace firmware {

// ADC1 continuous (DMA) scan: RAIN_ADC at ~16 kHz for piezo pulse capture, VIN_ADC and NTC_SENSE
// interleaved at ~2 kHz. Calibrated to millivolts with the chip's curve-fitting scheme.
class AnalogFrontEnd {
public:
    static constexpr std::uint32_t kTotalRateHz = 20000;  // 8 rain + 1 vin + 1 ntc per 10 conversions

    bool begin();
    // Blocks up to `timeout_ms` for a DMA frame. Calls on_rain(mv) for every rain sample and
    // accumulates VIN / NTC means retrieved with take_means().
    template <typename F>
    bool pump(std::uint32_t timeout_ms, F&& on_rain);
    bool take_means(float& vin_mv, float& ntc_mv);
    bool running() const { return running_; }

private:
    int to_mv(int channel, int raw) const;
    adc_continuous_handle_t handle_{nullptr};
    adc_cali_handle_t cali_[3]{nullptr, nullptr, nullptr};
    bool running_{false};
    double vin_sum_{0.0};
    double ntc_sum_{0.0};
    std::uint32_t vin_n_{0};
    std::uint32_t ntc_n_{0};
    std::uint8_t frame_[256]{};
};

// LEDC PWM on HEAT_PWM driving the AO3400A reflector heater MOSFET.
class HeaterPwm {
public:
    bool begin(int gpio);
    void set_duty_pct(float pct);

private:
    bool ready_{false};
};

}  // namespace firmware
}  // namespace station

#include "Analog.inl"
