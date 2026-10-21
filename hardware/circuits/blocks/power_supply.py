"""TI TPS54360 4.5-60 V asynchronous buck (datasheet SLVSBB4G section 8.2 design procedure), instantiated twice on
the `power` board: +5V for the electronics and +12V_HEAT for the heaters and the crown transducer drivers.

Common: f_sw = 92417 x RT^-0.991 = 743 kHz (RT = 130k, 243 kHz away from the AS3935 500 kHz band); catch diode
B360A (60 V, 3 A); UVLO 511k / 82.5k (start 8.0 V, stop 6.25 V); gm_ps = 12 A/V, gm_ea = 350 uA/V, VREF = 0.8 V.

+5V (logic): L 8.2 uH (ripple 0.70 A at 36 V), COUT 2x 47 uF 10 V (~40 uF at 5 V), modulator pole 796 Hz,
  f_co 17 kHz: R4 = 2 pi f_co C VOUT / (gm_ps VREF gm_ea) = 6.49k, C5 = 1 / (2 pi R4 f_p) = 33 nF,
  C8 = 1 / (pi R4 f_sw) = 68 pF; FB 53.6k / 10.2k -> 5.00 V. Min on-time at 44 V: 168 ns > 135 ns.
+12V_HEAT: L 22 uH (ripple 0.46 A at 32 V, 0.20 A at 16 V), COUT 2x 22 uF 25 V (~14 uF effective at 12 V DC bias),
  modulator pole 1.5 A / (2 pi 12 V 14 uF) = 1.42 kHz, f_co 20 kHz: R4 = 6.49k, C5 = 1 / (2 pi 6.49k 1.42 kHz) = 18 nF,
  C8 = 68 pF; FB 140k / 10k -> 0.8 x 15 = 12.0 V. Min on-time at 44 V: 367 ns.
  Below ~13.5 V input the high-side switch simply stays on (datasheet 7.3.4, 100 % duty with BOOT refresh): the rail
  then follows VIN minus ~1 V (10.4-11 V on a 12 V supply). Heater power is held by the firmware's constant-wattage
  PWM on the measured rail; the crown transducer drive amplitude follows the rail (receiver AGC absorbs +/-1.2 dB)."""

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
POWERPAD8 = "Package_SO:TI_SO-PowerPAD-8_ThermalVias"
D_SMA = "Diode_SMD:D_SMA"
L_SWPA6045 = "Inductor_SMD:L_Sunlord_SWPA6045S"

# per-rail values: inductor (value, mpn, lcsc, note), output cap, compensation zero cap, FB divider (top, bottom)
RAILS = {
    5.0: {"l": ("8.2uH", "SWPA6045S8R2MT", "C96963", "Isat 3.9 A, Irms 2.6 A, 56 mOhm"),
          "cout": "47u 10V", "c5": ("33n 50V", "f_z = 742 Hz"), "fb": ("53k6", "10k2")},
    12.0: {"l": ("22uH", "SWPA6045S220MT", "C83454", "Isat 1.9 A, Irms 1.6 A, 140 mOhm"),
           "cout": "22u 25V", "c5": ("18n 50V", "f_z = 1.36 kHz"), "fb": ("140k", "10k")},
}


def res(value, source, ref):
    return part("Device", "R", value, R0603, src(source=source), ref=ref)


def cap(value, source, ref, footprint=C0603):
    return part("Device", "C", value, footprint, src(source=source), ref=ref)


def tps54360(name, vin, vout, gnd, u, l, d, c, r, volts=5.0):
    """One TPS54360 buck at `volts` (5.0 or 12.0). `c` = 9 capacitor refs, `r` = 6 resistor refs (stable layout)."""
    block(f"buck_{name}")
    cfg = RAILS[volts]
    tag = name.upper()
    sw, bst, en, fb, rt, comp, cc = (Net(f"{tag}_{n}") for n in ("SW", "BST", "EN", "FB", "RT", "COMP", "CC"))
    ic = part("Regulator_Switching", "TPS54360DDA", "TPS54360DDAR", POWERPAD8,
              src(mpn="TPS54360DDAR", mfr="Texas Instruments", lcsc="C44377",
                  source=f"TPS54360 4.5-60 V input, 3.5 A buck converter ({name} {volts:g} V rail, 743 kHz)"),
              ref=u)
    for pin, net in (("1", bst), ("2", vin), ("3", en), ("4", rt), ("5", fb), ("6", comp), ("7", gnd),
                     ("8", sw), ("9", gnd)):
        ic[pin] += net
    l_val, l_mpn, l_lcsc, l_note = cfg["l"]
    ind = part("Device", "L", l_val, L_SWPA6045,
               src(mpn=l_mpn, mfr="Sunlord", lcsc=l_lcsc,
                   source=f"{l_val} shielded power inductor, {l_note} (6 x 6 x 4.5 mm)"),
               ref=l)
    ind[1] += sw
    ind[2] += vout
    diode = part("Device", "D_Schottky", "B360A", D_SMA,
                 src(mpn="B360A-13-F", mfr="Diodes Inc", lcsc="C94193",
                     source="TPS54360 catch diode (60 V, 3 A Schottky, SW -> GND)"),
                 ref=d)
    diode["K"] += sw
    diode["A"] += gnd
    c5_val, c5_note = cfg["c5"]
    caps = (("2u2 100V", vin, gnd, "TPS54360 input capacitor 1 (100 V X7R, >= 3 uF effective with C2)", C1206),
            ("2u2 100V", vin, gnd, "TPS54360 input capacitor 2", C1206),
            ("100n 100V", vin, gnd, "TPS54360 input high-frequency decoupling (at VIN/GND pins)", C0603),
            ("100n 50V", bst, sw, "TPS54360 bootstrap capacitor (datasheet 0.1 uF)", C0603),
            (cfg["cout"], vout, gnd, "TPS54360 output capacitor 1", C1206),
            (cfg["cout"], vout, gnd, "TPS54360 output capacitor 2", C1206),
            ("100n 50V", vout, gnd, "TPS54360 output high-frequency decoupling", C0603),
            (c5_val, cc, gnd, f"TPS54360 compensation zero capacitor C5 ({c5_note})", C0603),
            ("68p C0G 50V", comp, gnd, "TPS54360 compensation pole capacitor C8", C0603))
    for ref, (val, a, b, source, fp) in zip(c, caps):
        x = cap(val, f"{source} [{name}]", ref, fp)
        x[1] += a
        x[2] += b
    fb_top, fb_bot = cfg["fb"]
    ress = (("511k", vin, en, "TPS54360 EN UVLO top (511k/82.5k -> 8.0 V start, 6.25 V stop)"),
            ("82k5", en, gnd, "TPS54360 EN UVLO bottom"),
            (fb_top, vout, fb, f"TPS54360 FB top: VOUT = 0.8 * (1 + {fb_top} / {fb_bot}) = {volts:.2f} V"),
            (fb_bot, fb, gnd, "TPS54360 FB bottom"),
            ("130k", rt, gnd, "TPS54360 RT: 743 kHz switching frequency"),
            ("6k49", comp, cc, "TPS54360 compensation resistor R4 (f_co ~ 17-20 kHz)"))
    for ref, (val, a, b, source) in zip(r, ress):
        x = res(val, f"{source} [{name}]", ref)
        x[1] += a
        x[2] += b
