#pragma once

#include <cstdint>

namespace station {
namespace astro {

struct SunPosition {
    float altitude_deg{0.0F};   // geometric altitude above the horizon
    float azimuth_deg{0.0F};    // 0 = north, 90 = east
    float declination_deg{0.0F};
    float equation_of_time_min{0.0F};
};

struct MoonPosition {
    float altitude_deg{0.0F};   // topocentric
    float illumination{0.0F};   // 0..1
    float age_days{0.0F};       // days since new moon (0..29.53)
    bool waxing{true};
};

struct RiseSet {
    bool has_rise{false};
    bool has_set{false};
    float rise_min{0.0F};       // local minutes after local midnight
    float set_min{0.0F};
    bool always_up{false};
    bool always_down{false};
};

class Astronomy {
public:
    // NOAA solar position algorithm (accuracy ~0.01 deg, 1800-2100).
    static SunPosition sun(std::int64_t unix_s, double lat_deg, double lon_deg);
    // Low-precision lunar theory (Schlyter / Meeus main perturbation terms, ~0.3 deg).
    static MoonPosition moon(std::int64_t unix_s, double lat_deg, double lon_deg);

    // Rise / set for the local calendar day containing unix_s (utc_offset_s = local - UTC).
    static RiseSet sun_rise_set(std::int64_t unix_s, std::int32_t utc_offset_s, double lat_deg, double lon_deg);
    static RiseSet moon_rise_set(std::int64_t unix_s, std::int32_t utc_offset_s, double lat_deg, double lon_deg);

    // Haurwitz clear-sky global horizontal irradiance (W/m^2) for a sun altitude.
    static float clear_sky_ghi_w_m2(float sun_altitude_deg);

    // Expected semidiurnal (S2) solar-tide pressure change over the last 3 hours (hPa).
    // Amplitude ~1.2 hPa * cos^3(latitude), maxima near 10:00 and 22:00 local solar time.
    static float solar_tide_3h_hpa(std::int64_t unix_s, double lat_deg, double lon_deg);

    static const char* moon_phase_name(const MoonPosition& m);
};

}  // namespace astro
}  // namespace station
