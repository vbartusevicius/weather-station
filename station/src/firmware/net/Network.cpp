#include "Network.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include <esp_event.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_wifi.h>
#include <mqtt_client.h>

#include "../Board.hpp"

namespace station {
namespace firmware {

namespace {
bool g_manual_clock = false;
volatile bool g_sntp_synced = false;
void on_time_sync(struct timeval* /*tv*/) { g_sntp_synced = true; }
constexpr std::int64_t kMinValidUnix = 1704067200;  // 2024-01-01: anything earlier = clock never set

std::int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::int64_t tm_to_seconds(const std::tm& t) {
    return days_from_civil(t.tm_year + 1900, static_cast<unsigned>(t.tm_mon + 1), static_cast<unsigned>(t.tm_mday)) * 86400 +
           t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
}
}  // namespace

void Network::apply_timezone(const char* tz_posix) {
    setenv("TZ", (tz_posix != nullptr && tz_posix[0] != '\0') ? tz_posix : "UTC0", 1);
    tzset();
}

bool Network::clock_valid() {
    return g_manual_clock || g_sntp_synced;
}

void Network::set_clock_from_browser(std::int64_t unix_s) {
    if (unix_s < kMinValidUnix || g_sntp_synced) {
        return;  // SNTP time always wins over a browser clock
    }
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(unix_s);
    settimeofday(&tv, nullptr);
    g_manual_clock = true;
}

std::int64_t Network::unix_now() {
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return static_cast<std::int64_t>(tv.tv_sec);
}

std::int32_t Network::utc_offset_s(std::int64_t unix_s) {
    const time_t t = static_cast<time_t>(unix_s);
    std::tm local{};
    std::tm utc{};
    localtime_r(&t, &local);
    gmtime_r(&t, &utc);
    return static_cast<std::int32_t>(tm_to_seconds(local) - tm_to_seconds(utc));
}

void Network::begin(const core::StationConfig& cfg) {
    std::uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    std::snprintf(node_id_, sizeof(node_id_), "%02x%02x%02x", mac[3], mac[4], mac[5]);

    esp_netif_init();
    esp_event_loop_create_default();
    sta_netif_ = esp_netif_create_default_wifi_sta();
    ap_netif_ = esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Network::on_wifi_event, this);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Network::on_wifi_event, this);

    have_creds_ = cfg.wifi_ssid[0] != '\0';
    esp_wifi_set_mode(have_creds_ ? WIFI_MODE_STA : WIFI_MODE_APSTA);
    if (have_creds_) {
        wifi_config_t sta{};
        std::strncpy(reinterpret_cast<char*>(sta.sta.ssid), cfg.wifi_ssid, sizeof(sta.sta.ssid) - 1);
        std::strncpy(reinterpret_cast<char*>(sta.sta.password), cfg.wifi_pass, sizeof(sta.sta.password) - 1);
        sta.sta.threshold.authmode = cfg.wifi_pass[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        esp_wifi_set_config(WIFI_IF_STA, &sta);
    }
    esp_wifi_start();
    esp_wifi_set_max_tx_power(board::wifi_max_tx_power_quarter_dbm);
    if (have_creds_) {
        esp_wifi_connect();
    } else {
        start_ap();
    }

    apply_timezone(cfg.tz_posix);
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp.sync_cb = on_time_sync;
    esp_netif_sntp_init(&sntp);

    std::strncpy(mqtt_uri_, cfg.mqtt_uri, sizeof(mqtt_uri_) - 1);
    std::strncpy(ha_prefix_, cfg.ha_prefix, sizeof(ha_prefix_) - 1);
}

void Network::start_ap() {
    if (ap_on_) {
        return;
    }
    wifi_config_t ap{};
    std::snprintf(reinterpret_cast<char*>(ap.ap.ssid), sizeof(ap.ap.ssid), "AeroMet-%s", node_id_);
    ap.ap.ssid_len = static_cast<std::uint8_t>(std::strlen(reinterpret_cast<char*>(ap.ap.ssid)));
    std::strncpy(reinterpret_cast<char*>(ap.ap.password), "aeromet-setup", sizeof(ap.ap.password) - 1);
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    ap.ap.channel = 6;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    ap_on_ = true;
}

void Network::on_wifi_event(void* arg, const char* base, std::int32_t id, void* data) {
    auto* self = static_cast<Network*>(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        self->got_ip_ = false;
        if (self->have_creds_) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* e = static_cast<ip_event_got_ip_t*>(data);
        std::snprintf(self->ip_, sizeof(self->ip_), IPSTR, IP2STR(&e->ip_info.ip));
        self->got_ip_ = true;
        self->seconds_without_ip_ = 0;
    }
}

void Network::tick_1s(const core::StationConfig& cfg) {
    if (!got_ip_) {
        ++seconds_without_ip_;
        if (seconds_without_ip_ == 60) {
            start_ap();  // provisioning stays reachable while STA keeps retrying
        }
        return;
    }
    if (mqtt_ == nullptr && mqtt_uri_[0] != '\0') {
        start_mqtt(cfg);
    }
    if (mqtt_connected_ && !discovery_sent_) {
        publish_discovery(cfg);
    }
}

void Network::start_mqtt(const core::StationConfig& cfg) {
    (void)cfg;
    esp_mqtt_client_config_t m{};
    m.broker.address.uri = mqtt_uri_;
    m.session.keepalive = 60;
    auto client = esp_mqtt_client_init(&m);
    if (client == nullptr) {
        return;
    }
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, &Network::on_mqtt_event, this);
    esp_mqtt_client_start(client);
    mqtt_ = client;
}

void Network::on_mqtt_event(void* arg, const char* /*base*/, std::int32_t id, void* /*data*/) {
    auto* self = static_cast<Network*>(arg);
    if (id == MQTT_EVENT_CONNECTED) {
        self->mqtt_connected_ = true;
        self->discovery_sent_ = false;
    } else if (id == MQTT_EVENT_DISCONNECTED) {
        self->mqtt_connected_ = false;
    }
}

void Network::publish_discovery(const core::StationConfig& /*cfg*/) {
    auto client = static_cast<esp_mqtt_client_handle_t>(mqtt_);
    std::string topic, payload;
    for (std::size_t i = 0; i < telemetry::TelemetryFormatter::ha_sensor_count(); ++i) {
        if (telemetry::TelemetryFormatter::build_ha_discovery(i, node_id_, ha_prefix_, topic, payload)) {
            esp_mqtt_client_publish(client, topic.c_str(), payload.c_str(), static_cast<int>(payload.size()), 1, 1);
        }
    }
    discovery_sent_ = true;
}

void Network::mqtt_publish_state(const std::string& json) {
    if (!mqtt_connected_) {
        return;
    }
    const std::string topic = telemetry::TelemetryFormatter::ha_state_topic(node_id_);
    esp_mqtt_client_publish(static_cast<esp_mqtt_client_handle_t>(mqtt_), topic.c_str(), json.c_str(),
                            static_cast<int>(json.size()), 0, 0);
}

telemetry::WifiInfo Network::wifi_info() const {
    telemetry::WifiInfo w{};
    w.connected = got_ip_;
    w.ap_mode = ap_on_;
    w.external_ufl = true;
    w.tx_power_dbm = static_cast<float>(board::wifi_max_tx_power_quarter_dbm) * 0.25F;
    wifi_ap_record_t rec{};
    if (got_ip_ && esp_wifi_sta_get_ap_info(&rec) == ESP_OK) {
        w.rssi_dbm = rec.rssi;
    }
    std::strncpy(w.ip, got_ip_ ? ip_ : (ap_on_ ? "192.168.4.1" : ""), sizeof(w.ip) - 1);
    return w;
}

}  // namespace firmware
}  // namespace station
