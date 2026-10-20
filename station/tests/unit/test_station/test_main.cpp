#include <cmath>
#include <cstring>
#include <string>
#if __has_include(<unity.h>)
#include <unity.h>
#else
#include <cassert>
#define UNITY_BEGIN() 0
#define UNITY_END() 0
#define RUN_TEST(fn) fn()
#define TEST_ASSERT_TRUE(cond) assert(cond)
#define TEST_ASSERT_FALSE(cond) assert(!(cond))
#define TEST_ASSERT_NULL(ptr) assert((ptr) == nullptr)
#define TEST_ASSERT_NOT_NULL(ptr) assert((ptr) != nullptr)
#define TEST_ASSERT_EQUAL(exp, act) assert((exp) == (act))
#define TEST_ASSERT_EQUAL_INT(exp, act) assert((exp) == (act))
#define TEST_ASSERT_EQUAL_UINT8(exp, act) assert((exp) == (act))
#define TEST_ASSERT_EQUAL_UINT16(exp, act) assert((exp) == (act))
#define TEST_ASSERT_EQUAL_UINT32(exp, act) assert((exp) == (act))
#define TEST_ASSERT_EQUAL_STRING(exp, act) assert(std::strcmp((exp), (act)) == 0)
#define TEST_ASSERT_FLOAT_WITHIN(delta, exp, act) \
    do { const double a_ = (act), e_ = (exp); assert(a_ - e_ <= (delta) && e_ - a_ <= (delta)); } while (0)
#endif

#include <ArduinoJson.h>

#include "../../../src/airquality/AirQuality.hpp"
#include "../../../src/airquality/GasIndex.hpp"
#include "../../../src/astro/Astronomy.hpp"
#include "../../../src/conditioning/Conditioners.hpp"
#include "../../../src/core/Pipeline.hpp"
#include "../../../src/environment/MetCalculations.hpp"
#include "../../../src/firmware/ButtonController.hpp"
#include "../../../src/heater/DeIcingHeater.hpp"
#include "../../../src/history/TieredHistory.hpp"
#include "../../../src/rain/HapticRain.hpp"
#include "../../../src/rs485/Rs485Protocol.hpp"
#include "../../../src/telemetry/HomeAssistantMqtt.hpp"
#include "../../../src/wind/UltrasonicWind.hpp"

using station::airquality::AirQualityEvaluator;
using station::airquality::AirQualityInput;
using station::airquality::AqiLevel;
using station::astro::Astronomy;
using station::core::DerivedState;
using station::core::EventQueues;
using station::core::Pipeline;
using station::core::RawState;
using station::core::StationConfig;
using station::environment::EnvLongevityManager;
using station::environment::MetCalculator;
using station::heater::DeIcingHeaterController;
using station::heater::HeaterSensorInput;
using station::heater::HeaterState;
using station::history::Metric;
using station::history::Tier;
using station::history::TieredHistory;
using station::rain::HapticRainProcessor;
using station::rain::PrecipitationType;
using station::rain::RainImpactEvent;
using station::rs485::CommandType;
using station::rs485::Rs485Protocol;
using station::telemetry::TelemetryFormatter;
using station::telemetry::WifiInfo;
using station::wind::ImuCompassReading;
using station::wind::TofQuad;
using station::wind::UltrasonicWindSolver;
using station::wind::WmoWindAggregator;

void setUp(void) {}
void tearDown(void) {}

namespace {
// Vilnius, 2025-06-21 (EEST, UTC+3)
constexpr double kLat = 54.6872;
constexpr double kLon = 25.2797;
constexpr std::int64_t kSolsticeNoonUtc = 1750507200;  // 2025-06-21 12:00:00 UTC

StationConfig vilnius() {
    StationConfig c{};
    c.location_set = true;
    c.latitude_deg = kLat;
    c.longitude_deg = kLon;
    c.altitude_m = 112.0F;
    return c;
}

// Fill every sensor with a plausible steady value at monotonic time `now_ms`.
void fill_steady(RawState& r, std::uint32_t now_ms) {
    r.now_ms = now_ms;
    r.thermo.temp_c.set(20.0F, now_ms);
    r.thermo.rh_pct.set(50.0F, now_ms);
    r.baro.pressure_hpa.set(1000.0F, now_ms);
    r.baro.ripple_pa.set(0.1F, now_ms);
    station::core::WindSecond w{};
    w.v_east_m_s = 5.0F;   // air moving east => wind FROM the west (270)
    w.v_north_m_s = 0.0F;
    w.speed_mean_m_s = 5.0F;
    w.sound_speed_m_s = 343.4F;
    w.virtual_temp_c = 20.6F;
    w.samples = 4;
    r.wind.second.set(w, now_ms);
    r.air.pm1_ug_m3.set(3.0F, now_ms);
    r.air.pm25_ug_m3.set(6.0F, now_ms);
    r.air.pm4_ug_m3.set(8.0F, now_ms);
    r.air.pm10_ug_m3.set(12.0F, now_ms);
    r.air.voc_index.set(100.0F, now_ms);
    r.air.nox_index.set(1.0F, now_ms);
    r.air.co2_ppm.set(420.0F, now_ms);
    r.power.vin_v.set(24.0F, now_ms);
    r.power.reflector_c.set(18.0F, now_ms);
    r.power.rain_adc_ok = true;
    station::core::VibrationSecond v{};
    v.pga_mg = 0.5F;
    v.ppv_mm_s = 0.05F;
    v.dominant_hz = 18.0F;
    r.imu.vibration.set(v, now_ms);
}

JsonDocument parse(const std::string& s) {
    JsonDocument doc;
    deserializeJson(doc, s);
    return doc;
}
}  // namespace

void test_ultrasonic_zero_wind_and_virtual_temp(void) {
    // D=54 mm, H=48 mm => L_eff = 110.15 mm => calm ToF at 20 C (343.2 m/s) is 320.95 us
    const TofQuad calm{320.95F, 320.95F, 320.95F, 320.95F};
    const auto s = UltrasonicWindSolver::solve(calm);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, s.speed_m_s);
    TEST_ASSERT_FLOAT_WITHIN(1.5F, 343.2F, s.sound_speed_m_s);
    TEST_ASSERT_FLOAT_WITHIN(2.0F, 20.0F, s.acoustic_temp_c);
}

void test_ultrasonic_soft_wind_and_cycle_slip_lock_up_to_60ms(void) {
    // 1. Soft wind: 0.08 m/s (Δt = 73 ns => t_NS = 320.9135 us, t_SN = 320.9865 us)
    const TofQuad soft_breeze{320.9135F, 320.9865F, 320.95F, 320.95F};
    const auto soft = UltrasonicWindSolver::solve(soft_breeze);
    TEST_ASSERT_TRUE(soft.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.015F, 0.08F, soft.speed_m_s);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 0.0F, soft.direction_deg);

    // 2. Storm / Hurricane wind: ~45 m/s where Δt = 41.3 us (> 1.6x 25.0 us carrier period!)
    // Without U4B envelope lock, zero-crossing alone would slip by 25.0 us; with env_*_us it locks cleanly:
    TofQuad storm{};
    storm.t_ns_us = 301.5F + 25.0F;  // Slipped by +1 carrier period (25 us)
    storm.env_ns_us = 301.5F;        // Comparator B envelope gate identifies the true 301.5 us cycle
    storm.t_sn_us = 342.8F - 25.0F;  // Slipped by -1 carrier period (-25 us)
    storm.env_sn_us = 342.8F;        // Comparator B envelope gate identifies the true 342.8 us cycle
    storm.t_ew_us = 320.95F;
    storm.t_we_us = 320.95F;
    const auto locked = UltrasonicWindSolver::solve(storm);
    TEST_ASSERT_TRUE(locked.valid);
    TEST_ASSERT_FLOAT_WITHIN(1.5F, 44.8F, locked.speed_m_s);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 0.0F, locked.direction_deg);
}

void test_ultrasonic_cardinal_directions_and_sound_cancellation(void) {
    // Wind blowing FROM North TO South (v_north < 0 => t_NS shorter, t_SN longer)
    const TofQuad north_wind{301.0F, 309.0F, 305.0F, 305.0F};
    const auto n = UltrasonicWindSolver::solve(north_wind);
    TEST_ASSERT_TRUE(n.valid);
    TEST_ASSERT_TRUE(n.speed_m_s > 5.0F);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 0.0F, n.direction_deg);

    // Wind blowing FROM East TO West (v_east < 0 => t_EW shorter, t_WE longer)
    const TofQuad east_wind{305.0F, 305.0F, 302.0F, 308.0F};
    const auto e = UltrasonicWindSolver::solve(east_wind);
    TEST_ASSERT_TRUE(e.valid);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 90.0F, e.direction_deg);

    // Out-of-bounds ToF rejected as invalid
    const TofQuad invalid{50.0F, 305.0F, 305.0F, 305.0F};
    TEST_ASSERT_FALSE(UltrasonicWindSolver::solve(invalid).valid);
}

void test_ultrasonic_auto_north_and_tilt_compensation(void) {
    const TofQuad north_rel{301.0F, 309.0F, 305.0F, 305.0F};
    ImuCompassReading imu{};
    imu.mag_x_ut = 20.0F;
    imu.mag_y_ut = 0.0F;  // Rotated +90 deg East
    const auto s = UltrasonicWindSolver::solve(north_rel, imu);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 90.0F, s.north_offset_deg);
    TEST_ASSERT_FLOAT_WITHIN(1.5F, 90.0F, s.direction_deg);
}

void test_wmo_wind_3s_gust_10min_mean_and_direction_across_north(void) {
    WmoWindAggregator agg;
    // 10 minutes of steady 5 m/s from the north (air flows south: v_north = -5)
    for (int i = 0; i < 600; ++i) {
        agg.push_second(true, 0.0F, -5.0F, 5.0F);
    }
    // A single 1-s spike of 15 m/s: the WMO gust is the max 3-s MEAN, i.e. (15 + 5 + 5) / 3
    agg.push_second(true, 0.0F, -15.0F, 15.0F);
    agg.push_second(true, 0.0F, -5.0F, 5.0F);
    const auto s = agg.summary();
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.05F, 8.33F, s.gust_m_s);
    TEST_ASSERT_FLOAT_WITHIN(0.05F, 5.0F, s.lull_m_s);
    TEST_ASSERT_FLOAT_WITHIN(0.05F, 5.02F, s.avg_10m_m_s);
    TEST_ASSERT_TRUE(s.dir_valid);
    TEST_ASSERT_TRUE(s.dir_10m_deg > 359.0F || s.dir_10m_deg < 1.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 100.0F, s.rose_freq_pct[0]);

    // Vector mean across north: 355 / 5 / 358 deg must average near 0, not 180
    WmoWindAggregator n;
    const float dirs[3] = {355.0F, 5.0F, 358.0F};
    for (float d : dirs) {
        const float rad = d * 3.14159265F / 180.0F;
        n.push_second(true, -6.0F * std::sin(rad), -6.0F * std::cos(rad), 6.0F);
    }
    const auto ns = n.summary();
    TEST_ASSERT_TRUE(ns.dir_10m_deg > 355.0F || ns.dir_10m_deg < 5.0F);

    // A missing second breaks the 3-s mean: no "instantaneous" wind is published
    n.push_second(false, 0.0F, 0.0F, 0.0F);
    TEST_ASSERT_FALSE(n.summary().valid);

    // Moist-air acoustic virtual temperature correction (30 C / 85 % RH -> ~2.5 C lower)
    const float t_true = UltrasonicWindSolver::correct_acoustic_temp_for_humidity(30.0F, 85.0F, 1013.25F);
    TEST_ASSERT_TRUE(t_true < 28.2F && t_true > 26.0F);
}

void test_haptic_rain_rolling_rate_event_and_daily(void) {
    HapticRainProcessor rain;
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, 0.0F, rain.process_impact(RainImpactEvent{120, 80, 200, 0.01F}));
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, 0.0F, rain.process_impact(RainImpactEvent{450, 90, 950, 0.35F}));  // mast shake
    TEST_ASSERT_EQUAL_UINT32(1, rain.metrics().rejected_vibrations);

    // 20 drops per second for 60 s: the rate is the rolling 60-s depth x 60
    float depth = 0.0F;
    for (int s = 0; s < 60; ++s) {
        for (int i = 0; i < 20; ++i) {
            depth += rain.process_impact(RainImpactEvent{620, 180, 220, 0.02F});
        }
        rain.tick_second();
    }
    TEST_ASSERT_TRUE(rain.metrics().is_raining);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, depth * 60.0F, rain.metrics().rate_mm_h);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, depth, rain.metrics().daily_mm);
    TEST_ASSERT_EQUAL_UINT32(1200, rain.metrics().drop_count);

    // Hail: sharp high-amplitude impact
    rain.process_impact(RainImpactEvent{1850, 180, 110, 0.04F});
    rain.tick_second();
    TEST_ASSERT_EQUAL_UINT32(1, rain.metrics().hail_hits);
    TEST_ASSERT_EQUAL(PrecipitationType::Hail, rain.metrics().state);
    TEST_ASSERT_TRUE(rain.metrics().max_hail_diameter_mm >= 5.0F && rain.metrics().max_hail_diameter_mm <= 15.0F);

    // Rate decays to 0 after 60 dry seconds; the shower ends after 15 dry minutes
    for (int s = 0; s < 61; ++s) {
        rain.tick_second();
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, 0.0F, rain.metrics().rate_mm_h);
    TEST_ASSERT_EQUAL(PrecipitationType::None, rain.metrics().state);
    TEST_ASSERT_TRUE(rain.metrics().is_raining);
    for (int s = 0; s < 15 * 60; ++s) {
        rain.tick_second();
    }
    TEST_ASSERT_FALSE(rain.metrics().is_raining);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, 0.0F, rain.metrics().event_mm);
    TEST_ASSERT_TRUE(rain.metrics().daily_mm > 0.0F);
    rain.new_day();
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, 0.0F, rain.metrics().daily_mm);

    // Wind-driven oblique impacts and a hot PZT dome both reduce the attributed depth
    HapticRainProcessor a, b, c, e;
    TEST_ASSERT_TRUE(a.process_impact(RainImpactEvent{600, 206, 200, 0.01F, 12.0F, 20.0F}) <
                     b.process_impact(RainImpactEvent{600, 206, 200, 0.01F, 0.0F, 20.0F}));
    TEST_ASSERT_TRUE(c.process_impact(RainImpactEvent{600, 206, 200, 0.01F, 0.0F, 45.0F}) <
                     e.process_impact(RainImpactEvent{600, 206, 200, 0.01F, 0.0F, 0.0F}));
}

void test_meteorological_formulas(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 9.3F, MetCalculator::dew_point_c(20.0F, 50.0F));
    TEST_ASSERT_FLOAT_WITHIN(0.3F, 13.7F, MetCalculator::wet_bulb_c(20.0F, 50.0F));
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 8.6F, MetCalculator::absolute_humidity_g_m3(20.0F, 50.0F));
    TEST_ASSERT_FLOAT_WITHIN(0.02F, 1.17F, MetCalculator::vapor_pressure_deficit_kpa(20.0F, 50.0F));
    TEST_ASSERT_FLOAT_WITHIN(5.0F, 1334.0F, MetCalculator::cloud_base_lcl_m(20.0F, 9.3F));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 1.20F, MetCalculator::moist_air_density_kg_m3(1013.25F, 20.0F, 50.0F));
    // Hypsometric QNH: 1000 hPa at 112 m, 15 C -> ~1013.3 hPa
    TEST_ASSERT_FLOAT_WITHIN(0.3F, 1013.3F, MetCalculator::sea_level_pressure_hpa(1000.0F, 15.0F, 112.0F));
    // Barometer port suction in a 25 m/s gale is compensated (+0.84 hPa with the 0.22 port coefficient)
    TEST_ASSERT_FLOAT_WITHIN(0.3F, 1000.84F, MetCalculator::compensate_wind_dynamic_pressure_hpa(1000.0F, 25.0F, 15.0F));
    TEST_ASSERT_TRUE(MetCalculator::wind_chill_c(-5.0F, 8.0F) < -10.0F);
    TEST_ASSERT_TRUE(MetCalculator::heat_index_c(33.0F, 75.0F) > 40.0F);
    // Feels-like: shade apparent temperature in the mild range (no unmeasured solar term)
    TEST_ASSERT_FLOAT_WITHIN(0.05F, 18.45F, MetCalculator::feels_like_c(20.0F, 50.0F, 2.0F));

    // Cloud cover against a real clear-sky reference
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 0.0F, MetCalculator::estimate_cloud_cover_pct(880.0F, 880.0F));
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 100.0F, MetCalculator::estimate_cloud_cover_pct(200.0F, 880.0F));
    TEST_ASSERT_TRUE(MetCalculator::estimate_cloud_cover_pct(600.0F, 880.0F) > 50.0F);

    // Night sky: 0.0008 lx -> ~21.6 mag/arcsec^2 (rural dark sky)
    const float sqm = MetCalculator::sky_quality_mpsas(0.0008F);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 21.57F, sqm);
    TEST_ASSERT_TRUE(MetCalculator::bortle_class_from_sqm(sqm) <= 3);

    TEST_ASSERT_TRUE(MetCalculator::sager_rain_probability_pct(992.0F, -4.2F, -25.0F, 92.0F, false) >
                     MetCalculator::sager_rain_probability_pct(1020.0F, 0.4F, 15.0F, 15.0F, false));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 5.78F, MetCalculator::thunder_distance_km(16.8F, 344.0F));
    TEST_ASSERT_EQUAL_STRING("Heavy Truck / Train", MetCalculator::classify_vibration_source(5.2F, 24.0F, 3.0F));
    TEST_ASSERT_EQUAL_STRING("Wind Sway", MetCalculator::classify_vibration_source(5.2F, 3.0F, 15.0F));
    TEST_ASSERT_EQUAL_STRING("MMI II-III", MetCalculator::mmi_from_pga(5.2F));

    // LTR-390 ALS conversion and TSL25911 -> LTR-390 handoff above 80 klx
    const float ltr_lux = MetCalculator::lux_from_ltr390_als(400000U, 3.0F, 100.0F);
    TEST_ASSERT_FLOAT_WITHIN(10.0F, 80000.0F, ltr_lux);
    TEST_ASSERT_FLOAT_WITHIN(10.0F, 80000.0F, MetCalculator::fuse_uv_hardened_lux(55000.0F, ltr_lux));

    station::environment::DomeOpticalScheduler sched{};
    const auto c1 = sched.tick_500ms(60000U);
    TEST_ASSERT_TRUE(c1.read_ltr390_uvs);
    TEST_ASSERT_EQUAL(station::environment::Tsl2591Gain::Low1x, c1.tsl2591_next_gain);

    TEST_ASSERT_EQUAL(station::environment::PressureTrend::FallingRapidly, MetCalculator::classify_pressure_trend(-3.8F));
    TEST_ASSERT_EQUAL_STRING("Showery, Becoming Unsettled",
                             MetCalculator::zambretti_forecast(1014.2F, station::environment::PressureTrend::Falling, 190.0F));
}

void test_air_quality_pm_only_aqi_nowcast_and_eaqi(void) {
    AirQualityInput clean{3.0F, 5.4F, 7.0F, 12.0F, 95, 2, 418, 50.0F, 1013.25F};
    const auto r_clean = AirQualityEvaluator::evaluate(clean);
    TEST_ASSERT_EQUAL(AqiLevel::Good, r_clean.level);
    TEST_ASSERT_EQUAL_UINT8(1, r_clean.eu_eaqi);

    // A high NOx index no longer inflates the EPA AQI (relative Sensirion index, not an EPA pollutant)
    AirQualityInput traffic{4.0F, 6.0F, 8.0F, 15.0F, 140, 180, 460, 55.0F, 1013.25F};
    const auto r_nox = AirQualityEvaluator::evaluate(traffic);
    TEST_ASSERT_EQUAL_STRING("PM2.5", r_nox.dominant_pollutant);
    TEST_ASSERT_EQUAL(AqiLevel::Good, r_nox.level);

    // Fog (92 % RH): hygroscopic water growth is removed from PM2.5
    AirQualityInput foggy{10.0F, 28.0F, 35.0F, 42.0F, 90, 2, 420, 92.0F, 980.0F};
    const auto r_fog = AirQualityEvaluator::evaluate(foggy);
    TEST_ASSERT_TRUE(r_fog.dry_pm2_5_ug_m3 < 8.5F);
    TEST_ASSERT_EQUAL_UINT16(420, r_fog.pressure_comp_co2_ppm);  // no double pressure correction

    // EPA NowCast: steady 12 h -> equals the hourly mean; a fresh spike is weighted toward recent hours
    float flat[12];
    bool all[12];
    for (int i = 0; i < 12; ++i) {
        flat[i] = 10.0F;
        all[i] = true;
    }
    float nc = 0.0F;
    TEST_ASSERT_TRUE(AirQualityEvaluator::nowcast(flat, all, 12, nc));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 10.0F, nc);
    float spike[12] = {40.0F, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10};
    TEST_ASSERT_TRUE(AirQualityEvaluator::nowcast(spike, all, 12, nc));
    TEST_ASSERT_TRUE(nc > 20.0F && nc < 40.0F);
    bool sparse[12] = {true, false, false};
    TEST_ASSERT_FALSE(AirQualityEvaluator::nowcast(flat, sparse, 12, nc));

    TEST_ASSERT_EQUAL_UINT8(1, AirQualityEvaluator::eaqi_band(5.0F, 15.0F));
    TEST_ASSERT_EQUAL_UINT8(4, AirQualityEvaluator::eaqi_band(30.0F, 15.0F));
    TEST_ASSERT_EQUAL_UINT8(5, AirQualityEvaluator::eaqi_band(5.0F, 120.0F));
}

void test_astronomy_sun_moon_and_clear_sky(void) {
    const int off = 3 * 3600;
    const auto rs = Astronomy::sun_rise_set(kSolsticeNoonUtc, off, kLat, kLon);
    TEST_ASSERT_TRUE(rs.has_rise && rs.has_set);
    TEST_ASSERT_FLOAT_WITHIN(3.0F, 4 * 60 + 42, rs.rise_min);   // 04:42 EEST
    TEST_ASSERT_FLOAT_WITHIN(3.0F, 22 * 60 + 0, rs.set_min);    // 22:00 EEST
    const auto noon = Astronomy::sun(kSolsticeNoonUtc - 6120, kLat, kLon);  // solar noon 10:18 UTC
    TEST_ASSERT_FLOAT_WITHIN(0.2F, 58.75F, noon.altitude_deg);
    TEST_ASSERT_FLOAT_WITHIN(3.0F, 180.0F, noon.azimuth_deg);

    const auto full = Astronomy::moon(1759808820, kLat, kLon);  // full moon 2025-10-07 03:47 UTC
    TEST_ASSERT_TRUE(full.illumination > 0.99F);
    TEST_ASSERT_EQUAL_STRING("Full Moon", Astronomy::moon_phase_name(full));
    const auto fq = Astronomy::moon(1761754860, kLat, kLon);   // first quarter 2025-10-29 16:21 UTC
    TEST_ASSERT_FLOAT_WITHIN(0.03F, 0.5F, fq.illumination);
    TEST_ASSERT_TRUE(fq.waxing);

    TEST_ASSERT_FLOAT_WITHIN(10.0F, 879.0F, Astronomy::clear_sky_ghi_w_m2(58.8F));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, Astronomy::clear_sky_ghi_w_m2(-3.0F));
    // The S2 tide is ~0.3 hPa per 3 h at mid latitudes, smaller toward the poles
    TEST_ASSERT_TRUE(std::fabs(Astronomy::solar_tide_3h_hpa(kSolsticeNoonUtc, kLat, kLon)) < 0.5F);
    TEST_ASSERT_TRUE(std::fabs(Astronomy::solar_tide_3h_hpa(kSolsticeNoonUtc, 0.0, kLon)) >
                     std::fabs(Astronomy::solar_tide_3h_hpa(kSolsticeNoonUtc, 70.0, kLon)));
}

void test_tiered_history_rollup_gaps_and_persistence(void) {
    static_assert(sizeof(station::history::Slot) == 16, "slot layout");
    TieredHistory h;
    // Two hours of 1-s samples: temperature ramps 10 -> ~17.2 C, 0.01 mm of rain per second
    for (std::int64_t t = 0; t < 7200; ++t) {
        h.advance(t / 60, t / 3600, t / 86400);
        h.add(Metric::TempC, 10.0F + static_cast<float>(t) * 0.001F);
        h.add(Metric::RainMm, 0.01F);
    }
    float v = 0.0F;
    // Closed minutes hold 60 samples each; minute 119 is still running, newest closed = 7080..7139 s
    TEST_ASSERT_TRUE(h.slot(Metric::TempC, Tier::Minute, 59).value(station::history::Agg::Mean, v));
    TEST_ASSERT_FLOAT_WITHIN(0.002F, 10.0F + 7109.5F * 0.001F, v);
    TEST_ASSERT_EQUAL_UINT16(60, h.slot(Metric::TempC, Tier::Minute, 59).n);
    // The newest closed hour (0..3599 s) sums 3600 x 0.01 mm
    TEST_ASSERT_TRUE(h.slot(Metric::RainMm, Tier::Hour, 23).value(station::history::Agg::Sum, v));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 36.0F, v);
    // 24-h window min / max include the closed hours plus the running accumulator
    TEST_ASSERT_TRUE(h.window_min(Metric::TempC, Tier::Hour, 23, v));
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 10.0F, v);
    TEST_ASSERT_TRUE(h.window_max(Metric::TempC, Tier::Hour, 23, v));
    TEST_ASSERT_FLOAT_WITHIN(0.002F, 17.199F, v);
    // Today's running accumulator = everything since the day started
    TEST_ASSERT_TRUE(h.current(Metric::RainMm, Tier::Day).value(station::history::Agg::Sum, v));
    TEST_ASSERT_FLOAT_WITHIN(0.05F, 72.0F, v);

    // A 10-minute outage leaves 10 empty minute slots (no fake data)
    h.advance(7200 / 60 + 10, 7200 / 3600, 0);
    TEST_ASSERT_EQUAL_UINT16(0, h.slot(Metric::TempC, Tier::Minute, 59).n);
    TEST_ASSERT_EQUAL_UINT16(0, h.slot(Metric::TempC, Tier::Minute, 51).n);
    TEST_ASSERT_EQUAL_UINT16(60, h.slot(Metric::TempC, Tier::Minute, 49).n);

    // Hour + day tiers survive a reboot (saved at hour 2, restored 3 hours later -> shifted by 3)
    static std::uint8_t buf[TieredHistory::kBytes];
    const std::size_t n = h.serialize(buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL(h.serialized_size(), n);
    TieredHistory r;
    TEST_ASSERT_TRUE(r.deserialize(buf, n, 2 + 3, 0));
    TEST_ASSERT_TRUE(r.slot(Metric::RainMm, Tier::Hour, 20).value(station::history::Agg::Sum, v));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 36.0F, v);
    TEST_ASSERT_FALSE(r.deserialize(buf, n - 1, 5, 0));
    // Memory budget for all 27 metrics x (60 + 24 + 31) slots
    TEST_ASSERT_TRUE(TieredHistory::kBytes < 52 * 1024);
}

void test_conditioners_vibration_pressure_and_rain_pulses(void) {
    // 20 Hz, 10 mg vertical vibration: PGA ~10 mg, PPV = a / (2 pi f) = 0.098 m/s^2 / 125.7 = 0.78 mm/s
    station::conditioning::VibrationAnalyzer vib;
    station::core::Vec3 g{};
    station::core::VibrationSecond vs{};
    for (int sec = 0; sec < 3; ++sec) {
        for (int i = 0; i < 200; ++i) {
            const float t = static_cast<float>(sec * 200 + i) / 200.0F;
            vib.add({0.0F, 0.0F, 1.0F + 0.010F * std::sin(2.0F * 3.14159265F * 20.0F * t)});
        }
        vib.take_second(g, vs);
    }
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 10.0F, vs.pga_mg);
    TEST_ASSERT_FLOAT_WITHIN(0.15F, 0.78F, vs.ppv_mm_s);
    TEST_ASSERT_FLOAT_WITHIN(1.5F, 20.0F, vs.dominant_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 1.0F, g.z);

    // Barometer: quiet 100 Hz stream, then a 3 Pa thunder N-wave onset
    station::conditioning::PressureAnalyzer pa;
    station::core::ThunderArrival onset{};
    bool heard = false;
    std::uint32_t t_ms = 0;
    for (int i = 0; i < 3000; ++i, t_ms += 10) {
        const float noise = 0.05F * std::sin(static_cast<float>(i) * 1.7F);
        heard = heard || pa.add(100000.0F + noise, t_ms, onset);
    }
    TEST_ASSERT_FALSE(heard);
    for (int i = 0; i < 20; ++i, t_ms += 10) {
        heard = heard || pa.add(100000.0F + (i < 10 ? 3.0F : -3.0F), t_ms, onset);
    }
    TEST_ASSERT_TRUE(heard);
    TEST_ASSERT_TRUE(onset.peak_pa > 2.0F);
    float mean_hpa = 0.0F, ripple = 0.0F;
    TEST_ASSERT_TRUE(pa.take_second(mean_hpa, ripple));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 1000.0F, mean_hpa);

    // Rain AFE: one 400 mV / 250 us pulse on a 206 mV bias
    station::conditioning::RainPulseDetector rd;
    station::core::RainImpact imp{};
    bool got = false;
    for (int i = 0; i < 1600; ++i) {
        const bool pulse = i >= 1000 && i < 1004;  // 4 samples at 16 kHz = 250 us
        got = got || rd.add(pulse ? 606 : 206, static_cast<std::uint32_t>(i / 16), imp);
    }
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_UINT16(606, imp.peak_mv);
    TEST_ASSERT_FLOAT_WITHIN(5.0F, 206.0F, static_cast<float>(imp.baseline_mv));
    TEST_ASSERT_FLOAT_WITHIN(70.0F, 250.0F, static_cast<float>(imp.width_us));
}

void test_pipeline_missing_sensors_publish_null_not_defaults(void) {
    Pipeline p;
    RawState raw{};
    EventQueues ev{};
    StationConfig cfg{};
    DerivedState d{};
    raw.now_ms = 5000;
    p.step(raw, ev, cfg, d);
    const auto doc = parse(TelemetryFormatter::build_state_json(d, "test", WifiInfo{}));
    TEST_ASSERT_TRUE(doc["environment"]["temp_c"].isNull());
    TEST_ASSERT_TRUE(doc["environment"]["qnh_hpa"].isNull());
    TEST_ASSERT_TRUE(doc["wind"]["speed_m_s"].isNull());
    TEST_ASSERT_TRUE(doc["air_quality"]["aqi"].isNull());
    TEST_ASSERT_TRUE(doc["solar"]["uv_index"].isNull());
    TEST_ASSERT_TRUE(doc["seismic"]["ppv_mm_s"].isNull());
    TEST_ASSERT_FALSE(doc["location_set"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("—", doc["environment"]["forecast"].as<const char*>());
}

void test_pipeline_three_hour_run_trends_history_and_publishers(void) {
    Pipeline p;
    RawState raw{};
    EventQueues ev{};
    const StationConfig cfg = vilnius();
    DerivedState d{};
    // Start 2025-12-21 18:00 UTC (20:00 EET, deep night). Pressure falls 1 hPa/h, temperature rises 1 C/h.
    const std::int64_t t0 = 1766340000;
    const int seconds = 3 * 3600 + 6 * 60;
    for (int s = 0; s < seconds; ++s) {
        fill_steady(raw, static_cast<std::uint32_t>(s) * 1000U);
        raw.clock.valid = true;
        raw.clock.unix_s = t0 + s;
        raw.clock.utc_offset_s = 2 * 3600;
        raw.thermo.temp_c.set(15.0F + static_cast<float>(s) / 3600.0F, raw.now_ms);
        raw.baro.pressure_hpa.set(1000.0F - static_cast<float>(s) / 3600.0F, raw.now_ms);
        raw.optical.tsl_lux.set(0.0015F, raw.now_ms);   // dark rural sky
        raw.optical.ltr_uvi.set(0.0F, raw.now_ms);
        p.step(raw, ev, cfg, d);
    }
    // Wind: steady 5 m/s from the west
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 5.0F, d.wind.avg_10m_m_s.v);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 270.0F, d.wind.dir_10m_deg.v);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 5.0F, d.wind.gust_m_s.v);
    TEST_ASSERT_TRUE(d.wind.load_kg_m2.ok);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 1.53F, d.wind.load_kg_m2.v);  // 1/2 rho v^2 / g
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 0.0F, d.wind.dir_change_3h_deg.v);
    // Barometer: 3 h x -1 hPa/h = -3.0 hPa station change. QNH = station x 1.0134 (112 m), and the
    // 3 K warming of the reduction temperature lowers QNH by another ~0.14 hPa -> ~-3.18 hPa.
    TEST_ASSERT_TRUE(d.baro.change_3h_hpa.ok);
    TEST_ASSERT_FLOAT_WITHIN(0.06F, -3.18F, d.baro.change_3h_hpa.v);
    TEST_ASSERT_TRUE(d.baro.tide_3h_hpa.ok);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, d.baro.change_3h_hpa.v - d.baro.tide_3h_hpa.v, d.baro.synoptic_3h_hpa.v);
    // Thermo: trend ~ +1 C/h, 24-h range from the hour tier, wind-chill/heat-index sensible
    TEST_ASSERT_TRUE(d.thermo.trend_c_per_h.ok);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 1.0F, d.thermo.trend_c_per_h.v);
    TEST_ASSERT_FLOAT_WITHIN(0.05F, 15.0F, d.thermo.temp_min_24h_c.v);
    TEST_ASSERT_FLOAT_WITHIN(0.05F, 18.1F, d.thermo.temp_max_24h_c.v);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, d.thermo.solar_offset_c.v);  // night: no radiation error
    // Astronomy / night sky: location + clock known
    TEST_ASSERT_TRUE(d.astro.is_night);
    TEST_ASSERT_TRUE(d.solar.sqm_mag_arcsec2.ok);
    TEST_ASSERT_FALSE(d.solar.cloud_cover_pct.ok);  // no cloud estimate in the dark
    TEST_ASSERT_TRUE(d.astro.sunrise_min.ok && d.astro.moon_illum_pct.ok);
    // Air quality: NowCast AQI from PM2.5 6 ug/m3 (Good), EAQI band 1
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 33.0F, d.air.aqi.v);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 1.0F, d.air.eaqi.v);

    // Publishers: lean state JSON (no 60-min arrays), minute charts with 60 bars, tiered export
    const std::string state = TelemetryFormatter::build_state_json(d, "test", WifiInfo{});
    TEST_ASSERT_NULL(std::strstr(state.c_str(), "_60m"));
    const auto doc = parse(state);
    TEST_ASSERT_FLOAT_WITHIN(0.06F, -3.18F, doc["environment"]["pressure_3h_delta_hpa"].as<float>());
    TEST_ASSERT_TRUE(doc["location_set"].as<bool>());
    const auto minutes = parse(TelemetryFormatter::build_minute_history_json(p.history()));
    TEST_ASSERT_EQUAL_INT(60, static_cast<int>(minutes["rain_mm_h"].size()));
    TEST_ASSERT_EQUAL_INT(60, static_cast<int>(minutes["strikes"].size()));
    const auto hours = parse(TelemetryFormatter::build_history_json(p.history(), Tier::Hour, "temp_c,qnh_hpa,bogus", t0 + seconds));
    TEST_ASSERT_EQUAL_INT(25, static_cast<int>(hours["series"]["temp_c"]["v"].size()));
    TEST_ASSERT_TRUE(hours["series"]["temp_c"]["v"][0].isNull());   // older than the run
    TEST_ASSERT_FALSE(hours["series"]["temp_c"]["v"][24].isNull());
    TEST_ASSERT_TRUE(hours["series"]["bogus"].isNull());

    // HA discovery + RS-485 frame read DerivedState only
    std::string topic, payload;
    TEST_ASSERT_TRUE(TelemetryFormatter::build_ha_discovery(0, "roof01", "homeassistant", topic, payload));
    TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/aeromet_roof01/temperature/config", topic.c_str());
    TEST_ASSERT_NOT_NULL(std::strstr(payload.c_str(), "\"stat_cla\":\"measurement\""));
    TEST_ASSERT_FALSE(TelemetryFormatter::build_ha_discovery(TelemetryFormatter::ha_sensor_count(), "x", "y", topic, payload));
    const std::string frame = Rs485Protocol::encode_telemetry_frame(d);
    TEST_ASSERT_NOT_NULL(std::strstr(frame.c_str(), "$AM,"));
    TEST_ASSERT_NOT_NULL(std::strstr(frame.c_str(), ",5.00,270.0,"));
}

void test_pipeline_daytime_cloud_cover_rain_lightning_and_thunder(void) {
    Pipeline p;
    RawState raw{};
    EventQueues ev{};
    const StationConfig cfg = vilnius();
    DerivedState d{};
    const std::int64_t t0 = kSolsticeNoonUtc - 6120;  // solar noon, sun 58.8 deg
    const float clear = Astronomy::clear_sky_ghi_w_m2(58.75F);
    std::uint32_t strike_ms = 0;
    for (int s = 0; s < 700; ++s) {
        fill_steady(raw, 10000U + static_cast<std::uint32_t>(s) * 1000U);
        raw.clock.valid = true;
        raw.clock.unix_s = t0 + s;
        raw.clock.utc_offset_s = 3 * 3600;
        raw.optical.tsl_lux.set(clear * 0.6F * 122.0F, raw.now_ms);  // 60 % of clear-sky
        raw.optical.ltr_uvi.set(4.0F, raw.now_ms);
        if (s >= 600) {
            for (int k = 0; k < 10; ++k) {
                ev.rain.push({raw.now_ms, 626, 206, 220});
            }
        }
        if (s == 650) {
            strike_ms = raw.now_ms;
            ev.lightning.push({raw.now_ms, 6, 120000});
        }
        if (s == 660) {
            ev.thunder.push({strike_ms + 10000U, 3.0F});  // 10 s flash-to-bang
        }
        p.step(raw, ev, cfg, d);
    }
    TEST_ASSERT_FALSE(d.astro.is_night);
    TEST_ASSERT_TRUE(d.solar.cloud_cover_pct.ok);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 83.1F, d.solar.cloud_cover_pct.v);  // Kasten-Czeplak: 100 (0.4/0.75)^(1/3.4)
    TEST_ASSERT_FALSE(d.solar.sqm_mag_arcsec2.ok);
    TEST_ASSERT_TRUE(d.solar.burn_time_min.ok);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 41.7F, d.solar.burn_time_min.v);
    TEST_ASSERT_TRUE(d.thermo.solar_offset_c.v > 0.1F);  // shield radiation error removed in sun

    TEST_ASSERT_TRUE(d.rain.raining);
    TEST_ASSERT_TRUE(d.rain.rate_mm_h > 0.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.02F, d.rain.today_mm, d.rain.last_hour_mm);

    TEST_ASSERT_EQUAL_UINT32(1, d.lightning.strikes_total);
    TEST_ASSERT_EQUAL_UINT32(1, d.lightning.strikes_1h);
    TEST_ASSERT_EQUAL_STRING("acoustic", d.lightning.method);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 3.434F, d.lightning.distance_km.v);  // 10 s x 343.4 m/s
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 10.0F, d.lightning.thunder_delay_s.v);
    TEST_ASSERT_FLOAT_WITHIN(1.5F, 49.0F, d.lightning.last_strike_age_s.v);
}

void test_constant_wattage_heater_scaling_across_12_to_32v(void) {
    // Freezing outdoor air (-4 C), reflector at 0 C (NTC ~32.6k -> ~2525 mV)
    HeaterSensorInput in_12v{1091, 2525, -4.0F, true};  // 12.0 V -> 1091 mV
    HeaterSensorInput in_24v{2182, 2525, -4.0F, true};  // 24.0 V -> 2182 mV
    HeaterSensorInput in_32v{2909, 2525, -4.0F, true};  // 32.0 V -> 2909 mV

    const auto out_12 = DeIcingHeaterController::update(in_12v);
    const auto out_24 = DeIcingHeaterController::update(in_24v);
    const auto out_32 = DeIcingHeaterController::update(in_32v);

    TEST_ASSERT_EQUAL(HeaterState::Heating, out_12.state);
    TEST_ASSERT_EQUAL(HeaterState::Heating, out_24.state);
    TEST_ASSERT_EQUAL(HeaterState::Heating, out_32.state);

    // At 24 V and 32 V, PWM duty scales down so regulated thermal wattage stays capped at 4.0 W!
    TEST_ASSERT_FLOAT_WITHIN(0.15F, 4.0F, out_24.actual_power_w);
    TEST_ASSERT_FLOAT_WITHIN(0.15F, 4.0F, out_32.actual_power_w);
    TEST_ASSERT_TRUE(out_32.pwm_duty_percent < out_24.pwm_duty_percent);

    // Service button press decoded when NTC_SENSE pulled below 150 mV
    HeaterSensorInput btn{2182, 40, 18.0F, false};
    TEST_ASSERT_TRUE(DeIcingHeaterController::update(btn).service_button_pressed);
}

void test_longevity_sht45_bakeout_and_scd41_extreme_cold_guard(void) {
    EnvLongevityManager mgr;
    // 1. Extreme cold (-35 C) pauses SCD41 photoacoustic pulsing; hysteresis holds pause until >= -8 C
    TEST_ASSERT_TRUE(mgr.tick_second(-35.0F, 70.0F, false).scd41_extreme_cold_pause);
    TEST_ASSERT_TRUE(mgr.tick_second(-9.0F, 70.0F, false).scd41_extreme_cold_pause);
    TEST_ASSERT_FALSE(mgr.tick_second(-7.5F, 70.0F, false).scd41_extreme_cold_pause);

    // 2. 30 minutes of >=96% RH freezing fog triggers 1s SHT45 bake-out pulse + 15s thermal lockout
    for (int m = 0; m < 29; ++m) {
        const auto st = mgr.tick_second(1.5F, 98.0F, true);
        TEST_ASSERT_FALSE(st.sht45_bakeout_pulse);
        TEST_ASSERT_FALSE(st.sht45_reading_locked);
    }
    const auto trig = mgr.tick_second(1.5F, 98.0F, true);
    TEST_ASSERT_TRUE(trig.sht45_bakeout_pulse);
    TEST_ASSERT_TRUE(trig.sht45_reading_locked);
    TEST_ASSERT_EQUAL_UINT8(15, trig.cooldown_seconds);

    // Next 15 seconds count down thermal recovery lockout, then unlock SHT45 readings
    for (int s = 0; s < 14; ++s) {
        TEST_ASSERT_TRUE(mgr.tick_second(1.5F, 98.0F, false).sht45_reading_locked);
    }
    TEST_ASSERT_FALSE(mgr.tick_second(1.5F, 98.0F, false).sht45_reading_locked);
}

void test_reboot_and_factory_reset_button_controller(void) {
    station::firmware::ButtonController btn{};
    // 1. Short press (250 ms) -> triggers Reboot on release
    TEST_ASSERT_EQUAL(station::firmware::ButtonAction::None, btn.update(true, 1000));
    TEST_ASSERT_EQUAL(station::firmware::ButtonAction::None, btn.update(true, 1150));
    TEST_ASSERT_EQUAL(station::firmware::ButtonAction::Reboot, btn.update(false, 1250));

    // 2. Long press (>= 10,000 ms) -> triggers FactoryReset while held and does NOT re-trigger Reboot on release
    TEST_ASSERT_EQUAL(station::firmware::ButtonAction::None, btn.update(true, 5000));
    TEST_ASSERT_EQUAL(station::firmware::ButtonAction::None, btn.update(true, 14999));
    TEST_ASSERT_EQUAL(station::firmware::ButtonAction::FactoryReset, btn.update(true, 15000));
    TEST_ASSERT_EQUAL(station::firmware::ButtonAction::None, btn.update(false, 15500));
}

void test_rs485_command_parsing(void) {
    TEST_ASSERT_EQUAL(CommandType::Ping, Rs485Protocol::parse_command_frame("PING\r\n", 6));
    TEST_ASSERT_EQUAL(CommandType::RequestState, Rs485Protocol::parse_command_frame("STATE?\n", 7));
    TEST_ASSERT_EQUAL(CommandType::ResetDailyRain, Rs485Protocol::parse_command_frame("RESET_RAIN", 10));
    TEST_ASSERT_EQUAL(CommandType::None, Rs485Protocol::parse_command_frame("BAD", 3));
    // Fields without a valid value are left empty rather than sending fake numbers
    DerivedState d{};
    d.uptime_s = 120;
    const std::string frame = Rs485Protocol::encode_telemetry_frame(d);
    TEST_ASSERT_NOT_NULL(std::strstr(frame.c_str(), "$AM,120,,,0.00,0.00,,,,,*"));
}

void test_sgp41_gas_index_algorithm_blackout_baseline_event_and_state(void) {
    station::airquality::GasIndexPair g;
    float v = 0.0F;
    // First 10 s: SGP41 conditioning (VOC only), then 45 s algorithm blackout -> no index yet
    for (int s = 0; s < 40; ++s) {
        g.process(30000, s >= 10, 16000);
    }
    TEST_ASSERT_FALSE(g.voc(v));
    TEST_ASSERT_TRUE(g.learning());
    // Two hours of constant clean air: VOC settles at the 100 offset, NOx at its 1 baseline
    for (int s = 0; s < 2 * 3600; ++s) {
        g.process(30000, true, 16000);
    }
    TEST_ASSERT_TRUE(g.voc(v));
    TEST_ASSERT_FLOAT_WITHIN(3.0F, 100.0F, v);
    TEST_ASSERT_TRUE(g.nox(v));
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 1.0F, v);
    // A VOC event lowers the MOx raw signal: the index rises well above typical air
    for (int s = 0; s < 120; ++s) {
        g.process(27000, true, 16000);
    }
    TEST_ASSERT_TRUE(g.voc(v));
    TEST_ASSERT_TRUE(v > 200.0F);
    // State persistence is only offered after >= 3 h of operation (Sensirion requirement)
    float s0 = 0.0F, s1 = 0.0F;
    TEST_ASSERT_FALSE(g.voc_state(s0, s1));
    for (int s = 0; s < 3600; ++s) {
        g.process(30000, true, 16000);
    }
    TEST_ASSERT_TRUE(g.voc_state(s0, s1));
    station::airquality::GasIndexPair r;
    r.restore_voc_state(s0, s1);
    for (int s = 0; s < 60; ++s) {
        r.process(30000, true, 16000);
    }
    TEST_ASSERT_TRUE(r.voc(v));  // resumes without the 45 s blackout / initial learning
    TEST_ASSERT_FLOAT_WITHIN(10.0F, 100.0F, v);
}

void test_pipeline_gas_source_and_uv_fusion(void) {
    // SEN55 only (SGP41 absent): its on-module indices are used
    Pipeline p;
    RawState raw{};
    EventQueues ev{};
    StationConfig cfg{};
    DerivedState d{};
    fill_steady(raw, 1000);
    raw.optical.ltr_uvi.set(3.0F, 1000);
    p.step(raw, ev, cfg, d);
    TEST_ASSERT_EQUAL_STRING("SEN55", d.air.gas_source);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 100.0F, d.air.voc_index.v);
    TEST_ASSERT_EQUAL_STRING("LTR-390", d.solar.uv_source);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 3.0F, d.solar.uv_index.v);

    // SGP41 present: after the blackout its gas index wins; SEN55 stays published for cross-checking
    Pipeline q;
    for (int s = 0; s < 120; ++s) {
        const std::uint32_t t = 1000U + static_cast<std::uint32_t>(s) * 1000U;
        fill_steady(raw, t);
        raw.air.sgp_sraw_voc.set(30000, t);
        if (s >= 10) raw.air.sgp_sraw_nox.set(16000, t);
        raw.optical.ltr_uvi.set(3.0F, t);
        raw.optical.guvb_uvi.set(5.0F, t);
        q.step(raw, ev, cfg, d);
    }
    TEST_ASSERT_EQUAL_STRING("SGP41", d.air.gas_source);
    TEST_ASSERT_TRUE(d.air.gas_learning);
    TEST_ASSERT_TRUE(d.air.voc_index.ok);
    TEST_ASSERT_TRUE(d.air.voc_index_sen55.ok);
    // UV: 80 % true UV-B sensor + 20 % LTR-390, both per-sensor values published
    TEST_ASSERT_EQUAL_STRING("GUVB-C31SM + LTR-390", d.solar.uv_source);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 4.6F, d.solar.uv_index.v);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 5.0F, d.solar.uv_index_guvb.v);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 3.0F, d.solar.uv_index_ltr.v);
    const auto doc = parse(TelemetryFormatter::build_state_json(d, "t", WifiInfo{}));
    TEST_ASSERT_EQUAL_STRING("SGP41", doc["air_quality"]["gas_source"].as<const char*>());
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 5.0F, doc["solar"]["uv_index_guvb"].as<float>());
}


int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ultrasonic_zero_wind_and_virtual_temp);
    RUN_TEST(test_ultrasonic_soft_wind_and_cycle_slip_lock_up_to_60ms);
    RUN_TEST(test_ultrasonic_cardinal_directions_and_sound_cancellation);
    RUN_TEST(test_ultrasonic_auto_north_and_tilt_compensation);
    RUN_TEST(test_wmo_wind_3s_gust_10min_mean_and_direction_across_north);
    RUN_TEST(test_haptic_rain_rolling_rate_event_and_daily);
    RUN_TEST(test_meteorological_formulas);
    RUN_TEST(test_air_quality_pm_only_aqi_nowcast_and_eaqi);
    RUN_TEST(test_constant_wattage_heater_scaling_across_12_to_32v);
    RUN_TEST(test_longevity_sht45_bakeout_and_scd41_extreme_cold_guard);
    RUN_TEST(test_rs485_command_parsing);
    RUN_TEST(test_reboot_and_factory_reset_button_controller);
    RUN_TEST(test_astronomy_sun_moon_and_clear_sky);
    RUN_TEST(test_tiered_history_rollup_gaps_and_persistence);
    RUN_TEST(test_conditioners_vibration_pressure_and_rain_pulses);
    RUN_TEST(test_pipeline_missing_sensors_publish_null_not_defaults);
    RUN_TEST(test_pipeline_three_hour_run_trends_history_and_publishers);
    RUN_TEST(test_pipeline_daytime_cloud_cover_rain_lightning_and_thunder);
    RUN_TEST(test_sgp41_gas_index_algorithm_blackout_baseline_event_and_state);
    RUN_TEST(test_pipeline_gas_source_and_uv_fusion);
    return UNITY_END();
}
