#pragma once

#include <cstdint>

namespace station {
namespace firmware {

enum class ButtonAction : std::uint8_t {
    None = 0,
    Reboot,        // 1 short press (50 ms .. < 10 s, triggered on release) -> Reboot ESP32
    FactoryReset,  // >= 10,000 ms continuous hold -> Wipe NVS / Factory Reset + Reboot
};

class ButtonController {
public:
    static constexpr std::uint32_t kDebounceMs = 50;
    static constexpr std::uint32_t kFactoryResetHoldMs = 10000;

    constexpr ButtonController() = default;

    ButtonAction update(bool pressed_active_low, std::uint32_t now_ms) noexcept {
        if (pressed_active_low) {
            if (!is_pressed_) {
                is_pressed_ = true;
                press_start_ms_ = now_ms;
                factory_triggered_ = false;
                return ButtonAction::None;
            }
            const std::uint32_t held_ms = now_ms - press_start_ms_;
            if (!factory_triggered_ && held_ms >= kFactoryResetHoldMs) {
                factory_triggered_ = true;
                return ButtonAction::FactoryReset;
            }
            return ButtonAction::None;
        }

        if (is_pressed_) {
            const std::uint32_t held_ms = now_ms - press_start_ms_;
            const bool was_factory = factory_triggered_;
            is_pressed_ = false;
            factory_triggered_ = false;
            if (!was_factory && held_ms >= kDebounceMs && held_ms < kFactoryResetHoldMs) {
                return ButtonAction::Reboot;
            }
        }
        return ButtonAction::None;
    }

private:
    bool is_pressed_{false};
    bool factory_triggered_{false};
    std::uint32_t press_start_ms_{0};
};

}  // namespace firmware
}  // namespace station
