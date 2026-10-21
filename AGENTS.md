# Roof Weather Station (AeroMet V1)

## Project intent and current scope

Design and build a solid-state, cylindrical rooftop weather and air-quality station (`Ø 96 × 222 mm` in UV-stabilized ASA) with full end-to-end automated hardware, mechanical, and firmware verification:

- **Power & Dual-Path Connectivity (`main` PCB):**
  - Wide-input **12–32 V DC wired supply + RS-485** on a single 4-pole `3.5 mm` push-in spring terminal (`J1`: `KF250-3.5-4P-1`, `C475156` — `1: VIN+ Red`, `2: GND Black`, `3: RS485_A+ White`, `4: RS485_B- Yellow`), protected by a `1.1 A` PTC fuse, `SMAJ33A` TVS diode, and `SS16` reverse-polarity Schottky diode, with the separate `power` board (2× `SCT2430` 40 V / 3.5 A: `+5V` electronics, `+5V_HEAT` heaters; no `12–32 V` conductor leaves the base) + `LM66100` ideal diode feeding a socketed **Seeed XIAO ESP32-C6**.
  - **Boosted Wi-Fi (`+20.5 dBm` + External U.FL Dipole):** Firmware drives `XIAO ESP32-C6` internal `GPIO3 = LOW` and `GPIO14 = HIGH` to switch the onboard `FM8625H` RF switch to a horizontal `2.4 / 5.8 GHz` U.FL FPC dipole antenna mounted inside `base.stl`.
  - **Hardware Auto-Direction RS-485 (`115,200 bps`):** `SP3485EN-L/TR` (`U7`) + `SN74LVC2G04` (`U8`) on `D10 / GPIO18` (`UART1`), streaming CRC-16-protected `$AM,...*<CRC16>` telemetry frames and accepting commands (`PING`, `STATE?`, `RESET_RAIN`, `HEAT_CHECK`).
- **6-Board Modular PCB Stack (`124.5 × 55.7 mm` Breakaway Panel):**
  1. **`main` (`32.0 × 52.0 mm`, right half of the base bay):** Power input, `XIAO ESP32-C6`, `SP3485EN` RS-485, `LSM6DS3TR-C` 6-axis IMU, `AS3935` lightning detector, `AO3400A` de-icing heater MOSFET, `ADS1115` heater power monitor (shunts for both heaters).
  2. **`crown` (`34.0 × 34.0 mm` octagon):** 4× `40 kHz` ultrasonic transducer ports (`J2..J5`), reciprocal `1 kΩ` ultrasonic AFE (`SN74LVC2G04`, `CD4052B`, `TLV9062`, `LM393` zero-cross + envelope gate), 4× `20 mm` Murata piezo headers (`J6..J9`) + `TLV9062` charge/envelope AFE, `MMC5603NJ` 20-bit 3-axis AMR compass, and center 4-pin vertical B2B socket `J10`.
  3. **`reflector` (`24.0 × 24.0 mm`):** Horizontal T-cap heater board beneath the ultrasonic reflector cone with 4× `27 Ω` `2512` `1 W` parallel resistors on `+5V` (`6.75 Ω`, `3.5 W` regulated), center `10k` NTC (`TH1`), and bottom 4-pin SMD male header `J1`.
  4. **`dome` (`Ø 13.6 mm` round):** Elevated UV/solar/NIR micro-PCB on a 4-pin vertical B2B riser inside the hollow `Ø 18 mm` `JGS1` fused-silica quartz hemisphere, carrying `LTR-390UV-01` (`U1`, UV + `157k lx` `ALS`), `TSL25911FN` (`U2`, `LCSC C176812`, `Visible + Near-IR 700–1100 nm` + `188 µLux` night sky), and `GUVB-C31SM` (`U3`, `AlGaN` `UV-B 240–320 nm`, populated by default, primary UV-index source) on `F.Cu` and `C1, C2, C3, J1` on `B.Cu`.
  5. **`probe` (`14.0 × 50.0 mm`):** Vertical pagoda stem carrying `SCD41-D-R2` ($\text{CO}_2$) + backside `0.42 W` (`+5V`) `+8 °C` thermostatic heater (`2× 120 Ω 1206` on `+5V`, `0.42 W`, + `LM393` with hysteresis + `TH1`), `SGP41-D-R4` ($\text{VOC}/\text{NO}_x$) + `BMP585` barometer (thunder pressure waves) above it, milled thermal isolation slot, and `SHT40-AD1F-R2` (`Temp/RH`) at the lower tip.
  6. **`power` (`28.5 × 30.0 mm`, flat under `main`, 1 mm above the bay floor):** 2× `SCT2430` → `+5V` (electronics) and `+5V_HEAT` (reflector + probe heaters, spare pair to crown `J11`); 8-pin JST-SH `J1` ↔ `main J5`.
- **10-Part Modular 3D-Printed Enclosure with True Meshing Threads (`hardware/releases/v1/enclosure/`):**
  - `base` (with the `PMSA003I` duct bed), `mast_clamp`, `orings`, `probe_sleeve_back`, `probe_sleeve_front`, `pagoda`, `wind_tunnel`, `struts`, `crown_base`, and `crown` (`.stl` and `.step` + individual and whole-enclosure VTK renders in `renders/`).

## Repository layout and verification commands

- `station/`: ESP32-C6 C++17 firmware (PlatformIO / ESP-IDF 5.5), LittleFS Web UI, MQTT Home Assistant Auto-Discovery, RS-485 protocol driver, and native Unity unit tests.
- `hardware/`: SKiDL circuit generators (`circuits/`), KiCad 10 PCB layouts (`layout/v1/`), JLCPCB/EasyEDA pin-for-pin verification (`scripts/jlc_parts.py`), 5-board breakaway panelizer (`scripts/panelize.py`), and manufacturing package (`releases/v1/`).
- `mechanical/`: Parametric CadQuery 10-part 3D roof station assembly (`scripts/build_enclosure.py`) and VTK ray-traced 3D renders (`scripts/render_assembly3d.py`).

### Commands

- Full hardware + CAD + release + test pipeline: `python3 hardware/scripts/build.py`
- Firmware native unit tests: `docker compose --profile lint run --rm -T cpp-lint "pio test -d station -e native"`
- Lint (Python & C++): `docker compose --profile lint run --rm lint check .`
- ESP32-C6 firmware build (compile check incl. drivers): `docker compose --profile lint run --rm -T cpp-lint "cd station && pio run -e dev_c6"` (native tests do NOT compile `src/firmware/`).
- Web UI dev server: `python3 station/scripts/mock_webui_server.py` (`http://127.0.0.1:8765`). It auto-builds `station/build/station_sim` (host build of the real `src/core` pipeline, needs `c++` and the ArduinoJson copy in `station/.pio/libdeps/native`, i.e. run the native tests once) and serves the same API as the firmware. Scenarios: `POST /api/action {"action":"set_scenario","scenario":"<name>"}` with `breeze`, `storm`, `freeze`, `heatwave`, `plume`, `train`, `night_dark`, `night_moon` (unknown keys are ignored). Scenario raw-sensor models live in `station/tools/sim/station_sim.cpp`. Repack firmware assets after UI edits: `python3 station/scripts/pack_webui.py`.
- Web UI runtime smoke test: `node` is not installed locally. Run `station/data/app.js` with macOS JXA (`osascript -l JavaScript`) using a stub `document`/`fetch` harness and feed `/api/state` JSON into `applyTelemetry(d)` for each scenario. `applyTelemetry` errors are swallowed by `.catch(() => {})` in the browser, so a throw shows up as cards that silently stop updating. JXA predefines a global `$`, so `app.js` must not declare one (its DOM helper is `el`).

## Firmware data architecture

- Drivers (`src/firmware/drivers/`) + host-tested conditioners (`src/conditioning/`) write corrected 1-s values into `core::RawState` (`Reading<T>` = value + timestamp + valid) and discrete events (rain impacts, lightning, thunder onsets) into `core::EventQueues`.
- `core::Pipeline::step()` runs once per second: processors read a RawState copy, drain events, and fully rewrite `core::DerivedState` (`Value<T>`; `ok == false` is published as JSON `null`, shown as "—"). Stale/missing sensors never produce defaults.
- `history::TieredHistory`: 27 metrics × (60 × 1 min + 24 × 1 h + 31 × 1 day), mean/min/max/sum/count/vector-direction slots, ~50 KB RAM; hour/day tiers persist to SPIFFS hourly.
- Publishers read DerivedState/history only: `/api/state` + WebSocket + MQTT state (current values only; Home Assistant keeps long-term history), `/api/history/minutes` (60-bar card charts), `/api/history?tier=minute|hour|day&metrics=a,b`, `/api/config`, `/api/action` (`reset_rain`, `set_time`, `reboot`), RS-485 `$AM` frames.
- The Web UI is display-only: no metric is computed in `app.js`.
- Flash: the XIAO ESP32-C6 has **4 MB**. `partitions.csv` = 2 × 1.75 MB OTA app slots + 384 KB LittleFS `storage` (history, `/data`); `sdkconfig.defaults` and `platformio.ini` (`board_upload.flash_size`) must stay at 4 MB.
- The Web UI is embedded in the firmware as gzip (`WebAssets.hpp`, generated by `scripts/pack_webui.py`; never edit it by hand). Edit `station/data/` instead.
- UV index: GUVB-C31SM (fitted by default, factory NVM offset/B_Scale or `guvb_counts_per_uvi`) fused 80/20 with LTR-390; both per-sensor values are published.
- Air: PMSA003I (PM1/2.5/10, no PM4) lying face-down on the left half of the `base` bay on two sealed duct chambers (geometry in `enclosure_features.PM`, tested by `test_pm_bed_ducts_take_outside_air_only`); `main` sits on the right half (`x_offset` 15.5 in `spec.yaml`), I2C 0x12 at 100 kHz, 30 s fan warm-up. VOC/NOx: SGP41 raw ticks through Sensirion Gas Index Algorithm 3.2.3 (`src/airquality/GasIndex.*`); the library has Arduino metadata, hence `lib_compat_mode = off`, and must be included via `<SensirionGasIndexAlgorithm.h>` so PlatformIO's LDF links it.
- Barometer: BMP585 on the probe (`U5`, same register map as BMP581, chip ID 0x51, I2C 0x47).
- New JLC parts: fetch the supplier data (`hardware/build/jlc/<LCSC>.{jlc,easyeda}.json`), then `python3 hardware/scripts/jlc_parts.py snapshot --cached` in the `eda` container; footprints must land pin-for-pin on the EasyEDA pads.
- Heaters: both on +5V_HEAT from power-board U2 (separate SCT2430; electronics on +5V from U1): reflector (4x 27 ohm = 6.75 ohm, 3.5 W firmware constant-wattage PWM on the measured +5V, NTC, UVLO 4.5 V) and probe (0.42 W, analog +8 C thermostat). Probe cable = +5V_HEAT x2, HEAT_SW x2, GND, NTC_SENSE, +3V3, CO2_HEAT_RTN, SDA, SCL. Crown cable is 12-pin (adds +5V_HEAT + GND, ending at crown J11). ADS1115 AIN2 = +5V_HEAT/2, AIN3 = +5V/2. Both currents are measured by the ADS1115 on main (I2C 0x48, AIN0 reflector 0.1 ohm, AIN1 probe 1 ohm via the CO2_HEAT_RTN cable wire, AIN2 +5V/2); the probe heater zone temperature is the SCD41 on-chip temperature. `HEAT_CHECK` (RS-485) or the `heater_check` HTTP action runs a 10 s 2 W reflector pulse and reports check-pass/check-fail in `heater.reflector_status`.
- Removing auto-referenced parts renumbers SKiDL's references (R/C), and parts with explicit refs created early shift later auto refs: create explicit-ref parts last, or remap the layout keys from the old/new `parts.json` (source + value + nets).
- Heights: the base bay is 15 mm taller (`envelope.above_base_raise` in spec.yaml). Parts above the base (pagoda, probe, reflector, struts, crown, dome) keep their design-frame coordinates in spec + `build_enclosure.py` and are lifted by `ef.RAISE` (builders wrap `_build_*().translate`, `ef.board_point` adds it for non-base boards); `main`/`power` y in spec are absolute. Inside the bay: power board top 41.7, main top 56.5 (reset pin +`BTN_UP` 10), umbrella +`UMB` 12, PM face 45.5.
