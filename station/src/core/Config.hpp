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
    // Diffuse transmission of the UV / light cover over the dome sensors: ~0.11 for the default PTFE hemisphere diffuser
    // (0.8 mm PTFE, spectrally flat), ~0.92 for the JGS1 window. LTR-390 UVI / lux, TSL25911 lux and the
    // factory-scaled GUVB UVI are divided by it (a field `guvb_counts_per_uvi` already includes the cover).
    float optics_transmission{0.11F};

    // Added for the home calibration procedures (src/calibration, Web UI "Calibrate"); appended like the fields above.
    std::uint8_t heading_mode{0};             // 0 = magnetometer (+ declination + heading_offset_deg), 1 = fixed
    float heading_fixed_deg{0.0F};            // true bearing the station's N transducer points to (heading_mode 1)
    float heading_offset_deg{0.0F};           // residual compass error from the N-mark reference (heading_mode 0)
    float mag_offset_ut[3]{0.0F, 0.0F, 0.0F}; // magnetometer hard-iron offset (compass spin)
    float mag_scale[2]{1.0F, 1.0F};           // magnetometer x / y soft-iron scale (compass spin)
    float temp_offset_c{0.0F};                // SHT40 offset added to every reading (ice-point check)
    float rh_offset_pct{0.0F};                // SHT40 offset added to every reading (saturated-salt check)
    std::uint32_t cal_unix[16]{};             // unix time each calibration::Kind was last applied (0 = never)

    // MQTT (appended): broker login, base topic ("" = "aeromet/<node id>"), Home Assistant discovery, state interval
    char mqtt_user[33]{""};
    char mqtt_pass[65]{""};
    char mqtt_topic[48]{""};
    std::uint8_t ha_discovery{1};
    std::uint16_t mqtt_interval_s{10};
};

// Reading behind the UV / light cover -> free-sky value (transmission clamped to 0.02..1).
inline float cover_corrected(float value, float transmission) {
    const float tr = transmission < 0.02F ? 0.02F : (transmission > 1.0F ? 1.0F : transmission);
    return value / tr;
}

}  // namespace core
}  // namespace station
