#include <cstring>
#if __has_include(<unity.h>)
#include <unity.h>
#else
#include <cassert>
#define UNITY_BEGIN() 0
#define UNITY_END() 0
#define RUN_TEST(fn) fn()
#define TEST_ASSERT_TRUE(cond) assert(cond)
#define TEST_ASSERT_FALSE(cond) assert(!(cond))
#define TEST_ASSERT_NOT_NULL(ptr) assert((ptr) != nullptr)
#define TEST_ASSERT_EQUAL(exp, act) assert((exp) == (act))
#define TEST_ASSERT_EQUAL_UINT8(exp, act) assert((exp) == (act))
#define TEST_ASSERT_EQUAL_UINT32(exp, act) assert((exp) == (act))
#define TEST_ASSERT_EQUAL_STRING(exp, act) assert(std::strcmp((exp), (act)) == 0)
#define TEST_ASSERT_FLOAT_WITHIN(delta, exp, act) assert(((act) - (exp)) <= (delta) && ((exp) - (act)) <= (delta))
#endif

#include "../../../src/airquality/AirQuality.hpp"
#include "../../../src/environment/MetCalculations.hpp"
#include "../../../src/firmware/ButtonController.hpp"
#include "../../../src/heater/DeIcingHeater.hpp"
#include "../../../src/rain/HapticRain.hpp"
#include "../../../src/rs485/Rs485Protocol.hpp"
#include "../../../src/telemetry/HomeAssistantMqtt.hpp"
#include "../../../src/wind/UltrasonicWind.hpp"

using station::airquality::AirQualityEvaluator;
using station::airquality::AirQualityInput;
using station::airquality::AqiLevel;
using station::environment::AtmosphereReading;
using station::environment::EnvLongevityManager;
using station::environment::MetCalculator;
using station::heater::DeIcingHeaterController;
using station::heater::HeaterSensorInput;
using station::heater::HeaterState;
using station::rain::HapticRainProcessor;
using station::rain::PrecipitationType;
using station::rain::RainImpactEvent;
using station::rs485::CommandType;
using station::rs485::Rs485Protocol;
using station::telemetry::StationSnapshot;
using station::telemetry::TelemetryFormatter;
using station::wind::ImuCompassReading;
using station::wind::TofQuad;
using station::wind::UltrasonicWindSolver;
using station::wind::WmoWindAggregator;

void setUp(void) {}
void tearDown(void) {}

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

void test_wmo_circular_wind_aggregation_across_north(void) {
    WmoWindAggregator agg;
    agg.push_sample(4.0F, 355.0F);
    agg.push_sample(6.0F, 5.0F);
    agg.push_sample(8.0F, 358.0F);
    const auto sum = agg.summary();
    TEST_ASSERT_FLOAT_WITHIN(0.05F, 6.0F, sum.avg_1min_m_s);
    TEST_ASSERT_FLOAT_WITHIN(0.05F, 8.0F, sum.gust_3s_m_s);
    TEST_ASSERT_FLOAT_WITHIN(0.05F, 4.0F, sum.lull_m_s);
    // Circular mean of 355, 5, 358 deg must be near 0/360 deg, NOT 180 deg!
    TEST_ASSERT_TRUE(sum.mean_dir_deg > 355.0F || sum.mean_dir_deg < 5.0F);
    TEST_ASSERT_TRUE(sum.turbulence_intensity_pct > 5.0F);
    TEST_ASSERT_TRUE(sum.gust_factor > 1.1F);
    // All 3 samples (355, 5, 358 deg) fall into sector 0 (North: 348.75..11.25 deg) => 100% in sector 0!
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 100.0F, sum.rose_freq_pct[0]);

    // Moist air acoustic virtual temperature Tv -> T_true correction:
    // At 30 C and 85% RH, moist air has higher sound speed so T_true < T_virtual by ~2.5 C
    const float t_true = UltrasonicWindSolver::correct_acoustic_temp_for_humidity(30.0F, 85.0F, 1013.25F);
    TEST_ASSERT_TRUE(t_true < 28.2F && t_true > 26.0F);
}

void test_haptic_rain_vibration_rejection_and_accumulation(void) {
    HapticRainProcessor rain;
    // Sub-threshold noise ignored
    TEST_ASSERT_FALSE(rain.process_impact(RainImpactEvent{120, 80, 200, 0.01F}));
    // Dry mast wind-shake rejected by IMU filter
    TEST_ASSERT_FALSE(rain.process_impact(RainImpactEvent{450, 90, 950, 0.35F}));
    TEST_ASSERT_EQUAL_UINT32(1, rain.metrics().rejected_vibrations);

    // Valid raindrop impact on wet capacitive dome counted
    for (int i = 0; i < 20; ++i) {
        TEST_ASSERT_TRUE(rain.process_impact(RainImpactEvent{620, 180, 220, 0.02F}));
    }
    TEST_ASSERT_TRUE(rain.metrics().is_raining);
    TEST_ASSERT_TRUE(rain.metrics().daily_mm > 0.015F);
    TEST_ASSERT_EQUAL_UINT32(20, rain.metrics().drop_count);

    // Sharp high-amplitude impact classified as Hail with estimated diameter (mm)
    TEST_ASSERT_TRUE(rain.process_impact(RainImpactEvent{1850, 180, 110, 0.04F}));
    TEST_ASSERT_EQUAL_UINT32(1, rain.metrics().hail_hits);
    TEST_ASSERT_EQUAL(PrecipitationType::Hail, rain.metrics().state);
    TEST_ASSERT_TRUE(rain.metrics().max_hail_diameter_mm >= 5.0F && rain.metrics().max_hail_diameter_mm <= 15.0F);

    // 15 dry minutes clear active rain flag
    for (int m = 0; m < 16; ++m) {
        rain.tick_minute();
    }
    TEST_ASSERT_FALSE(rain.metrics().is_raining);
    TEST_ASSERT_EQUAL(PrecipitationType::None, rain.metrics().state);

    // Wind-driven oblique rain impact normalization reduces wind-boosted drop volume
    HapticRainProcessor calm_rain;
    HapticRainProcessor windy_rain;
    calm_rain.process_impact(RainImpactEvent{600, 206, 200, 0.01F, 0.0F, 20.0F});
    windy_rain.process_impact(RainImpactEvent{600, 206, 200, 0.01F, 12.0F, 20.0F});
    TEST_ASSERT_TRUE(windy_rain.metrics().daily_mm < calm_rain.metrics().daily_mm);

    // PZT thermal drift compensation: hot dome (+45 C) scales down thermally inflated piezo charge
    HapticRainProcessor cold_dome;
    HapticRainProcessor hot_dome;
    cold_dome.process_impact(RainImpactEvent{600, 206, 200, 0.01F, 0.0F, 0.0F});
    hot_dome.process_impact(RainImpactEvent{600, 206, 200, 0.01F, 0.0F, 45.0F});
    TEST_ASSERT_TRUE(hot_dome.metrics().daily_mm < cold_dome.metrics().daily_mm);
}

void test_meteorological_calculations(void) {
    AtmosphereReading in{};
    in.temperature_c = 22.0F;
    in.humidity_rh = 60.0F;
    in.station_pressure_hpa = 1001.2F;
    in.altitude_m = 100.0F;
    in.wind_speed_m_s = 3.5F;
    in.ambient_lux = 63300.0F;
    in.raw_uv_counts = 11500;

    const auto d = MetCalculator::compute(in);
    TEST_ASSERT_TRUE(d.solar_heating_offset_c > 0.05F);
    TEST_ASSERT_TRUE(d.compensated_temp_c < in.temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(0.6F, 13.7F, d.dew_point_c);
    TEST_ASSERT_TRUE(d.wet_bulb_c > d.dew_point_c && d.wet_bulb_c < d.compensated_temp_c);
    TEST_ASSERT_TRUE(d.static_pressure_hpa > in.station_pressure_hpa);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 1013.0F, d.sea_level_pressure_hpa);
    TEST_ASSERT_FLOAT_WITHIN(5.0F, 500.0F, d.solar_irradiance_w_m2);
    TEST_ASSERT_FLOAT_WITHIN(0.2F, 5.0F, d.uv_index);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 11.5F, d.absolute_humidity_g_m3);

    // Bernoulli dynamic wind-pressure compensation restores ~1.6 hPa suction in 25 m/s gale
    const float p_corr = MetCalculator::compensate_wind_dynamic_pressure_hpa(1000.0F, 25.0F, 15.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.3F, 1000.84F, p_corr);

    // Wind chill below 10 C
    TEST_ASSERT_TRUE(MetCalculator::wind_chill_c(-5.0F, 8.0F) < -10.0F);
    // Heat index above 27 C
    TEST_ASSERT_TRUE(MetCalculator::heat_index_c(33.0F, 75.0F) > 40.0F);
    // VPD, Cloud Base LCL, WBGT, Solar-UV Cloud Cover %, and Zambretti Barometric Forecaster
    TEST_ASSERT_TRUE(d.vpd_kpa > 0.8F && d.vpd_kpa < 1.5F);
    TEST_ASSERT_TRUE(d.cloud_base_m > 800.0F && d.cloud_base_m < 1400.0F);
    TEST_ASSERT_TRUE(d.wbgt_c > d.wet_bulb_c);
    TEST_ASSERT_TRUE(d.cloud_cover_pct > 25.0F && d.cloud_cover_pct < 90.0F);
    // Clear sky (850 W/m2, UV 8.5) has lower cloud cover than overcast sky (120 W/m2, UV 1.8)
    TEST_ASSERT_TRUE(MetCalculator::estimate_cloud_cover_pct(850.0F, 8.5F) <
                     MetCalculator::estimate_cloud_cover_pct(120.0F, 1.8F));
    // UV-degraded VEML7700 epoxy (reading 55k lx when UV-hardened LTR-390UV ALS reads 80k lx) recovers to 80k lx
    const float ltr_lux = MetCalculator::lux_from_ltr390_als(400000U, 3.0F, 100.0F);
    TEST_ASSERT_FLOAT_WITHIN(10.0F, 80000.0F, ltr_lux);
    TEST_ASSERT_FLOAT_WITHIN(10.0F, 80000.0F, MetCalculator::fuse_uv_hardened_lux(55000.0F, ltr_lux));

    station::environment::BarographTracker baro{};
    baro.push_qnh(1018.0F);
    baro.push_qnh(1014.2F);
    TEST_ASSERT_FLOAT_WITHIN(0.05F, -3.8F, baro.delta_3h_hpa());
    const auto trend = MetCalculator::classify_pressure_trend(baro.delta_3h_hpa());
    TEST_ASSERT_EQUAL(station::environment::PressureTrend::FallingRapidly, trend);
    TEST_ASSERT_EQUAL_STRING("Showery, Becoming Unsettled", MetCalculator::zambretti_forecast(1014.2F, trend, 190.0F));
}

void test_air_quality_multi_gas_and_pm_evaluation(void) {
    AirQualityInput clean{3.0F, 5.4F, 7.0F, 12.0F, 95, 2, 418, 50.0F, 1013.25F};
    const auto r_clean = AirQualityEvaluator::evaluate(clean);
    TEST_ASSERT_EQUAL(AqiLevel::Good, r_clean.level);
    TEST_ASSERT_EQUAL_UINT8(1, r_clean.eu_eaqi);
    TEST_ASSERT_TRUE(r_clean.us_epa_aqi <= 50);

    // Elevated NOx traffic plume dominates AQI when PM2.5 is low
    AirQualityInput traffic{4.0F, 6.0F, 8.0F, 15.0F, 140, 180, 460, 55.0F, 1013.25F};
    const auto r_nox = AirQualityEvaluator::evaluate(traffic);
    TEST_ASSERT_EQUAL_STRING("NOx", r_nox.dominant_pollutant);
    TEST_ASSERT_EQUAL(AqiLevel::Unhealthy, r_nox.level);

    // Foggy morning (92% RH) hygroscopic water growth stripped from raw 28 ug/m3 PM2.5 -> Good AQI!
    AirQualityInput foggy{10.0F, 28.0F, 35.0F, 42.0F, 90, 2, 420, 92.0F, 980.0F};
    const auto r_fog = AirQualityEvaluator::evaluate(foggy);
    TEST_ASSERT_TRUE(r_fog.dry_pm2_5_ug_m3 < 8.5F);
    TEST_ASSERT_EQUAL(AqiLevel::Good, r_fog.level);
    TEST_ASSERT_TRUE(r_fog.pressure_comp_co2_ppm > 425);
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

void test_telemetry_and_ha_discovery_json(void) {
    StationSnapshot snap{};
    snap.uptime_s = 3600;
    const std::string state = TelemetryFormatter::build_state_json(snap, "26.10.16");
    TEST_ASSERT_NOT_NULL(std::strstr(state.c_str(), "\"wind\":{"));
    TEST_ASSERT_NOT_NULL(std::strstr(state.c_str(), "\"air_quality\":{"));
    TEST_ASSERT_NOT_NULL(std::strstr(state.c_str(), "\"heater\":{"));

    const std::string disc = TelemetryFormatter::build_ha_discovery_json(
        "roof01", "wind_speed", "Wind Speed", "m/s", "wind_speed", "{{ value_json.wind.speed_m_s }}");
    TEST_ASSERT_NOT_NULL(std::strstr(disc.c_str(), "aeromet_roof01_wind_speed"));
    TEST_ASSERT_NOT_NULL(std::strstr(state.c_str(), "\"external_ufl\":true"));
    TEST_ASSERT_TRUE(TelemetryFormatter::select_external_ufl_antenna(-55, -72));
    TEST_ASSERT_FALSE(TelemetryFormatter::select_external_ufl_antenna(-88, -62));
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

void test_rs485_protocol_framing_and_commands(void) {
    StationSnapshot snap{};
    snap.uptime_s = 120;
    snap.wind.current_speed_m_s = 4.25F;
    snap.wind.current_dir_deg = 215.0F;
    const std::string frame = Rs485Protocol::encode_telemetry_frame(snap);
    TEST_ASSERT_NOT_NULL(std::strstr(frame.c_str(), "$AM,120,4.25,215.0,"));
    TEST_ASSERT_NOT_NULL(std::strstr(frame.c_str(), "*"));
    TEST_ASSERT_EQUAL(CommandType::Ping, Rs485Protocol::parse_command_frame("PING\r\n", 6));
    TEST_ASSERT_EQUAL(CommandType::RequestState, Rs485Protocol::parse_command_frame("STATE?\n", 7));
    TEST_ASSERT_EQUAL(CommandType::ResetDailyRain, Rs485Protocol::parse_command_frame("RESET_RAIN", 10));
    TEST_ASSERT_EQUAL(CommandType::None, Rs485Protocol::parse_command_frame("BAD", 3));
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ultrasonic_zero_wind_and_virtual_temp);
    RUN_TEST(test_ultrasonic_soft_wind_and_cycle_slip_lock_up_to_60ms);
    RUN_TEST(test_ultrasonic_cardinal_directions_and_sound_cancellation);
    RUN_TEST(test_ultrasonic_auto_north_and_tilt_compensation);
    RUN_TEST(test_wmo_circular_wind_aggregation_across_north);
    RUN_TEST(test_haptic_rain_vibration_rejection_and_accumulation);
    RUN_TEST(test_meteorological_calculations);
    RUN_TEST(test_air_quality_multi_gas_and_pm_evaluation);
    RUN_TEST(test_constant_wattage_heater_scaling_across_12_to_32v);
    RUN_TEST(test_telemetry_and_ha_discovery_json);
    RUN_TEST(test_longevity_sht45_bakeout_and_scd41_extreme_cold_guard);
    RUN_TEST(test_rs485_protocol_framing_and_commands);
    RUN_TEST(test_reboot_and_factory_reset_button_controller);
    return UNITY_END();
}
