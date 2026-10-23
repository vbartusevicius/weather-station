#pragma once

#include <cstddef>
#include <cstdint>

#include "../../core/Config.hpp"
#include "../../history/Climate.hpp"
#include "../../history/HistoryStore.hpp"

namespace station {
namespace firmware {

// The "storage" data partition as a raw flash device for history::HistoryStore (no file system).
class PartitionFlash : public history::FlashDevice {
public:
    bool begin(const char* label = "storage");
    std::size_t size() const override { return size_; }
    bool read(std::size_t offset, void* buf, std::size_t len) const override;
    bool write(std::size_t offset, const void* buf, std::size_t len) override;
    bool erase_sector(std::size_t offset) override;

private:
    const void* part_{nullptr};   // const esp_partition_t*
    std::size_t size_{0};
};

// NVS: configuration, records (highs / lows) and the VOC algorithm state. Hour / day history lives in HistoryStore.
class Storage {
public:
    bool begin();
    bool load_config(core::StationConfig& cfg);
    bool save_config(const core::StationConfig& cfg);
    bool save_records(const history::Climate& c);
    bool load_records(history::Climate& c);
    bool save_voc_state(float s0, float s1, std::int64_t unix_s);
    bool load_voc_state(float& s0, float& s1, std::int64_t& unix_s);
};

}  // namespace firmware
}  // namespace station
