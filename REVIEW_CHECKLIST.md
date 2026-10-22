# AeroMet V1 — Manufacturing-Readiness Fix Checklist

Source: design review of 2026-10-20 (commit `22ec981`). Phase 1 fixes applied 2026-10-21 (independent electrical / mechanical / docs items).
Status of the design: **NOT ready to order.** Phase 2 (crown STM32 wind / rain / lightning, 12 V heater rail, RS-485 RX) is in source and the
release regenerates cleanly, but it is unprototyped: the bench / prototype checks listed per item are still required.
Commissioning / calibration steps for every built station: `CALIBRATION.md`.
Tick an item only when the fix is in the source (`hardware/circuits`, `hardware/circuits/variants/v1`, `mechanical/scripts`,
`station/src`), the release is regenerated (`python3 hardware/scripts/build.py`), and the listed acceptance check passes.

Severity: **B** = blocker (board/part will not work or cannot be assembled) · **H** = high (works badly / out of spec)
· **M** = medium (accuracy, robustness) · **D** = documentation/BOM.

---

## 1. Electronics (schematic)

- [x] **E1 (B) RS-485 receive path is impossible on one GPIO.** `blocks/mcu_xiao.py`, `firmware/main.cpp` (`uart_set_pin(UART_NUM_1, 18, 18, …)`).
  - **Status:** DONE in source (Phase 2): SP3485 DI / RO / DE+RE on GPIO16 / 17 / 19 (UART1 TX / RX / RTS, `UART_MODE_RS485_HALF_DUPLEX`), 390R fail-safe bias (≈ 0.25 V across a 120 R terminator), optional hand-fit 120 R terminator R26. OPEN: bench test with a USB-RS485 adapter.
  - TX push-pull fights RO (via 1 kΩ); in open-drain mode a received LOW enables DE → echo/oscillation.
  - Fix: separate TX and RX GPIOs (free a pin, e.g. move `US_SEL_A/B` to an I²C GPIO expander or use a TX-gated DE with RO on its own pin), or use a transceiver with true auto-direction.
  - Fail-safe bias 4k7/4k7 gives ≈ 41 mV with a 120 Ω terminator (needs ≥ 200 mV): use ≈ 560–680 Ω bias or document "no termination".
  - Accept: bench test with a USB-RS485 adapter (terminated) — PING/PONG + telemetry both ways at 115 200 bps.
- [x] **E2 (B) Probe heater thermostat LM393 outside common-mode range.** `blocks/probe_sensors.py` U4 on +3V3, inputs ≈ 2.27 V (CMR ≤ VCC−1.5 V = 1.8 V, ≤ 1.3 V over temp).
  - **Status:** DONE (Phase 1): U4 = LM2903 on +5V_HEAT (CMR 0..3.0 V over temp covers the 2.27 V node); hysteresis comment corrected to ~1.4 K.
  - Fix: swap NTC/resistor positions so the node sits ≈ 1.0 V (recompute setpoint + hysteresis), or power U4 from +5V_HEAT, or use a rail-to-rail comparator (e.g. TLV7031/TLV3201).
  - Accept: hand calc / SPICE shows both inputs within CMR from −40 °C to +60 °C; recompute hysteresis (current R7 gives ≈ 1.4 K, not 0.4 K).
- [x] **E3 (H) LM393DR2G is 0…+70 °C only** (crown U4, probe U4). Replace with LM2903 (−40…+125 °C) or automotive LM393.
  - **Status:** DONE (Phase 1): crown U4 and probe U4 are LM2903DR2G (C57474, -40..+125 C).
- [x] **E4 (B) 3.3 V logic into 5 V CMOS on the crown.** `blocks/crown_sensors.py`: SN74LVC2G04 U1 (VIH = 0.7·VCC = 3.5 V) and CD4052 select A/B (VIH 3.5 V) driven by 3.3 V GPIO over a 260 mm cable.
  - **Status:** DONE (Phase 2): no logic crosses the cable any more; the crown runs everything at 3.3 V (STM32, 74HC4052 at 3.3 V, UCC27524 TTL-threshold inputs).
  - Fix: 74AHCT1G04 / 74HCT-family driver, 74HCT4052 (or level shifter), or run the mux at 3.3 V.
- [x] **E5 (H) Ultrasonic TX AC-coupled below the mux VEE.** `US_TX_AC` swings ± around 0 V into CD4052 with VEE = GND → clamps into body diodes.
  - **Status:** DONE (Phase 2): TX no longer passes the mux; UCC27524 H-bridge per transducer with BAV99 T/R isolation; RX AC-coupled and biased at VREF inside the mux rails (ngspice: 0.25 mA peak mux-diode current).
  - Fix: bias the TX node at mid-supply or drive the mux with VEE negative / use a dedicated TX switch.
- [x] **E6 (H) Ultrasonic link budget unverified (likely 20+ dB short).** ≈ 3 Vpp through 1 kΩ into closed-face transducers; RX gain 529; threshold 230 mV.
  - **Status:** DONE in design (Phase 2): 21.8 Vpp H-bridge drive from +12V_HEAT (ngspice `hardware/sim/crown_us_link.py`), receive chain G 7.8 x PGA 2..32 sized for a 3..170 mV echo. OPEN: the echo amplitude is an estimate until measured on the real transducers + reflector.
  - Fix: higher-voltage TX (H-bridge/transformer, 20–100 Vpp per transducer datasheet), reconsider 1 kΩ RX load, re-derive gain.
  - Accept: breadboard with the actual transducers + printed reflector geometry shows ≥ 10× margin over threshold at −20 °C and in rain.
- [x] **E7 (H) Zero-cross + envelope comparators are wired-AND to one pin** → output is just "amp > VTH" (amplitude-dependent timing, cycle-slip = 25 µs ≈ 29 m/s).
  - **Status:** DONE (Phase 2): echoes digitised at 2 MSPS; 50 % envelope fit for the mean ToF + reciprocal cross-correlation (coarse lobe from the envelope, so no cycle slips at > 1 period); host tests on simulated echoes (`pio test -d crown -e native`).
  - Fix: envelope gate latches/enables a true zero-cross edge (separate outputs or logic), firmware measures several zero-crossings / phase.
- [x] **E8 (B) AS3935 antenna is a shielded power inductor.** `blocks/main_sensors.py` L2 SWPA6045S221MT (closed core, ±20 %, f0 = 495 kHz < 500 kHz).
  - **Status:** DONE (Phase 1): L2 = Coilcraft MA5532-AE (hand-fit) + 2x 470 pF C0G (AN04 953 pF target), 10k Q damping, In1/In2 plane keepout under the antenna. Verify tuning (TUN_CAP near 73 pF) on the prototype.
  - Fix: Coilcraft MA5532-AE (or datasheet-approved ferrite rod) + matching C0G tank cap; keep 500 kHz ± 3.5 % tunable.
- [x] **E9 (B) Both SCT2430 bucks switch at 500 kHz = AS3935 receive band**, ~10 mm away. `blocks/power_supply.py` RT = 200k.
  - **Status:** DONE (Phase 1): bucks at 743 kHz. Phase 2: move the AS3935 + MA5532 to the crown board on the STM32 SPI bus (away from the bucks / ESP32, fixes E10 too).
  - Fix: move fsw to 1.5–2.2 MHz (re-design L/C/compensation per datasheet) and/or relocate AS3935 away from the power board and ESP32.
- [x] **E10 (H) AS3935 over I²C is known-unreliable** (vendor/breakout notes). Consider SPI (needs pins — see E1) or verify on prototype.
  - **Status:** DONE (Phase 2): AS3935 on the crown STM32 SPI1 (SI = GND), automatic TUN_CAP tuning by counting LCO/16; AS3935 + MA5532 removed from main.
- [x] **E11 (B) Input surge protection cannot protect SCT2430 (40 V max).** SMAJ33A clamps at 53.3 V; PTC rated 33 V.
  - **Status:** DONE (Phase 1): 12-32 V kept; power board = 2x TPS54360 (60 V), SMAJ36A (58.1 V clamp), 2920L110/60V PTC, 100 V input MLCCs on main and power.
  - Fix: 60 V-class regulators (e.g. LMR36015/TPS54560 class) or lower the input spec (e.g. 12–24 V) with a matching TVS; consider SMBJ/SMCJ size for a roof cable.
- [x] **E12 (H) No surge/ESD protection on RS485_A/B** (outdoor cable, lightning region). Add SM712-type TVS + optional series PTC/resistors.
  - **Status:** DONE (Phase 1): D4 = PSM712 (C32677, basic part) at J1.
- [x] **E13 (H) Piezo rain AFE.** `blocks/crown_sensors.py` U5A: non-inverting input at 0 V on single supply, 100k‖18p (corner 88 kHz → not a charge amp), hail drives input below GND through only 1 kΩ.
  - **Status:** DONE (Phase 2): true charge amplifier (22 nF ‖ 4.7 MΩ at VREF), 1 k series + BAV99 clamp, digitised by STM32 ADC2 at 50 kSPS (`RainDetector`, host-tested). OPEN: rain calibration (S2).
  - Fix: bias at VREF, true charge amp (e.g. 1–10 nF ‖ 10 MΩ), ≥ 10 kΩ series + clamp diodes (BAV99) to rails; add a real peak detector if an envelope is wanted.
- [x] **E14 (M) I²C rise time.** 4k7 pull-ups on ≈ 200 pF (12 devices + 260 mm + 60 mm + 100 mm cables) → ≈ 0.8 µs > 300 ns (400 kHz).
  - **Status:** DONE (Phase 1): I2C pull-ups 2.2k (C4190, basic). Firmware still 400 kHz (PMSA003I device at 100 kHz).
  - Fix: 2.2 kΩ (check all sink currents) and/or 100 kHz bus; keep US_TX away from SDA/SCL in the 12-pin cable pin order.
- [x] **E15 (M) Reflector heater resistors at 93 % of rating** (4× 27 Ω 2512 1 W at 5 V) touching the plastic deck (0.08 mm). Use 2 W parts or more resistors; add firmware over-temperature cut-off on TH1.
  - **Status:** DONE (Phase 1): reflector resistors 27R 2512 2 W HP12 (C2791259) = 46 % of rating; firmware open-NTC SensorFault lockout (unit-tested).
- [ ] **E16 (M) 3V3 budget from the XIAO LDO** (≈ 550 mA peaks: ESP32-C6 TX + SCD41 + sensors; Seeed rates 600–700 mA). Measure on prototype or add a dedicated 3V3 regulator.

## 2. PCB layout / fab

- [x] **P1 (H) Power board is auto-routed with 0.3 mm tracks** for VIN / SW / VOUT, GND plane only on In1, no pours, few vias.
  - **Status:** DONE: TPS54360 stages per SLVSBB4G; SW nodes, VIN and VOUT are F.Cu pours (SW ~14 mm2, 3.5 mm residual track + 1 via to the bottom boot cap), J1 along the top edge, board 28.5 x 21.5 mm; 0 DRC / 0 unrouted.
  - Fix: hand layout per SCT2430 datasheet (tight hot loop, pours on F.Cu, thermal vias under EP, SW node minimal area).
- [ ] **P2 (M) Probe: reflector heater current (now ≈ 0.4 A at 12 V, was 0.74 A) routed past SHT40**, and the In1 GND plane connects SHT40 to J1 (which sits in the warm base bay).
  - Fix: route heater pair on the far edge, add copper keep-out / second slot between J1 and SHT40.
- [ ] **P3 (M) Panel has no fiducials, tooling holes or edge rails** (5-board panel 146.6 × 52.1 mm; the crown ships as its own board). Add rails + fiducials or order "added by JLCPCB" for both orders.
- [ ] **P4 (D) Double-sided assembly** on the panel (main, power, probe, dome, reflector bottom parts) and on the crown — budget for it.

## 3. Mechanical (verify on released STEP, not only on the parametric model)

- [x] **M17 (B) Main J1 pin labels were mirrored** ("+V GND A+ B-" over a 180-deg rotated terminal whose pins run B-, A+, GND, +V).
  - **Status:** DONE: per-pin labels at each pad; every connector now carries a silk label with the board/port it connects to (`connector_labels`).

- [x] **M1 (B) Bayonet O-rings have zero compression.** Gland 1.70 mm (up to 1.90 mm with lug play) vs 1.5 mm CS, on base↔pagoda, pagoda↔wind_tunnel, crown_base↔crown.
  - **Status:** DONE (Phase 1): 88 x 2.0 mm O-rings; glands 1.5 mm modelled / 1.7 mm at rest (15 %) / 1.23 mm pushed (38 %, 77 % fill); `test_bayonet_o_rings_are_squeezed_15_to_40_percent`.
  - Fix: design the gland for 15–25 % squeeze of a real O-ring (e.g. depth ≈ 1.15–1.25 mm with CS 1.5 at the locked position, or CS 2.0 mm ring); make lug shelf hold the compressed state.
  - Accept: new test computing gland height at the locked lug position vs purchasable O-ring CS.
- [x] **M2 (B) Printed M12 strut nuts: thread groove (r 6.22) cuts through the 12 mm flats** — nut section splits into pieces. Pockets Ø15.2/Ø16.4 cannot take real M12 nuts (18 AF).
  - **Status:** DONE (Phase 1): printed nuts 15 mm AF (1.28 mm wall at the flats), pockets r 9.0; README no longer claims metal-nut compatibility; `test_printed_strut_nuts_keep_a_wall_at_the_flats`.
  - Fix: either real ISO M12 nuts (pocket ≥ 18.6 AF/round 21) or a non-standard printed thread with ≥ 1.5 mm wall at the flats.
- [x] **M3 (B) ISO 4762 M6 socket heads (Ø10 × 6 mm) collide with all 4 reflector board corners** (2.4 mm³ each). Tests model a 2.4 mm head.
  - **Status:** DONE (Phase 1): DIN 7984 low-head M6x55 (head 10 x 3.5) in BOM/CAD, head wells Ø 10.6; test models real head + ISO 4032 nut + bolt tip vs pagoda, wind_tunnel, struts, reflector board, base.
  - Fix: DIN 7984 low head / button head, or chamfer reflector corners, or move bolts; head wells Ø10.0 → Ø10.6.
- [ ] **M4 (B) Crown cable path.** Ø4.2 mm cable vs 3.35 mm sleeve conduit (73 mm³ overlap); 4.38 mm gap under wind_tunnel deck with the NE M6 head on the radial path; 90° bend into the strut within ≈ 3 mm; plug must pass the M12 nut bore.
  - **Status:** PARTIAL: wind_tunnel deck raised 6 mm (10.4 mm gap above the pagoda deck, cable passes over the M6 heads), probe 56 mm; BOM cable OD <= 3.2 mm for the 3.35 mm sleeve conduit. The 12-pin crown cable is final (Phase 2 pin map). OPEN: swept-cable test T3 along the real route.
  - Fix: widen conduit ≥ 5 mm, dedicated radial groove in the wind_tunnel underside clear of bolts, larger bend space, or use a flat FFC/ribbon + inline connector.
  - Accept: test sweeping a Ø(cable + 0.5) tube along the full routed path from main J2 to crown J1 with zero intersection.
- [ ] **M5 (B) Transducer barrels Ø10.5 mm vs listed Ø14+ mm parts.** Pick ONE purchasable waterproof 40 kHz transceiver, then size barrel, aperture and retaining ring to its drawing.
  - **Status:** PARTIAL: Ø 14.6 mm barrels + printed Ø 10 mm sleeves (see Q5); BOM allows Ø 10 x 7 (default) or Ø 14 x 9 mm. OPEN: size against the purchased part's drawing.
- [x] **M6 (B) Captive printed-in-place parts.** base.stl: umbrella (0.40 mm gap) and reset button (0.30 mm) inside the base; struts.stl: nuts threaded on with 0.2 mm, rings 0.098 mm from the dome nut. MJF/SLS fuse < ~0.5 mm; FDM cannot print the floating umbrella.
  - **Status:** DONE (Phase 1): `enclosure/print/<part>_<nn>.stl` one body per file (base 3, pagoda 5, struts 21, orings 5, mast_shims 4); README/BOM point to it; `test_every_print_body_is_exported_separately_and_printable`.
  - Fix: export every separate body as its own STL (`umbrella.stl`, `reset_button.stl`, `strut_half_*.stl`, `m12_nut.stl`, …); use ≥ 0.5 mm clearance where parts must print captive.
- [x] **M7 (H) Mast clamp only clamps Ø33.5–34 mm** (split gap 0.5 mm), 19 mm pole engagement. Fix: ≥ 2–3 mm clamp travel + shims for 25–34 mm, ≥ 40 mm engagement, consider U-bolts / hose clamps.
  - **Status:** DONE: 50 mm pole engagement (socket from y = -31), 6x M6 bolts in 3 rows, 2.0 mm clamp travel, 4 shim sizes 50 mm tall (Ø 24-34 mm poles). Keep bolt torque moderate on PA12 (fender washers).
- [x] **M8 (H) Pagoda louvers ≈ 0.71 mm thick** (measured) on ≈ 24 mm spans. Fix: ≥ 1.5–2 mm normal thickness.
  - **Status:** DONE (Phase 1): louver blades 1.6 mm normal thickness (lower surface moved down).
- [x] **M9 (H) Radiation shield leaks:** 64/256 horizontal and 124/204 upward (−10°) rays reach the probe core. Fix: overlapping louvers that block lines of sight both from sky and from roof (classic double-louver Stevenson profile).
  - **Status:** DONE: labyrinth = upturned rim on each blade's inner edge (r 24.2–25.8, +7.0 mm) + down-skirt under the blade / top deck above (r 30.0–31.4, to +5.0 mm). `test_radiation_labyrinth_blocks_every_line_of_sight_to_the_probe`: 0 of 2 × 2 480 rays aimed at the shielded zone (y 76–115.5) (−75…+75°) reach the probe core (92 per section without it). Cost: the narrowest air passage drops from ≈ 9.4 mm to ≈ 3.3 mm (rim-to-skirt channel), so expect more self-heating error in still air — check against an aspirated reference (S3).
- [ ] **M10 (M) Reflector deck "thermal contact" relies on a 0.08 mm gap** with stack-up tolerance ±0.3 mm. Add a compliant thermal pad or define a hard stop.
- [x] **M11 (M) No U.FL antenna cradle in CAD** (claimed in README/BOM). Add it or remove the claim.
  - **Status:** DONE (Phase 1, docs): no cradle in V1; BOM/README say adhesive-mount the dipole on the inner wall.
- [ ] **M12 (M) Reset-button bore is an open hole** (no O-ring in CAD, README says waterproofed). Add gland or membrane.
  - **Status:** PARTIAL: README no longer claims a waterproof button; the bore is still open.
- [ ] **M13 (M) JGS1 dome stack-up.** Flange must be ≤ 1.2 mm thick (194.2 − 193.0); dome PCB clears the inner hemisphere by only ≈ 0.3 mm; no desiccant/vent → internal dew. Get a real dome drawing and add an ePTFE vent or desiccant.
  - **Status:** PARTIAL: vents DONE (4x 3-slit 1.2 mm downward grilles in the crown_base floor; the dome breathes through the retainer bore). OPEN: real dome drawing (flange <= 1.2 mm, see B3); optional ePTFE adhesive vent + desiccant bag.
- [ ] **M14 (M) Cable gland needs a hollow mast** (gland dome nut extends below the y = 19 mm pole stop). Document the required mast ID.
- [ ] **M15 (M) Probe sleeve final angle is set by thread run-out + O-ring squeeze** (uncontrolled). Add a key/index or a non-threaded keyed bayonet.
- [ ] **M16 (D) "No supports needed" FDM claim** is not true for base clips/hooks, barrels at 29°, umbrella. Re-check print orientation per part.

## 4. Firmware

- [x] **F1 (B)** UART single-pin config (see E1).
  - **Status:** DONE in source (Phase 2): separate RX pin, RS-485 half-duplex mode with RTS-driven DE (see E1).
- [x] **F2 (H)** Ultrasonic: 300 µs settle is short for ringing closed-face transducers + 48 mm cavity reverberation; first-edge capture has cycle-slip risk. Implement multi-zero-crossing / phase measurement, longer settle, and cross-check N→S vs S→N cycle index.
  - **Status:** DONE (Phase 2): crown firmware (`crown/`) with sampled-echo DSP (see E7); the station receives CRC-checked times of flight over I²C and rejects quads with any bad path.
- [x] **F3 (M)** RS-485 streams telemetry unconditionally every cycle → collides with a polling master. Respond only when polled or use a time slot.
  - **Status:** DONE in source: `task_rs485` answers `STATE?` with the latest frame (plus `PING`, `RESET_RAIN`, `HEAT_CHECK`) and never transmits unsolicited. OPEN: bench test with a polling master.
- [ ] **F4 (M)** I²C at 400 kHz for every device (see E14); PMSA003I 100 kHz max.
- [x] **F5 (M)** Heater over-temperature cut-off and NTC plausibility (open/short) before enabling PWM.
  - **Status:** DONE (Phase 1): `HeaterState::SensorFault` for an open reflector NTC (> 3270 mV), unit test added. 25 C over-temperature cut-off already existed.

## 4b. Phase 2 items still open (crown v2 / 12 V rail)

- [ ] **Q1 (H)** Crown firmware HAL glue (`crown/src/main.cpp`) compiles but has never run: bring up on the first board with SWD (TP1..TP5): clock, TIM8 burst + dead time on a scope, ADC/TIM6 sample timing, I²C peripheral, AS3935 SPI + tuning.
- [ ] **Q2 (H)** STM32G431 ROM I²C bootloader address: firmware tries 0x52 and 0x56; confirm against AN2606 / the first board (CALIBRATION.md step 2). `kCmdEnterBootloader` relies on a `.noinit` RAM flag the default linker script may zero; the BOOT0 line is the primary path. BOOT0 pin: RM0440 factory option bytes (`FLASH_OPTR` = 0xFFEF F8AA: nSWBOOT0 = 1, nBOOT0 = 1) take BOOT0 from PB8, so a factory-fresh chip honours CROWN_BOOT0 — confirm on the first board.
- [ ] **Q3 (H)** Echo amplitude: ngspice uses an estimated ~50 mVpp EMF; measure with the real transducers + printed reflector, then trim the preamp gain (R21) if needed. Wind-tunnel calibration (S1) still required.
- [ ] **Q4 (M)** AS3935 vs ultrasonic bursts: log disturber / noise events with bursts on and off on the prototype (actions `crown_bursts_off` / `crown_bursts_on`, counters in `/api/config` → `crown.disturbers`; CALIBRATION.md step 9).
- [x] **Q5 (M)** Transducer adapters (M5): barrels now Ø 14.6 mm (Ø 12 mm aperture, 8.8 mm shroud clipped below the crown PCB), crown notches 16 mm wide; printed `transducer_adapters` = 4 Ø 10 mm sleeves (installed by default) + 4 rings for Ø 14 mm cans. Tested: both can sizes, rings and apertures clear barrels, sleeves and the crown PCB. OPEN: confirm against the real transducer drawings (can height, pigtail exit) before printing.
- [ ] **Q6 (M)** +12V_HEAT pass-through below ~13.5 V input: verify the TPS54360 near-100 % duty behaviour and heater power on a 12 V supply.
- [x] **Q7 (H) UCC27524 enable pull-up.** ENA/ENB are pulled up to VDD = 12 V through 200 kΩ (SLUSFA9 `RENx`, typ.); the 10k pull-down left an undriven EN at 0.57 V against a 0.8 V minimum low threshold.
  - **Status:** DONE: R13–R16 = 4.7k → 0.28 V undriven (drivers off in reset / bootloader); the STM32 drives 3.3 V (> 2.3 V max high threshold) and sinks ≤ 60 µA from the internal pull-up when low.
- [x] **Q8 (H) Rain wetness flag stuck on.** Crown impacts carried a fixed 206 mV "baseline" ≥ the 140 mV wet threshold.
  - **Status:** DONE: crown impacts carry baseline 0 (no capacitive channel); `HapticRainProcessor` then reports wet while a rain event runs and keeps rejecting only long (> 850 µs) pulses during mast shake. New `rain_impact_scale` maps charge-amp mV to the drop / hail thresholds (CALIBRATION.md step 6). Unit-tested. OPEN: the thresholds themselves need the field calibration.
- [x] **Q9 (H) Wind wrong until calibrated.** The crown times the 50 % envelope point (~58 µs after the first arrival); `us_delay_us` defaulted to 0 → speed of sound and wind magnitude off by ~35 %.
  - **Status:** DONE: default 58 µs, plus the `wind_calibrate` action (60 s still air, delay + zero-wind offsets from the SHT40 temperature, refuses moving air; unit-tested) and `wind_speed_gain` for the reference comparison (CALIBRATION.md steps 4–5).
- [x] **Q10 (M) RS-485 multi-drop.** Every station answered `STATE?`; `PONG` carried a fake `*0000` checksum.
  - **Status:** DONE: `rs485_address` (0 = single station, 1–247 = `<addr>:CMD`, talker `$AM<addr>`), real CRC-16/MODBUS on `PONG`; unit-tested. OPEN: bench test (CALIBRATION.md step 3).
- [x] **Q11 (M) Crown status read unprotected.** The 2-byte status shared the 260 mm cable with the 50 mA drive bursts without a CRC.
  - **Status:** DONE: protocol v2, `kStatus` = status + CRC-8, the station drops bad reads (crown + station tests). OPEN: measure crosstalk on the first board (scope on SDA/SCL during bursts).
- [ ] **Q12 (H) Crown production programming.** Every crown STM32 needs one SWD flash (TP1–TP5) before the station can update it over I²C. Define the fixture (pogo pins on the 5 pads), the released image (`crown/.pio/build/crown/firmware.bin`, version in `kVersion`) and the station-side version check / auto-update (not implemented).
- [ ] **Q13 (M) Transducer drive rating.** ~22 Vpp differential bursts (8 cycles, 16 / s): confirm the chosen transducer's maximum drive voltage from its datasheet before ordering (EU10AIF40H07T/R or the Ø 14 mm alternative).
- [x] **Q14 (M) Power-board hot loop after the Phase 2 re-route.** Re-measure SW-node copper on the final released `power.kicad_pcb`.
  - **Status:** DONE (measured on the release): LOGIC_SW / HEAT_SW pours 13.7 mm² each on F.Cu, 3.8 / 3.4 mm of residual 0.4 mm track, 1 via each (boot cap); VIN_PROT 22.4 mm², +5V 17.6 mm², +12V_HEAT 18.2 mm² pours — same as the Phase 1 hand layout.
- [ ] **Q15 (D) Crown is a separate PCBA order.** 60 × 60 mm non-rectangular 4-layer board, double-sided assembly, 0.5 mm-pitch LQFP-64 on the bottom: quote it separately (see B5).
- [x] **Q16 (M) Config migration.** New `StationConfig` fields used to reset the whole NVS config (Wi-Fi, location) on every firmware update (`len == sizeof`).
  - **Status:** DONE: fields are only appended; a shorter stored blob fills the prefix and new fields keep their defaults.

## 5. Sensor accuracy (needs prototype + calibration, not just a fix)

- [ ] **S1** Wind: wind-tunnel calibration table per direction (strut wakes along 45°/135°/225°/315° cross the centre).
- [ ] **S2** Rain: calibrate drop count / energy vs a tipping-bucket gauge; remove "0.01 mm" claim until done.
- [ ] **S3** T/RH: compare with a reference aspirated sensor in sun / low wind; quantify base-bay and roof-radiation bias.
- [ ] **S4** UV/light: TSL25911 saturates ≈ 88 klx and is rated −30…+70 °C under a sealed dome; derive UVI/irradiance calibration.
- [ ] **S5** Temperature ratings below −10 °C: SGP41, PMSA003I, SCD41 (heater covers SCD41 only), JST-SH (−25 °C).

## 6. Chemistry / assembly

- [x] **C1 (B) README tells to use Molykote 111 / RTV silicone in the probe sleeve groove next to SGP41.** Siloxanes poison MOX sensors. Replace with a non-silicone sealant (e.g. PU / PTFE grease) and align README with `assembly_parts.yaml`.
  - **Status:** DONE (Phase 1): README now forbids silicone anywhere in the station; PTFE grease or PU resin in the sleeve groove; non-silicone coatings.
- [x] **C2 (D)** Conformal-coat keep-out list names `VEML7700` (part is TSL25911).
  - **Status:** DONE (Phase 1).

## 7. BOM / sourcing

- [x] **B1 (B)** Crown cable listed as 10-core + SHR-10V; main J2 / crown J1 are **12-pin** (BM12B). Fix BOM: 12-core, SHR-12V, pin-1↔pin-1, real routed length (spec says 160 mm, BOM 260 mm).
  - **Status:** DONE (Phase 1): BOM = 12-conductor, OD <= 3.2 mm, SHR-12V both ends pin 1 <-> pin 1, 260 mm (+ note about threading through the strut nut).
- [ ] **B2 (H)** LTR-390UV-01 and AS3935 show 0 stock at JLC → order from Digi-Key/Mouser and hand-fit (the AS3935 QFN-16 now sits on the crown, the MA5532-AE antenna too); plan hot-air rework for 2×2 mm / QFN-16.
- [ ] **B3 (H)** JGS1 Ø18 dome with Ø20.5 flange is custom — get a quote + drawing before freezing crown CAD.
- [ ] **B4 (M)** PMSA003I needs a custom 6-pin ZH → 2×5 1.27 mm harness; transducers need JST-SH pigtails. Define both with drawings.
- [ ] **B5 (M)** Budget (re-quote at order time): two JLC PCBA orders — panel (61 BOM lines, 42 extended) and crown (37 lines, 23 extended: STM32G431RBT6 ≈ $5.77, 4× UCC27524 ≈ $0.25, 74HC4052 ≈ $0.17, TLV9062 ≈ $0.12, 8 MHz crystal ≈ $0.13) → ≈ 65 extended-part fees per batch; + hand parts (XIAO, PMSA003I ≈ $35–45, AS3935, MA5532-AE, LTR-390, GUVB, transducers, dome, prints) + ST-Link for the one-time crown flash.

## 8. Documentation consistency (single source = `hardware/product/spec.yaml`)

- [x] **D1** AGENTS.md: main 41×56 → 32×52; BMP581 on main → BMP585 on probe; SCT2400 → 2× SCT2430 on `power`; reflector 4×100 Ω → 4×27 Ω; 5 boards / 126×56.1 → 6 boards / 124.5×55.7.
  - **Status:** DONE (rewritten for Phase 2: 12 V heater rail, crown v2, 5-board panel + separate crown). Re-check at each release.
- [ ] **D2** README (PARTIAL: Phase 2 hardware, panel and ordering text updated; re-check the mechanical claims below): bolts r = 23 vs 20.5; strut bore Ø6.4 vs Ø6.0; O-ring count 6 vs 5 in `orings.stl`; antenna cradle; waterproof button; "M12 standard nut compatible"; "Ø25–34 mm poles"; "5-Board Breakaway Panel".
- [ ] **D3** `assembly_parts.yaml`: dome retainer M22×1.0 vs M22×1.5; mast bolts M6×25 vs M6×35; transducer options.
- [ ] **D4** `spec.yaml` stale comments/fields: M3/M4 fasteners, `terminals.power_in` (KF142R 2-pole), crown cable 160 mm.
- [ ] **D5** Circuit docstrings: "SHT45" vs SHT40-AD1F, "LIS2MDL" vs MMC5603NJ, "10-pin crown", "41×56 crown/reflector".

## 9. Test-suite gaps to close (so these cannot regress)

- [x] **T1** Fasteners modelled at real ISO sizes (M6 head Ø10×6, nut 10 AF×5.2, M12 nut 18 AF) against all parts and boards.
  - **Status:** DONE (Phase 1): DIN 7984 head + ISO 4032 nut + bolt tip in `test_boards_clear_enclosure_and_threads_truly_mesh`; printed M12 nut walls tested separately.
- [x] **T2** O-ring gland squeeze check at the locked bayonet position for every seal.
  - **Status:** DONE (Phase 1): `test_bayonet_o_rings_are_squeezed_15_to_40_percent`.
- [ ] **T3** Swept-cable clearance test for every harness (diameter + bend radius).
- [x] **T4** Released STEP/STL body count + min inter-body gap ≥ 0.5 mm (or split into separate files).
  - **Status:** DONE: print plates with every body ≥ 6 mm apart, `test_every_print_body_is_exported_separately_and_printable`.
- [ ] **T5** Min wall thickness check (louvers, nuts, shelves) ≥ process minimum.
- [ ] **T6** Electrical rule checks: comparator CMR, logic VIH/VIL across supply domains, part temperature ratings vs −40…+60 °C.
