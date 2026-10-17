# Roof Weather Station (AeroMet V1)

## Project intent and current scope

Design and build an all-solid-state, monolithic cylindrical rooftop weather and air-quality station (`Ø 96 × 198 mm` in UV-stabilized ASA) inspired by the Ambient Weather WS-4000 / Ecowitt WS90, WeatherFlow Tempest, and Vaisala WXT536, engineered to the same end-to-end verification standards as `knx-dali-gateway`:

- **Power & Connectivity:** Wide-input **12–32 V DC wired roof supply** (`KF142R-5.08-2P` push-in terminal, `1.1 A` PTC fuse, `SMAJ33A` TVS surge protection, `SS16` reverse-polarity protection, `470 µF` reservoir, `SCT2400` 40 V buck converter + `LM66100` ideal diode feeding a socketed **Seeed XIAO ESP32-C6**), plus a closed-loop **constant-wattage (`4.0 W`) PWM de-icing heater** (`AO3400A` MOSFET + `2× 27 Ω 2512` heater array + `10k` NTC thermistor) that scales duty cycle automatically from measured `VIN_ADC` (`12..32 V`).
- **All-Solid-State Sensor Suite (12 Parameters, Zero Moving Parts):**
  1. **Ultrasonic Wind Speed & Direction (`0–60 m/s`, `0–359.9°`, `4 Hz`):** 4 downward-facing waterproof 40 kHz transducers (`N, E, S, W`) in the upper crown bouncing acoustic bursts off a heated conical reflector across a `46 mm` aerodynamic wind tunnel, multiplexed via `CD4052BM96`, driven by `SN74LVC2G04`, and captured via `TLV9062` bandpass amplification + `LM393` zero-crossing detection.
  2. **Auto-North Compass & Mast Plumb/Tilt Compensation:** Onboard 3-axis magnetometer (`LIS2MDLTR`) + 6-axis IMU (`LSM6DS3TR-C`) automatically align wind direction to True/Magnetic North and compensate for mast tilt.
  3. **Hybrid Haptic Piezoelectric + Capacitive Rain Sensor:** Piezo ceramic acoustic impact disc under the sloped top crown dome + `TLV9062` charge/envelope amplifier + capacitive rain-film sense + IMU mast-vibration subtraction, reporting rain onset, rain rate (`mm/h`), daily/event accumulation (`mm`), and hail hits.
  4. **Precision Temperature & Relative Humidity:** Thermally isolated **Sensirion `SHT45-AD1F-R2`** (`±0.1 °C`, `±1.0% RH` with PTFE membrane) suspended at the tip of the vertical `probe` PCB inside a 5-plate double-louvred passive Stevenson pagoda shield.
  5. **Barometric Pressure:** **Bosch `BMP581`** (`±0.03 hPa` relative precision) tracking station pressure, sea-level QNH, and 3-hour storm tendency.
  6. **UV Index & Solar Irradiance:** **Lite-On `LTR-390UV-01`** (`UV-A/UV-B` index `0–16+`) + **Vishay `VEML7700-TT`** (`0–140,000 lx` ambient light & `W/m²` solar irradiance) under a flush top-crown optical window.
  7. **Lightning Strike Detector:** **ScioSense `AS3935-BQFT`** (`1–40 km` storm front distance, strike count, and energy).
  8. **Comprehensive Outdoor Air Quality (Multi-Gas + True CO₂ + Particulate Matter):**
     - **Sensirion `SGP41-D-R4`** (MOX multi-gas **VOC Index** + **NOx Index** from traffic/combustion),
     - **Sensirion `SCD41-D-R2`** (Photoacoustic **true CO₂** `400–5,000 ppm`),
     - **Sensirion `SEN55-SDN-T`** (Laser-scattering **PM1.0, PM2.5, PM4.0, PM10** via 6-pin JST-ZH bay in the lower labyrinth intake).

## Repository layout and verification commands

- `station/`: ESP32-C6 C++17 firmware (PlatformIO / ESP-IDF 5.5), 4-panel LittleFS Web UI (`Overview & Wind Rose`, `Air Quality & Lightning`, `Ultrasonic & Rain Diagnostics`, `Settings & OTA`), MQTT Home Assistant Auto-Discovery, and native Unity unit tests.
- `hardware/`: SKiDL circuit generators (`circuits/`), KiCad 10 PCB layouts (`layout/v1/`), JLCPCB/EasyEDA pin-for-pin verification (`scripts/jlc_parts.py`), 3-board breakaway panelizer (`scripts/panelize.py`), and complete manufacturing package (`releases/v1/`).
- `mechanical/`: Parametric CadQuery 3D roof station assembly (`base`, `pagoda`, `crown`, `clamp`), fit-check SVG drawings (`fit_side.svg`, `fit_top.svg`), and VTK ray-traced 3D renders (`exploded.png`, `iso.png`, `front.png`, `section.png`, `inside.png`).

### Commands

- Full hardware + CAD + release + test pipeline: `python3 hardware/scripts/build.py`
- Firmware native unit tests: `pio test -d station -e native`
- Firmware ESP32-C6 build: `pio run -d station -e dev_c6 -e release_c6`
- Lint (Python & C++): `docker compose --profile lint run --rm lint check .`
