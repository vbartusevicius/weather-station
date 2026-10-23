# AeroMet V1 — Commissioning & Calibration Procedure

Work through the steps in order on every new station. Bench steps (1–4) need the opened enclosure; field steps
(5–11) need the station on its mast. Record every result in the table at the end and keep it with the unit.

All settings live in the Web UI (**Setup → Sensor Calibration**, guided home procedures in the **Calibration** tab, section 0) and in `GET/POST /api/config`; actions are
`POST /api/action {"action": "<name>"}`. Examples below use `curl` against `http://<station-ip>`.

```sh
S=http://192.168.1.50                                   # station address
curl -s $S/api/config | python3 -m json.tool             # current calibration + "crown" diagnostics
curl -s -X POST $S/api/config -d '{"wind_speed_gain": 1.012}'
curl -s -X POST $S/api/action -d '{"action": "wind_calibrate"}'
```

Equipment: bench supply 10–32 V / 2 A with current display, multimeter, ST-Link V2 (SWD), USB–RS-485 adapter,
a closed cardboard box that fits over the upper station (still-air hood), a reference thermometer / hygrometer
(aspirated if possible), a manual rain gauge or tipping bucket, a UV-index meter, a phone compass.

---

## 0. Home calibration without lab equipment (Web UI → **Calibration** tab)

The **Calibration** tab starts with **Station health** (every subsystem ok / warn / fail with a reason, plus the
automatic data-quality flags): fix anything red there first. Below it, the tab walks through every user calibration with household items or public reference data. Each
procedure shows what you need, step-by-step instructions, live readings and a progress bar. The station does the
measuring and the arithmetic (`src/calibration/Calibration.*`, host-tested). It shows the result next to the current
value, and nothing changes until **Apply result**; the date of each applied step is kept in the configuration.
API: `GET /api/calibration` (status), `POST /api/calibration {"cmd": "start" | "finish" | "apply" | "cancel" |
"clear_runs", "kind": ..., "p1": .., "p2": .., "p3": .., "fixed": bool}`.

| # | Procedure (`kind`) | Reference | Sets | Expected accuracy |
| --- | --- | --- | --- | --- |
| 1 | Compass spin (`compass`), bench | 2 slow turns of the assembled station in 90 s, ≥ 1 m from steel | `mag_offset_ut`, `mag_scale` (hard / soft iron, x–y) | removes station-fixed bias; needs 12/12 directions |
| 2 | North reference (`heading`), mounted | A: shadow of a stick at the S barrel crossing the N mark (sun azimuth from the clock + location); B: landmark ≥ 300 m sighted over S→N (map coordinates); C: true-north compass bearing | `heading_offset_deg`, or `heading_mode` 1 + `heading_fixed_deg` (magnetometer ignored; best on a steel mast) | A/B ±1°, C ±5° |
| 3 | Wind zero (still air) | closed cardboard box, 10 min settling (step 4 below) | `us_delay_us`, `dt_*_offset_us` | < 0.1 m/s still-air reading |
| 4 | Level | live `tilt_deg` | — (mast shims) | < 1° |
| 5 | Wind speed (`car_run`) | ≥ 2 runs of a known air speed: A car in calm air (pole ≥ 50 cm above the roof, GPS, both directions); B hand-held on a 1–1.5 m pole while walking / jogging / cycling (GPS, or a measured distance ≥ 100 m and its time; both directions; USB-C power bank with a 12 V PD trigger cable); C a handheld vane anemometer beside the station in steady wind or 1–2 m in front of a ≥ 50 cm box fan (a hair dryer's jet is narrower than the sound paths and too uneven) | `wind_speed_gain` = Σ reference / Σ station (before gain) | A, C ±3–5 %, B ±5–10 % (body wake) |
| 6 | Pressure (`pressure`) | A (`p2` 1): ground elevation from a map (Google Earth, OpenTopoMap, national geoportal / LiDAR) + barometer height above it, no measurement; B: reference sea-level pressure ≤ 50 km, ≤ 30 min old (airport METAR `Q`, national weather service, SYNOP via ogimet.com), wind < 5 m/s | `altitude_m` (B: inverse of the pipeline's QNH reduction) | A ±1–3 m, B ±0.3 hPa ≈ ±3 m |
| 7 | Light cover (`clear_sky`) | cloudless sky, sun ≥ 25°, light steady within 4 % for 2 min, against the pipeline's clear-sky model × 122 lm/W | `optics_transmission` | ±10–15 % (aerosols); average 2–3 days |
| 8 | UV (`uv_reference`, optional: the GUVB-C31SM has a factory scale) | A (`p2` 1): no reference, the LTR-390 factory UV index through the cover ÷ `optics_transmission` from step 7, UVI ≥ 1; B: TEMIS (temis.nl) clear-sky UV index at solar noon for the site, cloudless, within 15 min of solar noon (shown in the live bar); C: a UV-index meter beside the dome | `guvb_counts_per_uvi` | A ±15–20 %, B ±10 %, C ±5–10 % |
| 9 | Rain drops (`drop_test`) | syringe drops (counted per ml) from a measured height, ≥ 8 impacts; impact speed with quadratic drag, amplitude ∝ kinetic energy at the HapticRain drop model | `rain_impact_scale` = expected / median | ±15 % |
| 10 | Rain amount (`rain_gauge`) | cheap gauge, or a straight-walled container + kitchen scale (mm = g × 1000 / area mm²), ≥ 1 mm on both | `rain_gain` × gauge / station | ±10–20 % per event |
| 11 | Humidity (`salt_rh`) | probe sealed over damp NaCl 6–12 h (75.3–75.7 %, Greenspan 1977), drift < 1 % over the 10 min | `rh_offset_pct` | ±0.5 % RH |
| 12 | Temperature (`ice_point`) | probe double-bagged in an ice-water slurry, steady within 0.15 °C | `temp_offset_c` | ±0.1 °C |
| 13 | CO₂ (`co2_fresh_air`) | outdoor air (425 ppm default), steady within ±20 ppm for 3 min | SCD41 forced recalibration (FRC 0x362F) | ±(10 ppm + background uncertainty) |
| 14 | Self-tests | heater check; lightning antenna (crown auto-tunes it, health shows 482–518 kHz), interference (disturbers per 10 min, bursts on / off), storm comparison with lightningmaps.org; vibration tap test on the mast | — (nothing to calibrate: AS3935 distance is a ±30 % step estimate, the accelerometer is factory calibrated) | step 9 below |

Mark the **N barrel** once during assembly. The crown board's one large cut corner (NE) carries a silk **N ↑**:
with that corner top-right (seen from above) the barrel at the top is N, and its transducer plugs into `J2`
(silkscreen **N**). The four posts on the diagonals are struts. A paint dot on the outside of that barrel is what
steps 1, 2 and 5 refer to. A station reboot cancels a running
session (the rain-gauge comparison then starts again). The sections below remain the lab / commissioning procedure;
where both exist, the lab reference is the better one.

---

## 1. Power and heater rail (bench, crown and probe connected)

| Check | How | Pass |
| --- | --- | --- |
| Input current, idle | 24 V supply, heaters off (room temperature) | < 120 mA |
| `+5V` | `heater.rail_logic_v` in `/api/state` | 4.9–5.1 V |
| `+12V_HEAT` regulated | 24 V input → `heater.rail_heat_v` | 11.7–12.3 V |
| `+12V_HEAT` pass-through | 12.0 V input → `rail_heat_v` | 10.3–11.2 V (input − ≈ 1 V) and no buck whine / ripple > 0.3 V on a scope |
| Heater check | action `heater_check` → `heater.reflector_status` | `check-pass` (2 W pulse, 41.7 % duty at 12 V) |
| `+3V3` (XIAO) and `+3V3_PROBE` (power-board LDO) | scope both at main J5 / J3 pin 7 while Wi-Fi transmits and the SCD41 measures (every 5 s) | each 3.2–3.4 V, dips < 100 mV |
| Reverse polarity | swap VIN/GND at J1 briefly | nothing happens, station recovers |

## 2. Crown firmware and link

1. First flash (a factory-blank STM32 is fine: its ROM I²C bootloader is always present). Build the image with
   `docker compose --profile lint run --rm -T cpp-lint "pio run -d crown -e crown"`, then let the station flash it:
   `curl -s -X POST --data-binary @crown/.pio/build/crown/firmware.bin $S/api/crown/firmware` (the ESP32 raises
   BOOT0, resets the crown into the ROM bootloader, erases, writes and verifies). SWD is only needed for debugging or
   if that fails: Tag-Connect TC2030-CTX-NL cable on `J12` (crown bottom, label `SWD`; 1 VCC, 2 SWDIO, 3 NRST,
   4 SWCLK, 5 GND) + ST-Link, program `firmware.bin` at `0x08000000` with STM32CubeProgrammer / `st-flash`.
2. `/api/config` → `crown.present = true`, `crown.diag_ok = true`, `crown.bursts_per_s ≈ 16`, `crown.i2c_errors` not
   increasing.
3. Step 1's flash confirms checklist Q2 (`{"ok":true,...,"crown_present":true}`). If it reports
   `NoBootloader`, the STM32 ROM did not answer at 0x52 or 0x56: check BOOT0 (main GPIO2) with a scope during the
   reset pulse and note the address that works.
4. AS3935 antenna: `crown.as3935_lco_hz` within 482 500–517 500 Hz (500 kHz ± 3.5 %) and `crown.as3935_tun_cap`
   0–15. Outside the range: check the MA5532-AE and the two 470 pF caps; nothing is adjustable in software.
5. First board only (checklist Q1 / N1 / N6), scope on the crown: the preamp output (U8 pin 1 / PA1) idles at
   1.65 V and PA3 sits at 1.65 V (VREF into the PGA bias input); TIM8 drives INA / INB with 8 cycles of 40 kHz and
   353 ns dead time, and **both** UCC27524 inputs are low again within 1 µs of the last cycle (no level held on OUTA
   during the capture). `/api/config` → crown wind blocks must not carry the `clipped` flag in still air.

## 3. RS-485 (bench, USB–RS-485 adapter on J1 pins 3 A+ / 4 B−)

The Web UI **Setup → RS-485** card shows whether a master is polling (requests per command, replies, ignored lines,
last command and its age) and decodes the current `$AM` frame field by field.

```python
import serial                                   # pip install pyserial
s = serial.Serial("/dev/ttyUSB0", 115200, timeout=1)
s.write(b"PING\n");   print(s.readline())       # b'$AM,PONG*xxxx\r\n'
s.write(b"STATE?\n"); print(s.readline())       # b'$AM,<uptime>,...*CRC16\r\n'
```

The frame carries every published sensor: 33 comma-separated fields after the talker, in the order of the Setup card
(the first ten are unchanged from earlier firmware; an empty field = no valid value).

Pass: replies within 50 ms, CRC-16/MODBUS over everything before the `*` (including `$AM...`) matches, nothing is
sent without a command. For a multi-station bus set `rs485_address` (1–247) per station and prefix commands
(`7:STATE?`); the reply talker becomes `$AM7`. Fit the 120 Ω terminator R26 only on the two stations at the
physical bus ends. Every station carries the 390 Ω fail-safe bias (R9 to +3V3 on A, R10 to GND on B), sized for one
biased node: on a bus with more than two stations remove R9 and R10 (0603, main board top) from every station except
one, otherwise the parallel bias loads the drivers below the RS-485 output spec (checklist N8).

## 4. Wind: still-air calibration (bench or indoors, every station)

Sets `us_delay_us` (electronics + transducer + echo-timing delay, factory estimate 58 µs) and the zero-wind offsets
`dt_ns_offset_us` / `dt_ew_offset_us`.

1. Station fully assembled, powered ≥ 30 min indoors so the crown, transducers and air are at one temperature.
2. Put the closed box over the upper station (wind tunnel + crown) without touching it; no fans / HVAC drafts. Wait
   5 min.
3. Press **Still-air wind calibration (60 s)** (action `wind_calibrate`). Do not move near the station for 60 s.
4. Reload Settings: `wind_cal_status` must start with `ok`, e.g.
   `ok: 240 quads, delay 57.84 us, dt_ns 0.412 us, dt_ew -0.207 us, spread 0.061 us`. The values are saved.
   Refusals: `air not still` (spread > 0.25 µs → draft, retry), `delay out of range` (check transducers and SHT40),
   `no SHT40 temperature / humidity`.
5. Verify with the box still on: `wind.speed_m_s` < 0.10 m/s for 2 min and `wind.acoustic_temp_c` within ±1.0 °C of
   the SHT40 (`environment.temp_c`) — the solver reports the virtual temperature, ≈ +0.3…1.0 °C above the true
   temperature in humid air.
6. Repeat after changing transducers (Ø 10 ↔ Ø 14 mm), the crown board, or the crown firmware.

## 5. Wind: speed gain and direction (wind tunnel, car roof or reference mast)

The solver uses the nominal V-path geometry (`kTransducerSeparationM`, `kReflectorGapM`); printing tolerances and
the strut wakes make the real gain differ by a few percent and vary with direction.

1. Reference: calibrated cup / sonic anemometer at the same height, ≥ 1 m away, or a wind tunnel.
2. Record 10-min means at ≥ 3 speeds (3, 8, 15 m/s) and, in a tunnel, at 8 headings (0°, 45° … 315°; the 45° headings
   are behind the struts).
3. `wind_speed_gain` = Σ(reference) / Σ(station) over all non-calm points (0.5–2.0 accepted). Enter it, repeat one
   point to confirm ±3 %.
4. Note the worst-heading error in the record. If it exceeds ±5 %, a per-sector correction is needed (not
   implemented in V1 firmware — checklist S1).
5. Direction: compass spin on the bench and the North reference after mounting (section 0, steps 1–2; the reference
   absorbs the declination), then compare `wind.dir_10m_deg` with the reference in steady wind (±10°). On a steel
   mast, or if the error stays large, use the fixed heading (`heading_mode` 1): the magnetometer is then ignored.

## 6. Rain

Two settings: `rain_impact_scale` maps the crown charge-amplifier deviation to the mV the drop model expects
(drops counted above 165 mV net, hail above 1400 mV with ≤ 140 µs pulses), and `rain_gain` scales the final depth.

1. Drop test (bench, lid on, level): from a pipette let single drops fall 1.0 m onto the lid centre between the
   piezos. `/api/config` → `crown.last_rain_peak_mv` and `crown.rain_events`. A tap on the mast or enclosure wall
   must give a much wider pulse (`last_rain_width_us` > 850) than a drop.
2. Start with `rain_impact_scale` = 1.0. In the first real rain, read `crown.last_rain_peak_mv` repeatedly during
   moderate rain (2–5 mm/h). Set the scale so the typical value × scale is 300–500 mV (a 2 mm drop at terminal
   velocity ≈ 330 mV in the model).
3. Over ≥ 3 events and ≥ 20 mm total next to a manual gauge / tipping bucket:
   `rain_gain` = gauge_mm / station_mm. Expect ±20 % per event even after calibration (drop-size dependence).
4. Hail cannot be calibrated on demand; record any hail event with photos and the station's `rain.hail_diam_mm`.

## 7. Temperature / humidity / pressure

1. Co-locate an aspirated (or well-shielded) reference within 2 m at the same height for ≥ 7 days, including sunny
   low-wind afternoons and clear nights.
2. Report mean and 95th-percentile difference of `environment.temp_c` and `environment.humidity_rh` split by sun / night and by wind
   < 1 m/s. The labyrinth louvers trade ventilation for radiation blocking: daytime still-air errors of +0.5…1.5 °C are
   expected; if larger, investigate the base-bay heat (main / power board) path into the probe.
   Probe heater (checklist N2, first build): in a freezer / cold night below −8 °C, log `heater.probe_on` with the
   reference: the SHT40 bias while the probe heater runs must stay < 1 K in light wind (model: ~0.5–1.7 K at
   h = 25–50 W/m²K, up to ~5 K in dead calm). The heater must be off above about −5 °C (thermostat −7.6 °C, ~2.3 K
   hysteresis).
3. Pressure: compare `qnh_hpa` with the nearest airport METAR over a calm day (±0.5 hPa after `altitude_m` is set).
   In wind, adjust `pressure_wind_coef` until the pressure no longer rises with wind speed.

## 8. UV / light

The default cover is the flanged PTFE dome diffuser, lathe or CNC version (`optics_transmission` = 0.11, an estimate:
PTFE batches differ); with the backup `crown_window` JGS1 disc set `optics_transmission` ≈ 0.92 first. LTR-390 UVI / lux, TSL25911 lux
and the factory-scaled GUVB UVI are divided by it.

1. Clear-sky solar noon, UV meter next to the station: compare `solar.uv_index_ltr` with the reference and set
   `optics_transmission` = current value × station UVI / reference UVI (0.02–1). Check the illuminance against a lux
   meter held level at the same time (expect ±10 %).
2. Compare `solar.uv_index_guvb` with the reference. If it differs by > 10 %, set `guvb_counts_per_uvi` = current
   scale × station UVI / reference UVI, where the current scale is `guvb_counts_per_uvi` or, while that is 0,
   `guvb_factory_b_scale` divided by `optics_transmission` (both in `/api/config`). A field scale includes the cover.
3. Low sun (dome covers): on a clear morning compare at 0–20° solar elevation; the station must still see the direct
   beam at sunrise / sunset (the backup flat JGS1 window under its lip loses it below ~19–34°). A dome is not an
   ideal cosine collector: it over-reads low sun relative to a flat cosine-corrected meter (the CNC hemisphere more
   than the lathe version, whose flat top is closer to a cosine diffuser). Note the ratio at 10° and
   20° in the record; it does not matter for UVI (small at low sun) but does for illuminance / irradiance at dawn.
4. PTFE changes its transmission by a few % around +19 °C (crystal transition); calibrate on a day near the season's
   typical temperature and note it. With the JGS1 window, lux above ~88 klx saturates the TSL25911 and the LTR-390
   provides the illuminance; behind the diffuser the TSL25911 covers full sun.

## 9. Lightning interference (checklist Q4, once per design and after crown changes)

1. Normal operation for 30 min (no thunderstorm within 100 km): note `crown.disturbers`.
2. Action `crown_bursts_off` (wind stops), 30 min: note `crown.disturbers` again; action `crown_bursts_on`.
3. Pass: disturber count with bursts ≤ 2 × without. If much higher, the drive harmonics reach the antenna: add the
   RC low-pass per transducer or increase the AS3935 noise floor / watchdog.
4. Heater PWM (checklist N10): repeat 1–2 below +2 °C (reflector heater running at 1 kHz PWM, `heater.reflector_w`
   > 0) or with a `heater_check` pulse: disturbers with the heater on ≤ 2 × off. If much higher, raise the heater
   MOSFET gate resistor (main, 330 Ω → 4.7 kΩ) to slow the 0.4 A edges.
5. Rain channel during bursts (checklist N5): in dry weather with bursts on, `crown.rain_events` must not increase
   (the crown blanks the rain detector for each burst + 1 ms; R24 / C22 = 22 kΩ / 1 nF low-pass).

## 10. Compass, tilt, CO₂

* Tilt: `wind.tilt_deg` < 2° after mounting (the solver corrects up to ~45°, but the beams then leave the reflector).
* CO₂: the SCD41 runs automatic self-calibration (needs outdoor air ≥ 4 h per week). After the first week the outdoor
  value should read 410–450 ppm.

## 10a. Enclosure seals (first build of each print batch, checklist N9)

The bayonet glands give only 15 % O-ring squeeze at rest (1.70 mm gland, 2.0 mm ring); a +0.25 mm print / stack error
leaves ~3 %. With the three bayonets locked, measure each gland height (lug resting on its shelf) with feeler gauges or
a depth gauge: ≤ 1.75 mm passes. Then a hose test (garden-hose spray, 2 min per side, station upright) with tissue paper
in the base bay and crown: dry. If a gland is too tall, shim the O-ring seat or switch to a 2.1 mm ring.

## 11. Record

| Station S/N | Date | Step | Value | Pass | By |
| --- | --- | --- | --- | --- | --- |
| | | 1 `+12V_HEAT` at 12 / 24 V | | | |
| | | 2 crown fw / bootloader address / LCO Hz | | | |
| | | 3 RS-485 PING / STATE? | | | |
| | | 4 `us_delay_us`, `dt_ns`, `dt_ew`, spread | | | |
| | | 5 `wind_speed_gain`, worst heading error | | | |
| | | 6 `rain_impact_scale`, `rain_gain` | | | |
| | | 7 T / RH bias (day / night), QNH offset | | | |
| | | 8 `optics_transmission`, `guvb_counts_per_uvi` | | | |
| | | 9 disturbers on / off | | | |
| | | 10 tilt, CO₂ outdoor baseline | | | |
