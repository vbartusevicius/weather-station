#include "Ultrasonic.hpp"

#include <algorithm>

#include <driver/gpio.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>

#include "../Board.hpp"

namespace station {
namespace firmware {

namespace {
constexpr std::uint32_t kRmtResolutionHz = 10000000;  // 0.1 us ticks
constexpr int kBurstCycles = 8;
constexpr float kMinTofUs = 200.0F;  // ignore crosstalk / ringing before the acoustic arrival
constexpr float kMaxTofUs = 520.0F;
rmt_symbol_word_t g_burst[kBurstCycles];
}  // namespace

bool IRAM_ATTR Ultrasonic::on_capture(mcpwm_cap_channel_handle_t ch, const mcpwm_capture_event_data_t* e, void* ctx) {
    auto* self = static_cast<Ultrasonic*>(ctx);
    if (ch == self->cap_tx_) {
        if (!self->tx_seen_.load()) {
            self->tx_tick_.store(e->cap_value);
            self->tx_seen_.store(true);
        }
    } else if (self->tx_seen_.load() && !self->rx_seen_.load()) {
        const std::uint32_t dt = e->cap_value - self->tx_tick_.load();
        const float us = static_cast<float>(dt) * 1e6F / static_cast<float>(self->cap_hz_);
        if (us >= kMinTofUs) {
            self->rx_tick_.store(e->cap_value);
            self->rx_seen_.store(true);
        }
    }
    return false;
}

bool Ultrasonic::begin(int tx_gpio, int rx_gpio, int sel_a_gpio, int sel_b_gpio) {
    sel_a_ = sel_a_gpio;
    sel_b_ = sel_b_gpio;
    for (int i = 0; i < kBurstCycles; ++i) {
        g_burst[i].level0 = 1;
        g_burst[i].duration0 = 125;  // 12.5 us high
        g_burst[i].level1 = 0;
        g_burst[i].duration1 = 125;  // 12.5 us low  => 40.0 kHz
    }

    rmt_tx_channel_config_t tx_cfg{};
    tx_cfg.gpio_num = static_cast<gpio_num_t>(tx_gpio);
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz = kRmtResolutionHz;
    tx_cfg.mem_block_symbols = 48;
    tx_cfg.trans_queue_depth = 2;
    tx_cfg.flags.io_loop_back = 1;  // keep the pad input path enabled for the MCPWM TX capture
    if (rmt_new_tx_channel(&tx_cfg, &tx_chan_) != ESP_OK) {
        return false;
    }
    rmt_copy_encoder_config_t enc_cfg{};
    if (rmt_new_copy_encoder(&enc_cfg, &copy_enc_) != ESP_OK || rmt_enable(tx_chan_) != ESP_OK) {
        return false;
    }

    mcpwm_capture_timer_config_t tcfg{};
    tcfg.group_id = 0;
    tcfg.clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT;
    if (mcpwm_new_capture_timer(&tcfg, &cap_timer_) != ESP_OK) {
        return false;
    }
    mcpwm_capture_channel_config_t ccfg{};
    ccfg.prescale = 1;
    ccfg.flags.pos_edge = 1;
    ccfg.flags.neg_edge = 0;
    ccfg.gpio_num = tx_gpio;
    if (mcpwm_new_capture_channel(cap_timer_, &ccfg, &cap_tx_) != ESP_OK) {
        return false;
    }
    ccfg.gpio_num = rx_gpio;
    if (mcpwm_new_capture_channel(cap_timer_, &ccfg, &cap_rx_) != ESP_OK) {
        return false;
    }
    mcpwm_capture_event_callbacks_t cbs{};
    cbs.on_cap = on_capture;
    mcpwm_capture_channel_register_event_callbacks(cap_tx_, &cbs, this);
    mcpwm_capture_channel_register_event_callbacks(cap_rx_, &cbs, this);
    mcpwm_capture_channel_enable(cap_tx_);
    mcpwm_capture_channel_enable(cap_rx_);
    mcpwm_capture_timer_enable(cap_timer_);
    mcpwm_capture_timer_start(cap_timer_);
    mcpwm_capture_timer_get_resolution(cap_timer_, &cap_hz_);
    ready_ = cap_hz_ > 0;
    return ready_;
}

bool Ultrasonic::measure_path(std::uint8_t path, float& tof_us) {
    gpio_set_level(static_cast<gpio_num_t>(sel_a_), path & 0x01);
    gpio_set_level(static_cast<gpio_num_t>(sel_b_), (path >> 1) & 0x01);
    ets_delay_us(300);  // mux + AFE settle, previous echo decay
    tx_seen_.store(false);
    rx_seen_.store(false);
    rmt_transmit_config_t tcfg{};
    if (rmt_transmit(tx_chan_, copy_enc_, g_burst, sizeof(g_burst), &tcfg) != ESP_OK) {
        return false;
    }
    rmt_tx_wait_all_done(tx_chan_, 5);
    for (int i = 0; i < 20 && !rx_seen_.load(); ++i) {
        ets_delay_us(50);
    }
    if (!tx_seen_.load() || !rx_seen_.load()) {
        return false;
    }
    const std::uint32_t dt = rx_tick_.load() - tx_tick_.load();
    tof_us = static_cast<float>(dt) * 1e6F / static_cast<float>(cap_hz_);
    return tof_us >= kMinTofUs && tof_us <= kMaxTofUs;
}

bool Ultrasonic::measure_quad(wind::TofQuad& out, int bursts, float fixed_delay_us) {
    if (!ready_) {
        return false;
    }
    const std::uint8_t paths[4] = {board::us_path_ns, board::us_path_sn, board::us_path_ew, board::us_path_we};
    float med[4]{};
    for (int p = 0; p < 4; ++p) {
        float v[8];
        int n = 0;
        for (int b = 0; b < std::min(bursts, 8); ++b) {
            float t = 0.0F;
            if (measure_path(paths[p], t)) {
                v[n++] = t;
            }
        }
        if (n == 0) {
            return false;
        }
        std::sort(v, v + n);
        med[p] = v[n / 2] - fixed_delay_us;
    }
    out = wind::TofQuad{};
    out.t_ns_us = med[0];
    out.t_sn_us = med[1];
    out.t_ew_us = med[2];
    out.t_we_us = med[3];
    return true;
}

}  // namespace firmware
}  // namespace station
