#include <cstdio>

#include <driver/gpio.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "airquality/AirQuality.hpp"
#include "environment/MetCalculations.hpp"
#include "firmware/Board.hpp"
#include "firmware/WebAssets.hpp"
#include "heater/DeIcingHeater.hpp"
#include "rain/HapticRain.hpp"
#include "telemetry/HomeAssistantMqtt.hpp"
#include "wind/UltrasonicWind.hpp"

#ifndef STATION_FW_VERSION
#define STATION_FW_VERSION "26.10.16"
#endif

namespace {

station::telemetry::StationSnapshot g_snapshot{};
station::wind::WmoWindAggregator g_wind_agg{};
station::rain::HapticRainProcessor g_rain_proc{};

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
                           (1ULL << station::firmware::board::heat_pwm_pin);
    gpio_config(&io_conf);
}

}  // namespace

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    init_gpio();
    start_web_server();

    while (true) {
        g_snapshot.uptime_s = static_cast<std::uint32_t>(esp_timer_get_time() / 1000000ULL);
        g_snapshot.wind_raw = station::wind::UltrasonicWindSolver::solve({});
        g_wind_agg.push_sample(g_snapshot.wind_raw.speed_m_s, g_snapshot.wind_raw.direction_deg);
        g_snapshot.wind = g_wind_agg.summary();
        g_snapshot.rain = g_rain_proc.metrics();
        g_snapshot.atmo = station::environment::MetCalculator::compute(g_snapshot.atmo_raw);
        g_snapshot.aq = station::airquality::AirQualityEvaluator::evaluate(g_snapshot.aq_raw);
        g_snapshot.heater = station::heater::DeIcingHeaterController::update({});
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
