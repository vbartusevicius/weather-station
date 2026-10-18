#include <cstdio>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "airquality/AirQuality.hpp"
#include "environment/MetCalculations.hpp"
#include "firmware/Board.hpp"
#include "firmware/ButtonController.hpp"
#include "firmware/WebAssets.hpp"
#include "heater/DeIcingHeater.hpp"
#include "rain/HapticRain.hpp"
#include "rs485/Rs485Protocol.hpp"
#include "telemetry/HomeAssistantMqtt.hpp"
#include "wind/UltrasonicWind.hpp"

#ifndef STATION_FW_VERSION
#define STATION_FW_VERSION "26.10.16"
#endif

namespace {

station::telemetry::StationSnapshot g_snapshot{};
station::wind::WmoWindAggregator g_wind_agg{};
station::rain::HapticRainProcessor g_rain_proc{};
station::firmware::ButtonController g_btn_ctrl{};

esp_err_t handle_index(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, station::firmware::web_assets::index_html_data,
                           static_cast<ssize_t>(station::firmware::web_assets::index_html_size));
}

esp_err_t handle_css(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/css; charset=utf-8");
    return httpd_resp_send(req, station::firmware::web_assets::app_css_data,
                           static_cast<ssize_t>(station::firmware::web_assets::app_css_size));
}

esp_err_t handle_js(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    return httpd_resp_send(req, station::firmware::web_assets::app_js_data,
                           static_cast<ssize_t>(station::firmware::web_assets::app_js_size));
}

esp_err_t handle_api_state(httpd_req_t* req) {
    const std::string json = station::telemetry::TelemetryFormatter::build_state_json(g_snapshot, STATION_FW_VERSION);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

void start_web_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &config) == ESP_OK) {
        const httpd_uri_t u_idx = {"/", HTTP_GET, handle_index, nullptr};
        const httpd_uri_t u_css = {"/app.css", HTTP_GET, handle_css, nullptr};
        const httpd_uri_t u_js = {"/app.js", HTTP_GET, handle_js, nullptr};
        const httpd_uri_t u_api = {"/api/state", HTTP_GET, handle_api_state, nullptr};
        httpd_register_uri_handler(server, &u_idx);
        httpd_register_uri_handler(server, &u_css);
        httpd_register_uri_handler(server, &u_js);
        httpd_register_uri_handler(server, &u_api);
    }
}

void init_gpio() {
    gpio_config_t io_conf{};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << station::firmware::board::us_sel_a_pin) |
                           (1ULL << station::firmware::board::us_sel_b_pin) |
                           (1ULL << station::firmware::board::us_tx_pin) |
                           (1ULL << station::firmware::board::heat_pwm_pin) |
                           (1ULL << station::firmware::board::rf_switch_power_pin) |
                           (1ULL << station::firmware::board::rf_antenna_select_pin);
    gpio_config(&io_conf);

    // Enable XIAO ESP32-C6 FM8625H RF switch (GPIO3 = LOW) and route RF to external U.FL FPC dipole (GPIO14 = HIGH)
    gpio_set_level(station::firmware::board::rf_switch_power_pin, 0);
    gpio_set_level(station::firmware::board::rf_antenna_select_pin, 1);

    gpio_config_t btn_conf{};
    btn_conf.intr_type = GPIO_INTR_DISABLE;
    btn_conf.mode = GPIO_MODE_INPUT;
    btn_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    btn_conf.pin_bit_mask = (1ULL << station::firmware::board::fab_sw_pin);
    gpio_config(&btn_conf);
}

void init_rs485_uart() {
    uart_config_t uart_cfg{};
    uart_cfg.baud_rate = static_cast<int>(station::rs485::Rs485Protocol::kDefaultBaudRate);
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_DISABLE;
    uart_cfg.stop_bits = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_DEFAULT;
    uart_driver_install(UART_NUM_1, 512, 512, 0, nullptr, 0);
    uart_param_config(UART_NUM_1, &uart_cfg);
    uart_set_pin(UART_NUM_1, station::firmware::board::rs485_io_pin,
                 station::firmware::board::rs485_io_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void poll_and_publish_rs485() {
    char rx_buf[64];
    const int n_rx = uart_read_bytes(UART_NUM_1, rx_buf, sizeof(rx_buf) - 1, 0);
    if (n_rx > 0) {
        rx_buf[n_rx] = '\0';
        const auto cmd = station::rs485::Rs485Protocol::parse_command_frame(rx_buf, static_cast<std::size_t>(n_rx));
        if (cmd == station::rs485::CommandType::Ping) {
            static constexpr const char kPong[] = "$AM,PONG*0000\r\n";
            uart_write_bytes(UART_NUM_1, kPong, sizeof(kPong) - 1);
        } else if (cmd == station::rs485::CommandType::ResetDailyRain) {
            g_rain_proc.reset_daily();
        }
    }
    const std::string frame = station::rs485::Rs485Protocol::encode_telemetry_frame(g_snapshot);
    if (!frame.empty()) {
        uart_write_bytes(UART_NUM_1, frame.c_str(), frame.size());
    }
}

}  // namespace

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    init_gpio();
    init_rs485_uart();
    start_web_server();

    while (true) {
        const std::uint32_t now_ms = static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL);
        const bool btn_pressed = (gpio_get_level(station::firmware::board::fab_sw_pin) == 0);
        const auto btn_action = g_btn_ctrl.update(btn_pressed, now_ms);
        if (btn_action == station::firmware::ButtonAction::FactoryReset) {
            nvs_flash_erase();
            esp_restart();
        } else if (btn_action == station::firmware::ButtonAction::Reboot) {
            esp_restart();
        }

        g_snapshot.uptime_s = now_ms / 1000U;
        g_snapshot.wind_raw = station::wind::UltrasonicWindSolver::solve({});
        g_wind_agg.push_sample(g_snapshot.wind_raw.speed_m_s, g_snapshot.wind_raw.direction_deg);
        g_snapshot.wind = g_wind_agg.summary();
        g_snapshot.rain = g_rain_proc.metrics();
        g_snapshot.atmo = station::environment::MetCalculator::compute(g_snapshot.atmo_raw);
        g_snapshot.aq = station::airquality::AirQualityEvaluator::evaluate(g_snapshot.aq_raw);
        g_snapshot.heater = station::heater::DeIcingHeaterController::update({});
        poll_and_publish_rs485();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
