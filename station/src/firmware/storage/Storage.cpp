#include "Storage.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace station {
namespace firmware {

namespace {
constexpr const char* kNs = "aeromet";
constexpr const char* kCfgKey = "cfg_v2";
constexpr const char* kRecKey = "records";
}  // namespace

bool Storage::begin() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    return ret == ESP_OK;
}

bool Storage::load_config(core::StationConfig& cfg) {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    core::StationConfig tmp{};
    std::size_t len = sizeof(tmp);
    // Fields are only ever appended to StationConfig: a shorter blob from older firmware fills the prefix and the
    // new fields keep their defaults (instead of losing Wi-Fi / location on every firmware update).
    const bool ok = nvs_get_blob(h, kCfgKey, &tmp, &len) == ESP_OK && len > 0 && len <= sizeof(tmp);
    nvs_close(h);
    if (ok) {
        tmp.tz_posix[sizeof(tmp.tz_posix) - 1] = '\0';
        tmp.wifi_ssid[sizeof(tmp.wifi_ssid) - 1] = '\0';
        tmp.wifi_pass[sizeof(tmp.wifi_pass) - 1] = '\0';
        tmp.mqtt_uri[sizeof(tmp.mqtt_uri) - 1] = '\0';
        tmp.ha_prefix[sizeof(tmp.ha_prefix) - 1] = '\0';
        cfg = tmp;
    }
    return ok;
}

bool Storage::save_config(const core::StationConfig& cfg) {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_blob(h, kCfgKey, &cfg, sizeof(cfg)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

namespace {
struct VocState {
    float s0;
    float s1;
    std::int64_t unix_s;
};
}  // namespace

bool Storage::save_voc_state(float s0, float s1, std::int64_t unix_s) {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const VocState v{s0, s1, unix_s};
    const bool ok = nvs_set_blob(h, "voc_state", &v, sizeof(v)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

bool Storage::load_voc_state(float& s0, float& s1, std::int64_t& unix_s) {
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    VocState v{};
    std::size_t len = sizeof(v);
    const bool ok = nvs_get_blob(h, "voc_state", &v, &len) == ESP_OK && len == sizeof(v);
    nvs_close(h);
    if (ok) {
        s0 = v.s0;
        s1 = v.s1;
        unix_s = v.unix_s;
    }
    return ok;
}

bool Storage::save_records(const history::Climate& c) {
    std::uint8_t buf[1024];
    const std::size_t n = c.serialize(buf, sizeof buf);
    nvs_handle_t h;
    if (n == 0 || nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_blob(h, kRecKey, buf, n) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

bool Storage::load_records(history::Climate& c) {
    std::uint8_t buf[1024];
    std::size_t len = sizeof buf;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_get_blob(h, kRecKey, buf, &len) == ESP_OK;
    nvs_close(h);
    return ok && c.deserialize(buf, len);
}

// ------------------------------------------------------------------------------------------------- PartitionFlash
bool PartitionFlash::begin(const char* label) {
    const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    part_ = p;
    size_ = p != nullptr ? p->size : 0;
    return p != nullptr;
}

bool PartitionFlash::read(std::size_t offset, void* buf, std::size_t len) const {
    return part_ != nullptr && esp_partition_read(static_cast<const esp_partition_t*>(part_), offset, buf, len) == ESP_OK;
}

bool PartitionFlash::write(std::size_t offset, const void* buf, std::size_t len) {
    return part_ != nullptr && esp_partition_write(static_cast<const esp_partition_t*>(part_), offset, buf, len) == ESP_OK;
}

bool PartitionFlash::erase_sector(std::size_t offset) {
    return part_ != nullptr && esp_partition_erase_range(static_cast<const esp_partition_t*>(part_), offset, kSector) == ESP_OK;
}

}  // namespace firmware
}  // namespace station
