#pragma once

#include <soc/soc_caps.h>

namespace station {
namespace firmware {

inline bool AnalogFrontEnd::pump(std::uint32_t timeout_ms) {
    if (!running_) {
        return false;
    }
    std::uint32_t got = 0;
    if (adc_continuous_read(handle_, frame_, sizeof(frame_), &got, timeout_ms) != ESP_OK) {
        return false;
    }
    for (std::uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= got; i += SOC_ADC_DIGI_RESULT_BYTES) {
        const auto* p = reinterpret_cast<const adc_digi_output_data_t*>(&frame_[i]);
        const int ch = static_cast<int>(p->type2.channel);
        const int raw = static_cast<int>(p->type2.data);
        const int mv = to_mv(ch, raw);
        if (ch == 0) {
            vin_sum_ += mv;
            ++vin_n_;
        } else if (ch == 1) {
            ntc_sum_ += mv;
            ++ntc_n_;
        }
    }
    return true;
}

}  // namespace firmware
}  // namespace station
