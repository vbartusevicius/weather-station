#pragma once

#include <cstdint>

namespace station {
namespace core {

// User / installation configuration (persisted in NVS by the firmware, edited from the Web UI).
struct StationConfig {
    // Location: required for astronomy, clear-sky cloud cover, solar tide and night detection.
    bool location_set{false};
    double latitude_deg{0.0};
    double longitude_deg{0.0};
    float altitude_m{0.0F};
    char tz_posix[48]{"UTC0"};  // e.g. "EET-2EEST,M3.5.0/3,M10.5.0/4"

    // Sensor installation / calibration
    float declination_deg{0.0F};          // magnetic declination added to the compass heading
    float dt_ns_offset_us{0.0F};          // ultrasonic zero-wind residual N-S
    float dt_ew_offset_us{0.0F};          // ultrasonic zero-wind residual E-W
    // transducer + AFE group delay subtracted from every ToF. The crown times the 50 % point of the echo envelope,
    // ~58 us after the first arrival for a Q ~ 30 closed 40 kHz transducer (crown/test, 8-cycle burst): the still-air
    // calibration (CALIBRATION.md, `wind_calibrate` action) replaces this estimate.
    float us_delay_us{58.0F};
    float pressure_wind_coef{0.22F};      // fraction of dynamic pressure q seen by the barometer port
    float rain_gain{1.0F};                // tipping-cup cross calibration of the piezo rain estimate
    std::uint8_t as3935_tun_cap{0};       // AS3935 antenna tuning capacitor (0..15, 8 pF steps)
    float guvb_counts_per_uvi{0.0F};      // GUVB-C31SM field calibration; 0 = use the factory NVM B_Scale
    bool as3935_indoor{false};

    // Networking
    char wifi_ssid[33]{""};
    char wifi_pass[65]{""};
    char mqtt_uri[64]{""};
    char ha_prefix[32]{"homeassistant"};

    // Added for crown v2 (appended so a shorter config blob saved by older firmware still loads; see Storage)
    float rain_impact_scale{1.0F};        // crown charge-amp mV -> HapticRain mV (drop / hail thresholds)
    std::uint8_t rs485_address{0};        // 0 = answer un-addressed commands (single station); 1..247 = "<addr>:CMD"
    float wind_speed_gain{1.0F};          // wind-tunnel / reference-anemometer speed correction (CALIBRATION.md)
};

}  // namespace core
}  // namespace station
