# AeroMet V1 — Solid-State Roof Weather & Air-Quality Station

Open-source roof weather and air-quality station (`Ø 96 × 207 mm` in UV-stabilized ASA) with no moving parts. It measures wind speed and direction by ultrasonic time-of-flight, rainfall and hail by acoustic impact on a 4-piezo dome, UV and solar radiation under a fused-silica quartz hemisphere, temperature, humidity, barometric pressure, lightning distance, and outdoor air quality ($\text{PM}_{1.0}$, $\text{PM}_{2.5}$, $\text{PM}_{4.0}$, $\text{PM}_{10}$, $\text{VOC}$, $\text{NO}_x$, and $\text{CO}_2$).

The station runs from a wired `12–32 V DC` supply and communicates over both **Wi-Fi** (boosted by an internal `2.4 / 5.8 GHz` U.FL dipole antenna) and **RS-485** (`115,200 bps` over the spare pair in a 4-wire roof cable).

| Isometric (`iso.png`) | Cutaway Section (`section.png`) | X-Ray Inside (`inside.png`) | Exploded View (`exploded.png`) |
| :---: | :---: | :---: | :---: |
| ![Isometric](hardware/releases/v1/enclosure/renders/iso.png) | ![Section](hardware/releases/v1/enclosure/renders/section.png) | ![Inside](hardware/releases/v1/enclosure/renders/inside.png) | ![Exploded](hardware/releases/v1/enclosure/renders/exploded.png) |

---

## Measured Parameters & Hardware Architecture

| Parameter | Sensor / Circuit | Range & Resolution | Location & Protection |
| :--- | :--- | :--- | :--- |
| **Wind Speed & Direction** | 4× `40 kHz` waterproof ultrasonic transducers + `SN74LVC2G04` + `CD4052B` + `TLV9062` + `LM393` | `0–60 m/s` (`0.01 m/s`), `0–359.9°` (`0.1°`), `4 Hz` | Downward-facing in `crown.stl` across a `48 mm` vertical wind gap (`D = 54 mm` span). |
| **Compass & Mast Tilt** | `MEMSIC MMC5603NJ` 20-bit AMR compass + `ST LSM6DS3TR-C` 6-axis IMU | `±0.5°` heading, `±0.1°` tilt | Auto-aligns wind direction to North and subtracts pole vibration from the rain sensor. |
| **Rain & Hail** | 4× `20 mm` `Murata 7BB-20-6L0` piezo discs in parallel + `TLV9062` charge/envelope amp | `0–200 mm/h`, `0.01 mm` step, hail hit counter | Bonded to four `1.0 mm`-thin ceiling pockets in `crown.stl` (`45°, 135°, 225°, 315°`). |
| **UV Index & Solar Light** | `Lite-On LTR-390UV-01` (UV-A/UV-B) + `Vishay VEML7700-TT` (ALS) | `UVI 0–16+`, `0–140,000 lx` (`0–1,500 W/m²`) | Mounted on the elevated round `dome` PCB inside a hollow `Ø 18 mm` `JGS1` fused-silica quartz hemisphere. |
| **Temperature & Humidity** | `Sensirion SHT40-AD1F-R2` with factory IP67 PTFE membrane | `-40..+85 °C` (`±0.2 °C`), `0–100% RH` (`±1.8%`) | Bottom tip of `probe` PCB below a milled thermal slot, exposed through a `4.6 × 4.6 mm` window in `probe_sleeve_front.stl`. |
| **Barometric Pressure** | `Bosch BMP581` | `300–1,250 hPa` (`±0.03 hPa` relative) | Bottom of `main` PCB (`B.Cu`) with dynamic wind-pressure (Bernoulli) compensation. |
| **Particulate Matter** | `Sensirion SEN55-SDN-T` | $\text{PM}_{1.0}, \text{PM}_{2.5}, \text{PM}_{4.0}, \text{PM}_{10}$ (`µg/m³`) | Lower `base.stl` bay behind `1.2 mm` bug-screened intake slits; plugs into `main J4`. |
| **VOC & NOx** | `Sensirion SGP41-D-R4` | VOC Index (`1–500`), NOx Index (`1–500`) | Upper section of `probe` PCB behind a 5-slit draft hood in `probe_sleeve_front.stl`. |
| **True $\text{CO}_2$** | `Sensirion SCD41-D-R2` + closed-loop `+8 °C` backside heater (`2× 470 Ω 1206` + `LM393` + `TH1`) | `400–5,000 ppm` | Upper section of `probe` PCB; backside pocket in `probe_sleeve_back.stl` traps warmth in winter. |
| **Lightning Detector** | `ScioSense AS3935-BQFT` + `220 µH` / `470 pF C0G` `500 kHz` tank | `1–40 km` storm distance & strike count | Mounted on `main` PCB inside the sealed lower base bay. |
| **Wind-Tunnel De-Icer** | 4× `100 Ω` `2512` `1 W` parallel resistors (`25 Ω`, `4.0 W`) + `10k` NTC `TH1` | Constant `4.0 W` PWM (`12–32 V` auto-scaled) | Horizontal `reflector` PCB (`24 × 24 mm`) seated directly under the ultrasonic reflector cone. |
| **Power & Connectivity** | `KF250-3.5-4P` terminal (`J1`) + `SCT2400` buck + `XIAO ESP32-C6` + `SP3485EN` RS-485 | `12–32 V DC`, Wi-Fi 6 (`+20.5 dBm` + U.FL dipole) + `115,200 bps` RS-485 | All 4 wires of the roof cable (`VIN+`, `GND`, `RS485_A`, `RS485_B`) terminate in `main J1`. |

---

## Modular 3D-Printed Enclosure (`hardware/releases/v1/enclosure/`)

The enclosure is split into **10 modular parts** using **8-lug Push-Turn-Click Bayonet Interlocks (`20.5°` / `~19 mm` turn travel with O-ring pre-load and a `0.47 mm` anti-unscrew detent step)** on the three main outer housing rings so internal wires never twist during assembly, while using **true right-hand helical screw threads** on the `probe_sleeve` bottom watertight bulkhead, the 4 split-C struts, and the `JGS1` UV quartz dome retainer nut.

| Part & Files | Render | Function & Assembly |
| :--- | :---: | :--- |
| **1. `base`**<br>[`base.stl`](hardware/releases/v1/enclosure/base.stl) · [`base.step`](hardware/releases/v1/enclosure/base.step) | ![base](hardware/releases/v1/enclosure/renders/base.png) | Sealed lower electronics bay for `main` PCB and `SEN55`. Includes an **internal conical Drip Umbrella Canopy (`y = 57.2..62.0 mm`)** over the `main` PCB, a **waterproofed bottom-protruding REBOOT / FACTORY-RESET button plunger (`x = -16.0, z = +16.5 mm` under `SW1`)**, 4× 4-slit `1.2 mm` bottom micro-grilles + perimeter weep drains, 4 PCB snap-clips, a U.FL antenna cradle, a center `M12 / PG7` cable-gland hole, and 8 external bayonet lugs (`y = 66.4..69.9 mm`). |
| **2. `mast_clamp`**<br>[`mast_clamp.stl`](hardware/releases/v1/enclosure/mast_clamp.stl) · [`mast_clamp.step`](hardware/releases/v1/enclosure/mast_clamp.step) | ![mast_clamp](hardware/releases/v1/enclosure/renders/mast_clamp.png) | Detachable front pole saddle (`4× Ø 6.4 mm` M6 bolt holes) with flat outer seating faces for standard M6 bolts and washers. Clamps onto `Ø 25–34 mm` roof poles. |
| **3. `orings`**<br>[`orings.stl`](hardware/releases/v1/enclosure/orings.stl) · [`orings.step`](hardware/releases/v1/enclosure/orings.step) | ![orings](hardware/releases/v1/enclosure/renders/orings.png) | Six elastomeric gasket rings (standard metric EPDM/NBR O-rings or printable in TPU 95A / JLC3DP Flexible Elastomer): three `Ø 88 × 1.5 mm` bayonet seals (`base ↔ pagoda`, `pagoda ↔ wind_tunnel`, `crown_base ↔ crown`), one `Ø 21 × 1.5 mm` `probe_sleeve` bottom seal, one `Ø 17 × 1.5 mm` `JGS1` quartz dome seal, and one `Ø 4 × 1.5 mm` bottom reset-button plunger O-ring. |
| **4. `probe_sleeve_back`**<br>[`probe_sleeve_back.stl`](hardware/releases/v1/enclosure/probe_sleeve_back.stl) · [`probe_sleeve_back.step`](hardware/releases/v1/enclosure/probe_sleeve_back.step) | ![probe_sleeve_back](hardware/releases/v1/enclosure/renders/probe_sleeve_back.png) | Rear half of the `probe` PCB cartridge. Features a close-fitting (`0.10 mm` clearance) PCB pinch dam with a grease/sealant reservoir groove at the bottom, a dedicated `12.6 × 3.35 mm` vertical rear cable conduit for the 10-wire `crown` cable, a smooth press-fit top spigot, and the rear half of the bottom ISO `M22 × 1.5` helical thread. |
| **5. `probe_sleeve_front`**<br>[`probe_sleeve_front.stl`](hardware/releases/v1/enclosure/probe_sleeve_front.stl) · [`probe_sleeve_front.step`](hardware/releases/v1/enclosure/probe_sleeve_front.step) | ![probe_sleeve_front](hardware/releases/v1/enclosure/renders/probe_sleeve_front.png) | Front half of the `probe` cartridge. Fully encloses `probe J1` on the front (zero external notch), pinches within `0.10 mm` of the PCB above `J1` with a grease/glue groove, screws watertight into `pagoda`'s bottom deck via the ISO `M22 × 1.5` helical thread, and press-fits into `pagoda`'s upper collar. |
| **6. `pagoda`**<br>[`pagoda.stl`](hardware/releases/v1/enclosure/pagoda.stl) · [`pagoda.step`](hardware/releases/v1/enclosure/pagoda.step) | ![pagoda](hardware/releases/v1/enclosure/renders/pagoda.png) | Bolted 5-plate Stevenson radiation shield (`y = 66.9..123.9 mm`). Clamped by 4× `M6 × 55 mm` stainless steel through-bolts (`at r = 23.0 mm`) inside `Ø 10.0 mm OD / Ø 6.2 mm ID` spacer columns with interlocking male/female spigots and recessed top bolt-head wells, plus **4× 3-slit `1.2 mm` top ventilation & moisture-dissipation micro-grilles** sheltered under the 5th louver. |
| **7. `wind_tunnel`**<br>[`wind_tunnel.stl`](hardware/releases/v1/enclosure/wind_tunnel.stl) · [`wind_tunnel.step`](hardware/releases/v1/enclosure/wind_tunnel.step) | ![wind_tunnel](hardware/releases/v1/enclosure/renders/wind_tunnel.png) | Compact heated conical ultrasonic reflector deck (`y = 120.6..130.0 mm`). Locks onto the top of `pagoda.stl` via an 8-lug internal Push-Turn-Click bayonet, sits in flush thermal contact (`y = 124.48 mm`) with the `24 × 24 mm` `reflector` heater resistors, and routes the `45°` NE strut cable radially inward to `probe_sleeve_back`. |
| **8. `struts`**<br>[`struts.stl`](hardware/releases/v1/enclosure/struts.stl) · [`struts.step`](hardware/releases/v1/enclosure/struts.step) | ![struts](hardware/releases/v1/enclosure/renders/struts.png) | Four separate **Double-C split hollow threaded tubes** (`8 C-shaped halves`, `Ø 6.4 mm` internal cable bore) with `Ø 14.0 mm` top and bottom flange stoppers (`y = 126.5` and `174.0 mm`), **standard ISO `M12 × 1.75` external helical threaded ends**, and **8 matching ISO `M12 × 1.75` hex locknuts** (compatible with standard off-the-shelf metal `M12 × 1.75` nuts). |
| **9. `crown_base`**<br>[`crown_base.stl`](hardware/releases/v1/enclosure/crown_base.stl) · [`crown_base.step`](hardware/releases/v1/enclosure/crown_base.step) | ![crown_base](hardware/releases/v1/enclosure/renders/crown_base.png) | Lower ultrasonic transducer & `crown` PCB tray (`y = 172.5..189.8 mm`). Open on top so you can tighten the 4 top `M12 × 1.75` strut hex locknuts, install the 4 downward `40 kHz` transducers, and snap in the `crown` PCB before locking on `crown.stl` via the upper `Ø 88 mm` O-ring and 8-lug Push-Turn-Click bayonet (`20.5°` turn). |
| **10. `crown`**<br>[`crown.stl`](hardware/releases/v1/enclosure/crown.stl) · [`crown.step`](hardware/releases/v1/enclosure/crown.step) | ![crown](hardware/releases/v1/enclosure/renders/crown.png) | Removable top piezo rain & UV dome lid (`y = 186.5..198.0 mm`). Locks onto `crown_base.stl` via an 8-lug internal Push-Turn-Click bayonet, holds the 4× `20 mm` Murata piezo discs under `1.0 mm`-thin ceiling pockets, and includes the **standard ISO `M22 × 1.5` Hex-Flange Retainer Nut** clamping the hollow `Ø 18 mm` `JGS1` fused-silica hemisphere + O-ring (compatible with standard metal `M22 × 1.5` panel locknuts). |

---

## 5-Board PCB Stack & Single-Order Breakaway Panel (`hardware/releases/v1/`)

All five 4-layer `1.2 mm` PCBs (`main`, `crown`, `reflector`, `dome`, `probe` — with the round `dome` PCB stacked above `reflector` and ringed with curved mouse-bites) are panelized onto a single **`126.0 × 56.1 mm` mouse-bite breakaway panel** (`0 DRC errors, 0 unrouted`, 58 consolidated BOM lines, 128 SMT placements) so the entire station can be ordered at JLCPCB as **one PCB/PCBA job**:

| Panel Top (`panel/renders/top.svg`) | Panel Bottom (`panel/renders/bottom.svg`) |
| :---: | :---: |
| ![Panel Top](hardware/releases/v1/panel/renders/top.svg) | ![Panel Bottom](hardware/releases/v1/panel/renders/bottom.svg) |

| Board | Size | Role & Key Components | Schematics, Gerbers & Renders |
| :--- | :---: | :--- | :--- |
| **1. `main`** | `41.0 × 56.0 mm` | 4-pole terminal `J1` (`12–32 V` + `RS-485 A/B`), `1.1 A` PTC fuse, `SMAJ33A` TVS, `SCT2400` `5 V` buck, `LM66100` ideal diode, socketed `Seeed XIAO ESP32-C6`, `SP3485EN` auto-direction RS-485 transceiver, `BMP581` barometer, `LSM6DS3TR-C` IMU, `AS3935` lightning detector, `AO3400A` heater switch. | [`hardware/releases/v1/main/`](hardware/releases/v1/main/) · [Top SVG](hardware/releases/v1/main/renders/top.svg) · [Bottom SVG](hardware/releases/v1/main/renders/bottom.svg) · [Schematic PDF](hardware/releases/v1/main/schematic.pdf) |
| **2. `crown`** | `41.0 × 56.0 mm` | 4× ultrasonic transducer headers (`J2..J5`), reciprocal `1 kΩ` `40 kHz` AFE (`SN74LVC2G04`, `CD4052B`, `TLV9062`, `LM393` zero-cross + envelope gate), 4× Murata piezo headers (`J6..J9`) + `TLV9062` charge/envelope amp, `MMC5603NJ` compass, center 4-pin vertical B2B socket `J10` (`to dome`). | [`hardware/releases/v1/crown/`](hardware/releases/v1/crown/) · [Top SVG](hardware/releases/v1/crown/renders/top.svg) · [Bottom SVG](hardware/releases/v1/crown/renders/bottom.svg) · [Schematic PDF](hardware/releases/v1/crown/schematic.pdf) |
| **3. `reflector`** | `24.0 × 24.0 mm` | Horizontal de-icing heater board under the wind-tunnel cone: 4× `100 Ω` `2512` `1 W` resistors wired in parallel (`25 Ω`, `4.0 W`), center `10k 1%` NTC thermistor (`TH1`), and underside 4-pin SMD male header `J1` plugging directly into `probe J2`. | [`hardware/releases/v1/reflector/`](hardware/releases/v1/reflector/) · [Top SVG](hardware/releases/v1/reflector/renders/top.svg) · [Bottom SVG](hardware/releases/v1/reflector/renders/bottom.svg) · [Schematic PDF](hardware/releases/v1/reflector/schematic.pdf) |
| **4. `dome`** | `Ø 13.6 mm` round | Elevated UV & solar micro-PCB inside the hollow `JGS1` fused-silica hemisphere: `LTR-390UV-01` (`U1`) and `VEML7700-TT` (`U2`) on `F.Cu` (`zero top passives to avoid horizon shadows`), with `C1`, `C2`, and the 4-pin vertical B2B male header `J1` on `B.Cu`. | [`hardware/releases/v1/dome/`](hardware/releases/v1/dome/) · [Top SVG](hardware/releases/v1/dome/renders/top.svg) · [Bottom SVG](hardware/releases/v1/dome/renders/bottom.svg) · [Schematic PDF](hardware/releases/v1/dome/schematic.pdf) |
| **5. `probe`** | `14.0 × 50.0 mm` | Vertical pagoda stem: `SCD41-D-R2` ($\text{CO}_2$) + `SGP41-D-R4` ($\text{VOC}/\text{NO}_x$) above a milled thermal isolation slot, `SHT40-AD1F-R2` (`Temp/RH`) at the lower intake tip, backside `0.5 W` parallel heater (`2× 470 Ω 1206`) + `LM393`/`TH1` `+8 °C` thermostat behind `SCD41`, and top right-angle 4-pin SMD socket `J2` (`to reflector`). | [`hardware/releases/v1/probe/`](hardware/releases/v1/probe/) · [Top SVG](hardware/releases/v1/probe/renders/top.svg) · [Bottom SVG](hardware/releases/v1/probe/renders/bottom.svg) · [Schematic PDF](hardware/releases/v1/probe/schematic.pdf) |

---

## JLC3DP (3D Printing) & JLCPCB (PCB / PCBA) Ordering & Weatherproofing Guide

### 1. Ordering the 9 Structural Plastic Parts (`base`, `mast_clamp`, `probe_sleeve_back`, `probe_sleeve_front`, `pagoda`, `wind_tunnel`, `struts`, `crown_base`, `crown`)

| Method | Process & Material to Select | Surface / Slicer Settings | Why |
| :--- | :--- | :--- | :--- |
| **Option A: JLC3DP Industrial Printing (Recommended)** | **MJF (Multi-Jet Fusion) `PA12-HP Nylon`** or **SLS `PA1172 / PA12 Nylon`**<br>*(Do NOT pick standard SLA 8000/9000 resin — UV sunlight makes SLA resin brittle outdoors)* | **Color:** Natural Grey (or Dyed Black for `base`/`mast_clamp`), then spray outside of `pagoda`/`crown` with 2 coats of **white UV-resistant acrylic/polyurethane clear-coat**.<br>*Before uploading `pagoda.stl`, `struts.stl`, and `crown.stl` to JLC3DP, click **"Split to Objects"** in PrusaSlicer/Bambu Studio so the louver rings, C-strut halves, and nuts upload as separate parts.* | MJF/SLS `PA12` has `100%` isotropic z-strength (threads and bayonet lugs never delaminate), `±0.15 mm` accuracy, and survives `-40..+120 °C`. |
| **Option B: Local FDM Printer (or JLC3DP FDM)** | **UV-Stabilized `ASA` Filament** (`Polymaker Polylite ASA` White / Titanium Grey) | **Nozzle:** `0.4 mm` · **Layer height:** `0.16 mm` (`0.12 mm` on threads)<br>**Wall loops:** `4` (`1.6 mm`) · **Top/Bottom shells:** `5` · **Infill:** `35%` Gyroid<br>**Supports:** None needed after `Split to Objects` on `pagoda.stl`, `struts.stl`, and `crown.stl` (lay the 8 C-strut halves flat on their backs). | `ASA` is inherently UV-stable and weather-resistant; `0.16 mm` layers print the `45°` `M12 × 1.75` and `M22 × 1.5` helical threads cleanly. |

### 2. Ordering / Printing the 6 Elastomeric O-Rings (`orings.stl`)

- **Option A — Standard Off-the-Shelf Rubber O-Rings (Best Long-Term Seal):**
  - `3×` **`ID 88 mm × CS 1.5 mm`** (`OD 91 mm`, EPDM or NBR 70A) for `base ↔ pagoda`, `pagoda ↔ wind_tunnel`, and `crown_base ↔ crown` bayonet joints.
  - `1×` **`ID 21 mm × CS 1.5 mm`** (`OD 24 mm`, EPDM/NBR 70A) for the `probe_sleeve ↔ pagoda` bottom M22 bulkhead.
  - `1×` **`ID 17 mm × CS 1.5 mm`** (`OD 20 mm`, EPDM/NBR 70A) for the `JGS1` fused-silica quartz dome flange.
  - `1×` **`ID 4.0 mm × CS 1.5 mm`** (`OD 7.0 mm`, EPDM/NBR 70A) for the bottom `REBOOT / RESET` button plunger gland.
- **Option B — 3D-Printing `orings.stl`:**
  - **Local FDM:** Print `orings.stl` in **TPU 90A or 95A** (`0.12 mm` layer height, `100%` concentric infill, `3` wall loops, `25 mm/s` speed).
  - **JLC3DP:** Upload `orings.stl` and select **DLP Flexible Resin / Elastomer (Shore 70A–80A Black)**.

### 3. Ordering the 5-Board Breakaway Panel (`hardware/releases/v1/panel/`) at JLCPCB

1. **PCB Fabrication Settings (`panel/gerbers/`):**
   - **Base Material:** `FR-4` · **Layers:** `4` · **Dimensions:** `126.0 × 56.1 mm`
   - **PCB Thickness:** **`1.2 mm`** *(required — `probe_sleeve` grooves and snap-clips are sized for `1.2 mm`)*
   - **Surface Finish:** **`ENIG (Immersion Gold)`** *(essential for outdoor corrosion resistance on `probe`, `dome`, and `reflector` instead of HASL tin/lead)*
   - **Impedance Control:** `Yes` → **`JLC04121H-7628`** (`4-layer 1.2 mm` standard stackup)
   - **Copper Weight:** `1 oz` outer / `0.5 oz` inner · **Via Covering:** **`Tented`** (or **Epoxy Filled & Capped** for maximum moisture protection)
2. **Weatherproofing Exposed PCBs (`probe`, `dome`, `reflector`, `crown`):**
   - After assembly and testing, brush or spray **Acrylic / Silicone Conformal Coating (`MG Chemicals 419D` or `422C`)** across both sides of `probe`, `reflector`, `crown`, and the underside of `dome`.
   - **Keep-Out Windows (`F.SilkS` / `B.SilkS` boxes marked on the boards):** Keep conformal coating **OFF** the optical/gas/pressure apertures of `SHT40` (`probe U3`), `SCD41` (`probe U1`), `SGP41` (`probe U2`), `BMP581` (`main U4`), `LTR-390UV` (`dome U1`), `VEML7700` (`dome U2`), and theconnector pins (`J1..J10`).
   - Fill the `2.0 mm` grease/sealant groove (`y = 77.2..79.2 mm`) inside `probe_sleeve_front` + `probe_sleeve_back` with **neutral-cure silicone grease (`Dow Molykote 111`) or RTV silicone sealant** before closing the two sleeve halves around the `probe` PCB.

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
