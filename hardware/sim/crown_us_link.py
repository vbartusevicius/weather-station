"""ngspice model of the crown v2 ultrasonic link (hardware/circuits/blocks/crown_sensors.py), run in the `eda` container.

    python3 hardware/sim/crown_us_link.py            -> prints the measurements, writes hardware/build/sim/crown_us_link.json

What is modelled (one transmit / one receive pair, N -> S):
  - +12V_HEAT -> 10R -> VDRV + 22 uF; each UCC27524 output as a push-pull switch pair (0.5 ohm on, 120 ns dead time
    ignored), OUTA / OUTB in anti-phase for 8 cycles at 40 kHz; the idle receive driver holds both outputs low.
  - BAV99 anti-parallel T/R isolation diodes (1N4148-class model) in every driver lead; 330R B-terminal reference.
  - Transducers: Butterworth-Van Dyke (C0 2 nF; motional 79 mH / 200 pF / 660 ohm -> 40.0 kHz, Q 30).
  - Acoustics: the transmitter's motional current, delayed 290 us by a lossless line, drives an EMF source in the
    receiver's motional branch. The coupling `K_ACOUSTIC` is set for ~50 mVpp open-circuit EMF at 21 Vpp drive: a
    closed 10 mm 40 kHz part (~106 dB SPL @ 30 cm / 10 Vrms, ~-75 dB re 1 V/ubar) over the 96 mm reflected path. It
    is an estimate - the prototype measurement replaces it; the electrical conclusions below do not depend on it.
  - Receive: 47k + 10 nF + 1M-to-VREF per channel (the transducers stay at 0 V DC so the T/R diodes stay off),
    74HC4052 input protection diodes to 3.3 V / 0 V, 100 ohm on-resistance, TLV9062 stage (G = 7.8, 1.6 kHz high-pass,
    106 kHz low-pass) before the STM32 PGA (x2..x32).
  - A copy of the receive transducer WITHOUT isolation diodes (leads straight to its idle, grounded driver) shows
    what the diodes buy.
"""

import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "hardware/build/sim"
K_ACOUSTIC = 0.0022  # V of receive EMF per mA of transmit motional current (see docstring)

NETLIST = f"""* crown v2 ultrasonic link
.model DSW sw(Vt=0.5 Vh=0.1 Ron=0.5 Roff=1e9)
.model D1N4148 D(Is=2.52n Rs=0.568 N=1.752 Cjo=4p M=0.4 tt=20n BV=100)
.model DESD D(Is=1e-14 Rs=10 N=1.0 Cjo=5p)

V12 v12 0 12
RDRV v12 vdrv 10
CDRV vdrv 0 22u IC=12
* gate signals: 8 cycles of 40 kHz, A and B in anti-phase, idle low
VPA pa 0 PULSE(0 1 0 1n 1n 12.5u 25u 8)
VPB pb 0 PULSE(0 1 12.5u 1n 1n 12.5u 25u 8)
* ---- transmit driver (N): OUTA, OUTB push-pull
S1 vdrv outa pa 0 DSW
S2 outa 0 na 0 DSW
S3 vdrv outb pb 0 DSW
S4 outb 0 nb 0 DSW
BNA na 0 V=1-V(pa)
BNB nb 0 V=1-V(pb)
* isolation diodes (BAV99 anti-parallel) into the transducer leads
DA1 outa xna D1N4148
DA2 xna outa D1N4148
DB1 outb xnb D1N4148
DB2 xnb outb D1N4148
RBN xnb 0 330
* transmit transducer (BVD)
C0N xna xnb 2n
L1N xna m1n 79m
C1N m1n m2n 200p
R1N m2n mi_n 660
VSN mi_n xnb 0
* its own receive resistor + mux protection diodes (deselected channel)
RRXN xna rxnc 47k
CRXN rxnc rxn 10n
RBN2 rxn vref 1Meg
DN1 rxn v33 DESD
DN2 0 rxn DESD
CN rxn 0 10p
V33 v33 0 3.3
* ---- acoustic path: transmitter motional current -> 290 us delay -> receiver EMF
HAC ac 0 VSN 1000
TAC ac 0 acd 0 Z0=50 TD=290u
RTERM acd 0 50
* ---- receive transducer (S), idle driver outputs low, isolation diodes
S5 vdrv osa zero 0 DSW
S6 osa 0 one 0 DSW
S7 vdrv osb zero 0 DSW
S8 osb 0 one 0 DSW
VZERO zero 0 0
VONE one 0 1
DS1 osa xsa D1N4148
DS2 xsa osa D1N4148
DS3 osb xsb D1N4148
DS4 xsb osb D1N4148
RBS xsb 0 330
C0S xsa xsb 2n
L1S xsa m1s 79m
C1S m1s m2s 200p
R1S m2s m3s 660
ERX m3s xsb acd 0 {K_ACOUSTIC}
RRXS xsa rxsc 47k
CRXS rxsc rxs 10n
RBIAS rxs vref 1Meg
DS5 rxs v33 DESD
DS6 0 rxs DESD
CS rxs 0 10p
RON rxs rxmux 100
CMUX rxmux 0 10p
VREF vref 0 1.65
* TLV9062 stage: 1.6 kHz high-pass (Rg / Cg), gain 7.8, 106 kHz low-pass (Rf / Cf)
EAMP ampi 0 rxmux vref 7.8
RHP ampi amph 1k
CHP amph ampo 100n
RLP ampo 0 22k
CLP ampo 0 68p
* ---- comparison copy WITHOUT isolation diodes (receiver leads straight to its grounded idle driver)
C0X xxa 0 2n
L1X xxa m1x 79m
C1X m1x m2x 200p
R1X m2x m3x 660
ERXX m3x 0 acd 0 {K_ACOUSTIC}
RONX xxa 0 0.5
RRXX xxa rxxc 47k
CRXX rxxc rxx 10n
RBX rxx vref 1Meg

.ic v(rxs)=1.65 v(rxn)=1.65 v(rxx)=1.65 v(rxmux)=1.65 v(rxsc)=0 v(rxnc)=0 v(rxxc)=0
.tran 0.1u 900u 0 0.1u UIC
.control
run
let vtx = v(xna) - v(xnb)
meas tran tx_pp PP vtx from=50u to=190u
meas tran drv_droop MIN v(vdrv) from=5u to=250u
meas tran inj_max MAX i(V33) from=5u to=250u
meas tran inj_min MIN i(V33) from=5u to=250u
meas tran rx_pp PP v(rxmux) from=420u to=650u
meas tran amp_pp PP v(ampo) from=420u to=650u
meas tran rx_nodiode_pp PP v(rxx) from=420u to=650u
meas tran rx_leak_pp PP v(rxmux) from=30u to=250u
meas tran rx_quiet_pp PP v(rxmux) from=250u to=285u
meas tran emf_pp PP v(acd) from=420u to=650u
.endc
.end
"""


def run():
    OUT.mkdir(parents=True, exist_ok=True)
    cir = OUT / "crown_us_link.cir"
    cir.write_text(NETLIST)
    proc = subprocess.run(["ngspice", "-b", str(cir)], capture_output=True, text=True, timeout=600, check=False)
    text = proc.stdout + proc.stderr
    vals = {}
    for m in re.finditer(r"^\s*(\w+)\s*=\s*([-+0-9.eE]+)", text, re.MULTILINE):
        vals[m.group(1).lower()] = float(m.group(2))
    need = ("tx_pp", "drv_droop", "inj_max", "inj_min", "rx_pp", "amp_pp", "rx_nodiode_pp", "rx_leak_pp", "emf_pp",
            "rx_quiet_pp")
    missing = [k for k in need if k not in vals]
    if missing:
        sys.exit(f"ngspice did not report {missing}:\n{text[-3000:]}")
    res = {
        "tx_drive_vpp": vals["tx_pp"],
        "vdrv_min_v": vals["drv_droop"],
        "mux_injection_ma": 1e3 * max(abs(vals["inj_max"]), abs(vals["inj_min"])),
        "rx_emf_vpp_scaled": vals["emf_pp"] * K_ACOUSTIC,
        "rx_mux_mvpp": 1e3 * vals["rx_pp"],
        "preamp_out_mvpp": 1e3 * vals["amp_pp"],
        "rx_without_isolation_mvpp": 1e3 * vals["rx_nodiode_pp"],
        "rx_during_tx_mvpp": 1e3 * vals["rx_leak_pp"],
        "rx_before_echo_mvpp": 1e3 * vals["rx_quiet_pp"],
    }
    (OUT / "crown_us_link.json").write_text(json.dumps(res, indent=2))
    return res


if __name__ == "__main__":
    for k, v in run().items():
        print(f"{k:28s} {v:10.3f}")
