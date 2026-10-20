#pragma once

#include <cstdint>
#include <string>

#include "../../core/Config.hpp"
#include "../../telemetry/HomeAssistantMqtt.hpp"

namespace station {
namespace firmware {

// Wi-Fi (station with fallback provisioning AP), SNTP wall clock + POSIX time zone, MQTT client.
class Network {
public:
    // Starts STA if credentials exist; otherwise (or after 60 s without an IP) opens the
    // "AeroMet-XXXX" setup access point (WPA2, password "aeromet-setup") at 192.168.4.1.
    void begin(const core::StationConfig& cfg);
    void tick_1s(const core::StationConfig& cfg);  // AP fallback, MQTT (re)connect

    telemetry::WifiInfo wifi_info() const;

    // Wall clock: valid after SNTP sync (or a manual set from the Web UI when offline).
    static void apply_timezone(const char* tz_posix);
    static bool clock_valid();
    static void set_clock_from_browser(std::int64_t unix_s);
    static std::int64_t unix_now();
    static std::int32_t utc_offset_s(std::int64_t unix_s);

    bool mqtt_connected() const { return mqtt_connected_; }
    void mqtt_publish_state(const std::string& json);
    const char* node_id() const { return node_id_; }

private:
    void start_ap();
    void start_mqtt(const core::StationConfig& cfg);
    static void on_wifi_event(void* arg, const char* base, std::int32_t id, void* data);
    static void on_mqtt_event(void* arg, const char* base, std::int32_t id, void* data);
    void publish_discovery(const core::StationConfig& cfg);

    void* sta_netif_{nullptr};
    void* ap_netif_{nullptr};
    void* mqtt_{nullptr};
    bool have_creds_{false};
    bool got_ip_{false};
    bool ap_on_{false};
    bool mqtt_connected_{false};
    bool discovery_sent_{false};
    std::uint32_t seconds_without_ip_{0};
    char ip_[16]{""};
    char node_id_[16]{"roof"};
    char mqtt_uri_[64]{""};
    char ha_prefix_[32]{"homeassistant"};
};

}  // namespace firmware
}  // namespace station
