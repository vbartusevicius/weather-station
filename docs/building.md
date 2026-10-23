# AeroMet V1 — Building: Ordering, Printing, Weatherproofing & Harnesses

Part details: [hardware.md](hardware.md). After assembly: first flash and bring-up in [CALIBRATION.md](../CALIBRATION.md).

## JLC3DP (3D printing) & JLCPCB (PCB / PCBA) ordering

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
  - `1×` **`ID 18 mm × CS 1.0 mm`** (`OD 20 mm`, EPDM/NBR 70A), squeezed flat on the PTFE dome flange in the default `crown` top, or `1×` **`ID 17 mm × CS 1.0 mm`** between the `JGS1` window and the lip of the backup `crown_window` top.
- **Option B — 3D-Printing `orings.stl`:**
  - **Local FDM:** Print `orings.stl` in **TPU 90A or 95A** (`0.12 mm` layer height, `100%` concentric infill, `3` wall loops, `25 mm/s` speed).
  - **JLC3DP:** Upload `orings.stl` and select **DLP Flexible Resin / Elastomer (Shore 70A–80A Black)**.

### 3. Ordering the Breakaway Panel (`hardware/releases/v1/panel/`) at JLCPCB

1. **PCB Fabrication Settings (`panel/gerbers/`):**
   - **Base Material:** `FR-4` · **Layers:** `4` · **Dimensions:** panel `140.6 × 63.2 mm` ("Panel by Customer"; edge rails + fiducials: added by JLCPCB)
   - **PCB Thickness:** **`1.6 mm`** *(required — `probe_sleeve` slots, snap-clips and standoffs are sized for `1.6 mm`)*
   - **Surface Finish:** **`ENIG (Immersion Gold)`** *(essential for outdoor corrosion resistance on `probe`, `dome`, and `reflector` instead of HASL tin/lead)*
   - **Impedance Control:** `Yes` → **`JLC04161H-7628`** (`4-layer 1.6 mm` standard stackup)
   - **Copper Weight:** `1 oz` outer / `0.5 oz` inner (main board `VIN` / heater power tracks are `0.4 mm`, sized for `0.5 oz` inner layers) · **Via Covering:** **`Tented`** (or **Epoxy Filled & Capped** for maximum moisture protection)
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
| Crown cable | main `J2` ↔ crown `J1`, JST SH 12-pin (`SHR-12V-S-B`) | `260 mm`, OD ≤ `3.2 mm` (30 AWG, wire insulation OD ≤ `0.6 mm`; pre-crimped 28 AWG SH leads bundle to ~4 mm and do not fit) | 1–2 `+12V_HEAT`, 3–4 `GND`, 5 `+5V`, 6 `GND`, 7 `SDA`, 8 `GND`, 9 `SCL`, 10 `CROWN_IRQ`, 11 `CROWN_NRST`, 12 `CROWN_BOOT0`; thread the bare cable through the NE strut before crimping the crown end (`SSH-003T-P0.2` contacts, SH-capable crimper) |
| Probe jumper | main `J3` ↔ probe `J1`, JST SH 10-pin (`SHR-10V-S-B`) | `60 mm` | 1–2 `+12V_HEAT`, 3–4 `HEAT_SW`, 5 `GND`, 6 `NTC_SENSE`, 7 `+3V3_PROBE`, 8 `CO2_HEAT_RTN`, 9 `SDA`, 10 `SCL` |
| Power jumper | main `J5` ↔ power `J1`, JST SH 10-pin (`SHR-10V-S-B`) | `28 mm` | 1–2 `VIN_PROT`, 3–4 `GND`, 5–6 `+5V`, 7–8 `+12V_HEAT`, 9 `GND`, 10 `+3V3_PROBE` |
| PM sensor | main `J4` JST ZH 6-pin (`ZHR-6`) → PMSA003I `2 × 5` `1.27 mm` IDC | `100 mm` | ZH 1 `+5V` → module 1+2, ZH 2 `GND` → 3+4, ZH 3 `SDA` → 9, ZH 4 `SCL` → 7; module 5, 6, 8, 10 open |
| Transducers ×4 | crown `J2..J5` JST SH 2-pin (`SHR-02V-S-B`) | pigtail | 1 = A, 2 = B (driven differentially, neither is ground; keep all four the same way round) |
| Piezo discs ×4 | crown `J6..J9` JST SH 2-pin | pigtail | 1 = disc (+, ceramic side), 2 = `GND` (brass) |

* **UV cover & hand-fit parts:** the default UV / light cover is a turned PTFE dome with a flat-faced `Ø 20.4 × 1.5 mm`
  flange: lathe version (`ptfe_dome_lathe.step` / `drawings/ptfe_dome_lathe.svg`, default, plain turning from
  `Ø 22–25 mm` rod) or CNC hemisphere version (`ptfe_dome_cnc.step` / `.svg`). No stock part: order it from PCBWay CNC
  turning in white PTFE, from an AliExpress custom-PTFE turning shop, or turn it yourself. Keep it white and matte: PTFE
  is a diffuser, not a window, and cannot be made clear by polishing. The backup is the `crown_window`
  top with a stock uncoated `JGS1` disc `Ø 20 × 2 mm` (AliExpress, see `drawings/jgs1_window.svg`; avoid AR-coated
  laser windows). `AS3935-BQFT`,
  `MA5532-AE`, `LTR-390UV-01` and `GUVB-C31SM` are not stocked by JLCPCB: buy them (Digi-Key / Mouser) and either
  consign them to JLCPCB or use its Global Sourcing so they are placed with the rest; see `REVIEW_CHECKLIST.md` B2 / B5
  for current stock, prices and the per-station budget.
* **Reflector heater:** put a silicone-free thermal gap filler (BOM) between the four heater resistors and the
  underside of the `wind_tunnel` deck before closing it.
