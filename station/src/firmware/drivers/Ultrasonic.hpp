#pragma once

#include <atomic>
#include <cstdint>

#include <driver/mcpwm_cap.h>
#include <driver/rmt_tx.h>

#include "../../wind/UltrasonicWind.hpp"

namespace station {
namespace firmware {

// Reciprocal ultrasonic time-of-flight front end:
//   - CD4052 mux (US_SEL_A/B) picks the TX / RX transducer pair,
//   - RMT emits an 8-cycle 40 kHz burst on US_TX,
//   - one MCPWM capture timer timestamps the first TX edge (loop-back of US_TX) and the first
//     LM393 zero-crossing on US_RX, so ToF = rx - tx is free of interrupt latency.
class Ultrasonic {
public:
    bool begin(int tx_gpio, int rx_gpio, int sel_a_gpio, int sel_b_gpio);
    // Measures all four paths (N->S, S->N, E->W, W->E), `bursts` times each, median per path.
    // `fixed_delay_us` = transducer + AFE group delay (calibrated), subtracted from every path.
    bool measure_quad(wind::TofQuad& out, int bursts, float fixed_delay_us);
    bool ready() const { return ready_; }

private:
    bool measure_path(std::uint8_t path, float& tof_us);
    static bool on_capture(mcpwm_cap_channel_handle_t ch, const mcpwm_capture_event_data_t* e, void* ctx);

    rmt_channel_handle_t tx_chan_{nullptr};
    rmt_encoder_handle_t copy_enc_{nullptr};
    mcpwm_cap_timer_handle_t cap_timer_{nullptr};
    mcpwm_cap_channel_handle_t cap_tx_{nullptr};
    mcpwm_cap_channel_handle_t cap_rx_{nullptr};
    std::uint32_t cap_hz_{0};
    int sel_a_{-1};
    int sel_b_{-1};
    bool ready_{false};

    std::atomic<std::uint32_t> tx_tick_{0};
    std::atomic<std::uint32_t> rx_tick_{0};
    std::atomic<bool> tx_seen_{false};
    std::atomic<bool> rx_seen_{false};
};

}  // namespace firmware
}  // namespace station
