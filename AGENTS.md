# Agent instructions: AeroMet V1 roof weather station

AeroMet V1 is a solid-state rooftop weather and air-quality station: six KiCad boards (SKiDL circuits), a 13-part
CadQuery enclosure, ESP32-C6 station firmware with an embedded Web UI, and STM32G431 crown firmware. Everything is
generated and verified by scripts, so change the source, never the outputs.

## Where to look first

- [`docs/development.md`](docs/development.md): design summary, pipeline internals, firmware architecture, every
  command and the CI. Read the section for the area you change before you change it.
- [`hardware/product/spec.yaml`](hardware/product/spec.yaml): the product spec (boards, variants, envelope heights).
  It is the source of truth; docs follow it.
- [`docs/hardware.md`](docs/hardware.md) (sensor / part / board reference), [`docs/building.md`](docs/building.md)
  (ordering, printing, harnesses), [`CALIBRATION.md`](CALIBRATION.md) (bring-up and calibration),
  [`REVIEW_CHECKLIST.md`](REVIEW_CHECKLIST.md) (open design items and acceptance checks). These ship in the hardware
  release zip, so keep them written for builders.
- `README.md` stays short: overview, small galleries, collapsed extra renders and links into `docs/`.

## Verify before you finish

Run the checks for what you changed. The hardware tools (KiCad, SKiDL, fastroute) are only in the `eda` container and
`node` is not installed locally.

| Changed | Run |
| :--- | :--- |
| Any Python or C++ | `docker compose --profile lint run --rm lint check .` |
| `station/src/` (not `firmware/`) | `docker compose --profile lint run --rm -T cpp-lint "pio test -d station -e native"` |
| `station/src/firmware/` (drivers) | also `docker compose --profile lint run --rm -T cpp-lint "cd station && pio run -e dev_c6"` (native tests do not compile it) |
| `crown/` | `docker compose --profile lint run --rm -T cpp-lint "pio test -d crown -e native"` and `"pio run -d crown -e crown"` |
| `station/data/` (Web UI) | `python3 station/scripts/pack_webui.py`, then the smoke test against the simulator (see docs/development.md) |
| Circuits, layouts, enclosure, release | `python3 hardware/scripts/build.py` with the matching flags (`--eda`, `--cad`, `--release`, `--test`, `--boards`, `--fresh`); the full run takes 1–2 h |

## Rules

### Generated files
- Never edit generated files by hand: `station/src/firmware/WebAssets.hpp` (`pack_webui.py`),
  `station/src/firmware/CrownImage.hpp` (`pack_crown_image.py`), `hardware/releases/v1/` (`release.py`),
  `hardware/product/jlc_reference.json` (`jlc_parts.py snapshot`).
- Never commit `station/build/firmware/`; CI builds and publishes the firmware.

### Firmware
- Never hard-code the firmware version. `scripts/gen_version.py` derives it from the commit time.
- `StationConfig` (NVS blob) and the RS-485 `$AM` frame are append-only: never reorder or remove fields.
- After changing `crown/`, bump `kVersion` in `crown/src/main.cpp`, build it and run
  `python3 station/scripts/pack_crown_image.py`. CI fails if the embedded crown image is stale.
- Stale or missing sensors publish `null` ("—"), never a default value. The Web UI only displays values; it computes
  none, and `index.html` holds no readings.
- Keep flash at 4 MB (`partitions.csv`, `sdkconfig.defaults`, `platformio.ini`). A changed `sdkconfig.defaults` only
  reaches an existing build after you delete `.pio/build/<env>/sdkconfig`.
- Keep every I²C device at 100 kHz.
- In `station/data/app.js`, report failures through `logErr`, never `.catch(() => {})`. Do not declare a global `$`
  (it breaks the JXA test harness). Code that runs at load goes after the definitions it uses.

### Hardware
- Passives are 0805 or larger. Use a leaded IC package where one exists, otherwise edge pads (hand-soldering policy
  in docs/development.md).
- Every new part must pass the JLCPCB pin-for-pin check in the release. After part changes, refresh the snapshot with
  `python3 hardware/scripts/jlc_parts.py snapshot`.
- Create explicit-reference SKiDL parts last: removing or adding auto-referenced parts renumbers R / C references.
- Placement or routing changes in `layout_<board>.yaml` only take effect with
  `python3 hardware/scripts/build.py --eda --fresh --boards <board>`.
- Never narrow a design rule to make the router pass without asking the user (e.g. adding a part to
  `fine_pitch_power_pads`, which lets power tracks neck to 0.15 mm in its courtyard).
- Routing passes only when `kicad-cli pcb drc` of the imported board has no errors beyond its unrouted baseline and
  board connectivity reports zero unrouted connections (`layout.route`). Don't use the router's exit code or DRC's
  capped unconnected list as the result.

### Mechanical
- Check geometry against real ISO fastener, O-ring and cable sizes, not the modelled placeholders.
- Keep the PA12 powder-print rules (`mechanical/tests/test_print_rules.py`): register new snap beams in
  `ef.snap_beams()`.
- Hard-coded design-frame y values above the base take `+ ef.RAISE` (pagoda, probe sleeve) or `+ ef.UPPER`
  (wind tunnel and above).

### Process
- Before an order, run the `/manufacturing-review` skill (`.claude/skills/manufacturing-review/`) for an
  independent review.
- Record new durable project knowledge in `docs/development.md` (or the builder docs), not here. Add to this file only
  instructions an agent must follow.
