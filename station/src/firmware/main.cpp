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
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../calibration/Calibration.hpp"
#include "../conditioning/Conditioners.hpp"
#include "../core/Pipeline.hpp"
#include "../crown/CrownDecode.hpp"
#include "../heater/DeIcingHeater.hpp"
#include "../rs485/Rs485Protocol.hpp"
#include "../telemetry/HomeAssistantMqtt.hpp"
#include "../wind/UltrasonicWind.hpp"
#include "../wind/WindCalibration.hpp"
#include "Board.hpp"
#include "ButtonController.hpp"
#include "CrownImage.hpp"
#include "WebAssets.hpp"
#include "drivers/Analog.hpp"
#include "drivers/CrownLink.hpp"
#include "drivers/I2cBus.hpp"
#include "drivers/Sensors.hpp"
#include "net/Network.hpp"
#include "storage/Storage.hpp"

// Injected by scripts/gen_version.py ($FW_VERSION or `git describe`); "dev" only if that script did not run.
#ifndef STATION_FW_VERSION
#define STATION_FW_VERSION "dev"
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
// Home calibration sessions (/api/calibration, Web UI "Calibrate"); raw magnetometer before its correction
calibration::Calibrator g_cal;
core::Vec3 g_mag_raw{};
bool g_mag_raw_ok = false;

struct Lock {
    Lock() { xSemaphoreTake(g_lock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(g_lock); }
};

std::uint32_t now_ms() { return static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL); }

firmware::I2cBus g_i2c;
firmware::Sht4x g_sht;
firmware::Bmp581 g_bmp;
firmware::Scd41 g_scd;
firmware::Pmsa003i g_pm;
firmware::Sgp41 g_sgp;
firmware::Ads1115 g_heatmon;
firmware::Lsm6ds3 g_imu;
firmware::Mmc5603 g_mag;
firmware::Ltr390 g_ltr;
firmware::Tsl2591 g_tsl;
firmware::GuvbC31sm g_guvb;
firmware::CrownLink g_crown;
std::string g_rs485_frame;   // latest $AM telemetry frame, answered on STATE?
rs485::BusStats g_rs485_stats;   // bus activity (Setup -> RS-485)
std::string g_rs485_last_reply;
// Still-air wind calibration (`wind_calibrate` action): raw quads are collected for kWindCalS seconds
constexpr std::uint32_t kWindCalS = 60;
wind::StillAirCalibrator g_wind_cal;
bool g_wind_cal_running = false;
std::uint32_t g_wind_cal_start_ms = 0;
std::string g_wind_cal_status = "never run";
std::string g_crown_update = "not needed";   // boot-time crown auto-update result (/api/config "crown")
// Crown diagnostics for commissioning (/api/config "crown"): refreshed by task_crown
crown::DiagBlock g_crown_diag{};
bool g_crown_diag_ok = false;
crown::RainEvent g_last_rain{};
std::uint32_t g_rain_events = 0, g_strike_events = 0, g_disturber_events = 0;
bool g_crown_bursts = true;
firmware::AnalogFrontEnd g_adc;
firmware::HeaterPwm g_heater;
firmware::Storage g_storage;
firmware::PartitionFlash g_flash;   // "storage" data partition
history::HistoryStore g_store;      // closed hours (14 d) and days (6 months) on raw flash
// Records (highs / lows) to NVS; hours / days are appended to g_store as they close (caller holds the lock)
void persist_history() {
    g_storage.save_records(g_pipeline.climate());
}
firmware::Network g_net;
firmware::ButtonController g_button;
httpd_handle_t g_http = nullptr;
int g_ws_fds[4]{-1, -1, -1, -1};

// ------------------------------------------------------------------------------------------------
// Acquisition: fast I2C (IMU 200 Hz, barometer 100 Hz FIFO)
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
// Acquisition: slow I2C (SHT40, SGP41, PMSA003I, SCD41, dome optics, compass) + service button
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
        core::StationConfig cal_cfg{};
        float guvb_scale = 0.0F;
        float optics_tr = 1.0F;
        core::Reading<float> sht_t{}, sht_rh{};
        {
            Lock l;
            ctl = g_derived.controls;
            declination = g_cfg.declination_deg;
            cal_cfg = g_cfg;
            guvb_scale = g_cfg.guvb_counts_per_uvi;
            optics_tr = g_cfg.optics_transmission;
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

        // Heater power monitor: one ADS1115 channel per 100 ms tick (conversion takes 62.5 ms)
        {
            namespace b = firmware::board;
            using R = firmware::Ads1115::Range;
            const std::uint32_t phase = tick % 10;
            float v = 0.0F;
            if (phase == 1) {
                g_heatmon.start(0, R::Fs256mV);
            } else if (phase >= 2 && phase <= 5) {
                const bool ok = g_heatmon.read(v);
                if (phase == 2) g_heatmon.start(1, R::Fs256mV);
                if (phase == 3) g_heatmon.start(2, R::Fs4096mV);
                if (phase == 4) g_heatmon.start(3, R::Fs4096mV);
                if (ok) {
                    Lock l;
                    if (phase == 2) g_raw.power.refl_heater_a.set(v / b::refl_shunt_ohm, t);
                    if (phase == 3) g_raw.power.probe_heater_a.set(v / b::probe_shunt_ohm, t);
                    if (phase == 4) g_raw.power.rail_heat.set(v * b::rail_heat_divider, t);
                    if (phase == 5) g_raw.power.rail_5v_logic.set(v * b::rail5_divider, t);
                }
            }
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
            // free-sky values: the cover (PTFE diffuser / JGS1) transmission; a field GUVB scale already includes it
            using core::cover_corrected;
            Lock l;
            if (guvb_ok) {
                g_raw.optical.guvb_counts.set(uvb_counts, t);
                if (uvb_uvi_ok) g_raw.optical.guvb_uvi.set(guvb_scale > 0.0F ? uvb_uvi : cover_corrected(uvb_uvi, optics_tr), t);
            }
            if (ltr_ok && is_uvs) g_raw.optical.ltr_uvi.set(cover_corrected(v, optics_tr), t);
            if (ltr_ok && !is_uvs) g_raw.optical.ltr_lux.set(cover_corrected(v, optics_tr), t);
            if (tsl_ok) {
                g_raw.optical.tsl_lux.set(cover_corrected(lux, optics_tr), t);
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
        firmware::PmData pm{};
        const bool pm_ok = g_pm.read(t, pm);
        float co2 = 0.0F, st = 0.0F, srh = 0.0F;
        if (ctl.scd41_pause != scd_paused) {
            scd_paused = ctl.scd41_pause;
            scd_paused ? g_scd.stop() : g_scd.start();
        }
        const bool co2_ok = !scd_paused && g_scd.data_ready() && g_scd.read(co2, st, srh);
        float frc_ppm = 0.0F;
        bool frc_req = false;
        {
            Lock l;
            frc_req = g_cal.take_frc_request(frc_ppm);
        }
        if (frc_req) {   // applied `co2_fresh_air` calibration: SCD41 forced recalibration (stops / restarts it)
            int corr = 0;
            const bool frc_ok = !scd_paused && g_scd.forced_recalibration(frc_ppm, corr);
            Lock l;
            g_cal.report_frc(frc_ok, corr);
        }
        if (tick % 600 == 0 && ctl.ambient_pressure_hpa > 0.0F) {
            g_scd.set_ambient_pressure(ctl.ambient_pressure_hpa);  // in-sensor CO2 pressure compensation
        }
        core::Vec3 mag{};
        const bool mag_ok = g_mag.read(mag);

        Lock l;
        if (sht_ok && !ctl.sht_bakeout) {  // the reading right after a heater pulse is not ambient
            g_raw.thermo.temp_c.set(tc + cal_cfg.temp_offset_c, t);
            g_raw.thermo.rh_pct.set(std::min(100.0F, std::max(0.0F, rh + cal_cfg.rh_offset_pct)), t);
        }
        if (pm_ok) {
            g_raw.air.pm1_ug_m3.set(pm.pm1, t);
            g_raw.air.pm25_ug_m3.set(pm.pm25, t);
            g_raw.air.pm10_ug_m3.set(pm.pm10, t);
        }
        if (sgp_ok) {
            g_raw.air.sgp_sraw_voc.set(sraw_voc, t);
            if (nox_ok) g_raw.air.sgp_sraw_nox.set(sraw_nox, t);
        }
        if (co2_ok) {
            g_raw.air.co2_ppm.set(co2, t);
            g_raw.air.scd41_temp_c.set(st, t);
        }
        if (mag_ok) {
            // home-calibrated compass (hard / soft iron, N-mark offset), or a fixed heading that ignores it:
            // a zero field makes compute_north_heading_deg return the "declination" term alone
            g_mag_raw = mag;
            g_mag_raw_ok = true;
            const bool fixed = cal_cfg.heading_mode == 1;
            const core::Vec3 m = fixed ? core::Vec3{} : calibration::corrected_mag(mag, cal_cfg);
            g_orientation.mag_x_ut = m.x;
            g_orientation.mag_y_ut = m.y;
            g_orientation.mag_z_ut = m.z;
            g_orientation.declination_deg = fixed ? cal_cfg.heading_fixed_deg : declination + cal_cfg.heading_offset_deg;
            g_raw.wind.heading_deg.set(wind::UltrasonicWindSolver::compute_north_heading_deg(g_orientation), t);
        }
        if (g_raw.imu.gravity_g.valid) {
            g_raw.wind.tilt_deg.set(wind::UltrasonicWindSolver::compute_mast_tilt_deg(g_orientation), t);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Acquisition: crown STM32 (wind quads 4/s, rain impacts, lightning) over I2C + CROWN_IRQ
// ------------------------------------------------------------------------------------------------
// Feeds the still-air calibrator with raw quads and finishes it after kWindCalS seconds (task_crown context).
void wind_calibration_step(const wind::TofQuad& raw) {
    Lock l;
    if (!g_wind_cal_running) {
        return;
    }
    g_wind_cal.add(raw);
    if (now_ms() - g_wind_cal_start_ms < kWindCalS * 1000U) {
        return;
    }
    g_wind_cal_running = false;
    const std::uint32_t t = now_ms();
    if (!g_raw.thermo.temp_c.fresh(t, 10000) || !g_raw.thermo.rh_pct.fresh(t, 10000)) {
        g_wind_cal_status = "failed: no SHT40 temperature / humidity";
        return;
    }
    const float p = g_raw.baro.pressure_hpa.fresh(t, 10000) ? g_raw.baro.pressure_hpa.value : 1013.25F;
    const auto r = g_wind_cal.result(g_raw.thermo.temp_c.value, g_raw.thermo.rh_pct.value, p);
    char msg[160];
    std::snprintf(msg, sizeof msg, "%s: %lu quads, delay %.2f us, dt_ns %.3f us, dt_ew %.3f us, spread %.3f us",
                  r.ok ? "ok" : r.reason, static_cast<unsigned long>(r.quads), static_cast<double>(r.us_delay_us),
                  static_cast<double>(r.zero.dt_ns_offset_us), static_cast<double>(r.zero.dt_ew_offset_us),
                  static_cast<double>(r.dt_spread_us));
    g_wind_cal_status = msg;
    if (r.ok) {
        g_cfg.us_delay_us = r.us_delay_us;
        g_cfg.dt_ns_offset_us = r.zero.dt_ns_offset_us;
        g_cfg.dt_ew_offset_us = r.zero.dt_ew_offset_us;
        g_storage.save_config(g_cfg);
    }
}

void task_crown(void*) {
    conditioning::WindSecondAccumulator acc;
    TickType_t wake = xTaskGetTickCount();
    std::uint32_t last_second = now_ms();
    std::uint32_t last_seq = 0;
    std::uint32_t tick = 0;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
        ++tick;
        if (g_crown.flashing()) {
            continue;
        }
        // CROWN_IRQ gives low latency; the 250 ms status poll keeps wind flowing even with a broken IRQ wire
        std::uint16_t status = 0;
        if ((g_crown.irq() || tick % 3 == 0) && g_crown.read_status(status)) {
            const std::uint32_t t_now = now_ms();
            if (status & crown::kStWindNew) {
                crown::WindBlock w{};
                wind::TofQuad q{};
                crown_link::WindQuality wq{};
                if (g_crown.read_wind(w) && w.seq != last_seq && crown_link::to_tof_quad(w, q, wq)) {
                    last_seq = w.seq;
                    wind_calibration_step(q);
                    wind::ImuCompassReading orient{};
                    wind::ZeroWindCalibration cal{};
                    float delay_us = 0.0F, gain = 1.0F;
                    {
                        Lock l;
                        orient = g_orientation;
                        cal.dt_ns_offset_us = g_cfg.dt_ns_offset_us;
                        cal.dt_ew_offset_us = g_cfg.dt_ew_offset_us;
                        delay_us = g_cfg.us_delay_us;
                        gain = g_cfg.wind_speed_gain;
                    }
                    q.t_ns_us -= delay_us;
                    q.t_sn_us -= delay_us;
                    q.t_ew_us -= delay_us;
                    q.t_we_us -= delay_us;
                    const auto s = wind::UltrasonicWindSolver::solve(q, orient, cal);
                    if (s.valid) {
                        const float g = gain;
                        acc.add(g * s.v_east_m_s, g * s.v_north_m_s, g * s.speed_m_s, s.sound_speed_m_s, s.acoustic_temp_c);
                    }
                }
            }
            if (status & crown::kStRainPending) {
                crown::RainEvent ev[16];
                const int n = g_crown.pop_rain(ev, 16);
                Lock l;
                g_rain_events += static_cast<std::uint32_t>(n);
                if (n > 0) g_last_rain = ev[n - 1];
                for (int i = 0; i < n; ++i) {
                    g_events.rain.push(crown_link::to_rain_impact(ev[i], t_now, g_cfg.rain_impact_scale));
                    g_cal.add_rain_peak(ev[i].peak_mv);
                }
            }
            if (status & crown::kStStrikePending) {
                crown::StrikeEvent ev[8];
                const int n = g_crown.pop_strikes(ev, 8);
                Lock l;
                for (int i = 0; i < n; ++i) {
                    core::LightningStrike ls{};
                    (ev[i].kind == 1 ? g_strike_events : g_disturber_events)++;
                    (ev[i].kind == 1 ? g_raw.crown.strikes : g_raw.crown.disturbers)++;
                    if (crown_link::to_strike(ev[i], t_now, ls)) {
                        g_events.lightning.push(ls);
                    }
                }
            }
        }
        if (tick % 50 == 0) {   // every 5 s: crown diagnostics
            crown::DiagBlock dg{};
            const bool ok = g_crown.read_diag(dg);
            Lock l;
            g_crown_diag_ok = ok;
            if (ok) {
                g_crown_diag = dg;
                g_raw.crown.as3935_lco_hz.set(static_cast<std::uint32_t>(dg.lco_hz10) * 10U, now_ms());
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
// Acquisition: analog (supply voltage, reflector NTC / service button)
// ------------------------------------------------------------------------------------------------
void task_analog(void*) {
    std::uint32_t last_second = now_ms();
    for (;;) {
        g_adc.pump(50);
        const std::uint32_t t = now_ms();
        float vin_mv = 0.0F, ntc_mv = 0.0F;
        const bool second = t - last_second >= 1000U;
        const bool means = second && g_adc.take_means(vin_mv, ntc_mv);
        if (second) {
            Lock l;
            last_second = t;
            g_raw.power.rain_adc_ok = g_crown.present();   // rain is digitised on the crown
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
    // Separate TX / RX pins; the driver asserts RTS (-> SP3485 DE + /RE) only while transmitting.
    uart_set_pin(UART_NUM_1, firmware::board::rs485_tx_pin, firmware::board::rs485_rx_pin, firmware::board::rs485_de_pin,
                 UART_PIN_NO_CHANGE);
    uart_set_mode(UART_NUM_1, UART_MODE_RS485_HALF_DUPLEX);
}

// Poll / response only (the station never talks unsolicited, so it can share a bus with a polling master):
// one command per line; STATE? returns the latest telemetry frame, PING returns PONG.
void task_rs485(void*) {
    char line[64];
    std::size_t len = 0;
    for (;;) {
        char c = 0;
        if (uart_read_bytes(UART_NUM_1, &c, 1, pdMS_TO_TICKS(200)) != 1) {
            continue;
        }
        if (c != '\n') {
            len = (len < sizeof(line) - 1) ? len + 1 : 0;   // overlong garbage: start over
            if (len) line[len - 1] = c;
            continue;
        }
        std::uint8_t address = 0;
        {
            Lock l;
            address = g_cfg.rs485_address;
        }
        const auto cmd = rs485::Rs485Protocol::parse_command(line, len, address);
        {
            Lock l;
            g_rs485_stats.note(cmd, line, len, now_ms());
        }
        len = 0;
        std::string reply;
        if (cmd == rs485::CommandType::Ping) {
            reply = rs485::Rs485Protocol::encode_pong(address);
        } else if (cmd == rs485::CommandType::RequestState) {
            Lock l;
            reply = g_rs485_frame;
        }
        if (!reply.empty()) {
            uart_write_bytes(UART_NUM_1, reply.c_str(), reply.size());
            Lock l;
            ++g_rs485_stats.replies;
            g_rs485_last_reply = reply;
        }
        if (cmd == rs485::CommandType::ResetDailyRain) {
            Lock l;
            g_pipeline.reset_rain_today();
        } else if (cmd == rs485::CommandType::ForceHeaterCheck) {
            Lock l;
            g_pipeline.request_heater_check();
        }
    }
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
    std::uint32_t seconds = 0;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
        ++seconds;
        if (seconds == 60) {
            esp_ota_mark_app_valid_cancel_rollback();   // 60 s of normal operation: keep this firmware
        }
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
            g_cal.add_sample(calibration::make_sample(raw, d, g_cfg, g_mag_raw_ok ? &g_mag_raw : nullptr), g_cfg);
            // Once the wall clock is known: 24-h cache, today and the wind rose from the store, records from NVS.
            if (clock_ok && !g_history_loaded) {
                g_history_loaded = true;
                g_pipeline.restore(g_store);
                g_storage.load_records(g_pipeline.climate());
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
            // Closed hours / days -> flash (one ~630-byte record each); records follow every closed hour
            history::Tier tier{};
            history::ClosedInterval rec{};
            while (g_pipeline.take_record(tier, rec)) {
                g_store.append(tier, rec);
                if (tier == history::Tier::Hour) persist_history();
            }
        }
        g_heater.set_duty_pct(d.controls.heater_duty_pct);
        state = telemetry::TelemetryFormatter::build_state_json(d, STATION_FW_VERSION, g_net.wifi_info());
        ws_broadcast(state);
        {
            Lock l;
            g_rs485_frame = rs485::Rs485Protocol::encode_telemetry_frame(d, g_cfg.rs485_address);
        }
        core::StationConfig cfg{};
        {
            Lock l;
            cfg = g_cfg;
        }
        g_net.tick_1s(cfg);
        if (seconds % std::max<std::uint32_t>(1U, cfg.mqtt_interval_s) == 0) {
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
    char n_s[8]{};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "tier", tier_s, sizeof(tier_s));
        httpd_query_key_value(query, "metrics", metrics, sizeof(metrics));
        httpd_query_key_value(query, "n", n_s, sizeof(n_s));   // number of closed intervals (default 60 / 24 / 31)
    }
    const auto tier = std::strcmp(tier_s, "minute") == 0 ? history::Tier::Minute
                      : (std::strcmp(tier_s, "day") == 0 ? history::Tier::Day : history::Tier::Hour);
    std::string json;
    {
        Lock l;
        json = telemetry::TelemetryFormatter::build_history_json(g_pipeline.history(), tier, metrics, g_derived.unix_s,
                                                                 static_cast<std::size_t>(std::atoi(n_s)));
    }
    return send_json(req, json);
}

// GET /api/history.csv?tier=minute|hour|day&metrics=a,b: the same history as a spreadsheet download.
esp_err_t handle_history_csv(httpd_req_t* req) {
    char query[256]{};
    char tier_s[12]{"day"};
    char metrics[200]{};
    char n_s[8]{};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "tier", tier_s, sizeof(tier_s));
        httpd_query_key_value(query, "metrics", metrics, sizeof(metrics));
        httpd_query_key_value(query, "n", n_s, sizeof(n_s));
    }
    const auto tier = std::strcmp(tier_s, "minute") == 0 ? history::Tier::Minute
                      : (std::strcmp(tier_s, "hour") == 0 ? history::Tier::Hour : history::Tier::Day);
    std::string csv;
    {
        Lock l;
        csv = telemetry::TelemetryFormatter::build_history_csv(g_pipeline.history(), tier, metrics, g_derived.utc_offset_s,
                                                               static_cast<std::size_t>(std::atoi(n_s)));
    }
    char disp[64];
    std::snprintf(disp, sizeof disp, "attachment; filename=\"aeromet-%s.csv\"", tier_s);
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, csv.c_str(), static_cast<ssize_t>(csv.size()));
}

// GET /api/records: highs / lows for today, yesterday, this month and all time + the change since ~24 h ago.
esp_err_t handle_records(httpd_req_t* req) {
    std::string json;
    {
        Lock l;
        json = telemetry::TelemetryFormatter::build_records_json(g_pipeline.climate(), g_pipeline.history(), g_derived);
    }
    return send_json(req, json);
}

// GET /api/windrose?range=1h|24h|7d|14d|31d|6mo (the Trends ranges)
esp_err_t handle_windrose(httpd_req_t* req) {
    char query[64]{};
    char range[8]{"24h"};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "range", range, sizeof(range));
    }
    std::string json;
    {
        Lock l;
        json = telemetry::TelemetryFormatter::build_windrose_json(g_pipeline.history(), g_pipeline.climate(), range);
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
    doc["rain_impact_scale"] = c.rain_impact_scale;
    doc["rs485_address"] = c.rs485_address;
    doc["wind_speed_gain"] = c.wind_speed_gain;
    {
        Lock l;
        JsonObject cr = doc["crown"].to<JsonObject>();
        cr["present"] = g_crown.present();
        cr["auto_update"] = g_crown_update.c_str();
        char img[16];
        std::snprintf(img, sizeof img, "%u.%u.%u", firmware::kCrownImageVersion[0], firmware::kCrownImageVersion[1],
                      firmware::kCrownImageVersion[2]);
        cr["embedded_fw"] = firmware::kCrownImageLen ? img : "none";
        cr["bursts"] = g_crown_bursts;
        cr["diag_ok"] = g_crown_diag_ok;
        cr["uptime_s"] = g_crown_diag.uptime_s;
        cr["bursts_per_s"] = g_crown_diag.bursts_per_s;
        cr["as3935_tun_cap"] = g_crown_diag.tun_cap;
        cr["as3935_lco_hz"] = static_cast<std::uint32_t>(g_crown_diag.lco_hz10) * 10U;
        cr["i2c_errors"] = g_crown_diag.i2c_errors;
        cr["rain_events"] = g_rain_events;
        cr["last_rain_peak_mv"] = g_last_rain.peak_mv;
        cr["last_rain_width_us"] = g_last_rain.width_us;
        cr["strikes"] = g_strike_events;
        cr["disturbers"] = g_disturber_events;
    }
    {
        Lock l;
        doc["wind_cal_status"] = g_wind_cal_running ? std::string("running") : g_wind_cal_status;   // copied
    }
    doc["guvb_counts_per_uvi"] = c.guvb_counts_per_uvi;
    doc["optics_transmission"] = c.optics_transmission;
    doc["guvb_factory_b_scale"] = g_guvb.factory_b_scale();
    doc["as3935_tun_cap"] = c.as3935_tun_cap;
    doc["heading_mode"] = c.heading_mode;
    doc["heading_fixed_deg"] = c.heading_fixed_deg;
    doc["heading_offset_deg"] = c.heading_offset_deg;
    doc["temp_offset_c"] = c.temp_offset_c;
    doc["rh_offset_pct"] = c.rh_offset_pct;
    doc["as3935_indoor"] = c.as3935_indoor;
    {
        JsonObject ota = doc["ota"].to<JsonObject>();
        const esp_partition_t* run = esp_ota_get_running_partition();
        const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
        ota["version"] = STATION_FW_VERSION;
        ota["running"] = run != nullptr ? run->label : "?";
        ota["max_bytes"] = next != nullptr ? next->size : 0U;
    }
    doc["wifi_ssid"] = c.wifi_ssid;
    doc["mqtt_uri"] = c.mqtt_uri;
    doc["mqtt_user"] = c.mqtt_user;
    doc["mqtt_pass_set"] = c.mqtt_pass[0] != '\0';   // passwords are write-only
    doc["mqtt_topic"] = c.mqtt_topic;
    doc["ha_discovery"] = c.ha_discovery != 0;
    doc["mqtt_interval_s"] = c.mqtt_interval_s;
    doc["ha_prefix"] = c.ha_prefix;
    {
        JsonObject mq = doc["mqtt"].to<JsonObject>();
        mq["configured"] = g_net.mqtt_configured();
        mq["connected"] = g_net.mqtt_connected();
        mq["state_topic"] = g_net.state_topic();
        mq["node_id"] = g_net.node_id();
    }
    {
        Lock l;
        JsonObject rs = doc["rs485"].to<JsonObject>();
        const rs485::BusStats& b = g_rs485_stats;
        rs["address"] = c.rs485_address;
        rs["baud"] = rs485::Rs485Protocol::kDefaultBaudRate;
        rs["lines"] = b.lines;
        rs["ignored"] = b.ignored;
        rs["pings"] = b.pings;
        rs["states"] = b.states;
        rs["resets"] = b.resets;
        rs["heat_checks"] = b.heat_checks;
        rs["replies"] = b.replies;
        if (b.last_ms != 0) rs["last_cmd_age_s"] = (now_ms() - b.last_ms) / 1000U;
        rs["last_cmd"] = b.last_cmd;
        rs["last_reply"] = g_rs485_last_reply.c_str();
        rs["frame"] = g_rs485_frame.c_str();
        JsonArray fields = rs["fields"].to<JsonArray>();
        for (std::size_t i = 0; i < rs485::Rs485Protocol::kFrameFieldCount; ++i) {
            const rs485::FrameField& f = rs485::Rs485Protocol::frame_fields()[i];
            JsonObject o = fields.add<JsonObject>();
            o["key"] = f.key;
            o["label"] = f.label;
            o["unit"] = f.unit;
        }
    }
    std::string out;
    serializeJson(doc, out);
    return send_json(req, out);
}

esp_err_t handle_config_post(httpd_req_t* req) {
    char body[1024]{};
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
    if (in["rain_impact_scale"].is<float>()) c.rain_impact_scale = std::max(0.05F, in["rain_impact_scale"].as<float>());
    if (in["wind_speed_gain"].is<float>()) {
        const float g = in["wind_speed_gain"].as<float>();
        if (g > 0.5F && g < 2.0F) c.wind_speed_gain = g;
    }
    if (in["rs485_address"].is<int>()) {
        const int a = in["rs485_address"].as<int>();
        if (a >= 0 && a <= 247) c.rs485_address = static_cast<std::uint8_t>(a);
    }
    if (in["guvb_counts_per_uvi"].is<float>()) c.guvb_counts_per_uvi = std::max(0.0F, in["guvb_counts_per_uvi"].as<float>());
    if (in["optics_transmission"].is<float>()) {
        const float tr = in["optics_transmission"].as<float>();
        if (tr >= 0.02F && tr <= 1.0F) c.optics_transmission = tr;
    }
    if (in["heading_mode"].is<int>()) c.heading_mode = in["heading_mode"].as<int>() == 1 ? 1 : 0;
    if (in["heading_fixed_deg"].is<float>()) c.heading_fixed_deg = calibration::wrap360(in["heading_fixed_deg"].as<float>());
    if (in["heading_offset_deg"].is<float>()) c.heading_offset_deg = calibration::wrap180(in["heading_offset_deg"].as<float>());
    if (in["temp_offset_c"].is<float>()) c.temp_offset_c = std::min(3.0F, std::max(-3.0F, in["temp_offset_c"].as<float>()));
    if (in["rh_offset_pct"].is<float>()) c.rh_offset_pct = std::min(10.0F, std::max(-10.0F, in["rh_offset_pct"].as<float>()));
    if (in["reset_compass"] | false) {
        c.mag_offset_ut[0] = c.mag_offset_ut[1] = c.mag_offset_ut[2] = 0.0F;
        c.mag_scale[0] = c.mag_scale[1] = 1.0F;
    }
    if (in["as3935_tun_cap"].is<int>()) c.as3935_tun_cap = static_cast<std::uint8_t>(in["as3935_tun_cap"].as<int>() & 0x0F);
    if (in["mqtt_uri"].is<const char*>()) {
        copy(c.mqtt_uri, sizeof(c.mqtt_uri), in["mqtt_uri"].as<const char*>());
        reboot = true;
    }
    if (in["ha_prefix"].is<const char*>()) {
        copy(c.ha_prefix, sizeof(c.ha_prefix), in["ha_prefix"].as<const char*>());
        reboot = true;
    }
    if (in["mqtt_user"].is<const char*>()) {
        copy(c.mqtt_user, sizeof(c.mqtt_user), in["mqtt_user"].as<const char*>());
        reboot = true;
    }
    if (in["mqtt_pass"].is<const char*>()) {   // sent only when the user typed a new one
        copy(c.mqtt_pass, sizeof(c.mqtt_pass), in["mqtt_pass"].as<const char*>());
        reboot = true;
    }
    if (in["mqtt_topic"].is<const char*>()) {
        copy(c.mqtt_topic, sizeof(c.mqtt_topic), in["mqtt_topic"].as<const char*>());
        reboot = true;
    }
    if (in["ha_discovery"].is<bool>()) {
        c.ha_discovery = in["ha_discovery"].as<bool>() ? 1 : 0;
        reboot = true;
    }
    if (in["mqtt_interval_s"].is<int>()) {
        c.mqtt_interval_s = static_cast<std::uint16_t>(std::min(3600, std::max(1, in["mqtt_interval_s"].as<int>())));
    }
    if (in["as3935_indoor"].is<bool>()) c.as3935_indoor = in["as3935_indoor"].as<bool>();
    if (in["wifi_ssid"].is<const char*>()) {
        copy(c.wifi_ssid, sizeof(c.wifi_ssid), in["wifi_ssid"].as<const char*>());
        if (in["wifi_pass"].is<const char*>()) {   // absent = keep the stored password
            copy(c.wifi_pass, sizeof(c.wifi_pass), in["wifi_pass"].as<const char*>());
        }
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
    } else if (std::strcmp(action, "heater_check") == 0) {
        Lock l;
        g_pipeline.request_heater_check();
    } else if (std::strcmp(action, "wind_calibrate") == 0) {
        // still air required (station covered by a closed box or calm room) for kWindCalS seconds
        Lock l;
        g_wind_cal.reset();
        g_wind_cal_start_ms = now_ms();
        g_wind_cal_running = true;
    } else if (std::strcmp(action, "crown_bursts_off") == 0 || std::strcmp(action, "crown_bursts_on") == 0) {
        // lightning-interference test (CALIBRATION.md): pause / resume the ultrasonic bursts
        const bool on = std::strcmp(action, "crown_bursts_on") == 0;
        crown::ConfigBlock cc{};
        cc.quads_per_s = on ? 4 : 0;
        cc.burst_cycles = 8;
        cc.as3935_outdoor = g_cfg.as3935_indoor ? 0 : 1;
        cc.as3935_min_strikes = 1;
        cc.rain_trigger_mv = 40;
        if (g_crown.send_config(cc)) {
            Lock l;
            g_crown_bursts = on;
        }
    } else if (std::strcmp(action, "set_time") == 0) {
        firmware::Network::set_clock_from_browser(in["unix"] | static_cast<std::int64_t>(0));
    } else if (std::strcmp(action, "reboot") == 0) {
        {
            Lock l;
            persist_history();
        }
        send_json(req, "{\"ok\":true}");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    } else {
        return send_json(req, "{\"ok\":false,\"error\":\"unknown action\"}");   // e.g. the simulator-only set_scenario
    }
    return send_json(req, std::string("{\"ok\":true,\"action\":\"") + action + "\"}");
}

// GET /api/calibration: home-calibration session status; POST {"cmd": start|finish|apply|cancel|clear_runs, ...}
esp_err_t handle_calibration(httpd_req_t* req) {
    char body[256]{};
    if (req->method == HTTP_POST) {
        const int n = httpd_req_recv(req, body, sizeof(body) - 1);
        if (n <= 0) return send_json(req, "{\"ok\":false,\"error\":\"empty body\"}");
    }
    const std::int64_t unix_s = firmware::Network::clock_valid() ? firmware::Network::unix_now() : 0;
    bool changed = false;
    core::StationConfig c{};
    std::string out;
    {
        Lock l;
        out = g_cal.handle(body, g_cfg, unix_s, now_ms(), changed);
        c = g_cfg;
    }
    if (changed) g_storage.save_config(c);
    return send_json(req, out);
}

// POST /api/firmware (body = station firmware.bin): writes the inactive OTA slot, verifies the image (esp_ota_end
// checks the header and SHA-256), boots it and reboots. With rollback enabled the new image must reach
// mark_app_valid() (task_pipeline, after 60 s of normal operation) or the bootloader returns to the old one.
esp_err_t handle_firmware(httpd_req_t* req) {
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    const std::size_t len = req->content_len;
    if (part == nullptr || len < 1024 || len > part->size) {
        return send_json(req, "{\"ok\":false,\"error\":\"image size does not fit the OTA slot\"}");
    }
    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(part, len, &ota) != ESP_OK) {
        return send_json(req, "{\"ok\":false,\"error\":\"cannot start the update\"}");
    }
    static char buf[4096];
    std::size_t got = 0;
    bool first = true;
    while (got < len) {
        const int r = httpd_req_recv(req, buf, std::min(sizeof(buf), len - got));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            esp_ota_abort(ota);
            return ESP_FAIL;
        }
        if (first && static_cast<std::uint8_t>(buf[0]) != 0xE9) {   // ESP image magic: refuse crown / random files
            esp_ota_abort(ota);
            return send_json(req, "{\"ok\":false,\"error\":\"not an ESP32 firmware image (use the station firmware.bin)\"}");
        }
        first = false;
        if (esp_ota_write(ota, buf, static_cast<std::size_t>(r)) != ESP_OK) {
            esp_ota_abort(ota);
            return send_json(req, "{\"ok\":false,\"error\":\"flash write failed\"}");
        }
        got += static_cast<std::size_t>(r);
    }
    if (esp_ota_end(ota) != ESP_OK) {
        return send_json(req, "{\"ok\":false,\"error\":\"image check failed (wrong or damaged file)\"}");
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        return send_json(req, "{\"ok\":false,\"error\":\"cannot select the new image\"}");
    }
    {
        Lock l;
        persist_history();
    }
    send_json(req, "{\"ok\":true,\"reboot\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// POST /api/crown/firmware (body = crown .bin for 0x08000000): reflash the crown STM32 through its ROM bootloader.
esp_err_t handle_crown_firmware(httpd_req_t* req) {
    const std::size_t len = req->content_len;
    if (len == 0 || len > 128U * 1024U) {
        return send_json(req, "{\"ok\":false,\"error\":\"image must be 1..131072 bytes\"}");
    }
    auto* image = static_cast<std::uint8_t*>(std::malloc(len));
    if (image == nullptr) {
        return send_json(req, "{\"ok\":false,\"error\":\"out of memory\"}");
    }
    std::size_t got = 0;
    while (got < len) {
        const int r = httpd_req_recv(req, reinterpret_cast<char*>(image) + got, len - got);
        if (r <= 0) {
            std::free(image);
            return ESP_FAIL;
        }
        got += static_cast<std::size_t>(r);
    }
    const auto result = g_crown.flash(image, len);
    std::free(image);
    char msg[96];
    std::snprintf(msg, sizeof msg, "{\"ok\":%s,\"result\":%d,\"crown_present\":%s}",
                  result == crown_link::StmI2cBootloader::Result::Ok ? "true" : "false", static_cast<int>(result),
                  g_crown.present() ? "true" : "false");
    return send_json(req, msg);
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
    config.max_uri_handlers = 20;
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
        {"/api/history.csv", HTTP_GET, handle_history_csv, nullptr, false, false, nullptr},
        {"/api/records", HTTP_GET, handle_records, nullptr, false, false, nullptr},
        {"/api/windrose", HTTP_GET, handle_windrose, nullptr, false, false, nullptr},
        {"/api/config", HTTP_GET, handle_config_get, nullptr, false, false, nullptr},
        {"/api/config", HTTP_POST, handle_config_post, nullptr, false, false, nullptr},
        {"/api/action", HTTP_POST, handle_action, nullptr, false, false, nullptr},
        {"/api/crown/firmware", HTTP_POST, handle_crown_firmware, nullptr, false, false, nullptr},
        {"/api/firmware", HTTP_POST, handle_firmware, nullptr, false, false, nullptr},
        {"/api/calibration", HTTP_GET, handle_calibration, nullptr, false, false, nullptr},
        {"/api/calibration", HTTP_POST, handle_calibration, nullptr, false, false, nullptr},
        {"/ws", HTTP_GET, handle_ws, nullptr, true, false, nullptr},
    };
    for (const auto& u : uris) {
        httpd_register_uri_handler(g_http, &u);
    }
}

void init_gpio() {
    gpio_config_t out{};
    out.mode = GPIO_MODE_OUTPUT;
    out.pin_bit_mask = (1ULL << firmware::board::rf_switch_power_pin) | (1ULL << firmware::board::rf_antenna_select_pin);
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
    // First start after LittleFS (or a layout change): the store erases and re-initialises the partition (~4 s once).
    if (g_flash.begin() && g_store.begin(&g_flash)) {
        g_pipeline.history().attach(&g_store);
    }
    init_gpio();
    init_rs485();

    namespace b = firmware::board;
    g_i2c.init(b::i2c_sda_pin, b::i2c_scl_pin, i2c_speed_hz);
    g_sht.begin(g_i2c, b::i2c_addr_sht4x);
    g_bmp.begin(g_i2c, b::i2c_addr_bmp581);
    g_scd.begin(g_i2c, b::i2c_addr_scd41);
    g_pm.begin(g_i2c, b::i2c_addr_pmsa003i, now_ms());
    g_sgp.begin(g_i2c, b::i2c_addr_sgp41);
    g_heatmon.begin(g_i2c, b::i2c_addr_ads1115);
    g_imu.begin(g_i2c, b::i2c_addr_lsm6ds3);
    g_mag.begin(g_i2c, b::i2c_addr_mmc5603);
    g_ltr.begin(g_i2c, b::i2c_addr_ltr390);
    g_tsl.begin(g_i2c, b::i2c_addr_tsl25911);
    g_guvb.begin(g_i2c, b::i2c_addr_guvb_c31sm);
    const bool crown_up = g_crown.begin(g_i2c, b::crown_irq_pin, b::crown_nrst_pin, b::crown_boot0_pin);
    {
        // Keep the crown on the firmware this station release was built with; a factory-blank crown (no answer at
        // 0x2A) is programmed here through BOOT0 + the ROM I2C bootloader. One attempt per boot.
        std::uint8_t ver[4]{};
        const bool have_ver = crown_up && g_crown.version(ver);
        if (crown_link::crown_needs_update(have_ver ? ver : nullptr, firmware::kCrownImageVersion,
                                           firmware::kCrownImageLen)) {
            const auto r = g_crown.flash(firmware::kCrownImage, firmware::kCrownImageLen);
            char msg[64];
            std::snprintf(msg, sizeof msg, "%s -> %u.%u.%u (result %d)", have_ver ? "updated" : "programmed",
                          firmware::kCrownImageVersion[0], firmware::kCrownImageVersion[1],
                          firmware::kCrownImageVersion[2], static_cast<int>(r));
            g_crown_update = msg;
        }
    }
    if (g_crown.present()) {
        crown::ConfigBlock cc{};
        cc.quads_per_s = 4;
        cc.burst_cycles = 8;
        cc.as3935_outdoor = g_cfg.as3935_indoor ? 0 : 1;
        cc.as3935_min_strikes = 1;
        cc.rain_trigger_mv = 40;
        g_crown.send_config(cc);
    }
    g_adc.begin();
    g_heater.begin(b::heat_pwm_pin);

    g_net.begin(g_cfg);
    start_http();

    xTaskCreate(task_analog, "analog", 4096, nullptr, 7, nullptr);
    xTaskCreate(task_fast_i2c, "fast_i2c", 4096, nullptr, 6, nullptr);
    xTaskCreate(task_crown, "crown", 4096, nullptr, 5, nullptr);
    xTaskCreate(task_rs485, "rs485", 3072, nullptr, 4, nullptr);
    xTaskCreate(task_slow_i2c, "slow_i2c", 6144, nullptr, 4, nullptr);
    xTaskCreate(task_pipeline, "pipeline", 12288, nullptr, 3, nullptr);
}
