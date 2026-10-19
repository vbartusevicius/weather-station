#pragma once

#if __has_include(<esp_http_server.h>)
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/adc_types.h>
#include <nvs.h>
#include <nvs_flash.h>
#else
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_NVS_NO_FREE_PAGES = 0x110D;
constexpr esp_err_t ESP_ERR_NVS_NEW_VERSION_FOUND = 0x1110;

enum gpio_num_t {
    GPIO_NUM_0 = 0,
    GPIO_NUM_1 = 1,
    GPIO_NUM_2 = 2,
    GPIO_NUM_3 = 3,
    GPIO_NUM_9 = 9,
    GPIO_NUM_12 = 12,
    GPIO_NUM_13 = 13,
    GPIO_NUM_14 = 14,
    GPIO_NUM_16 = 16,
    GPIO_NUM_17 = 17,
    GPIO_NUM_18 = 18,
    GPIO_NUM_19 = 19,
    GPIO_NUM_20 = 20,
    GPIO_NUM_21 = 21,
    GPIO_NUM_22 = 22,
    GPIO_NUM_23 = 23,
};

enum adc_channel_t {
    ADC_CHANNEL_0 = 0,
    ADC_CHANNEL_1 = 1,
    ADC_CHANNEL_2 = 2,
};

enum gpio_int_type_t { GPIO_INTR_DISABLE = 0 };
enum gpio_mode_t { GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2 };
enum gpio_pullup_t { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 };

struct gpio_config_t {
    std::uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    int pull_down_en;
    gpio_int_type_t intr_type;
};

inline esp_err_t gpio_config(const gpio_config_t*) { return ESP_OK; }
inline esp_err_t gpio_set_level(gpio_num_t, std::uint32_t) { return ESP_OK; }
inline int gpio_get_level(gpio_num_t) { return 1; }

enum uart_port_t { UART_NUM_1 = 1 };
enum uart_word_length_t { UART_DATA_8_BITS = 3 };
enum uart_parity_t { UART_PARITY_DISABLE = 0 };
enum uart_stop_bits_t { UART_STOP_BITS_1 = 1 };
enum uart_hw_flowcontrol_t { UART_HW_FLOWCTRL_DISABLE = 0 };
enum uart_sclk_t { UART_SCLK_DEFAULT = 0 };
constexpr int UART_PIN_NO_CHANGE = -1;

struct uart_config_t {
    int baud_rate;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uart_hw_flowcontrol_t flow_ctrl;
    std::uint8_t rx_flow_ctrl_thresh;
    uart_sclk_t source_clk;
};

inline esp_err_t uart_driver_install(uart_port_t, int, int, int, void*, int) { return ESP_OK; }
inline esp_err_t uart_param_config(uart_port_t, const uart_config_t*) { return ESP_OK; }
inline esp_err_t uart_set_pin(uart_port_t, int, int, int, int) { return ESP_OK; }
inline int uart_read_bytes(uart_port_t, void*, std::uint32_t, std::uint32_t) { return 0; }
inline int uart_write_bytes(uart_port_t, const void*, std::size_t) { return 0; }

using httpd_handle_t = void*;
enum httpd_method_t { HTTP_GET = 1, HTTP_POST = 3 };
struct httpd_req_t {
    httpd_handle_t handle;
    int method;
    const char* uri;
    std::size_t content_len;
    void* user_ctx;
};

struct httpd_config_t {
    unsigned max_uri_handlers{8};
};
inline httpd_config_t HTTPD_DEFAULT_CONFIG() { return httpd_config_t{}; }

struct httpd_uri_t {
    const char* uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t* r);
    void* user_ctx;
    bool is_websocket;
    bool handle_ws_control_frames;
    const char* supported_subprotocol;
};

enum httpd_ws_type_t { HTTPD_WS_TYPE_TEXT = 1 };
struct httpd_ws_frame_t {
    bool final;
    bool fragmented;
    httpd_ws_type_t type;
    std::uint8_t* payload;
    std::size_t len;
};

inline esp_err_t httpd_start(httpd_handle_t*, const httpd_config_t*) { return ESP_OK; }
inline esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t*) { return ESP_OK; }
inline esp_err_t httpd_resp_set_type(httpd_req_t*, const char*) { return ESP_OK; }
inline esp_err_t httpd_resp_send(httpd_req_t*, const char*, ssize_t) { return ESP_OK; }
inline esp_err_t httpd_resp_send_chunk(httpd_req_t*, const char*, ssize_t) { return ESP_OK; }
inline esp_err_t httpd_resp_sendstr(httpd_req_t*, const char*) { return ESP_OK; }
inline int httpd_req_recv(httpd_req_t*, char*, std::size_t) { return 0; }
inline int httpd_req_to_sockfd(httpd_req_t*) { return 0; }
inline esp_err_t httpd_ws_recv_frame(httpd_req_t*, httpd_ws_frame_t*, std::size_t) { return ESP_OK; }
inline esp_err_t httpd_ws_send_frame_async(httpd_handle_t, int, httpd_ws_frame_t*) { return ESP_OK; }

inline esp_err_t nvs_flash_init() { return ESP_OK; }
inline esp_err_t nvs_flash_erase() { return ESP_OK; }
inline void esp_restart() {}
inline std::int64_t esp_timer_get_time() { return 0; }
inline std::uint32_t pdMS_TO_TICKS(std::uint32_t ms) { return ms; }
inline void vTaskDelay(std::uint32_t) {}
#endif
