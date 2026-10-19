#include <cstdio>
#include <cstring>

#include <ArduinoJson.h>

#include "../airquality/AirQuality.hpp"
#include "../environment/MetCalculations.hpp"
#include "../heater/DeIcingHeater.hpp"
#include "../rain/HapticRain.hpp"
#include "../rs485/Rs485Protocol.hpp"
#include "../telemetry/HomeAssistantMqtt.hpp"
#include "../wind/UltrasonicWind.hpp"
#include "Board.hpp"
#include "ButtonController.hpp"
#include "EspIdfHostStubs.hpp"
#include "WebAssets.hpp"

#ifndef STATION_FW_VERSION
#define STATION_FW_VERSION "26.10.19"
#endif

namespace {

struct StationConfig {
    float altitude_m{65.0F};
    float declination_deg{6.2F};
    float dt_ns_offset_us{0.0F};
    float dt_ew_offset_us{0.0F};
    char mqtt_uri[64]{"mqtt://192.168.1.10:1883"};
    char ha_prefix[32]{"homeassistant"};
};

station::telemetry::StationSnapshot g_snapshot{};
station::wind::WmoWindAggregator g_wind_agg{};
station::rain::HapticRainProcessor g_rain_proc{};
station::environment::BarographTracker g_baro_tracker{};
station::environment::EnvLongevityManager g_longevity{};
station::firmware::ButtonController g_btn_ctrl{};
StationConfig g_config{};
httpd_handle_t g_http_server = nullptr;
int g_ws_client_fds[4]{-1, -1, -1, -1};

esp_err_t serve_asset(httpd_req_t* req, const char* fs_path, const char* mime,
                      const char* fallback_data, std::size_t fallback_size) {
    httpd_resp_set_type(req, mime);
    FILE* f = std::fopen(fs_path, "rb");
    if (f != nullptr) {
        char chunk[512];
        std::size_t n = 0;
        while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) {
            if (httpd_resp_send_chunk(req, chunk, static_cast<ssize_t>(n)) != ESP_OK) {
                std::fclose(f);
                return ESP_FAIL;
            }
        }
        std::fclose(f);
        return httpd_resp_send_chunk(req, nullptr, 0);
    }
    return httpd_resp_send(req, fallback_data, static_cast<ssize_t>(fallback_size));
}

esp_err_t handle_index(httpd_req_t* req) {
    return serve_asset(req, "/littlefs/index.html", "text/html; charset=utf-8",
                       station::firmware::web_assets::index_html_data,
                       station::firmware::web_assets::index_html_size);
}

esp_err_t handle_css(httpd_req_t* req) {
    return serve_asset(req, "/littlefs/app.css", "text/css; charset=utf-8",
                       station::firmware::web_assets::app_css_data,
                       station::firmware::web_assets::app_css_size);
}

esp_err_t handle_js(httpd_req_t* req) {
    return serve_asset(req, "/littlefs/app.js", "application/javascript; charset=utf-8",
                       station::firmware::web_assets::app_js_data,
                       station::firmware::web_assets::app_js_size);
}

struct HistoryEntry {
    float temp{20.0F};
    float dew{10.0F};
    float feels{20.0F};
    float wind{0.0F};
    float gust{0.0F};
    float qnh{1013.25F};
    float rain_rate{0.0F};
    float hail_lwe_rate{0.0F};
};

constexpr std::size_t kHistorySlots = 48;
HistoryEntry g_history[kHistorySlots]{};
std::size_t g_history_head = 0;
std::size_t g_history_count = 0;

void push_history_point(const station::telemetry::StationSnapshot& s) {
    g_history[g_history_head] = HistoryEntry{
        s.atmo.compensated_temp_c,
        s.atmo.dew_point_c,
        s.atmo.feels_like_c,
        s.wind.avg_10min_m_s,
        s.wind.gust_3s_m_s,
        s.atmo.sea_level_pressure_hpa,
        s.rain.rate_mm_h,
        s.rain.hail_rate_mm_h,
    };
    g_history_head = (g_history_head + 1U) % kHistorySlots;
    if (g_history_count < kHistorySlots) {
        ++g_history_count;
    }
}

esp_err_t handle_api_state(httpd_req_t* req) {
    const std::string json = station::telemetry::TelemetryFormatter::build_state_json(g_snapshot, STATION_FW_VERSION);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t handle_api_history(httpd_req_t* req) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    const std::size_t n_pts = g_history_count;
    const std::size_t start = (n_pts < kHistorySlots) ? 0U : g_history_head;
    for (std::size_t i = 0; i < n_pts; ++i) {
        const HistoryEntry& e = g_history[(start + i) % kHistorySlots];
        JsonObject item = arr.add<JsonObject>();
        item["temp"] = e.temp;
        item["dew"] = e.dew;
        item["feels"] = e.feels;
        item["wind"] = e.wind;
        item["gust"] = e.gust;
        item["qnh"] = e.qnh;
        item["rain_rate"] = e.rain_rate;
        item["hail_lwe_rate"] = e.hail_lwe_rate;
    }
    std::string json;
    serializeJson(doc, json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t handle_api_config_get(httpd_req_t* req) {
    JsonDocument doc;
    doc["altitude_m"] = g_config.altitude_m;
    doc["declination_deg"] = g_config.declination_deg;
    doc["dt_ns_offset_us"] = g_config.dt_ns_offset_us;
    doc["dt_ew_offset_us"] = g_config.dt_ew_offset_us;
    doc["mqtt_uri"] = g_config.mqtt_uri;
    doc["ha_prefix"] = g_config.ha_prefix;
    std::string json;
    serializeJson(doc, json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

esp_err_t handle_api_action_post(httpd_req_t* req) {
    char body[192]{};
    const int n = httpd_req_recv(req, body, sizeof(body) - 1);
    JsonDocument resp;
    resp["ok"] = true;
    if (n > 0) {
        body[n] = '\0';
        JsonDocument req_doc;
        if (deserializeJson(req_doc, body) == DeserializationError::Ok) {
            const char* action = req_doc["action"] | "";
            resp["action"] = action;
            if (std::strstr(action, "reset_rain") != nullptr) {
                g_rain_proc.reset_daily();
            } else if (std::strstr(action, "reboot") != nullptr) {
                std::string out;
                serializeJson(resp, out);
                httpd_resp_sendstr(req, out.c_str());
                esp_restart();
            }
        }
    }
    std::string out;
    serializeJson(resp, out);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, out.c_str(), static_cast<ssize_t>(out.size()));
}

esp_err_t handle_ws(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        const int fd = httpd_req_to_sockfd(req);
        for (int& slot : g_ws_client_fds) {
            if (slot == -1 || slot == fd) {
                slot = fd;
                break;
            }
        }
        return ESP_OK;
    }
    httpd_ws_frame_t pkt{};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    if (httpd_ws_recv_frame(req, &pkt, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void broadcast_ws_state() {
    if (g_http_server == nullptr) {
        return;
    }
    const std::string json = station::telemetry::TelemetryFormatter::build_state_json(g_snapshot, STATION_FW_VERSION);
    httpd_ws_frame_t pkt{};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.payload = reinterpret_cast<std::uint8_t*>(const_cast<char*>(json.c_str()));
    pkt.len = json.size();
    for (int& fd : g_ws_client_fds) {
        if (fd != -1) {
            if (httpd_ws_send_frame_async(g_http_server, fd, &pkt) != ESP_OK) {
                fd = -1;
            }
        }
    }
}

void start_web_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    if (httpd_start(&g_http_server, &config) == ESP_OK) {
        const httpd_uri_t u_idx = {"/", HTTP_GET, handle_index, nullptr, false, false, nullptr};
        const httpd_uri_t u_css = {"/app.css", HTTP_GET, handle_css, nullptr, false, false, nullptr};
        const httpd_uri_t u_js = {"/app.js", HTTP_GET, handle_js, nullptr, false, false, nullptr};
        const httpd_uri_t u_api = {"/api/state", HTTP_GET, handle_api_state, nullptr, false, false, nullptr};
        const httpd_uri_t u_hist = {"/api/history", HTTP_GET, handle_api_history, nullptr, false, false, nullptr};
        const httpd_uri_t u_cfg = {"/api/config", HTTP_GET, handle_api_config_get, nullptr, false, false, nullptr};
        const httpd_uri_t u_act = {"/api/action", HTTP_POST, handle_api_action_post, nullptr, false, false, nullptr};
        const httpd_uri_t u_ws = {"/ws", HTTP_GET, handle_ws, nullptr, true, false, nullptr};
        httpd_register_uri_handler(g_http_server, &u_idx);
        httpd_register_uri_handler(g_http_server, &u_css);
        httpd_register_uri_handler(g_http_server, &u_js);
        httpd_register_uri_handler(g_http_server, &u_api);
        httpd_register_uri_handler(g_http_server, &u_hist);
        httpd_register_uri_handler(g_http_server, &u_cfg);
        httpd_register_uri_handler(g_http_server, &u_act);
        httpd_register_uri_handler(g_http_server, &u_ws);
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

    std::uint32_t last_baro_slot_s = 0;
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
        const station::wind::ZeroWindCalibration cal{g_config.dt_ns_offset_us, g_config.dt_ew_offset_us};
        g_snapshot.wind_raw = station::wind::UltrasonicWindSolver::solve({}, {}, cal);
        g_wind_agg.push_sample(g_snapshot.wind_raw.speed_m_s, g_snapshot.wind_raw.direction_deg);
        g_snapshot.wind = g_wind_agg.summary();
        g_snapshot.rain = g_rain_proc.metrics();
        g_snapshot.atmo_raw.altitude_m = g_config.altitude_m;
        g_snapshot.atmo_raw.wind_speed_m_s = g_snapshot.wind.current_speed_m_s;
        g_snapshot.atmo_raw.wind_dir_deg = g_snapshot.wind.current_dir_deg;
        g_snapshot.atmo_raw.pressure_3h_delta_hpa = g_baro_tracker.delta_3h_hpa();
        g_snapshot.atmo = station::environment::MetCalculator::compute(g_snapshot.atmo_raw);
        if (g_snapshot.uptime_s == 0 || (g_snapshot.uptime_s - last_baro_slot_s) >= 900U) {
            g_baro_tracker.push_qnh(g_snapshot.atmo.sea_level_pressure_hpa);
            last_baro_slot_s = g_snapshot.uptime_s;
        }
        g_longevity.tick_second(g_snapshot.atmo.compensated_temp_c, g_snapshot.atmo_raw.humidity_rh,
                                (g_snapshot.uptime_s % 60U) == 0U);
        g_snapshot.aq = station::airquality::AirQualityEvaluator::evaluate(g_snapshot.aq_raw);
        g_snapshot.heater = station::heater::DeIcingHeaterController::update({});
        if ((g_snapshot.uptime_s % 120U) == 0U || g_history_count == 0U) {
            push_history_point(g_snapshot);
        }
        broadcast_ws_state();
        poll_and_publish_rs485();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
