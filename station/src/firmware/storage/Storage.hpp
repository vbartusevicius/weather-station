#pragma once

#include <cstdint>

#include "../../core/Config.hpp"
#include "../../history/TieredHistory.hpp"

namespace station {
namespace firmware {

// NVS-backed configuration + LittleFS-backed hour/day history (minutes are not persisted).
class Storage {
public:
    bool begin();
    bool load_config(core::StationConfig& cfg);
    bool save_config(const core::StationConfig& cfg);
    bool save_history(const history::TieredHistory& h);
    bool load_history(history::TieredHistory& h);  // indices of `h` must already be wall-clock based
    bool save_voc_state(float s0, float s1, std::int64_t unix_s);
    bool load_voc_state(float& s0, float& s1, std::int64_t& unix_s);
    bool fs_ready() const { return fs_ready_; }

private:
    bool fs_ready_{false};
};

}  // namespace firmware
}  // namespace station
