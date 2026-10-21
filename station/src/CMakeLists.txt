file(GLOB_RECURSE station_sources "*.cpp")

idf_component_register(
    SRCS ${station_sources}
    INCLUDE_DIRS "." "../../shared/include"
    REQUIRES nvs_flash freertos esp_app_format esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_uart esp_adc esp_netif esp_wifi esp_event esp_http_server joltwallet__littlefs app_update mqtt lwip esp_timer esp_hw_support
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
