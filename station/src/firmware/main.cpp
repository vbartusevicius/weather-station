// AeroMet V1 firmware: acquisition tasks -> RawState / events -> 1 Hz Pipeline -> DerivedState -> publishers.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <ArduinoJson.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../conditioning/Conditioners.hpp"
#include "../core/Pipeline.hpp"
#include "../heater/DeIcingHeater.hpp"
#include "../rs485/Rs485Protocol.hpp"
#include "../telemetry/HomeAssistantMqtt.hpp"
#include "../wind/UltrasonicWind.hpp"
#include "Board.hpp"
#include "ButtonController.hpp"
#include "WebAssets.hpp"
#include "drivers/Analog.hpp"
#include "drivers/I2cBus.hpp"
#include "drivers/Sensors.hpp"
#include "drivers/Ultrasonic.hpp"
#include "net/Network.hpp"
#include "storage/Storage.hpp"

#ifndef STATION_FW_VERSION
#define STATION_FW_VERSION "26.11.0"
#endif

namespace {
using namespace station;
using firmware::board::i2c_speed_hz;

// ------------------------------------------------------------------------------------------------
// Shared state. Every access goes through `g_lock`; tasks copy what they need and release it fast.
// ------------------------------------------------------------------------------------------------
SemaphoreHandle_t g_lock = nullptr;
core::RawState g_raw{};
core::EventQueues g_events{};
core::DerivedState g_derived{};
core::StationConfig g_cfg{};
core::Pipeline g_pipeline{};
wind::ImuCompassReading g_orientation{};  // gravity + compass for the ultrasonic north rotation
bool g_history_loaded = false;

struct Lock {
    Lock() { xSemaphoreTake(g_lock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(g_lock); }
};

std::uint32_t now_ms() { return static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL); }

firmware::I2cBus g_i2c;
firmware::Sht4x g_sht;
firmware::Bmp581 g_bmp;
firmware::Scd41 g_scd;
firmware::Sen55 g_sen;
firmware::Sgp41 g_sgp;
firmware::Lsm6ds3 g_imu;
firmware::Mmc5603 g_mag;
firmware::As3935 g_as;
firmware::Ltr390 g_ltr;
firmware::Tsl2591 g_tsl;
firmware::GuvbC31sm g_guvb;
firmware::Ultrasonic g_us;
firmware::AnalogFrontEnd g_adc;
firmware::HeaterPwm g_heater;
firmware::Storage g_storage;
firmware::Network g_net;
firmware::ButtonController g_button;
httpd_handle_t g_http = nullptr;
int g_ws_fds[4]{-1, -1, -1, -1};

// ------------------------------------------------------------------------------------------------
// Acquisition: fast I2C (IMU 200 Hz, barometer 100 Hz FIFO, lightning poll)
// ------------------------------------------------------------------------------------------------
void task_fast_i2c(void*) {
    conditioning::VibrationAnalyzer vib;
    conditioning::PressureAnalyzer baro;
    TickType_t wake = xTaskGetTickCount();
    std::uint32_t tick = 0;
    std::uint32_t last_second = now_ms();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
        ++tick;
        core::Vec3 a{};
        if (g_imu.read_accel(a)) {
            vib.add(a);
        }
        if (tick % 2 == 0) {
            float pa[32];
            const int n = g_bmp.read_fifo(pa, 32);
            const std::uint32_t t = now_ms();
            for (int i = 0; i < n; ++i) {
                core::ThunderArrival onset{};
                if (baro.add(pa[i], t - static_cast<std::uint32_t>((n - 1 - i) * 10), onset)) {
                    Lock l;
                    g_events.thunder.push(onset);
                }
            }
        }
        if (tick % 5 == 0) {
            firmware::As3935Event ev{};
            if (g_as.poll(ev) && ev.kind == firmware::As3935Event::Kind::Lightning) {
                Lock l;
                g_events.lightning.push({now_ms(), ev.distance_km, ev.energy});
            }
        }
        const std::uint32_t t = now_ms();
        if (t - last_second >= 1000U) {
            last_second = t;
            core::Vec3 grav{};
            core::VibrationSecond vs{};
            const bool have_vib = vib.take_second(grav, vs);
            float mean_hpa = 0.0F, ripple = 0.0F;
            const bool have_baro = baro.take_second(mean_hpa, ripple);
            float bt = 0.0F;
            const bool have_bt = g_bmp.read_temperature(bt);
            Lock l;
            if (have_vib) {
                g_raw.imu.gravity_g.set(grav, t);
                g_raw.imu.vibration.set(vs, t);
                g_orientation.accel_x_g = grav.x;
                g_orientation.accel_y_g = grav.y;
                g_orientation.accel_z_g = grav.z;
            }
            if (have_baro) {
                g_raw.baro.pressure_hpa.set(mean_hpa, t);
                g_raw.baro.ripple_pa.set(ripple, t);
            }
            if (have_bt) {
                g_raw.baro.sensor_temp_c.set(bt, t);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Acquisition: slow I2C (SHT40, SEN55, SCD41, dome optics, compass) + service button
// ------------------------------------------------------------------------------------------------
void task_slow_i2c(void*) {
    TickType_t wake = xTaskGetTickCount();
    std::uint32_t tick = 0;
    bool scd_paused = false;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
        ++tick;
        const std::uint32_t t = now_ms();
        core::Controls ctl{};
        float declination = 0.0F;
        float guvb_scale = 0.0F;
        core::Reading<float> sht_t{}, sht_rh{};
        {
            Lock l;
            ctl = g_derived.controls;
            declination = g_cfg.declination_deg;
            guvb_scale = g_cfg.guvb_counts_per_uvi;
            sht_t = g_raw.thermo.temp_c;
            sht_rh = g_raw.thermo.rh_pct;
        }

        const auto btn = g_button.update(gpio_get_level(firmware::board::fab_sw_pin) == 0, t);
        if (btn == firmware::ButtonAction::FactoryReset) {
            g_storage.save_config(core::StationConfig{});
            esp_restart();
        } else if (btn == firmware::ButtonAction::Reboot) {
            esp_restart();
        }

        if (tick % 5 == 0) {  // 2 Hz: dome optics
            bool is_uvs = false;
            float v = 0.0F;
            const bool ltr_ok = g_ltr.step(is_uvs, v);
            float lux = 0.0F, nir = 0.0F;
            const bool tsl_ok = g_tsl.read(lux, nir);
            float uvb_counts = 0.0F, uvb_uvi = 0.0F;
            bool uvb_uvi_ok = false;
            const bool guvb_ok = g_guvb.read(guvb_scale, uvb_counts, uvb_uvi, uvb_uvi_ok);
            Lock l;
            if (guvb_ok) {
                g_raw.optical.guvb_counts.set(uvb_counts, t);
                if (uvb_uvi_ok) g_raw.optical.guvb_uvi.set(uvb_uvi, t);
            }
            if (ltr_ok && is_uvs) g_raw.optical.ltr_uvi.set(v, t);
            if (ltr_ok && !is_uvs) g_raw.optical.ltr_lux.set(v, t);
            if (tsl_ok) {
                g_raw.optical.tsl_lux.set(lux, t);
                g_raw.optical.tsl_nir_ratio.set(nir, t);
            }
        }
        if (tick % 10 != 0) {
            continue;
        }
        // 1 Hz. SGP41 first so its 1-s cadence stays regular; compensated with the latest SHT40 reading
        // (same probe, air the MOx plate actually sees, so the raw value rather than the radiation-corrected one).
        std::uint16_t sraw_voc = 0, sraw_nox = 0;
        bool nox_ok = false;
        const bool comp_ok = sht_t.fresh(t, 5000) && sht_rh.fresh(t, 5000);
        const bool sgp_ok = g_sgp.measure(comp_ok, sht_rh.value, sht_t.value, sraw_voc, sraw_nox, nox_ok);
        float tc = 0.0F, rh = 0.0F;
        const bool sht_ok = ctl.sht_bakeout ? g_sht.heater_pulse(tc, rh) : g_sht.measure(tc, rh);
        firmware::Sen55Data pm{};
        const bool pm_ok = g_sen.read(pm);
        float co2 = 0.0F, st = 0.0F, srh = 0.0F;
        if (ctl.scd41_pause != scd_paused) {
            scd_paused = ctl.scd41_pause;
            scd_paused ? g_scd.stop() : g_scd.start();
        }
        const bool co2_ok = !scd_paused && g_scd.data_ready() && g_scd.read(co2, st, srh);
        if (tick % 600 == 0 && ctl.ambient_pressure_hpa > 0.0F) {
            g_scd.set_ambient_pressure(ctl.ambient_pressure_hpa);  // in-sensor CO2 pressure compensation
        }
        core::Vec3 mag{};
        const bool mag_ok = g_mag.read(mag);

        Lock l;
        if (sht_ok && !ctl.sht_bakeout) {  // the reading right after a heater pulse is not ambient
            g_raw.thermo.temp_c.set(tc, t);
            g_raw.thermo.rh_pct.set(rh, t);
        }
        if (pm_ok) {
            auto put = [t](const core::Value<float>& v, core::Reading<float>& r) {
                if (v.ok) r.set(v.v, t);
            };
            put(pm.pm1, g_raw.air.pm1_ug_m3);
            put(pm.pm25, g_raw.air.pm25_ug_m3);
            put(pm.pm4, g_raw.air.pm4_ug_m3);
            put(pm.pm10, g_raw.air.pm10_ug_m3);
            put(pm.voc, g_raw.air.voc_index);
            put(pm.nox, g_raw.air.nox_index);
        }
        if (co2_ok) {
            g_raw.air.co2_ppm.set(co2, t);
        }
        if (sgp_ok) {
            g_raw.air.sgp_sraw_voc.set(sraw_voc, t);
            if (nox_ok) g_raw.air.sgp_sraw_nox.set(sraw_nox, t);
        }
        if (mag_ok) {
            g_orientation.mag_x_ut = mag.x;
            g_orientation.mag_y_ut = mag.y;
            g_orientation.mag_z_ut = mag.z;
            g_orientation.declination_deg = declination;
            g_raw.wind.heading_deg.set(wind::UltrasonicWindSolver::compute_north_heading_deg(g_orientation), t);
        }
        if (g_raw.imu.gravity_g.valid) {
            g_raw.wind.tilt_deg.set(wind::UltrasonicWindSolver::compute_mast_tilt_deg(g_orientation), t);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Acquisition: ultrasonic wind (4 quads per second)
// ------------------------------------------------------------------------------------------------
void task_ultrasonic(void*) {
    conditioning::WindSecondAccumulator acc;
    TickType_t wake = xTaskGetTickCount();
    std::uint32_t last_second = now_ms();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(250));
        wind::ImuCompassReading orient{};
        wind::ZeroWindCalibration cal{};
        float delay_us = 0.0F;
        {
            Lock l;
            orient = g_orientation;
            cal.dt_ns_offset_us = g_cfg.dt_ns_offset_us;
            cal.dt_ew_offset_us = g_cfg.dt_ew_offset_us;
            delay_us = g_cfg.us_delay_us;
        }
        wind::TofQuad q{};
        if (g_us.measure_quad(q, 3, delay_us)) {
            const auto s = wind::UltrasonicWindSolver::solve(q, orient, cal);
            if (s.valid) {
                acc.add(s.v_east_m_s, s.v_north_m_s, s.speed_m_s, s.sound_speed_m_s, s.acoustic_temp_c);
            }
        }
        const std::uint32_t t = now_ms();
        if (t - last_second >= 1000U) {
            last_second = t;
            core::WindSecond w{};
            if (acc.take(w)) {
                Lock l;
                g_raw.wind.second.set(w, t);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Acquisition: analog (rain piezo pulses, supply voltage, reflector NTC / service button)
// ------------------------------------------------------------------------------------------------
void task_analog(void*) {
    conditioning::RainPulseDetector rain;
    core::RainImpact batch[32];
    std::uint32_t last_second = now_ms();
    for (;;) {
        int n = 0;
        const std::uint32_t t_frame = now_ms();
        g_adc.pump(20, [&](std::uint16_t mv) {
            core::RainImpact imp{};
            if (rain.add(mv, t_frame, imp) && n < 32) {
                batch[n++] = imp;
            }
        });
        const std::uint32_t t = now_ms();
        float vin_mv = 0.0F, ntc_mv = 0.0F;
        const bool second = t - last_second >= 1000U;
        const bool means = second && g_adc.take_means(vin_mv, ntc_mv);
        if (n > 0 || second) {
            Lock l;
            for (int i = 0; i < n; ++i) {
                g_events.rain.push(batch[i]);
            }
            if (second) {
                last_second = t;
                g_raw.power.rain_adc_ok = g_adc.running();
            }
            if (means) {
                g_raw.power.vin_v.set(heater::DeIcingHeaterController::decode_supply_voltage_v(static_cast<std::uint16_t>(vin_mv)), t);
                g_raw.power.service_button = ntc_mv < heater::DeIcingHeaterController::kServiceBtnThresholdMv;
                if (!g_raw.power.service_button) {
                    g_raw.power.reflector_c.set(heater::DeIcingHeaterController::decode_ntc_temperature_c(static_cast<std::uint16_t>(ntc_mv)), t);
                }
            }
        }
        if (!g_adc.running()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

// ------------------------------------------------------------------------------------------------
// RS-485 (auto-direction transceiver on UART1)
// ------------------------------------------------------------------------------------------------
void init_rs485() {
    uart_config_t c{};
    c.baud_rate = static_cast<int>(rs485::Rs485Protocol::kDefaultBaudRate);
    c.data_bits = UART_DATA_8_BITS;
    c.parity = UART_PARITY_DISABLE;
    c.stop_bits = UART_STOP_BITS_1;
    c.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    c.source_clk = UART_SCLK_DEFAULT;
    uart_driver_install(UART_NUM_1, 512, 512, 0, nullptr, 0);
    uart_param_config(UART_NUM_1, &c);
    uart_set_pin(UART_NUM_1, firmware::board::rs485_io_pin, firmware::board::rs485_io_pin, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE);
}

void rs485_service(const core::DerivedState& d) {
    char rx[64];
    const int n = uart_read_bytes(UART_NUM_1, rx, sizeof(rx) - 1, 0);
    if (n > 0) {
        rx[n] = '\0';
        const auto cmd = rs485::Rs485Protocol::parse_command_frame(rx, static_cast<std::size_t>(n));
        if (cmd == rs485::CommandType::Ping) {
            static constexpr char kPong[] = "$AM,PONG*0000\r\n";
            uart_write_bytes(UART_NUM_1, kPong, sizeof(kPong) - 1);
        } else if (cmd == rs485::CommandType::ResetDailyRain) {
            Lock l;
            g_pipeline.reset_rain_today();
        }
    }
    const std::string frame = rs485::Rs485Protocol::encode_telemetry_frame(d);
    uart_write_bytes(UART_NUM_1, frame.c_str(), frame.size());
}

void ws_broadcast(const std::string& json) {
    if (g_http == nullptr) {
        return;
    }
    httpd_ws_frame_t pkt{};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.payload = reinterpret_cast<std::uint8_t*>(const_cast<char*>(json.c_str()));
    pkt.len = json.size();
    for (int& fd : g_ws_fds) {
        if (fd != -1 && httpd_ws_send_frame_async(g_http, fd, &pkt) != ESP_OK) {
            fd = -1;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Pipeline task: 1 Hz processing, controls, persistence, publishing
// ------------------------------------------------------------------------------------------------
void task_pipeline(void*) {
    static core::RawState raw{};
    static core::EventQueues ev{};
    static core::DerivedState d{};
    TickType_t wake = xTaskGetTickCount();
    std::int64_t last_saved_hour = -1;
    std::uint32_t seconds = 0;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
        ++seconds;
        const bool clock_ok = firmware::Network::clock_valid();
        const std::int64_t unix_s = firmware::Network::unix_now();
        std::string state;
        {
            Lock l;
            g_raw.now_ms = now_ms();
            g_raw.clock.valid = clock_ok;
            g_raw.clock.unix_s = unix_s;
            g_raw.clock.utc_offset_s = clock_ok ? firmware::Network::utc_offset_s(unix_s) : 0;
            raw = g_raw;
            ev = g_events;
            g_events = core::EventQueues{};
            g_pipeline.step(raw, ev, g_cfg, d);
            g_derived = d;
            // Restore persisted hour/day history once the wall clock is known.
            if (clock_ok && !g_history_loaded) {
                g_history_loaded = true;
                g_storage.load_history(g_pipeline.history());
                last_saved_hour = g_pipeline.history().hour_idx();
                // Sensirion: resume the VOC learning state only after an interruption < 10 min.
                float s0 = 0.0F, s1 = 0.0F;
                std::int64_t saved = 0;
                if (g_storage.load_voc_state(s0, s1, saved) && unix_s >= saved && unix_s - saved <= 600) {
                    g_pipeline.restore_voc_state(s0, s1);
                }
            }
            float s0 = 0.0F, s1 = 0.0F;
            if (clock_ok && seconds % 300 == 0 && g_pipeline.voc_state(s0, s1)) {
                g_storage.save_voc_state(s0, s1, unix_s);
            }
            if (clock_ok && g_pipeline.history().hour_idx() != last_saved_hour) {
                last_saved_hour = g_pipeline.history().hour_idx();
                g_storage.save_history(g_pipeline.history());
            }
        }
        g_heater.set_duty_pct(d.controls.heater_duty_pct);
        state = telemetry::TelemetryFormatter::build_state_json(d, STATION_FW_VERSION, g_net.wifi_info());
        ws_broadcast(state);
        rs485_service(d);
        core::StationConfig cfg{};
        {
            Lock l;
            cfg = g_cfg;
        }
        g_net.tick_1s(cfg);
        if (seconds % 10 == 0) {
            g_net.mqtt_publish_state(state);  // Home Assistant keeps its own history
        }
    }
}

// ------------------------------------------------------------------------------------------------
// HTTP API
// ------------------------------------------------------------------------------------------------
esp_err_t send_json(httpd_req_t* req, const std::string& json) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json.c_str(), static_cast<ssize_t>(json.size()));
}

// Embedded, gzip-compressed Web UI (see scripts/pack_webui.py).
esp_err_t send_gz(httpd_req_t* req, const char* mime, const std::uint8_t* data, std::size_t len) {
    httpd_resp_set_type(req, mime);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, reinterpret_cast<const char*>(data), static_cast<ssize_t>(len));
}
esp_err_t handle_index(httpd_req_t* req) {
    return send_gz(req, "text/html; charset=utf-8", firmware::web_assets::index_html_gz, firmware::web_assets::index_html_gz_size);
}
esp_err_t handle_css(httpd_req_t* req) {
    return send_gz(req, "text/css; charset=utf-8", firmware::web_assets::app_css_gz, firmware::web_assets::app_css_gz_size);
}
esp_err_t handle_js(httpd_req_t* req) {
    return send_gz(req, "application/javascript; charset=utf-8", firmware::web_assets::app_js_gz, firmware::web_assets::app_js_gz_size);
}

esp_err_t handle_state(httpd_req_t* req) {
    core::DerivedState d{};
    {
        Lock l;
        d = g_derived;
    }
    return send_json(req, telemetry::TelemetryFormatter::build_state_json(d, STATION_FW_VERSION, g_net.wifi_info()));
}

esp_err_t handle_minutes(httpd_req_t* req) {
    std::string json;
    {
        Lock l;
        json = telemetry::TelemetryFormatter::build_minute_history_json(g_pipeline.history());
    }
    return send_json(req, json);
}

// GET /api/history?tier=minute|hour|day&metrics=temp_c,qnh_hpa
esp_err_t handle_history(httpd_req_t* req) {
    char query[256]{};
    char tier_s[12]{"hour"};
    char metrics[200]{};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "tier", tier_s, sizeof(tier_s));
        httpd_query_key_value(query, "metrics", metrics, sizeof(metrics));
    }
    const auto tier = std::strcmp(tier_s, "minute") == 0 ? history::Tier::Minute
                      : (std::strcmp(tier_s, "day") == 0 ? history::Tier::Day : history::Tier::Hour);
    std::string json;
    {
        Lock l;
        json = telemetry::TelemetryFormatter::build_history_json(g_pipeline.history(), tier, metrics, g_derived.unix_s);
    }
    return send_json(req, json);
}

esp_err_t handle_config_get(httpd_req_t* req) {
    core::StationConfig c{};
    {
        Lock l;
        c = g_cfg;
    }
    JsonDocument doc;
    doc["location_set"] = c.location_set;
    doc["latitude"] = c.latitude_deg;
    doc["longitude"] = c.longitude_deg;
    doc["altitude_m"] = c.altitude_m;
    doc["tz"] = c.tz_posix;
    doc["declination_deg"] = c.declination_deg;
    doc["dt_ns_offset_us"] = c.dt_ns_offset_us;
    doc["dt_ew_offset_us"] = c.dt_ew_offset_us;
    doc["us_delay_us"] = c.us_delay_us;
    doc["pressure_wind_coef"] = c.pressure_wind_coef;
    doc["rain_gain"] = c.rain_gain;
    doc["guvb_counts_per_uvi"] = c.guvb_counts_per_uvi;
    doc["guvb_factory_b_scale"] = g_guvb.factory_b_scale();
    doc["as3935_tun_cap"] = c.as3935_tun_cap;
    doc["wifi_ssid"] = c.wifi_ssid;
    doc["mqtt_uri"] = c.mqtt_uri;
    doc["ha_prefix"] = c.ha_prefix;
    std::string out;
    serializeJson(doc, out);
    return send_json(req, out);
}

esp_err_t handle_config_post(httpd_req_t* req) {
    char body[768]{};
    const int n = httpd_req_recv(req, body, sizeof(body) - 1);
    JsonDocument in;
    if (n <= 0 || deserializeJson(in, body, static_cast<std::size_t>(n)) != DeserializationError::Ok) {
        return send_json(req, "{\"ok\":false,\"error\":\"bad json\"}");
    }
    bool reboot = false;
    core::StationConfig c{};
    {
        Lock l;
        c = g_cfg;
    }
    auto copy = [](char* dst, std::size_t cap, const char* src) {
        if (src != nullptr) {
            std::strncpy(dst, src, cap - 1);
            dst[cap - 1] = '\0';
        }
    };
    if (in["latitude"].is<double>() && in["longitude"].is<double>()) {
        const double lat = in["latitude"].as<double>();
        const double lon = in["longitude"].as<double>();
        if (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
            c.latitude_deg = lat;
            c.longitude_deg = lon;
            c.location_set = true;
        }
    }
    if (in["altitude_m"].is<float>()) c.altitude_m = in["altitude_m"].as<float>();
    if (in["tz"].is<const char*>()) copy(c.tz_posix, sizeof(c.tz_posix), in["tz"].as<const char*>());
    if (in["declination_deg"].is<float>()) c.declination_deg = in["declination_deg"].as<float>();
    if (in["dt_ns_offset_us"].is<float>()) c.dt_ns_offset_us = in["dt_ns_offset_us"].as<float>();
    if (in["dt_ew_offset_us"].is<float>()) c.dt_ew_offset_us = in["dt_ew_offset_us"].as<float>();
    if (in["us_delay_us"].is<float>()) c.us_delay_us = in["us_delay_us"].as<float>();
    if (in["pressure_wind_coef"].is<float>()) c.pressure_wind_coef = in["pressure_wind_coef"].as<float>();
    if (in["rain_gain"].is<float>()) c.rain_gain = in["rain_gain"].as<float>();
    if (in["guvb_counts_per_uvi"].is<float>()) c.guvb_counts_per_uvi = std::max(0.0F, in["guvb_counts_per_uvi"].as<float>());
    if (in["as3935_tun_cap"].is<int>()) c.as3935_tun_cap = static_cast<std::uint8_t>(in["as3935_tun_cap"].as<int>() & 0x0F);
    if (in["mqtt_uri"].is<const char*>()) {
        copy(c.mqtt_uri, sizeof(c.mqtt_uri), in["mqtt_uri"].as<const char*>());
        reboot = true;
    }
    if (in["ha_prefix"].is<const char*>()) copy(c.ha_prefix, sizeof(c.ha_prefix), in["ha_prefix"].as<const char*>());
    if (in["wifi_ssid"].is<const char*>()) {
        copy(c.wifi_ssid, sizeof(c.wifi_ssid), in["wifi_ssid"].as<const char*>());
        copy(c.wifi_pass, sizeof(c.wifi_pass), in["wifi_pass"] | "");
        reboot = true;
    }
    {
        Lock l;
        g_cfg = c;
    }
    firmware::Network::apply_timezone(c.tz_posix);
    const bool saved = g_storage.save_config(c);
    return send_json(req, std::string("{\"ok\":") + (saved ? "true" : "false") + ",\"reboot_required\":" + (reboot ? "true" : "false") + "}");
}

esp_err_t handle_action(httpd_req_t* req) {
    char body[192]{};
    const int n = httpd_req_recv(req, body, sizeof(body) - 1);
    JsonDocument in;
    if (n <= 0 || deserializeJson(in, body, static_cast<std::size_t>(n)) != DeserializationError::Ok) {
        return send_json(req, "{\"ok\":false}");
    }
    const char* action = in["action"] | "";
    if (std::strcmp(action, "reset_rain") == 0) {
        Lock l;
        g_pipeline.reset_rain_today();
    } else if (std::strcmp(action, "set_time") == 0) {
        firmware::Network::set_clock_from_browser(in["unix"] | static_cast<std::int64_t>(0));
    } else if (std::strcmp(action, "reboot") == 0) {
        {
            Lock l;
            g_storage.save_history(g_pipeline.history());
        }
        send_json(req, "{\"ok\":true}");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }
    return send_json(req, std::string("{\"ok\":true,\"action\":\"") + action + "\"}");
}

esp_err_t handle_ws(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        const int fd = httpd_req_to_sockfd(req);
        for (int& slot : g_ws_fds) {
            if (slot == -1 || slot == fd) {
                slot = fd;
                break;
            }
        }
        return ESP_OK;
    }
    httpd_ws_frame_t pkt{};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    return httpd_ws_recv_frame(req, &pkt, 0);
}

void start_http() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 8192;
    if (httpd_start(&g_http, &config) != ESP_OK) {
        return;
    }
    const httpd_uri_t uris[] = {
        {"/", HTTP_GET, handle_index, nullptr, false, false, nullptr},
        {"/app.css", HTTP_GET, handle_css, nullptr, false, false, nullptr},
        {"/app.js", HTTP_GET, handle_js, nullptr, false, false, nullptr},
        {"/api/state", HTTP_GET, handle_state, nullptr, false, false, nullptr},
        {"/api/history/minutes", HTTP_GET, handle_minutes, nullptr, false, false, nullptr},
        {"/api/history", HTTP_GET, handle_history, nullptr, false, false, nullptr},
        {"/api/config", HTTP_GET, handle_config_get, nullptr, false, false, nullptr},
        {"/api/config", HTTP_POST, handle_config_post, nullptr, false, false, nullptr},
        {"/api/action", HTTP_POST, handle_action, nullptr, false, false, nullptr},
        {"/ws", HTTP_GET, handle_ws, nullptr, true, false, nullptr},
    };
    for (const auto& u : uris) {
        httpd_register_uri_handler(g_http, &u);
    }
}

void init_gpio() {
    gpio_config_t out{};
    out.mode = GPIO_MODE_OUTPUT;
    out.pin_bit_mask = (1ULL << firmware::board::us_sel_a_pin) | (1ULL << firmware::board::us_sel_b_pin) |
                       (1ULL << firmware::board::rf_switch_power_pin) | (1ULL << firmware::board::rf_antenna_select_pin);
    gpio_config(&out);
    gpio_set_level(firmware::board::rf_switch_power_pin, 0);   // enable the FM8625H RF switch
    gpio_set_level(firmware::board::rf_antenna_select_pin, 1); // external U.FL dipole
    gpio_config_t btn{};
    btn.mode = GPIO_MODE_INPUT;
    btn.pull_up_en = GPIO_PULLUP_ENABLE;
    btn.pin_bit_mask = 1ULL << firmware::board::fab_sw_pin;
    gpio_config(&btn);
}
}  // namespace

extern "C" void app_main(void) {
    g_lock = xSemaphoreCreateMutex();
    g_storage.begin();
    g_storage.load_config(g_cfg);
    init_gpio();
    init_rs485();

    namespace b = firmware::board;
    g_i2c.init(b::i2c_sda_pin, b::i2c_scl_pin, i2c_speed_hz);
    g_sht.begin(g_i2c, b::i2c_addr_sht4x);
    g_bmp.begin(g_i2c, b::i2c_addr_bmp581);
    g_scd.begin(g_i2c, b::i2c_addr_scd41);
    g_sen.begin(g_i2c, b::i2c_addr_sen55);
    g_sgp.begin(g_i2c, b::i2c_addr_sgp41);
    g_imu.begin(g_i2c, b::i2c_addr_lsm6ds3);
    g_mag.begin(g_i2c, b::i2c_addr_mmc5603);
    g_as.begin(g_i2c, b::i2c_addr_as3935, g_cfg.as3935_tun_cap, g_cfg.as3935_indoor);
    g_ltr.begin(g_i2c, b::i2c_addr_ltr390);
    g_tsl.begin(g_i2c, b::i2c_addr_tsl25911);
    g_guvb.begin(g_i2c, b::i2c_addr_guvb_c31sm);
    g_us.begin(b::us_tx_pin, b::us_rx_pin, b::us_sel_a_pin, b::us_sel_b_pin);
    g_adc.begin();
    g_heater.begin(b::heat_pwm_pin);

    g_net.begin(g_cfg);
    start_http();

    xTaskCreate(task_analog, "analog", 4096, nullptr, 7, nullptr);
    xTaskCreate(task_fast_i2c, "fast_i2c", 4096, nullptr, 6, nullptr);
    xTaskCreate(task_ultrasonic, "ultrasonic", 4096, nullptr, 5, nullptr);
    xTaskCreate(task_slow_i2c, "slow_i2c", 6144, nullptr, 4, nullptr);
    xTaskCreate(task_pipeline, "pipeline", 12288, nullptr, 3, nullptr);
}
