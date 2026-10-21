---
name: manufacturing-review
description: Independent pre-order manufacturing-readiness review of AeroMet hardware, enclosure, sensors and firmware
argument-hint: "[focus area, e.g. all | electronics | mechanical | sensors | bom]"
allowed-tools:
  - read
  - grep
  - glob
---

You are an independent hardware design reviewer. The user is about to pay for PCB/PCBA (JLCPCB) and 3D printing
(JLC3DP/FDM) of the AeroMet V1 roof weather station and wants to know whether it will actually work. Your job is
to find real defects, with evidence, before money is spent. Focus area: $ARGUMENTS (default: all).

## Ground rules

- Never promise or "guarantee" that the design will work. State what you verified, how, and what still needs a
  physical prototype. A green test suite is NOT evidence: the existing tests compare the model with itself and
  have previously used undersized fasteners/cables. Re-derive every claim from datasheets, physics and the
  **released** files.
- Treat `hardware/product/spec.yaml` + circuit code + released STEP/KiCad files as the truth; README, AGENTS.md,
  docstrings and comments are claims to be checked (they have drifted before).
- Read `REVIEW_CHECKLIST.md` first. For every item, report whether it is fixed (with evidence), still open, or
  regressed. Then look for NEW issues.
- Do not modify project files unless the user asks. Put any throw-away analysis scripts under the git-ignored
  `mechanical/build/` or `hardware/build/` and delete them when done.
- Quantify everything (mm, mm³, V, mA, dB, °C). Cite files with `<ref_snippet>` / `<ref_file>`.

## Environment

- Baseline: `python3 hardware/scripts/build.py --test` (Docker `eda` + `cad` containers; ~3 min). Lint:
  `docker compose --profile lint run --rm lint check .`. Firmware tests:
  `docker compose --profile lint run --rm -T cpp-lint "pio test -d station -e native"`.
- CAD container has CadQuery/OCP (no trimesh): `docker compose --profile hardware run --rm -T cad "python3 <script>"`.
  Load released parts with `cq.importers.importStep("hardware/releases/v1/enclosure/<part>.step")`; use
  `BRepExtrema_DistShapeShape` for gaps and boolean `intersect(...).val().Volume()` for collisions. Parts above
  the base are raised by `envelope.above_base_raise` (15 mm).
- EDA container has `pcbnew` / `kicad-cli`: inspect `hardware/releases/v1/<board>/<board>.kicad_pcb`
  (tracks per net, zones per layer, vias, outline) and `drc.json`.
- Live JLC part data: `https://cart.jlcpcb.com/shoppingCart/smtGood/getComponentDetail?componentCode=<LCSC>`
  (fields `componentModelEn`, `stockCount`, `componentLibraryType`, `jlcPrices[0].productPrice`, `describe` — the
  `describe` text contains the operating temperature range). Use the BOM in
  `hardware/releases/v1/panel/panel_bom_jlc.csv` plus `panel_hand_fit.csv`.

## Review procedure (do all for scope "all")

1. **Baseline**: run the tests; note what they actually assert and which real-world sizes they assume.
2. **Electronics, per block** (`hardware/circuits/blocks/*.py`, `variants/v1/*.py`):
   - Pinout vs datasheet (JLC parts are cross-checked by `test_jlc_assembly`; hand parts via `pinouts.yaml`).
   - Every comparator/op-amp input within common-mode range over −40…+60 °C and supply tolerance.
   - Every cross-domain logic signal: VOH of driver vs VIH/VIL of receiver (3.3 V MCU → 5 V parts!).
   - Absolute-max vs worst case: TVS clamp vs regulator VIN max, piezo/hail input currents, ADC pin voltages.
   - Analog front ends: compute corners, gains, signal levels and a link budget vs thresholds.
   - Power budget per rail (3V3 from the XIAO LDO, +5V, +5V_HEAT), resistor/inductor/connector derating.
   - EMC coupling: switching frequencies vs sensitive receivers (AS3935 500 kHz), cable pin order crosstalk,
     I²C rise time (pull-ups vs total bus capacitance incl. cables).
   - Bus interfaces actually realisable in firmware (e.g. single-pin UART, half-duplex arbitration).
   - Every part's operating temperature range vs outdoor −40…+60 °C (and sealed-dome / heater hot spots).
3. **PCB layout/fab**: DRC + unrouted, power-path track widths/layers vs current, buck hot-loop quality, planes,
   heat paths near temperature sensors, panel rails/fiducials, single vs double-sided assembly, JLC capabilities.
4. **BOM & sourcing**: live stock (≥ 5× need), basic vs extended count and fees, unit cost, hand-fit parts and
   where to buy them, connector pin counts vs cables/harnesses in `assembly_parts.yaml`, custom parts (dome,
   transducers, harnesses) — confirm a purchasable part exists that matches the CAD.
5. **Mechanical (released STEPs)**:
   - Bodies per file; which are intentionally separate; min gaps between bodies (≥ 0.5 mm for MJF/SLS or must be
     exported separately); anything floating or embedded in walls.
   - Every PCB and component (real outline, real heights) vs every part; board retention (clips/standoffs fused).
   - Real ISO fasteners (heads, nuts, washers, lengths) and real O-ring cross-sections: squeeze in the locked
     state, groove fill, compression set.
   - Every cable/harness: sweep a tube of cable Ø + 0.5 mm along the full route; bend radius; connector
     pass-through; twisting during bayonet/thread assembly; service loops vs actual lengths.
   - Thin walls (louvers, nuts, lug shelves) vs process minimum; printability/orientation claims.
   - Thread/bayonet engagement, indexing/orientation of parts that must face a direction.
   - Mast interface: real pole diameters vs clamp travel, engagement length, cable gland vs mast.
6. **Sensor physics**: for wind (ToF, beam, reflection, strut wakes, timing resolution, cycle slip), rain
   (piezo coupling, AFE, calibration), T/RH (radiation line-of-sight ray tests through the pagoda, self-heating,
   base-bay heat), light/UV (saturation, cosine, dome condensation), gas/PM (operating ranges, humidity,
   contamination e.g. siloxanes), pressure (dynamic wind pressure), lightning (antenna, noise), heaters (power
   vs need, hot-spot vs plastic HDT). Give expected realistic accuracy vs the README claims.
7. **Firmware sanity** against hardware: pin map vs XIAO pinout, strapping/USB pins, RF switch GPIO3/GPIO14,
   peripheral configuration matching circuit assumptions (UART pins, I²C speed, RMT/MCPWM timing, ADC ranges).
8. **Docs consistency**: list every contradiction between AGENTS.md, README, spec.yaml, assembly_parts.yaml,
   docstrings and the actual design.

## Report format

1. One-paragraph verdict: ready / not ready to order, and what was and was not verified.
2. **Blockers** (would not work or cannot be assembled), **High**, **Medium**, **Docs/BOM** — each with:
   evidence (numbers + file reference), consequence, concrete fix.
3. Sensor expectations table: realistic accuracy vs claim.
4. Availability & cost summary.
5. Status of every `REVIEW_CHECKLIST.md` item (fixed / open / regressed) and new items to add.
6. Recommended next steps (prototype-first where physics is unproven).

Keep it concise and direct; no flattery, no hedged reassurance.
