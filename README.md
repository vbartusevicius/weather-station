# AeroMet V1 — All-Solid-State Ultrasonic, Haptic-Rain & Multi-Gas Roof Weather Station

Open-source, monolithic cylindrical rooftop weather and air-quality station (**`Ø 96 × 198 mm`** in UV-stabilized **ASA**) with **zero moving parts**, engineered with the same end-to-end verification pipeline as `knx-dali-gateway`: SKiDL circuit generation, KiCad 10 4-layer PCBs (`0 DRC errors, 0 unrouted`), pin-for-pin JLCPCB/EasyEDA assembly verification, single-order `100.0 × 56.1 mm` breakaway mouse-bite panelization, parametric CadQuery 3D enclosure with BRep collision/fit checks, and portable C++17 ESP32-C6 firmware with live WebSocket Web UI and Home Assistant MQTT Auto-Discovery.

| Exploded Assembly View | Internal X-Ray View | Vertical Cutaway Section | Exterior Isometric |
| :---: | :---: | :---: | :---: |
| ![Exploded View](hardware/releases/v1/enclosure/renders/exploded.png) | ![Internal View](hardware/releases/v1/enclosure/renders/inside.png) | ![Section View](hardware/releases/v1/enclosure/renders/section.png) | ![Isometric View](hardware/releases/v1/enclosure/renders/iso.png) |

---

## Sensor Suite & Measured Parameters (12-in-1, Zero Moving Parts)

| Category | Parameter | Sensor / IC | Board & Zone | Accuracy / Range |
| :--- | :--- | :--- | :--- | :--- |
| **Wind Speed & Direction** | Instantaneous, 1m/10m WMO avg, 3s Gust, Lull, Beaufort, Acoustic Virtual Temp | **4× 40 kHz Waterproof Ultrasonic Transducers** + `SN74LVC2G04` + `CD4052BM96` + `TLV9062IDR` + `LM393DR2G` | `crown` (`y = 174..182 mm`, downward into `48 mm` reflective wind tunnel) | `0–60 m/s` (`0.05 m/s` res), `0–359.9°` (`0.5°` res) |
| **Auto-North & Plumb** | Automatic True/Magnetic North wind alignment & mast tilt cosine correction | **ST `LIS2MDLTR`** (3-axis magnetometer) + **ST `LSM6DS3TR-C`** (6-axis IMU) | `crown` (`U8`) + `main` (`U5`) | `±0.5°` heading, `±0.1°` mast tilt |
| **Precipitation (Hybrid)** | Rain start/end, State (`Drizzle..Violent / Hail`), Rate (`mm/h`), Event/Daily/Total (`mm`) | **27 mm Piezo Acoustic Disc** + **Capacitive Wet-Dome Sense** + `TLV9062IDR` charge/envelope AFE + IMU shake rejection | `crown` (`J6`, `U5` under `12°` sloped top dome) | `0.01 mm` resolution, `0–400 mm/h` |
| **Temperature & Humidity** | Ambient Air Temp, Relative Humidity, Dew Point, Wet Bulb, Feels-Like, Abs Humidity | **Sensirion `SHT45-AD1F-R2`** (with factory PTFE membrane) | `probe` (`U3` at thermally isolated tip inside 5-plate pagoda) | `±0.1 °C` (`-40..+125 °C`), `±1.0% RH` |
| **Barometric Pressure** | Station Pressure, Sea-Level QNH (`hPa`), 3-Hour Storm Tendency | **Bosch `BMP581`** | `main` (`U4` in vented lower bay) | `±0.03 hPa` relative (`0.08 Pa` RMS noise) |
| **Solar & UV Radiation** | UV-A/UV-B Index (`0–16+`), Ambient Illuminance (`lx`), Solar Irradiance (`W/m²`) | **Lite-On `LTR-390UV-01`** + **Vishay `VEML7700-TT`** | `crown` (`U6`, `U7` under flush top fused-silica window) | `0–16+ UVI`, `0–140,000 lx` (`0–1,800 W/m²`) |
| **Lightning Detection** | Storm front distance (`1–40 km`), Strike count, Strike energy | **ScioSense `AS3935-BQFT`** + `220 µH` 500 kHz RF tank | `main` (`U6`, `L2`) | `1–40 km` radius |
| **Outdoor Air Quality** | **PM1.0, PM2.5, PM4.0, PM10** (`µg/m³`), **VOC Index**, **NOx Index**, **True CO₂** (`ppm`), US EPA AQI & EU EAQI | **Sensirion `SEN55-SDN-T`** (PM) + **`SGP41-D-R4`** (VOC/NOx) + **`SCD41-D-R2`** (photoacoustic CO₂) | `main` (`J4` lower labyrinth intake) + `probe` (`U1`, `U2` in pagoda) | `400–5,000 ppm` CO₂, `1–500` VOC/NOx index, `±10%` PM |

---

## How It Works

1. **Flexible `12–32 V DC` Wired Roof Supply & Constant-Wattage (`4.0 W`) De-Icing Heater:**
   - The roof supply enters through the hollow mast into push-in spring terminal **`main J1` (`KF142R-5.08-2P`)**, protected by a `1.1 A` resettable PTC fuse (`F1`), `SMAJ33A` TVS diode (`D1`), `SS16` reverse-polarity Schottky (`D2`), and `470 µF / 35 V` reservoir capacitor (`C1`).
   - An **`SCT2400`** wide-input buck converter (`4.5–40 V` in) steps `VIN_PROT` down to `+5V`, feeding the **Seeed XIAO ESP32-C6** (`U3`) through a **`TI LM66100`** ideal diode (`U2`) so USB-C service power never back-feeds the buck stage.
   - XIAO `D0/GPIO0` (`VIN_ADC`) continuously measures the exact roof supply voltage (`12.0..32.0 V` via a `100k/10k` divider) while `D1/GPIO1` (`NTC_SENSE`) reads the `10k B3950` NTC thermistor (`probe TH1`) thermally coupled to the underside of the ultrasonic reflector cone.
   - When outdoor temperature drops below `+2.5 °C`, `D3/GPIO21` (`HEAT_PWM`) drives N-MOSFET **`AO3400A` (`Q1`)** to power the `54 Ω` series heater array (`2× 27 Ω 2512` resistors `probe R1, R2`), automatically scaling PWM duty cycle $D = \min(1,\, P_{\text{target}} R_{\text{heat}} / V_{\text{in}}^2)$ so heater power stays regulated at **`4.0 W`** whether you feed `12 V`, `24 V`, or `32 V`.
2. **Reflective 40 kHz Ultrasonic Anemometer with Speed-of-Sound Cancellation:**
   - Four waterproof closed-face aluminum 40 kHz transducers (`N, E, S, W` at radius `21.0 mm`) face downward from the upper crown (`y = 174.0 mm`) toward the heated conical reflector deck `48.0 mm` below (`y = 126.0 mm`).
   - Four intercardinal airfoil struts (`45°, 135°, 225°, 315°` at radius `39.0 mm`) leave the cardinal `N↔S` and `E↔W` acoustic paths 100% unobstructed.
   - A cross-strapped **`CD4052BM96`** dual 4:1 analog mux (`crown U2`) steers the `SN74LVC2G04` TX burst and `TLV9062` 2-stage bandpass RX amplifier (`G ≈ 520×`) + `LM393` zero-crossing comparator across all 4 directions (`N→S`, `S→N`, `E→W`, `W→E`).
   - Firmware computes wind components from inverse transit times $\Delta(1/t) = 1/t_2 - 1/t_1$, algebraically cancelling temperature/humidity speed-of-sound drift while extracting acoustic virtual temperature from $\Sigma(1/t)$ and rotating the wind vector to True North using the onboard `LIS2MDLTR` magnetometer and `LSM6DS3TR-C` tilt accelerometer.
3. **Hybrid Haptic Piezoelectric + Capacitive + IMU Rain DSP:**
   - Raindrops striking the `12°` sloped top crown dome excite a bonded `27 mm` piezoelectric disc (`crown J6`) amplified and integrated by `TLV9062` (`crown U5`).
   - Firmware cross-checks every acoustic impulse against the capacitive wet-surface baseline and the `LSM6DS3TR-C` accelerometer, rejecting dry wind-induced mast vibrations while integrating drop volume ($\text{mm}^3 \to \text{mm}$) and classifying hail impacts.

---

## PCB Stack & Single-Order Breakaway Panel (`hardware/releases/v1/`)

All three 4-layer `1.2 mm` PCBs are combined onto a single **`100.0 × 56.1 mm` mouse-bite breakaway panel (`panel/`)** so the entire station can be ordered at JLCPCB as **1 PCB + 1 PCBA job**:

| Board | Dimensions | Top 3D Render | Bottom 3D Render | Layer Renders & Files |
| :---: | :---: | :---: | :---: | :--- |
| **`main`**<br>*(Lower Bay)* | `41.0 × 56.0 mm`<br>4 layers, `1.2 mm` | ![main top](hardware/releases/v1/enclosure/renders/main_top.png) | ![main bottom](hardware/releases/v1/enclosure/renders/main_bottom.png) | [`hardware/releases/v1/main/`](hardware/releases/v1/main/) |
| **`crown`**<br>*(Top Shroud)* | `41.0 × 56.0 mm`<br>4 layers, `1.2 mm` | ![crown top](hardware/releases/v1/enclosure/renders/crown_top.png) | ![crown bottom](hardware/releases/v1/enclosure/renders/crown_bottom.png) | [`hardware/releases/v1/crown/`](hardware/releases/v1/crown/) |
| **`probe`**<br>*(Pagoda Core)* | `14.0 × 56.0 mm`<br>4 layers, `1.2 mm` | ![probe top](hardware/releases/v1/enclosure/renders/probe_top.png) | ![probe bottom](hardware/releases/v1/enclosure/renders/probe_bottom.png) | [`hardware/releases/v1/probe/`](hardware/releases/v1/probe/) |

---

## Repository Structure & Developer Commands

| Folder | Contents |
| --- | --- |
| `station/` | ESP32-C6 C++17 firmware (PlatformIO / ESP-IDF 5.5), 4-panel LittleFS Web UI (`data/`), Home Assistant MQTT Auto-Discovery, and native Unity unit tests |
| `hardware/` | SKiDL circuit blocks (`circuits/`), KiCad 10 PCB layouts (`layout/v1/`), JLCPCB/EasyEDA pin-for-pin verifier (`scripts/jlc_parts.py`), breakaway panelizer (`scripts/panelize.py`), and release bundle (`releases/v1/`) |
| `mechanical/` | Parametric CadQuery 3D roof enclosure (`base.stl`, `pagoda.stl`, `crown.stl`, `crown_embossed.stl`, `crown_color.3mf`), fit drawings (`fit_side.svg`, `fit_front.svg`), and VTK ray-traced renders |

| Task | Command |
| --- | --- |
| **Full build, route, 3D CAD, release & test pipeline** | `python3 hardware/scripts/build.py` |
| **Run EDA + CAD + Python lint test suites** | `python3 hardware/scripts/build.py v1 --test` |
| **Run C++17 firmware native unit tests (Unity)** | `docker compose --profile lint run --rm -T cpp-lint "pio test -d station -e native"` |
| **Lint all Python sources (Ruff)** | `docker compose --profile lint run --rm -T lint check .` |

Licences per folder: [`LICENSE.md`](LICENSE.md). Architecture and engineering rules: [`AGENTS.md`](AGENTS.md).
