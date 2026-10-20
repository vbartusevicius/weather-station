#include "Astronomy.hpp"

#include <cmath>

namespace station {
namespace astro {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kD2R = kPi / 180.0;
constexpr double kR2D = 180.0 / kPi;
constexpr double kSynodicDays = 29.530588853;

double rev(double deg) {
    double r = std::fmod(deg, 360.0);
    return r < 0.0 ? r + 360.0 : r;
}
double sind(double d) { return std::sin(d * kD2R); }
double cosd(double d) { return std::cos(d * kD2R); }

double julian_day(std::int64_t unix_s) {
    return static_cast<double>(unix_s) / 86400.0 + 2440587.5;
}

struct SolarCore {
    double decl_deg;
    double eot_min;
    double app_lon_deg;
};

SolarCore solar_core(double jd) {
    const double t = (jd - 2451545.0) / 36525.0;
    const double l0 = rev(280.46646 + t * (36000.76983 + t * 0.0003032));
    const double m = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    const double e = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
    const double c = sind(m) * (1.914602 - t * (0.004817 + 0.000014 * t)) + sind(2.0 * m) * (0.019993 - 0.000101 * t) +
                     sind(3.0 * m) * 0.000289;
    const double omega = 125.04 - 1934.136 * t;
    const double lambda = l0 + c - 0.00569 - 0.00478 * sind(omega);
    const double eps0 = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
    const double eps = eps0 + 0.00256 * cosd(omega);
    const double decl = std::asin(sind(eps) * sind(lambda)) * kR2D;
    const double y = std::pow(std::tan(eps * kD2R / 2.0), 2.0);
    const double eot = 4.0 * kR2D *
                       (y * sind(2.0 * l0) - 2.0 * e * sind(m) + 4.0 * e * y * sind(m) * cosd(2.0 * l0) -
                        0.5 * y * y * sind(4.0 * l0) - 1.25 * e * e * sind(2.0 * m));
    return {decl, eot, rev(lambda)};
}

struct MoonEcl {
    double lon_deg;
    double lat_deg;
    double dist_er;  // Earth radii
    double sun_lon_deg;
    double sun_mean_lon_deg;
};

MoonEcl moon_ecliptic(double d) {
    const double n = rev(125.1228 - 0.0529538083 * d);
    const double inc = 5.1454;
    const double w = rev(318.0634 + 0.1643573223 * d);
    const double a = 60.2666;
    const double ecc = 0.054900;
    const double mm = rev(115.3654 + 13.0649929509 * d);

    const double ws = 282.9404 + 4.70935e-5 * d;
    const double ms = rev(356.0470 + 0.9856002585 * d);
    const double ls = rev(ws + ms);

    double ea = mm + ecc * kR2D * sind(mm) * (1.0 + ecc * cosd(mm));
    for (int k = 0; k < 4; ++k) {
        ea = ea - (ea - ecc * kR2D * sind(ea) - mm) / (1.0 - ecc * cosd(ea));
    }
    const double xv = a * (cosd(ea) - ecc);
    const double yv = a * std::sqrt(1.0 - ecc * ecc) * sind(ea);
    const double v = std::atan2(yv, xv) * kR2D;
    double r = std::sqrt(xv * xv + yv * yv);

    const double xh = r * (cosd(n) * cosd(v + w) - sind(n) * sind(v + w) * cosd(inc));
    const double yh = r * (sind(n) * cosd(v + w) + cosd(n) * sind(v + w) * cosd(inc));
    const double zh = r * (sind(v + w) * sind(inc));
    double lon = rev(std::atan2(yh, xh) * kR2D);
    double lat = std::atan2(zh, std::sqrt(xh * xh + yh * yh)) * kR2D;

    const double lm = n + w + mm;
    const double dd = lm - ls;
    const double f = lm - n;
    lon += -1.274 * sind(mm - 2.0 * dd) + 0.658 * sind(2.0 * dd) - 0.186 * sind(ms) - 0.059 * sind(2.0 * mm - 2.0 * dd) -
           0.057 * sind(mm - 2.0 * dd + ms) + 0.053 * sind(mm + 2.0 * dd) + 0.046 * sind(2.0 * dd - ms) +
           0.041 * sind(mm - ms) - 0.035 * sind(dd) - 0.031 * sind(mm + ms) - 0.015 * sind(2.0 * f - 2.0 * dd) +
           0.011 * sind(mm - 4.0 * dd);
    lat += -0.173 * sind(f - 2.0 * dd) - 0.055 * sind(mm - f - 2.0 * dd) - 0.046 * sind(mm + f - 2.0 * dd) +
           0.033 * sind(f + 2.0 * dd) + 0.017 * sind(2.0 * mm + f);
    r += -0.58 * cosd(mm - 2.0 * dd) - 0.46 * cosd(2.0 * dd);

    // Sun ecliptic longitude (same theory, for elongation)
    const double es = 0.016709 - 1.151e-9 * d;
    const double e_s = ms + es * kR2D * sind(ms) * (1.0 + es * cosd(ms));
    const double xs = cosd(e_s) - es;
    const double ys = std::sqrt(1.0 - es * es) * sind(e_s);
    const double vs = std::atan2(ys, xs) * kR2D;
    return {rev(lon), lat, r, rev(vs + ws), ls};
}

// Topocentric altitude of the Moon (deg) at `unix_s`.
double moon_altitude(std::int64_t unix_s, double lat_deg, double lon_deg, MoonEcl* out_ecl) {
    const double d = julian_day(unix_s) - 2451543.5;
    const MoonEcl m = moon_ecliptic(d);
    if (out_ecl != nullptr) {
        *out_ecl = m;
    }
    const double ecl = 23.4393 - 3.563e-7 * d;
    const double xe = cosd(m.lon_deg) * cosd(m.lat_deg);
    const double ye = cosd(ecl) * sind(m.lon_deg) * cosd(m.lat_deg) - sind(ecl) * sind(m.lat_deg);
    const double ze = sind(ecl) * sind(m.lon_deg) * cosd(m.lat_deg) + cosd(ecl) * sind(m.lat_deg);
    const double ra = std::atan2(ye, xe) * kR2D;
    const double dec = std::atan2(ze, std::sqrt(xe * xe + ye * ye)) * kR2D;
    const double ut_h = std::fmod(static_cast<double>(unix_s) / 3600.0, 24.0);
    const double lst = rev(m.sun_mean_lon_deg + 180.0 + ut_h * 15.0 + lon_deg);
    const double ha = lst - ra;
    const double sin_alt = sind(lat_deg) * sind(dec) + cosd(lat_deg) * cosd(dec) * cosd(ha);
    const double alt = std::asin(sin_alt) * kR2D;
    const double mpar = std::asin(1.0 / m.dist_er) * kR2D;
    return alt - mpar * cosd(alt);
}

std::int64_t local_midnight_utc(std::int64_t unix_s, std::int32_t utc_offset_s) {
    const std::int64_t local = unix_s + utc_offset_s;
    const std::int64_t day = (local >= 0) ? (local / 86400) : ((local - 86399) / 86400);
    return day * 86400 - utc_offset_s;
}
}  // namespace

SunPosition Astronomy::sun(std::int64_t unix_s, double lat_deg, double lon_deg) {
    const SolarCore sc = solar_core(julian_day(unix_s));
    const double utc_min = std::fmod(static_cast<double>(unix_s) / 60.0, 1440.0);
    const double tst = std::fmod(utc_min + sc.eot_min + 4.0 * lon_deg + 2880.0, 1440.0);
    const double ha = tst / 4.0 - 180.0;
    const double cos_zen = sind(lat_deg) * sind(sc.decl_deg) + cosd(lat_deg) * cosd(sc.decl_deg) * cosd(ha);
    const double zen = std::acos(std::fmax(-1.0, std::fmin(1.0, cos_zen))) * kR2D;
    const double az = rev(std::atan2(sind(ha), cosd(ha) * sind(lat_deg) - std::tan(sc.decl_deg * kD2R) * cosd(lat_deg)) *
                              kR2D +
                          180.0);
    SunPosition p{};
    p.altitude_deg = static_cast<float>(90.0 - zen);
    p.azimuth_deg = static_cast<float>(az);
    p.declination_deg = static_cast<float>(sc.decl_deg);
    p.equation_of_time_min = static_cast<float>(sc.eot_min);
    return p;
}

MoonPosition Astronomy::moon(std::int64_t unix_s, double lat_deg, double lon_deg) {
    MoonEcl m{};
    const double alt = moon_altitude(unix_s, lat_deg, lon_deg, &m);
    const double delta = rev(m.lon_deg - m.sun_lon_deg);  // 0 new, 180 full
    const double cos_elong = cosd(m.lat_deg) * cosd(m.lon_deg - m.sun_lon_deg);
    MoonPosition p{};
    p.altitude_deg = static_cast<float>(alt);
    p.illumination = static_cast<float>((1.0 - cos_elong) / 2.0);
    p.age_days = static_cast<float>(delta / 360.0 * kSynodicDays);
    p.waxing = delta < 180.0;
    return p;
}

RiseSet Astronomy::sun_rise_set(std::int64_t unix_s, std::int32_t utc_offset_s, double lat_deg, double lon_deg) {
    RiseSet rs{};
    const std::int64_t midnight = local_midnight_utc(unix_s, utc_offset_s);
    const SolarCore sc = solar_core(julian_day(midnight + 43200));
    const double cos_ha = (cosd(90.833) / (cosd(lat_deg) * cosd(sc.decl_deg))) - std::tan(lat_deg * kD2R) * std::tan(sc.decl_deg * kD2R);
    if (cos_ha > 1.0) {
        rs.always_down = true;
        return rs;
    }
    if (cos_ha < -1.0) {
        rs.always_up = true;
        return rs;
    }
    const double ha = std::acos(cos_ha) * kR2D;
    const double noon_utc_min = 720.0 - 4.0 * lon_deg - sc.eot_min;
    const double off_min = static_cast<double>(utc_offset_s) / 60.0;
    rs.has_rise = true;
    rs.has_set = true;
    rs.rise_min = static_cast<float>(std::fmod(noon_utc_min - 4.0 * ha + off_min + 2880.0, 1440.0));
    rs.set_min = static_cast<float>(std::fmod(noon_utc_min + 4.0 * ha + off_min + 2880.0, 1440.0));
    return rs;
}

RiseSet Astronomy::moon_rise_set(std::int64_t unix_s, std::int32_t utc_offset_s, double lat_deg, double lon_deg) {
    // Sample the topocentric altitude every 10 minutes across the local day and interpolate the
    // crossings of h0 = -0.833 deg (refraction + semi-diameter).
    RiseSet rs{};
    constexpr double kH0 = -0.833;
    const std::int64_t midnight = local_midnight_utc(unix_s, utc_offset_s);
    double prev = moon_altitude(midnight, lat_deg, lon_deg, nullptr) - kH0;
    bool any_up = prev > 0.0;
    bool any_down = prev <= 0.0;
    for (int k = 1; k <= 144; ++k) {
        const double cur = moon_altitude(midnight + k * 600, lat_deg, lon_deg, nullptr) - kH0;
        any_up = any_up || cur > 0.0;
        any_down = any_down || cur <= 0.0;
        if ((prev <= 0.0) != (cur <= 0.0)) {
            const double frac = prev / (prev - cur);
            const float t_min = static_cast<float>((k - 1 + frac) * 10.0);
            if (prev <= 0.0 && !rs.has_rise) {
                rs.has_rise = true;
                rs.rise_min = t_min;
            } else if (prev > 0.0 && !rs.has_set) {
                rs.has_set = true;
                rs.set_min = t_min;
            }
        }
        prev = cur;
    }
    rs.always_up = !any_down;
    rs.always_down = !any_up;
    return rs;
}

float Astronomy::clear_sky_ghi_w_m2(float sun_altitude_deg) {
    const double cz = sind(sun_altitude_deg);
    if (cz <= 0.0) {
        return 0.0F;
    }
    return static_cast<float>(1098.0 * cz * std::exp(-0.057 / cz));
}

float Astronomy::solar_tide_3h_hpa(std::int64_t unix_s, double lat_deg, double lon_deg) {
    auto lst_hours = [lon_deg](std::int64_t t) {
        const SolarCore sc = solar_core(julian_day(t));
        const double utc_min = std::fmod(static_cast<double>(t) / 60.0, 1440.0);
        return std::fmod(utc_min + sc.eot_min + 4.0 * lon_deg + 2880.0, 1440.0) / 60.0;
    };
    const double amp = 1.2 * std::pow(cosd(lat_deg), 3.0);
    const double now = amp * std::cos(2.0 * kPi * (lst_hours(unix_s) - 10.0) / 12.0);
    const double then = amp * std::cos(2.0 * kPi * (lst_hours(unix_s - 10800) - 10.0) / 12.0);
    return static_cast<float>(now - then);
}

const char* Astronomy::moon_phase_name(const MoonPosition& m) {
    const float pct = m.illumination * 100.0F;
    if (pct < 3.0F) return "New Moon";
    if (pct > 97.0F) return "Full Moon";
    if (pct >= 45.0F && pct <= 55.0F) return m.waxing ? "First Quarter" : "Last Quarter";
    if (pct < 50.0F) return m.waxing ? "Waxing Crescent" : "Waning Crescent";
    return m.waxing ? "Waxing Gibbous" : "Waning Gibbous";
}

}  // namespace astro
}  // namespace station
