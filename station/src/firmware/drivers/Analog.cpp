#include "Analog.hpp"

#include <algorithm>

#include <driver/ledc.h>
#include <esp_adc/adc_cali_scheme.h>

namespace station {
namespace firmware {

bool AnalogFrontEnd::begin() {
    adc_continuous_handle_cfg_t hcfg{};
    hcfg.max_store_buf_size = 4096;
    hcfg.conv_frame_size = sizeof(frame_);
    if (adc_continuous_new_handle(&hcfg, &handle_) != ESP_OK) {
        return false;
    }
    // Board: ADC1_CH0 = VIN_ADC, ADC1_CH1 = NTC_SENSE (rain is digitised on the crown STM32)
    static const std::uint8_t kOrder[2] = {0, 1};
    adc_digi_pattern_config_t pattern[2]{};
    for (int i = 0; i < 2; ++i) {
        pattern[i].atten = ADC_ATTEN_DB_12;
        pattern[i].channel = kOrder[i];
        pattern[i].unit = ADC_UNIT_1;
        pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }
    adc_continuous_config_t cfg{};
    cfg.pattern_num = 2;
    cfg.adc_pattern = pattern;
    cfg.sample_freq_hz = kTotalRateHz;
    cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    if (adc_continuous_config(handle_, &cfg) != ESP_OK) {
        return false;
    }
    for (int ch = 0; ch < 2; ++ch) {
        adc_cali_curve_fitting_config_t cc{};
        cc.unit_id = ADC_UNIT_1;
        cc.chan = static_cast<adc_channel_t>(ch);
        cc.atten = ADC_ATTEN_DB_12;
        cc.bitwidth = ADC_BITWIDTH_DEFAULT;
        adc_cali_create_scheme_curve_fitting(&cc, &cali_[ch]);
    }
    running_ = adc_continuous_start(handle_) == ESP_OK;
    return running_;
}

int AnalogFrontEnd::to_mv(int channel, int raw) const {
    int mv = 0;
    if (channel >= 0 && channel < 2 && cali_[channel] != nullptr && adc_cali_raw_to_voltage(cali_[channel], raw, &mv) == ESP_OK) {
        return mv;
    }
    return raw * 3300 / 4095;
}

bool AnalogFrontEnd::take_means(float& vin_mv, float& ntc_mv) {
    if (vin_n_ == 0 || ntc_n_ == 0) {
        return false;
    }
    vin_mv = static_cast<float>(vin_sum_ / vin_n_);
    ntc_mv = static_cast<float>(ntc_sum_ / ntc_n_);
    vin_sum_ = ntc_sum_ = 0.0;
    vin_n_ = ntc_n_ = 0;
    return true;
}

bool HeaterPwm::begin(int gpio) {
    ledc_timer_config_t t{};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num = LEDC_TIMER_0;
    t.freq_hz = 1000;
    t.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&t) != ESP_OK) {
        return false;
    }
    ledc_channel_config_t c{};
    c.gpio_num = gpio;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = LEDC_CHANNEL_0;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 0;
    c.hpoint = 0;
    ready_ = ledc_channel_config(&c) == ESP_OK;
    return ready_;
}

void HeaterPwm::set_duty_pct(float pct) {
    if (!ready_) {
        return;
    }
    const auto duty = static_cast<std::uint32_t>(std::min(100.0F, std::max(0.0F, pct)) * 1023.0F / 100.0F);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

}  // namespace firmware
}  // namespace station
