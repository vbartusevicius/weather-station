"""Test 2 — Pure Copper-Extracted End-to-End `ngspice` Simulation Gate (`libngspice` via `ctypes`).

Strictly ZERO imports from `hardware/circuits/`:
  - Extracts SPICE circuits via Union-Find strictly over physical copper tracks, vias, filled zones and pads from
    `hardware/releases/v1/<board>/<board>.kicad_pcb` (`GetUnconnectedCount(False) == 0`), reading all component values
    and MPNs from exported `<board>_bom.csv` and stitching board-to-board headers (`crown J10 <-> dome J1`,
    `probe J2 <-> reflector J1`) strictly by 3D `(X, Z)` mating pad coordinates.
  - Exercises `.op`, `.ac`, `.noise` (`setplot noise1`), and `.tran` via `libngspice.so.0` (`ctypes`) across all four
    station subsystems:
      1. `main` + `power` input protection, reverse-polarity & USB back-feed blocking, 80 V surge clamping, UVLO trip,
         regulated `+5V` / `+12V_HEAT` / `+3V3_PROBE` / `+3V3` rails, LDO dropout headroom, and buck `.ac` loop phase margin.
      2. `main` + `power` + `probe` + `reflector` 3D-stitched heaters, ADS1115 shunt monitors, NTC divider, and
         `-7.6 C` LM2903 closed-loop thermostat trip & hysteresis window.
      3. `crown` ultrasonic H-bridge 40 kHz burst & BAV99 T/R isolation, 74HC4052 + TLV9062 RX `.ac` band-pass &
         `.noise` (`setplot noise1`), piezo rain charge amp `.ac` + hail transient clamping, and AS3935 500 kHz LC tank.
      4. `crown` + `dome` 3D-stitched zero-bias UV-B / UV-A transimpedance amplifiers (`.op`, `.ac`, `.noise`, `.tran`).
"""

from __future__ import annotations

import ast
import cmath
import math
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/sim"))
import copper_spice as cs


def test_zero_circuit_generator_imports():
    """Guard: neither this test nor `copper_spice.py` may import `skidl` or any `hardware/circuits/` module."""
    for path in (Path(__file__), Path(cs.__file__)):
        tree = ast.parse(path.read_text())
        imported = [
            alias.name for node in ast.walk(tree)
            if isinstance(node, ast.Import) for alias in node.names
        ] + [
            node.module or "" for node in ast.walk(tree)
            if isinstance(node, ast.ImportFrom)
        ]
        forbidden = [m for m in imported if m in ("skidl", "common", "product_spec", "layout", "gen_circuits")
                     or m.startswith(("variants", "blocks"))]
        assert not forbidden, f"{path.name} imported generator modules: {forbidden}"


# =============================================================================
# 1. `main` + `power`: Input Protection, Regulator Tree, UVLO, Surge & Loop Stability
# =============================================================================

def test_main_and_power_rails_reverse_polarity_uvlo_and_backfeed():
    ng = cs.LibNgSpice.get()

    # A. Nominal +24 V roof input (`main J1.1` = VIN_RAW, `main J1.2` = GND)
    deck = cs.build_extracted_spice_deck(("main", "power"))
    vin_raw = deck.node("main", "J1", "1")
    vin_prot = deck.node("main", "C1", "1")
    vin_adc = deck.node("main", "U3", "1")
    v5 = deck.node("power", "C5", "1")
    v12 = deck.node("power", "C14", "1")
    v3_probe = deck.node("power", "C20", "1")
    v5_xiao = deck.node("main", "U3", "14")
    v3_main = deck.node("main", "U3", "12")
    mon_12v = deck.node("main", "U4", "6")       # AIN2: +12V_HEAT / 4
    mon_5v = deck.node("main", "U4", "7")        # AIN3: +5V / 2
    rs485_a = deck.node("main", "J1", "3")
    rs485_b = deck.node("main", "J1", "4")

    # Add 120 Ohm bus termination across J1.3 / J1.4 to verify worst-case terminated RS-485 fail-safe idle margin!
    net_nom = "\n".join(deck.lines + [
        f"V_ROOF {vin_raw} 0 DC 24.0",
        f"R_BUS_TERM {rs485_a} {rs485_b} 120",
    ])
    op = ng.run_op(net_nom, [vin_prot, vin_adc, v5, v12, v3_probe, v5_xiao, v3_main, mon_12v, mon_5v, rs485_a, rs485_b])

    assert 23.2 <= op[vin_prot] <= 23.9, f"VIN_PROT after PTC + SS16 unexpected: {op[vin_prot]:.3f} V"
    assert abs(op[vin_adc] - op[vin_prot] / 11.0) < 0.03, f"VIN_ADC 11:1 divider mismatch: {op[vin_adc]:.3f} V"
    assert 4.90 <= op[v5] <= 5.10, f"+5V buck out of spec: {op[v5]:.3f} V"
    assert 11.80 <= op[v12] <= 12.20, f"+12V_HEAT buck out of spec: {op[v12]:.3f} V"
    assert 3.25 <= op[v3_probe] <= 3.35, f"+3V3_PROBE LDO out of spec: {op[v3_probe]:.3f} V"
    assert op[v5] - op[v3_probe] >= 1.50, "AP2112K-3.3 LDO dropout headroom < 1.5 V"
    assert 4.45 <= op[v5_xiao] <= 4.85, f"+5V_XIAO after SS14 unexpected: {op[v5_xiao]:.3f} V"
    assert 3.25 <= op[v3_main] <= 3.35, f"XIAO +3V3 rail out of spec: {op[v3_main]:.3f} V"
    assert op[v5_xiao] - op[v3_main] >= 1.10, "XIAO 3.3V LDO headroom < 1.1 V"
    assert abs(op[mon_12v] - 0.25 * op[v12]) < 0.05, f"ADS1115 +12V_HEAT/4 divider wrong: {op[mon_12v]:.3f} V"
    assert abs(op[mon_5v] - 0.50 * op[v5]) < 0.05, f"ADS1115 +5V/2 divider wrong: {op[mon_5v]:.3f} V"
    assert (op[rs485_a] - op[rs485_b]) >= 0.20, (
        f"RS-485 terminated idle fail-safe bias {(op[rs485_a] - op[rs485_b]) * 1e3:.1f} mV < 200 mV minimum"
    )

    # B. Reverse-polarity wiring fault (-24 V on roof terminal J1.1): SS16 D2 blocks -24 V (unidirectional TVS D1
    # forward-clamps leakage at ~-0.64 V) and both +5V / +12V_HEAT rails stay at 0 V!
    deck_rev = cs.build_extracted_spice_deck(("main", "power"), extra_lines=[f"V_ROOF {vin_raw} 0 DC -24.0"])
    op_rev = ng.run_op("\n".join(deck_rev.lines), [vin_prot, v5, v12])
    assert -0.95 <= op_rev[vin_prot] <= 0.05, f"reverse polarity leaked onto VIN_PROT: {op_rev[vin_prot]:.3f} V"
    assert abs(op_rev[v5]) < 0.05 and abs(op_rev[v12]) < 0.05

    # C. USB-C bench flashing back-feed (+5V on XIAO pin 14 with roof supply unpowered): SS14 D5 must block +5V rail!
    deck_usb = cs.build_extracted_spice_deck(("main", "power"), extra_lines=[
        f"R_ROOF_OPEN {vin_raw} 0 100k",
        f"V_USB {v5_xiao} 0 DC 5.0",
    ])
    op_usb = ng.run_op("\n".join(deck_usb.lines), [v5, v3_main])
    assert op_usb[v5] < 0.15, f"USB +5V_XIAO back-fed into main +5V rail ({op_usb[v5]:.3f} V) — D5 reversed/shorted?"
    assert 3.25 <= op_usb[v3_main] <= 3.35

    # D. TPS54360 UVLO lockout below 6.25 V (at 5.5 V input) vs 12 V minimum input pass-through
    deck_uvlo = cs.build_extracted_spice_deck(("main", "power"), extra_lines=[f"V_ROOF {vin_raw} 0 DC 5.5"])
    op_uvlo = ng.run_op("\n".join(deck_uvlo.lines), [v5, v12])
    assert op_uvlo[v5] < 0.15 and op_uvlo[v12] < 0.15, f"bucks failed to stay off under UVLO at 5.5 V: {op_uvlo}"

    deck_12v = cs.build_extracted_spice_deck(("main", "power"), extra_lines=[f"V_ROOF {vin_raw} 0 DC 12.0"])
    op_12v = ng.run_op("\n".join(deck_12v.lines), [v5, v12, v3_probe])
    assert 4.90 <= op_12v[v5] <= 5.10
    assert 10.2 <= op_12v[v12] <= 11.4, f"+12V_HEAT near-100% duty pass-through at 12 V input unexpected: {op_12v[v12]:.2f} V"
    assert 3.25 <= op_12v[v3_probe] <= 3.35


def test_main_transient_surge_clamping_below_60v_buck_limit():
    """Inject a 100 V IEC 61000-4-5 2 Ohm surge pulse into `main J1.1` and verify SMAJ36A clamps `VIN_PROT`
    strictly below the 60 V TPS54360 maximum rating."""
    ng = cs.LibNgSpice.get()
    deck = cs.build_extracted_spice_deck(("main", "power"))
    vin_raw = deck.node("main", "J1", "1")
    vin_prot = deck.node("main", "C1", "1")
    v5 = deck.node("power", "C5", "1")

    net = "\n".join(deck.lines + [
        "V_SURGE surge_src 0 PULSE(24.0 100.0 5u 8u 20u 25u 200u)",
        f"R_SURGE surge_src {vin_raw} 2.0",
    ])
    _t, vecs = ng.run_tran(net, [vin_prot, v5], "0.5u", "90u")
    peak_prot = max(vecs[vin_prot])
    assert 41.0 <= peak_prot <= 58.1, f"SMAJ36A surge clamp out of bounds: peak VIN_PROT = {peak_prot:.2f} V (limit 60 V)"
    assert 4.80 <= max(vecs[v5]) <= 5.25, f"+5V rail disturbed during input surge: max={max(vecs[v5]):.2f} V"


@pytest.mark.parametrize("buck_u,ind_l,out_c,target_v", [
    ("U1", "L1", "C5", 5.0),
    ("U2", "L2", "C14", 12.0),
])
def test_power_tps54360_loop_stability_from_extracted_copper(buck_u: str, ind_l: str, out_c: str, target_v: float):
    """Break the extracted feedback loop of each TPS54360 buck on `power` with a Middlebrook AC injection source and
    verify Type-II compensation crossover frequency and phase margin >= 50 deg under the real BOM MLCC bank."""
    ng = cs.LibNgSpice.get()
    deck = cs.build_extracted_spice_deck(("power",))
    vin_nd = deck.node("power", buck_u, "2")
    fb_nd = deck.node("power", buck_u, "5")
    comp_nd = deck.node("power", buck_u, "6")
    vout_nd = deck.node("power", out_c, "1")

    passive_only = [
        ln for ln in deck.lines
        if not ln.startswith((f"X_power_{buck_u} ", f"RDCR_power_{ind_l} ", f"L_power_{ind_l} "))
    ]
    r_load = 10.0 if target_v == 5.0 else 24.0   # 0.5 A nominal load (SLVSBB4G section 8.2)
    loop_mb = "\n".join(passive_only + [
        f"V_VIN {vin_nd} 0 DC 24.0",
        f"R_LOAD {vout_nd} 0 {r_load}",
        "V_REF vref_node 0 DC 0.800",
        f"V_INJ {fb_nd} fb_ea DC 0 AC 1.0",
        f"G_EA 0 {comp_nd} vref_node fb_ea 350u",
        f"R_OEA {comp_nd} 0 8Meg",
        # Bias offset keeps DC V(vout) at target_v while AC loop gain is exact gm_ps = 12 A/V into real PCB Cout||Rload!
        f"I_DC 0 {vout_nd} DC {target_v / r_load}",
        f"G_PS 0 {vout_nd} {comp_nd} comp_dc 12.0",
        f"R_LP {comp_nd} comp_dc 1e9",
        "C_LP comp_dc 0 10",
    ])
    freqs, vecs = ng.run_ac(loop_mb, [fb_nd, "fb_ea"], 100.0, 500e3, pts_per_dec=50)
    # Loop gain T(jw) = -V(fb_nd) / V(fb_ea)
    t_loop = [-vf / ve for vf, ve in zip(vecs[fb_nd], vecs["fb_ea"])]
    mags_db = [20.0 * math.log10(max(abs(g), 1e-12)) for g in t_loop]

    # Locate 0 dB crossover frequency f_co and compute phase margin PM = 180 + phase(T(j 2 pi f_co)).
    crossings = [i for i in range(len(mags_db) - 1) if mags_db[i] >= 0.0 > mags_db[i + 1]]
    assert len(crossings) == 1, f"power {buck_u}: expected single 0 dB crossover, got {len(crossings)}"
    idx = crossings[0]
    frac = mags_db[idx] / (mags_db[idx] - mags_db[idx + 1])
    f_co = freqs[idx] + frac * (freqs[idx + 1] - freqs[idx])
    g_co = t_loop[idx] + frac * (t_loop[idx + 1] - t_loop[idx])
    phase_deg = math.degrees(cmath.phase(g_co))
    if phase_deg < -180.0:
        phase_deg += 360.0
    pm_deg = 180.0 + phase_deg

    assert 5.0e3 <= f_co <= 45.0e3, f"power {buck_u}: crossover f_co={f_co / 1e3:.1f} kHz outside 5..45 kHz target"
    assert pm_deg >= 50.0, f"power {buck_u}: phase margin {pm_deg:.1f} deg < 50 deg minimum"


# =============================================================================
# 2. `main` + `power` + `probe` + `reflector`: 3D-Mated Heaters & -7.6 C Thermostat
# =============================================================================

def test_mated_heaters_shunts_and_probe_thermostat_trip_and_hysteresis():
    ng = cs.LibNgSpice.get()

    # A. Warm ambient (+20 C): probe thermostat OFF, reflector PWM ON -> verify 4.6 W heater & ADS1115 shunt monitors.
    deck_warm = cs.build_extracted_spice_deck(
        ("main", "power", "probe", "reflector"),
        ntc_temp_c={("probe", "TH1"): 20.0, ("reflector", "TH1"): 25.0},
    )
    vin_raw = deck_warm.node("main", "J1", "1")
    heat_pwm = deck_warm.node("main", "U3", "4")
    ntc_sense = deck_warm.node("main", "U3", "2")
    mon_refl = deck_warm.node("main", "U4", "4")     # AIN0: 0.1 Ohm reflector shunt
    mon_probe = deck_warm.node("main", "U4", "5")    # AIN1: 1.0 Ohm probe shunt
    co2_gate = deck_warm.node("probe", "U4", "1")
    co2_vset = deck_warm.node("probe", "U4", "2")
    co2_ntc = deck_warm.node("probe", "U4", "3")
    v12_probe = deck_warm.node("probe", "U4", "8")

    cir_warm = "\n".join(deck_warm.lines + [
        f"V_ROOF {vin_raw} 0 DC 24.0",
        f"V_PWM {heat_pwm} 0 DC 3.3",
    ])
    op_w = ng.run_op(cir_warm, [ntc_sense, mon_refl, mon_probe, co2_gate, co2_vset, co2_ntc, v12_probe])

    # Reflector TH1 (10k @ 25C) with main R7 (10k to +3V3) across probe/reflector B2B + cable -> ~1.65 V!
    assert 1.60 <= op_w[ntc_sense] <= 1.70, f"reflector NTC_SENSE divider at 25 C wrong: {op_w[ntc_sense]:.3f} V"
    # Reflector 4x 120R = 30R on 12V -> ~0.39 A -> ~39 mV across R16 (0.1R)!
    assert 0.034 <= op_w[mon_refl] <= 0.044, f"reflector shunt MON_REFL wrong: {op_w[mon_refl] * 1e3:.1f} mV"
    # At +20 C, probe thermostat must be OFF (`co2_gate` low, `mon_probe` ~ 0 V) and inputs inside LM2903 CMR (0..VCC-2V)!
    assert op_w[co2_gate] < 0.20, f"probe heater stuck ON at +20 C: CO2_GATE={op_w[co2_gate]:.2f} V"
    assert op_w[mon_probe] < 0.002
    assert 0.2 <= op_w[co2_vset] <= op_w[v12_probe] - 2.0
    assert 0.2 <= op_w[co2_ntc] <= op_w[v12_probe] - 2.0

    # B. Deep cold (-12 C): probe thermostat ON -> verify 2x 680R = 340R (~35 mA = 0.42 W) & 1 Ohm shunt monitor!
    deck_cold = cs.build_extracted_spice_deck(
        ("main", "power", "probe", "reflector"),
        ntc_temp_c={("probe", "TH1"): -12.0, ("reflector", "TH1"): -12.0},
        extra_lines=[f"V_ROOF {vin_raw} 0 DC 24.0", f"V_PWM {heat_pwm} 0 DC 0.0"],
    )
    op_c = ng.run_op("\n".join(deck_cold.lines), [co2_gate, mon_probe, mon_refl])
    assert op_c[co2_gate] >= 3.0, f"probe thermostat failed to turn ON at -12 C: CO2_GATE={op_c[co2_gate]:.2f} V"
    assert 0.031 <= op_c[mon_probe] <= 0.039, f"probe heater shunt MON_PROBE wrong at -12 C: {op_c[mon_probe] * 1e3:.1f} mV"
    assert op_c[mon_refl] < 0.002

    # C. Transient temperature sweep (-4 C -> -11 C -> -4 C) verifying -7.6 C turn-on and ~2.3 K positive hysteresis!
    # Replace fixed `R_probe_TH1` with a time-varying thermistor driven by temperature waveform `V(temp_c)`:
    lines_hyst = [ln for ln in deck_warm.lines if not ln.startswith("R_probe_TH1 ")]
    th_p1 = deck_warm.node("probe", "TH1", "1")
    th_p2 = deck_warm.node("probe", "TH1", "2")
    cir_hyst = "\n".join(lines_hyst + [
        f"V_ROOF {vin_raw} 0 DC 24.0",
        f"V_PWM {heat_pwm} 0 DC 0.0",
        "V_TEMP temp_c 0 PWL(0 -4.0  50m -11.5  100m -4.0)",
        f"B_TH1 {th_p1} {th_p2} I = V({th_p1}, {th_p2}) / (10000.0 * exp(3950.0 * (1.0 / (V(temp_c) + 273.15) - 1.0 / 298.15)))",
    ])
    t_vec, v_hyst = ng.run_tran(cir_hyst, ["temp_c", co2_gate], "200u", "100m")
    temps = v_hyst["temp_c"]
    gates = v_hyst[co2_gate]

    # Cooling half (0..50 ms): find temperature where CO2_GATE crosses 2.0 V rising (turn-ON threshold).
    t_on = next(temps[i] for i in range(len(t_vec)) if t_vec[i] <= 50e-3 and gates[i] > 2.0)
    # Warming half (50..100 ms): find temperature where CO2_GATE crosses 2.0 V falling (turn-OFF threshold).
    t_off = next(temps[i] for i in range(len(t_vec)) if t_vec[i] > 50e-3 and gates[i] < 2.0)
    hyst_k = t_off - t_on

    # With heater OFF (CO2_GATE ~ 0 V), R7 (470k) parallels TH1 to GND (TH1||470k = 47k -> TH1 = 52.2k -> T_on ~ -8.1 C,
    # ~1.9 C above the SCD41 -10 C limit); once ON (CO2_GATE ~ 3.3 V), R7 parallels R3 (10k) -> T_off ~ -5.7 C (~2.4 K hysteresis).
    assert -8.6 <= t_on <= -7.5, f"probe thermostat turn-ON temp {t_on:.2f} C outside -8.6..-7.5 C (~1.9 C margin above -10 C)"
    assert -6.5 <= t_off <= -5.0, f"probe thermostat turn-OFF temp {t_off:.2f} C outside -6.5..-5.0 C"
    assert 1.8 <= hyst_k <= 3.2, f"probe thermostat hysteresis {hyst_k:.2f} K outside 1.8..3.2 K target (~2.4 K)"


# =============================================================================
# 3. `crown`: Ultrasonic H-Bridge TX/RX, Piezo Charge Amp & AS3935 500 kHz Tank
# =============================================================================

def test_crown_ultrasonic_rx_ac_noise_and_tx_burst_from_extracted_copper():
    ng = cs.LibNgSpice.get()
    deck = cs.build_extracted_spice_deck(("crown",))

    v5 = deck.node("crown", "J1", "5")
    v12 = deck.node("crown", "J1", "1")
    v3 = deck.node("crown", "U10", "2")
    v3a = deck.node("crown", "C8", "1")
    vref = deck.node("crown", "U1", "17")
    amp_out = deck.node("crown", "U1", "13")
    rain_adc = deck.node("crown", "U1", "18")
    mux_s0 = deck.node("crown", "U7", "10")
    mux_s1 = deck.node("crown", "U7", "9")
    tx_p = deck.node("crown", "U2", "2")
    tx_n = deck.node("crown", "U2", "4")
    en_n = deck.node("crown", "U2", "1")
    en_s = deck.node("crown", "U4", "1")
    xd_n_a = deck.node("crown", "J2", "1")
    xd_n_b = deck.node("crown", "J2", "2")
    xd_s_a = deck.node("crown", "J4", "1")
    xd_s_b = deck.node("crown", "J4", "2")
    vdrv = deck.node("crown", "C13", "1")

    # A. DC `.op` with undriven EN lines (simulating MCU reset): drivers must stay OFF (< 0.8 V) and preamps bias at 1.65 V!
    cir_op = "\n".join(deck.lines + [
        f"V5 {v5} 0 DC 5.0",
        f"V12 {v12} 0 DC 12.0",
        f"VS0 {mux_s0} 0 DC 0.0",
        f"VS1 {mux_s1} 0 DC 0.0",
        f"VTXP {tx_p} 0 DC 0.0",
        f"VTXN {tx_n} 0 DC 0.0",
    ])
    op = ng.run_op(cir_op, [v3, v3a, vref, amp_out, rain_adc, en_n, en_s])
    assert 3.25 <= op[v3] <= 3.35
    assert 3.24 <= op[v3a] <= 3.35
    assert 1.62 <= op[vref] <= 1.68, f"US_VREF off 1.65 V: {op[vref]:.3f} V"
    assert abs(op[amp_out] - op[vref]) < 0.03, f"RX preamp DC bias off VREF: {op[amp_out]:.3f} V"
    assert abs(op[rain_adc] - op[vref]) < 0.03, f"Rain charge amp DC bias off VREF: {op[rain_adc]:.3f} V"
    # ENA + ENB are tied on the PCB, putting two 200k internal pull-ups in parallel (100k) against 2.2k -> ~0.258 V (<< 0.80 V VEN_L min).
    assert 0.20 <= op[en_n] < 0.35 and 0.20 <= op[en_s] < 0.35, f"UCC27524 EN reset bias out of range: {op[en_n]:.3f} V"

    # B. `.ac` & `.noise` (`setplot noise1`) on N-channel ultrasonic RX (`MUX_S0=0, MUX_S1=0` selects X0 = XD_N_RX):
    cir_ac = "\n".join(deck.lines + [
        f"V5 {v5} 0 DC 5.0",
        f"V12 {v12} 0 DC 12.0",
        f"VS0 {mux_s0} 0 DC 0.0",
        f"VS1 {mux_s1} 0 DC 0.0",
        f"VTXP {tx_p} 0 DC 0.0",
        f"VTXN {tx_n} 0 DC 0.0",
        f"VEN_N {en_n} 0 DC 0.0",
        f"V_ECHO {xd_n_a} {xd_n_b} DC 0.0 AC 1.0",
    ])
    freqs, ac_v = ng.run_ac(cir_ac, [amp_out], 200.0, 500e3, pts_per_dec=40)
    gains = [abs(v) for v in ac_v[amp_out]]
    g_40k = min(zip(freqs, gains), key=lambda fg: abs(fg[0] - 40e3))[1]
    # Ideal midband gain 7.80 V/V minus ~0.55 dB across 47k into mux/trace C (~23 pF) and ~0.58 dB from 106 kHz LP -> ~6.80 V/V!
    assert 6.5 <= g_40k <= 8.0, f"40 kHz RX gain {g_40k:.2f} V/V outside 6.5..8.0 target"

    f_hp = next(f for f, g in zip(freqs, gains) if f < 40e3 and g >= g_40k / math.sqrt(2.0))
    f_lp = next(f for f, g in zip(freqs, gains) if f > 40e3 and g <= g_40k / math.sqrt(2.0))
    assert 1.1e3 <= f_hp <= 2.3e3, f"RX high-pass corner {f_hp:.0f} Hz outside 1.1..2.3 kHz"
    assert 75e3 <= f_lp <= 140e3, f"RX low-pass corner {f_lp / 1e3:.1f} kHz outside 75..140 kHz"

    noise_res = ng.run_noise(cir_ac, f"v({amp_out})", "v_echo", 1e3, 200e3, pts_per_dec=20)
    idx_40k = min(range(len(noise_res["freqs"])), key=lambda i: abs(noise_res["freqs"][i] - 40e3))
    onoise_40k = noise_res["onoise_spectrum"][idx_40k]
    assert 5e-8 <= onoise_40k <= 2e-6, f"40 kHz RX output noise {onoise_40k * 1e9:.1f} nV/rtHz unexpected"
    assert noise_res["onoise_total"] < 5e-4, f"integrated RX output noise {noise_res['onoise_total'] * 1e3:.3f} mVrms > 0.5 mVrms"

    # C. `.tran` 8-cycle 40 kHz H-bridge burst on N while listening on S (`MUX_S1=3.3, MUX_S0=0` -> X2 = XD_S_RX)
    cir_tran = "\n".join(deck.lines + [
        f"V5 {v5} 0 DC 5.0",
        f"V12 {v12} 0 DC 12.0",
        f"VS0 {mux_s0} 0 DC 0.0",
        f"VS1 {mux_s1} 0 DC 3.3",
        f"VEN_N {en_n} 0 PULSE(0 3.3 5u 50n 50n 205u 1m)",
        f"VEN_S {en_s} 0 DC 0.0",
        f"VTXP {tx_p} 0 PULSE(0 3.3 5u 20n 20n 12.4u 25u 8)",
        f"VTXN {tx_n} 0 PULSE(0 3.3 17.5u 20n 20n 12.4u 25u 8)",
        # 2 nF transducer load on N; 20 mVpp delayed echo on S after 250 us!
        f"C_XDN {xd_n_a} {xd_n_b} 2n",
        f"C_XDS {xd_s_a} s_echo 2n",
        f"V_SECHO s_echo {xd_s_b} SIN(0 0.010 40k 250u)",
    ])
    t_vec, tr = ng.run_tran(cir_tran, [xd_n_a, xd_n_b, vdrv, amp_out], "0.25u", "420u")
    v_diff_tx = [a - b for t, a, b in zip(t_vec, tr[xd_n_a], tr[xd_n_b]) if 20e-6 <= t <= 190e-6]
    tx_vpp = max(v_diff_tx) - min(v_diff_tx)
    assert 19.5 <= tx_vpp <= 24.0, f"H-bridge TX differential swing {tx_vpp:.2f} Vpp outside 19.5..24 Vpp"
    assert min(tr[vdrv]) > 11.0, f"VDRV drooped below 11.0 V during burst: {min(tr[vdrv]):.2f} V"

    rx_echo = [v for t, v in zip(t_vec, tr[amp_out]) if t >= 290e-6]
    echo_vpp = max(rx_echo) - min(rx_echo)
    assert 0.11 <= echo_vpp <= 0.19, f"20 mVpp S-channel echo amplified to {echo_vpp * 1e3:.1f} mVpp (expected ~150 mVpp)"


def test_crown_piezo_charge_amp_hail_clamp_and_as3935_500khz_tank():
    ng = cs.LibNgSpice.get()
    deck = cs.build_extracted_spice_deck(("crown",))

    v5 = deck.node("crown", "J1", "5")
    v12 = deck.node("crown", "J1", "1")
    pz_in = deck.node("crown", "J6", "1")
    rain_adc = deck.node("crown", "U1", "18")
    as_inp = deck.node("crown", "U9", "3")

    # A. Piezo charge amp `.ac` (1 nF piezo disc in series with 1 V AC = 1 nC/V -> expected passband gain 1n/22n = 45.5 mV/V)
    cir_pz_ac = "\n".join(deck.lines + [
        f"V5 {v5} 0 DC 5.0",
        f"V12 {v12} 0 DC 12.0",
        "V_PZ pz_src 0 DC 0.0 AC 1.0",
        f"C_PZ pz_src {pz_in} 1n",
    ])
    freqs, pv = ng.run_ac(cir_pz_ac, [rain_adc], 0.2, 100e3, pts_per_dec=30)
    pgains = [abs(v) for v in pv[rain_adc]]
    g_1k = min(zip(freqs, pgains), key=lambda fg: abs(fg[0] - 1e3))[1]
    g_40k = min(zip(freqs, pgains), key=lambda fg: abs(fg[0] - 40e3))[1]
    assert 0.041 <= g_1k <= 0.050, f"piezo charge amp sensitivity {g_1k * 1e3:.1f} mV/nC outside 41..50 mV/nC (1nF/22nF)"
    assert g_40k < 0.22 * g_1k, f"22k/1nF anti-alias filter failed to attenuate 40 kHz (>5x required): g_40k={g_40k:.4f}"

    # B. Hail impact overload `.tran` (+/-20 mA impulse into J6.1): BAV99 D9 must clamp PIEZO_IN to GND / +3V3 rails!
    cir_hail = "\n".join(deck.lines + [
        f"V5 {v5} 0 DC 5.0",
        f"V12 {v12} 0 DC 12.0",
        f"I_HAIL 0 {pz_in} SIN(0 20m 5k 50u)",
    ])
    _t, hv = ng.run_tran(cir_hail, [pz_in], "2u", "450u")
    assert min(hv[pz_in]) >= -0.95 and max(hv[pz_in]) <= 4.25, (
        f"BAV99 D9 failed to clamp hail overload on PIEZO_IN: [{min(hv[pz_in]):.2f}, {max(hv[pz_in]):.2f}] V"
    )

    # C. AS3935 500 kHz parallel LC antenna tank `.ac` impedance Z(f) = V(AS_INP) / 1A:
    cir_tank = "\n".join(deck.lines + [
        f"V5 {v5} 0 DC 5.0",
        f"V12 {v12} 0 DC 12.0",
        f"I_TANK 0 {as_inp} DC 0.0 AC 1.0",
    ])
    tfreqs, tv = ng.run_ac(cir_tank, [as_inp], 350e3, 650e3, pts_per_dec=200)
    zmags = [abs(v) for v in tv[as_inp]]
    pk_idx = max(range(len(zmags)), key=lambda i: zmags[i])
    f0 = tfreqs[pk_idx]
    z_pk = zmags[pk_idx]
    f_lo = next(tfreqs[i] for i in range(pk_idx, 0, -1) if zmags[i] <= z_pk / math.sqrt(2.0))
    f_hi = next(tfreqs[i] for i in range(pk_idx, len(zmags)) if zmags[i] <= z_pk / math.sqrt(2.0))
    q_eff = f0 / (f_hi - f_lo)

    assert 485e3 <= f0 <= 515e3, f"AS3935 tank resonance f0={f0 / 1e3:.2f} kHz outside 500 kHz +/- 3%"
    assert 10.0 <= q_eff <= 22.0, f"AS3935 damped tank Q={q_eff:.1f} outside 10..22 (AN04 target ~15)"


# =============================================================================
# 4. `crown` + `dome`: 3D-Mated Zero-Bias UV-B / UV-A Transimpedance Front End
# =============================================================================

def test_3d_mated_crown_and_dome_uv_transimpedance_gain_bandwidth_and_noise():
    ng = cs.LibNgSpice.get()
    deck = cs.build_extracted_spice_deck(("crown", "dome"))

    v5 = deck.node("crown", "J1", "5")
    v12 = deck.node("crown", "J1", "1")
    d1_a = deck.node("dome", "D1", "1")       # UV_VB (Anode)
    d1_k = deck.node("dome", "D1", "2")       # UVB_IN (Cathode -> U1A IN-)
    d2_a = deck.node("dome", "D2", "1")       # UV_VB (Anode)
    d2_k = deck.node("dome", "D2", "2")       # UVA_IN (Cathode -> U1B IN-)
    ain0 = deck.node("dome", "U3", "4")       # UVB_OUT
    ain1 = deck.node("dome", "U3", "5")       # UVA_OUT
    ain3 = deck.node("dome", "U3", "7")       # UV_VB differential reference

    # A. Dark `.op`: verify 3D-mated +3V3 rail powers dome, UV_VB = 0.30 V, and differential dark offset ~ 0 mV!
    cir_dark = "\n".join(deck.lines + [
        f"V5 {v5} 0 DC 5.0",
        f"V12 {v12} 0 DC 12.0",
    ])
    op_dark = ng.run_op(cir_dark, [ain0, ain1, ain3])
    assert 0.285 <= op_dark[ain3] <= 0.315, f"dome UV_VB bias off 0.30 V: {op_dark[ain3]:.4f} V"
    assert abs(op_dark[ain0] - op_dark[ain3]) < 1e-3
    assert abs(op_dark[ain1] - op_dark[ain3]) < 1e-3

    # B. Illuminated `.op`: I_uvb = 10 nA (10 MOhm -> +100 mV), I_uva = 150 nA (4.7 MOhm -> +705 mV)
    # Photocurrent flows from Cathode to Anode inside a photodiode (`d1_k -> d1_a`), pulling IN- down -> Vout rises!
    cir_sun = "\n".join(deck.lines + [
        f"V5 {v5} 0 DC 5.0",
        f"V12 {v12} 0 DC 12.0",
        f"I_UVB {d1_k} {d1_a} DC 10n AC 1.0",
        f"I_UVA {d2_k} {d2_a} DC 150n",
    ])
    op_sun = ng.run_op(cir_sun, [ain0, ain1, ain3])
    diff_uvb = op_sun[ain0] - op_sun[ain3]
    diff_uva = op_sun[ain1] - op_sun[ain3]
    assert 0.098 <= diff_uvb <= 0.102, f"UV-B TIA 10 nA * 10 MOhm output {diff_uvb * 1e3:.2f} mV != 100 mV"
    assert 0.695 <= diff_uva <= 0.715, f"UV-A TIA 150 nA * 4.7 MOhm output {diff_uva * 1e3:.2f} mV != 705 mV"

    # C. `.ac` & `.noise` (`setplot noise1`) on UV-B 10 MOhm || 10 nF TIA (tau = 0.1 s -> f_-3dB = 1.59 Hz)
    freqs, uac = ng.run_ac(cir_sun, [ain0, ain3], 0.05, 200.0, pts_per_dec=30)
    ztia = [abs(a - b) for a, b in zip(uac[ain0], uac[ain3])]
    z_dc = ztia[0]
    f_3db = next(f for f, z in zip(freqs, ztia) if z <= z_dc / math.sqrt(2.0))
    assert 9.8e6 <= z_dc <= 10.2e6, f"UV-B AC transimpedance {z_dc / 1e6:.2f} MOhm != 10 MOhm"
    assert 1.30 <= f_3db <= 1.90, f"UV-B TIA -3 dB corner {f_3db:.2f} Hz != 1.59 Hz"

    n_uvb = ng.run_noise(cir_sun, f"v({ain0},{ain3})", "i_uvb", 0.1, 1e3, pts_per_dec=20)
    assert n_uvb["onoise_total"] < 50e-6, (
        f"UV-B TIA integrated RMS noise {n_uvb['onoise_total'] * 1e6:.1f} uVrms >= 50 uVrms (min signal 460 uV)"
    )
