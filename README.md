# AeroMet V1 — Solid-State Roof Weather & Air-Quality Station

Open-source roof weather and air-quality station (`Ø 96 × 222 mm` in UV-stabilized ASA) with no moving parts. It measures wind speed and direction by ultrasonic time-of-flight, rainfall and hail by acoustic impact on a 4-piezo dome, UV and solar radiation under a fused-silica quartz hemisphere, temperature, humidity, barometric pressure, lightning distance, and outdoor air quality ($\text{PM}_{1.0}$, $\text{PM}_{2.5}$, $\text{PM}_{4.0}$, $\text{PM}_{10}$, $\text{VOC}$, $\text{NO}_x$, and $\text{CO}_2$).

The station runs from a wired `12–32 V DC` supply and communicates over both **Wi-Fi** (boosted by an internal `2.4 / 5.8 GHz` U.FL dipole antenna) and **RS-485** (`115,200 bps` over the spare pair in a 4-wire roof cable).

| Isometric (`iso.png`) | Cutaway Section (`section.png`) | X-Ray Inside (`inside.png`) | Exploded View (`exploded.png`) |
| :---: | :---: | :---: | :---: |
| ![Isometric](hardware/releases/v1/enclosure/renders/iso.png) | ![Section](hardware/releases/v1/enclosure/renders/section.png) | ![Inside](hardware/releases/v1/enclosure/renders/inside.png) | ![Exploded](hardware/releases/v1/enclosure/renders/exploded.png) |

---

## Measured Parameters & Hardware Architecture

| Parameter | Sensor / Circuit | Range & Resolution | Location & Protection |
| :--- | :--- | :--- | :--- |
| **Wind Speed & Direction** | 4× `40 kHz` waterproof ultrasonic transducers + crown `STM32G431` (4× `UCC27524` H-bridge, ≈ `21.8 Vpp` from `+12V_HEAT`, `BAV99` T/R isolation, `74HC4052` + `TLV9062` + PGA, 2 MSPS echo sampling, envelope + reciprocal cross-correlation timing) | `0–60 m/s` (`0.01 m/s`), `0–359.9°` (`0.1°`), `4 Hz` | Downward-facing in `crown.stl` across a `48 mm` vertical wind gap (`D = 54 mm` span). |
| **Compass & Mast Tilt** | `MEMSIC MMC5603NJ` 20-bit AMR compass + `ST LSM6DS3TR-C` 6-axis IMU | `±0.5°` heading, `±0.1°` tilt | Auto-aligns wind direction to North and subtracts pole vibration from the rain sensor. |
| **Rain & Hail** | 4× `20 mm` `Murata 7BB-20-6L0` piezo discs in parallel + `TLV9062` charge/envelope amp | `0–200 mm/h`, `0.01 mm` step, hail hit counter | Bonded to four `1.0 mm`-thin ceiling pockets in `crown.stl` (`45°, 135°, 225°, 315°`). |
| **UV-A/B, Solar & Night Sky** | `Lite-On LTR-390UV-01` (`U1`) + `ams OSRAM TSL25911FN` (`U2`, `C176812`) + `GenUV GUVB-C31SM` (`U3`) | `UVI 0–16+`, `0.00018–157,000 lx`, `NIR 700–1100 nm` | Mounted on the elevated round `dome` PCB inside a hollow `Ø 18 mm` `JGS1` fused-silica quartz hemisphere (`U3` `GUVB-C31SM` populated by default; primary UV-index source). |
| **Temperature & Humidity** | `Sensirion SHT40-AD1F-R2` with factory IP67 PTFE membrane | `-40..+85 °C` (`±0.2 °C`), `0–100% RH` (`±1.8%`) | Bottom tip of `probe` PCB below a milled thermal slot, exposed through a `4.6 × 4.6 mm` window in `probe_sleeve_front.stl`. |
| **Barometric Pressure & Thunder** | `Bosch BMP585` (gel-filled, liquid-resistant) | `300–1,250 hPa` (`±0.06 hPa` relative, `< 0.1 Pa RMS`), thunder pressure waves | Upper section of `probe` PCB (`U5`) in the vented probe chamber, directly behind the sleeve vent slots; dynamic wind-pressure (Bernoulli) compensation. |
| **Particulate Matter** | `Plantower PMSA003I` (I²C, outdoor-proven fan + laser counter) | $\text{PM}_{1.0}, \text{PM}_{2.5}, \text{PM}_{10}$ (`µg/m³`) | Lies face-down on the left half of the `base.stl` bay beside `main`, on two sealed duct chambers whose `1.2 mm` floor slits draw (front-left) and exhaust (rear-left) outside air only; harness to `main J4`. |
| **VOC & NOx** | `Sensirion SGP41-D-R4` + Sensirion Gas Index Algorithm `3.2.3` | VOC Index (`1–500`), NOx Index (`1–500`) | Upper section of `probe` PCB (`U2`) behind the sleeve vent slots. |
| **True $\text{CO}_2$** | `Sensirion SCD41-D-R2` + closed-loop `+8 °C` backside heater (`2× 680 Ω 1206` on `+12V_HEAT`, `0.42 W`, + `LM2903` on `+12V_HEAT` with `~1.4 K` hysteresis + `TH1`) | `400–5,000 ppm` | Upper section of `probe` PCB; backside pocket in `probe_sleeve_back.stl` traps warmth in winter. |
| **Lightning Detector** | `ScioSense AS3935-BQFT` on the crown STM32 SPI bus + `Coilcraft MA5532-AE` `100 µH` ferrite-rod antenna / `2× 470 pF C0G` `500 kHz` tank (AS3935-AN04; no copper under the antenna, automatic `TUN_CAP` tuning; events during a transducer burst are flagged) | `1–40 km` storm distance & strike count | SW edge of the `crown` PCB, far from the base-bay bucks and the ESP32. |
| **Wind-Tunnel De-Icer** | 4× `120 Ω` `2512` `2 W` parallel resistors on `+12V_HEAT` (`30 Ω`, `4.8 W` max at 12 V) + `10k` NTC `TH1` (open-NTC lockout in firmware) | Constant `4.5 W` PWM on the measured rail (12 V from ≈ 13.5 V input up; on a 12 V supply the rail passes through at ≈ 10.4–11 V and the heater delivers ≈ 3.6 W), current measured by the `ADS1115` | Horizontal `reflector` PCB (`24 × 24 mm`) seated directly under the ultrasonic reflector cone. |
| **Power & Connectivity** | `KF250-3.5-4P` terminal (`J1`, `60 V` PTC + `SMAJ36A` TVS) + `power` board (2× `TPS54360` `60 V` buck at `743 kHz`: `+5V` electronics, `+12V_HEAT` heaters + transducer drive) + `XIAO ESP32-C6` + `SP3485EN` RS-485 (+ `SM712` surge TVS) | `12–32 V DC`, Wi-Fi 6 (`+20.5 dBm` + U.FL dipole) + `115,200 bps` RS-485 | All 4 wires of the roof cable (`VIN+`, `GND`, `RS485_A`, `RS485_B`) terminate in `main J1`. |

---

## Modular 3D-Printed Enclosure (`hardware/releases/v1/enclosure/`)

The enclosure is split into **12 modular parts** printed from 8 print plates: `base`, `orings`, `pagoda`, `wind_tunnel`, `probe_sleeve` (front + back), `mast_clamp` (+ `mast_shims`), `struts` (+ `transducer_adapters`), `crown` (+ `crown_base`) in [`enclosure/print/`](hardware/releases/v1/enclosure/print/) — every body laid out `≥ 6 mm` apart; never print the assembled multi-body STLs) using **8-lug Push-Turn-Click Bayonet Interlocks (`20.5°` / `~19 mm` turn travel with O-ring pre-load and a `0.47 mm` anti-unscrew detent step)** on the three main outer housing rings so internal wires never twist during assembly, while using **true right-hand helical screw threads** on the `probe_sleeve` bottom watertight bulkhead, the 4 split-C struts, and the `JGS1` UV quartz dome retainer nut.

| Part & Files | Render | Function & Assembly |
| :--- | :---: | :--- |
| **1. `base`**<br>[`base.stl`](hardware/releases/v1/enclosure/base.stl) · [`base.step`](hardware/releases/v1/enclosure/base.step) | ![base](hardware/releases/v1/enclosure/renders/base.png)<br>[bay floor](hardware/releases/v1/enclosure/renders/base_floor.png) · [underside](hardware/releases/v1/enclosure/renders/base_underside.png) | Sealed lower electronics bay: `main` PCB on the right half, the `PMSA003I` particulate module lying face-down on the left half on two sealed duct chambers (floor slits to the outside, 2 snap hooks). The bay is `15 mm` taller than the original design (`y = 34.5..85.2 mm`): `power` board `6 mm` above the floor, `main` `9.8 mm` above the power board's inductors, PMSA003I face `11 mm` above the floor on tall duct chambers with a roofed intake duct reaching `6 mm` further from the exhaust. Includes an **internal conical Drip Umbrella Canopy (`y = 69.2..74.0 mm`)** over the `main` PCB, a **bottom-protruding REBOOT / FACTORY-RESET button plunger (`x = +18.5, z = -20.0 mm` under `SW1`; captive, laid out on `print/base.stl`; its bore is not sealed)**, 4× 4-slit `1.2 mm` bottom micro-grilles + perimeter weep drains, 8 free-standing PCB edge snap-clips (4 for `main`, 4 for `power`, each ≥ 1 mm clear of the wall so it can flex), (no antenna cradle: stick the U.FL dipole to the inner wall with its adhesive), a center `M12 / PG7` cable-gland hole, and 8 external bayonet lugs (`y = 81.4..84.9 mm`). Every part above the base is listed in its design coordinates below and sits `15 mm` higher when assembled. |
| **2. `mast_clamp`**<br>[`mast_clamp.stl`](hardware/releases/v1/enclosure/mast_clamp.stl) · [`mast_clamp.step`](hardware/releases/v1/enclosure/mast_clamp.step) | ![mast_clamp](hardware/releases/v1/enclosure/renders/mast_clamp.png) | Detachable front pole saddle (`40 mm` tall, `6× Ø 6.4 mm` M6 bolt holes in 3 rows) with flat outer seating faces for M6 bolts and `Ø 12 mm` washers. `31 mm` pole engagement, `2.0 mm` closing travel: grips `Ø 32–34 mm` poles directly, `Ø 24–32 mm` poles with the `mast_shims` (part 11). |
| **3. `orings`**<br>[`orings.stl`](hardware/releases/v1/enclosure/orings.stl) · [`orings.step`](hardware/releases/v1/enclosure/orings.step) | ![orings](hardware/releases/v1/enclosure/renders/orings.png) | Five elastomeric rings (buy standard EPDM/NBR 70A O-rings; TPU prints are a fallback): three `88 × 2.0 mm` bayonet face seals (`base ↔ pagoda`, `pagoda ↔ wind_tunnel`, `crown_base ↔ crown`; glands give `15 %` squeeze at rest and `38 %` while pushing through the detent — a `1.5 mm` ring does not seal), one `21 × 1.5 mm` `probe_sleeve` bottom seal, one `17 × 1.5 mm` `JGS1` quartz dome seal. |
| **4. `probe_sleeve_back`**<br>[`probe_sleeve_back.stl`](hardware/releases/v1/enclosure/probe_sleeve_back.stl) · [`probe_sleeve_back.step`](hardware/releases/v1/enclosure/probe_sleeve_back.step) | ![probe_sleeve_back](hardware/releases/v1/enclosure/renders/probe_sleeve_back.png) | Rear half of the `probe` PCB cartridge. Features a close-fitting (`0.10 mm` clearance) PCB pinch dam with a grease/sealant reservoir groove at the bottom, a dedicated `12.6 × 3.35 mm` vertical rear cable conduit for the 10-wire `crown` cable, a smooth press-fit top spigot, and the rear half of the bottom ISO `M22 × 1.5` helical thread. |
| **5. `probe_sleeve_front`**<br>[`probe_sleeve_front.stl`](hardware/releases/v1/enclosure/probe_sleeve_front.stl) · [`probe_sleeve_front.step`](hardware/releases/v1/enclosure/probe_sleeve_front.step) | ![probe_sleeve_front](hardware/releases/v1/enclosure/renders/probe_sleeve_front.png) | Front half of the `probe` cartridge. Fully encloses `probe J1` on the front (zero external notch), pinches within `0.10 mm` of the PCB above `J1` with a grease/glue groove, screws watertight into `pagoda`'s bottom deck via the ISO `M22 × 1.5` helical thread, and press-fits into `pagoda`'s upper collar. Air reaches the sensors through a `4.6 × 4.6 mm` `SHT40` window, a uniform grid of 6 slots over the `SCD41`, `SGP41` and `BMP585`, and 6 side slots per side that let the shield airflow cross the upper chamber. |
| **6. `pagoda`**<br>[`pagoda.stl`](hardware/releases/v1/enclosure/pagoda.stl) · [`pagoda.step`](hardware/releases/v1/enclosure/pagoda.step) | ![pagoda](hardware/releases/v1/enclosure/renders/pagoda.png) | Bolted 5-plate Stevenson radiation shield (`y = 66.9..123.9 mm`). Clamped by 4× DIN 7984 low-head `M6 × 55 mm` stainless through-bolts (`at r = 20.5 mm`; a standard ISO 4762 head hits the `reflector` board) inside `Ø 10.0 mm OD / Ø 6.2 mm ID` spacer columns, `1.6 mm`-thick louver blades with interlocking male/female spigots and recessed top bolt-head wells, plus **4× 3-slit `1.2 mm` top ventilation & moisture-dissipation micro-grilles** sheltered under the 5th louver. |
| **7. `wind_tunnel`**<br>[`wind_tunnel.stl`](hardware/releases/v1/enclosure/wind_tunnel.stl) · [`wind_tunnel.step`](hardware/releases/v1/enclosure/wind_tunnel.step) | ![wind_tunnel](hardware/releases/v1/enclosure/renders/wind_tunnel.png) | Compact heated conical ultrasonic reflector deck (`y = 120.6..136.0 mm`). Locks onto the top of `pagoda.stl` via an 8-lug internal Push-Turn-Click bayonet whose skirt leaves a `10.4 mm` crown-cable gap above the pagoda deck, sits in flush thermal contact (`y = 130.48 mm`) with the `24 × 24 mm` `reflector` heater resistors, and routes the `45°` NE strut cable radially inward to `probe_sleeve_back`. |
| **8. `struts`**<br>[`struts.stl`](hardware/releases/v1/enclosure/struts.stl) · [`struts.step`](hardware/releases/v1/enclosure/struts.step) | ![struts](hardware/releases/v1/enclosure/renders/struts.png) | Four separate **Double-C split hollow threaded tubes** (`8 C-shaped halves`, `Ø 5.8–6.0 mm` internal cable bore) with `Ø 14.0 mm` top and bottom flange stoppers (`y = 126.5` and `174.0 mm`), **`M12 × 1.75` external helical threaded ends**, and **8 printed `M12 × 1.75` hex locknuts (`15 mm` AF, `1.28 mm` wall at the flats; pockets are too small for metal ISO nuts)**. |
| **9. `crown_base`**<br>[`crown_base.stl`](hardware/releases/v1/enclosure/crown_base.stl) · [`crown_base.step`](hardware/releases/v1/enclosure/crown_base.step) | ![crown_base](hardware/releases/v1/enclosure/renders/crown_base.png) | Lower ultrasonic transducer & `crown` PCB tray (`y = 172.5..189.8 mm`). Open on top so you can tighten the 4 top `M12 × 1.75` strut hex locknuts, install the 4 downward `40 kHz` transducers, and snap in the `crown` PCB before locking on `crown.stl` via the upper `Ø 88 mm` O-ring and 8-lug Push-Turn-Click bayonet (`20.5°` turn). |
| **10. `crown`**<br>[`crown.stl`](hardware/releases/v1/enclosure/crown.stl) · [`crown.step`](hardware/releases/v1/enclosure/crown.step) | ![crown](hardware/releases/v1/enclosure/renders/crown.png) | Removable top piezo rain & UV dome lid (`y = 186.5..198.0 mm`). Locks onto `crown_base.stl` via an 8-lug internal Push-Turn-Click bayonet, holds the 4× `20 mm` Murata piezo discs under `1.0 mm`-thin ceiling pockets, and includes the printed **`M22 × 1.5` threaded retainer** (in `struts.stl`) clamping the hollow `Ø 18 mm` `JGS1` fused-silica hemisphere + O-ring. |
| **11. `mast_shims`**<br>[`mast_shims.stl`](hardware/releases/v1/enclosure/mast_shims.stl) · [`mast_shims.step`](hardware/releases/v1/enclosure/mast_shims.step) | — | Four pairs of split reducer half-shells (`1 / 2 / 3 / 4 mm` wall, `31 mm` tall) for `Ø 30–32`, `28–30`, `26–28` and `24–26 mm` poles. |
| **12. `transducer_adapters`**<br>[`transducer_adapters.stl`](hardware/releases/v1/enclosure/transducer_adapters.stl) · [`transducer_adapters.step`](hardware/releases/v1/enclosure/transducer_adapters.step) | — | Four slotted press-fit rings that lock the default `Ø 14 × 9 mm` Same Sky `CUSA-TR65-065-2200-TH68` transducers into the `Ø 14.6 mm` barrels, plus four optional sleeves for `Ø 10 × 7 mm` transducers (laid out beside the station). |

---

## 6-Board PCB Stack on One Breakaway Panel (`hardware/releases/v1/`)

All six 4-layer `1.2 mm` PCBs are packed onto one **`140.6 × 63.2 mm` mouse-bite breakaway panel** (`crown`, `main`, the vertical `probe`, and a column of `reflector`, `dome` and `power`; `0 DRC errors`, 92 consolidated BOM lines, 206 SMT placements, every board held by ≥ 2 tabs placed ≥ 1 mm from parts). The panel has no own rails or fiducials: order it with **"Edge rails / fiducials: added by JLCPCB"**. Each board also has its own release folder (Gerbers, BOM, CPL).

| Panel Top (`panel/renders/top.svg`) | Panel Bottom (`panel/renders/bottom.svg`) |
| :---: | :---: |
| ![Panel Top](hardware/releases/v1/panel/renders/top.svg) | ![Panel Bottom](hardware/releases/v1/panel/renders/bottom.svg) |

| Board | Size | Role & Key Components | Schematics, Gerbers & Renders |
| :--- | :---: | :--- | :--- |
| **1. `main`** | `32.0 × 52.0 mm` | 4-pole terminal `J1` (`12–32 V` + `RS-485 A/B`), `1.1 A` / `60 V` PTC fuse, `SMAJ36A` TVS, `SM712` RS-485 TVS, `J5` power-board cable (`VIN_PROT` out, `+5V` / `+12V_HEAT` in), `LM66100` ideal diode, socketed `Seeed XIAO ESP32-C6`, `SP3485EN` RS-485 transceiver (separate TX / RX / DE, `390 Ω` fail-safe bias, optional `120 Ω` terminator `R26`), `LSM6DS3TR-C` IMU, crown reset / boot / IRQ lines, `AO3400A` heater switch, `ADS1115` heater power monitor (`0.1 Ω` reflector + `1 Ω` probe heater shunts). | [`hardware/releases/v1/main/`](hardware/releases/v1/main/) · [Top SVG](hardware/releases/v1/main/renders/top.svg) · [Bottom SVG](hardware/releases/v1/main/renders/bottom.svg) · [Schematic PDF](hardware/releases/v1/main/schematic.pdf) |
| **2. `crown`** | `60 × 60 mm` (transducer notches, NE cable chamfer) | `STM32G431RBT6` I²C peripheral (`0x2A`, firmware in `crown/`, reflashed by the ESP32 through the ROM I²C bootloader; SWD debug port `J12` (Tag-Connect TC2030-NL footprint, optional: a blank crown is flashed by the station)), 4× `UCC27524` H-bridge drivers + `BAV99` T/R isolation, transducer headers `J2..J5` (1 = A, 2 = B), AC-coupled receive → `74HC4052` → `TLV9062` → STM32 PGA / ADC, 4× Murata piezo headers (`J6..J9`) + charge amplifier, `AS3935` + `MA5532-AE` lightning antenna (SPI), `MMC5603NJ` compass, `XC6206` 3.3 V LDO, center 4-pin B2B socket `J10` (`to dome`). | [`hardware/releases/v1/crown/`](hardware/releases/v1/crown/) · [Top SVG](hardware/releases/v1/crown/renders/top.svg) · [Bottom SVG](hardware/releases/v1/crown/renders/bottom.svg) · [Schematic PDF](hardware/releases/v1/crown/schematic.pdf) |
| **3. `reflector`** | `24.0 × 24.0 mm` | Horizontal de-icing heater board under the wind-tunnel cone: 4× `120 Ω` `2512` `2 W` resistors wired in parallel on `+12V_HEAT` (`30 Ω`, `4.5 W` regulated), center `10k 1%` NTC thermistor (`TH1`), and underside 4-pin SMD male header `J1` plugging directly into `probe J2`. | [`hardware/releases/v1/reflector/`](hardware/releases/v1/reflector/) · [Top SVG](hardware/releases/v1/reflector/renders/top.svg) · [Bottom SVG](hardware/releases/v1/reflector/renders/bottom.svg) · [Schematic PDF](hardware/releases/v1/reflector/schematic.pdf) |
| **4. `dome`** | `Ø 13.6 mm` round | Elevated UV, visible & NIR micro-PCB inside the hollow `JGS1` fused-silica hemisphere: `LTR-390UV-01` (`U1`), `TSL25911FN` (`U2`, `C176812`), and `GUVB-C31SM` (`U3`, populated by default) on `F.Cu` (`zero top passives to avoid horizon shadows`), with `C1`, `C2`, `C3`, and the 4-pin vertical B2B male header `J1` on `B.Cu`. | [`hardware/releases/v1/dome/`](hardware/releases/v1/dome/) · [Top SVG](hardware/releases/v1/dome/renders/top.svg) · [Bottom SVG](hardware/releases/v1/dome/renders/bottom.svg) · [Schematic PDF](hardware/releases/v1/dome/schematic.pdf) |
| **5. `probe`** | `14.0 × 56.0 mm` | Vertical pagoda stem: `SCD41-D-R2` ($\text{CO}_2$), `SGP41-D-R4` ($\text{VOC}/\text{NO}_x$) and `BMP585` barometer above a milled thermal isolation slot, `SHT40-AD1F-R2` (`Temp/RH`) at the lower intake tip, backside `0.42 W` parallel heater (`2× 680 Ω 1206` on `+12V_HEAT`) + `LM2903`/`TH1` `+8 °C` thermostat behind `SCD41`, and top right-angle 4-pin SMD socket `J2` (`to reflector`). | [`hardware/releases/v1/probe/`](hardware/releases/v1/probe/) · [Top SVG](hardware/releases/v1/probe/renders/top.svg) · [Bottom SVG](hardware/releases/v1/probe/renders/bottom.svg) · [Schematic PDF](hardware/releases/v1/probe/schematic.pdf) |
| **6. `power`** | `28.5 × 21.5 mm` | Two-rail supply lying flat under `main`: 2× `TPS54360` `4.5–60 V` at `743 kHz` (power stages on top, small-signal parts on the bottom): `U1` → `+5V` electronics (XIAO, sensors, PMSA003I, crown; `8.2 µH`, 2× `47 µF`) and `U2` → `+12V_HEAT` (`22 µH`, 2× `22 µF 25 V`; regulated above ≈ 13.5 V input, input − ≈ 1 V below) for the heaters, the crown transducer drivers and spare crown `J11`, so heater PWM steps and heater/cable faults never touch the electronics rail; `U3` `AP2112K-3.3` LDO → `+3V3_PROBE`, a dedicated rail for the probe sensors (SCD41 measurement peaks no longer load the XIAO regulator). 10-pin JST-SH `J1` along the top edge (half of it past `main`'s edge; plug it before fitting `main`), cabled to `main J5`; switch nodes, VIN and outputs are top-layer copper pours. | [`hardware/releases/v1/power/`](hardware/releases/v1/power/) · [Top SVG](hardware/releases/v1/power/renders/top.svg) · [Schematic PDF](hardware/releases/v1/power/schematic.pdf) |

---

## JLC3DP (3D Printing) & JLCPCB (PCB / PCBA) Ordering & Weatherproofing Guide

### 1. Ordering the Structural Plastic Parts (upload the 8 plates in `enclosure/print/`)

| Method | Process & Material to Select | Surface / Slicer Settings | Why |
| :--- | :--- | :--- | :--- |
| **Option A: JLC3DP Industrial Printing (Recommended)** | **MJF (Multi-Jet Fusion) `PA12-HP Nylon`** or **SLS `PA1172 / PA12 Nylon`**<br>*(Do NOT pick standard SLA 8000/9000 resin — UV sunlight makes SLA resin brittle outdoors)* | **Color:** Natural Grey (or Dyed Black for `base`/`mast_clamp`), then spray outside of `pagoda`/`crown` with 2 coats of **white UV-resistant acrylic/polyurethane clear-coat**.<br>*Upload the print plates from `enclosure/print/` (`base.stl`, `pagoda.stl`, `struts.stl`, …: each part's bodies laid out `≥ 6 mm` apart on the bed): the assembled `base.stl`, `pagoda.stl`, `struts.stl` and `mast_shims.stl` contain several bodies only `0.1–0.4 mm` apart that would fuse.* | MJF/SLS `PA12` has `100%` isotropic z-strength (threads and bayonet lugs never delaminate), `±0.15 mm` accuracy, and survives `-40..+120 °C`. |
| **Option B: Local FDM Printer (or JLC3DP FDM)** | **UV-Stabilized `ASA` Filament** (`Polymaker Polylite ASA` White / Titanium Grey) | **Nozzle:** `0.4 mm` · **Layer height:** `0.16 mm` (`0.12 mm` on threads)<br>**Wall loops:** `4` (`1.6 mm`) · **Top/Bottom shells:** `5` · **Infill:** `35%` Gyroid<br>**Supports:** expect supports for the base clip hooks, the crown_base transducer barrels and the umbrella; lay the 8 C-strut halves flat on their backs. | `ASA` is inherently UV-stable and weather-resistant; `0.16 mm` layers print the `45°` `M12 × 1.75` and `M22 × 1.5` helical threads cleanly. |

**FDM print orientation** (MJF / SLS need no supports and print the plates as laid out):

| Plate | Orientation | Supports |
| :--- | :--- | :--- |
| `base` | open bay up | clip hooks, cable window edge; print the umbrella and reset button upside down |
| `pagoda` | each louver plate upside down (blade top on the bed) | the labyrinth skirts and drip lips |
| `wind_tunnel` | cone up | none (bayonet slots bridge) |
| `probe_sleeve` | each half on its flat inner face | none |
| `mast_clamp` | ears flat | none; shims standing up |
| `struts` | C-halves flat on their backs; nuts flat; sleeves standing | none |
| `crown` | `crown_base` floor down, `crown` lid dome up | 29° transducer barrels (tree supports), lid bayonet lugs |
| `orings` | flat | none (TPU) |

### 2. Ordering / Printing the 5 Elastomeric O-Rings (`orings.stl`)

- **Option A — Standard Off-the-Shelf Rubber O-Rings (Best Long-Term Seal):**
  - `3×` **`ID 88 mm × CS 2.0 mm`** (`OD 92 mm`, EPDM or NBR 70A) for `base ↔ pagoda`, `pagoda ↔ wind_tunnel`, and `crown_base ↔ crown` bayonet joints (the glands are sized for `2.0 mm`).
  - `1×` **`ID 21 mm × CS 1.5 mm`** (`OD 24 mm`, EPDM/NBR 70A) for the `probe_sleeve ↔ pagoda` bottom M22 bulkhead.
  - `1×` **`ID 17 mm × CS 1.5 mm`** (`OD 20 mm`, EPDM/NBR 70A) for the `JGS1` fused-silica quartz dome flange.
- **Option B — 3D-Printing `orings.stl`:**
  - **Local FDM:** Print `orings.stl` in **TPU 90A or 95A** (`0.12 mm` layer height, `100%` concentric infill, `3` wall loops, `25 mm/s` speed).
  - **JLC3DP:** Upload `orings.stl` and select **DLP Flexible Resin / Elastomer (Shore 70A–80A Black)**.

### 3. Ordering the Breakaway Panel (`hardware/releases/v1/panel/`) at JLCPCB

1. **PCB Fabrication Settings (`panel/gerbers/`):**
   - **Base Material:** `FR-4` · **Layers:** `4` · **Dimensions:** panel `140.6 × 63.2 mm` ("Panel by Customer"; edge rails + fiducials: added by JLCPCB)
   - **PCB Thickness:** **`1.2 mm`** *(required — `probe_sleeve` grooves and snap-clips are sized for `1.2 mm`)*
   - **Surface Finish:** **`ENIG (Immersion Gold)`** *(essential for outdoor corrosion resistance on `probe`, `dome`, and `reflector` instead of HASL tin/lead)*
   - **Impedance Control:** `Yes` → **`JLC04121H-7628`** (`4-layer 1.2 mm` standard stackup)
   - **Copper Weight:** `1 oz` outer / `0.5 oz` inner · **Via Covering:** **`Tented`** (or **Epoxy Filled & Capped** for maximum moisture protection)
2. **Weatherproofing Exposed PCBs (`probe`, `dome`, `reflector`, `crown`):**
   - After assembly and testing, brush or spray a **non-silicone polyurethane or acrylic conformal coating** (`Humiseal 1A33`, `Electrolube URC` or `MG Chemicals 419D` acrylic) across both sides of `probe`, `reflector`, `crown`, and the underside of `dome`.
   - **Keep-Out Windows (`F.SilkS` / `B.SilkS` boxes marked on the boards):** Keep conformal coating **OFF** the optical/gas/pressure apertures of `SHT40` (`probe U3`), `SCD41` (`probe U1`), `SGP41` (`probe U2`), `BMP585` (`probe U5`), `LTR-390UV` (`dome U1`), `TSL25911FN` (`dome U2`), `GUVB-C31SM` (`dome U3`), and the connector pins (`J1..J11`).
   - Fill the `2.0 mm` sealant groove (`y = 77.2..79.2 mm`) inside `probe_sleeve_front` + `probe_sleeve_back` with a **silicone-free sealant** (PTFE grease such as `Krytox GPL 205`, or a 2-part polyurethane potting resin) before closing the two sleeve halves around the `probe` PCB.
   - **Never use silicone (RTV, silicone grease, Molykote 111) anywhere inside the station:** siloxane vapours permanently poison the `SGP41` MOX gas sensor and drift the `SHT40` (Sensirion handling guidelines).

### 4. Mast, Cables & Harnesses

* **Mast:** hollow tube, `Ø 32–34 mm` outside (or `Ø 24–32 mm` with `mast_shims`), **inside diameter ≥ `18 mm`**: the
  cable-gland dome nut reaches below the pole stop into the tube and the supply / RS-485 cable runs down inside it
  (e.g. `33.7 × 3.2 mm` steel pipe, ID `27.3 mm`).
* **Harnesses** (all JST pin 1 ↔ pin 1, straight-through; the swept-cable test checks the crown cable route at
  `Ø 3.2 mm` / `10 mm` bend radius):

| Harness | Ends | Length | Conductors / pinout |
| :--- | :--- | :--- | :--- |
| Crown cable | main `J2` ↔ crown `J1`, JST SH 12-pin (`SHR-12V-S-B`) | `260 mm`, OD ≤ `3.2 mm` | 1–2 `+12V_HEAT`, 3–4 `GND`, 5 `+5V`, 6 `GND`, 7 `SDA`, 8 `GND`, 9 `SCL`, 10 `CROWN_IRQ`, 11 `CROWN_NRST`, 12 `CROWN_BOOT0`; thread the bare cable through the NE strut before crimping the crown end |
| Probe jumper | main `J3` ↔ probe `J1`, JST SH 10-pin (`SHR-10V-S-B`) | `60 mm` | 1–2 `+12V_HEAT`, 3–4 `HEAT_SW`, 5 `GND`, 6 `NTC_SENSE`, 7 `+3V3_PROBE`, 8 `CO2_HEAT_RTN`, 9 `SDA`, 10 `SCL` |
| Power jumper | main `J5` ↔ power `J1`, JST SH 10-pin (`SHR-10V-S-B`) | `28 mm` | 1–2 `VIN_PROT`, 3–4 `GND`, 5–6 `+5V`, 7–8 `+12V_HEAT`, 9 `GND`, 10 `+3V3_PROBE` |
| PM sensor | main `J4` JST ZH 6-pin (`ZHR-6`) → PMSA003I `2 × 5` `1.27 mm` IDC | `100 mm` | ZH 1 `+5V` → module 1+2, ZH 2 `GND` → 3+4, ZH 3 `SDA` → 9, ZH 4 `SCL` → 7; module 5, 6, 8, 10 open |
| Transducers ×4 | crown `J2..J5` JST SH 2-pin (`SHR-02V-S-B`) | pigtail | 1 = A, 2 = B (driven differentially, neither is ground; keep all four the same way round) |
| Piezo discs ×4 | crown `J6..J9` JST SH 2-pin | pigtail | 1 = disc (+, ceramic side), 2 = `GND` (brass) |

* **Custom & hand-fit parts:** the `JGS1` dome is made to `hardware/releases/v1/enclosure/drawings/jgs1_dome.svg`
  (send it for quotes, e.g. VY Optoelectronics, Coligh Optics, Felix Glass; order samples first). `AS3935-BQFT`,
  `MA5532-AE`, `LTR-390UV-01` and `GUVB-C31SM` are not stocked by JLCPCB: buy them (Digi-Key / Mouser) and either
  consign them to JLCPCB or use its Global Sourcing so they are placed with the rest; see `REVIEW_CHECKLIST.md` B2 / B5
  for current stock, prices and the per-station budget.
* **Reflector heater:** put a silicone-free thermal gap filler (BOM) between the four heater resistors and the
  underside of the `wind_tunnel` deck before closing it.

---

## Repository Structure & Build Commands

| Folder | Contents |
| :--- | :--- |
| [`hardware/product/spec.yaml`](hardware/product/spec.yaml) | Single source of truth for board dimensions, coordinates, interconnects, MCU pins, and enclosure geometry. |
| [`hardware/circuits/`](hardware/circuits/) | SKiDL circuit generators (`main`, `crown`, `reflector`, `dome`, `probe`), custom KiCad symbols/footprints, and 3D STEP models. |
| [`hardware/releases/v1/`](hardware/releases/v1/) | Ready-to-order Gerbers, drills, JLCPCB BOM/CPL CSVs, schematics, 3D renders, STLs/STEPs, and [`assembly_parts.csv`](hardware/releases/v1/assembly_parts.csv). |
| [`mechanical/scripts/`](mechanical/scripts/) | CadQuery 3D enclosure generator (`build_enclosure.py`) and VTK ray-traced renderer (`render_assembly3d.py`). |
| [`station/`](station/) | ESP32-C6 C++17 firmware (`wind/`, `rain/`, `environment/`, `airquality/`, `heater/`, `rs485/`, `telemetry/`), Web UI (`station/data/`), and Unity native unit tests. |

```bash
# Run full hardware, CAD, release packaging, and verification test pipeline
python3 hardware/scripts/build.py

# Run C++17 firmware native unit tests (12 test suites)
docker compose --profile lint run --rm -T cpp-lint "pio test -d station -e native"

# Run Python & C++ linters
docker compose --profile lint run --rm lint check .
```
