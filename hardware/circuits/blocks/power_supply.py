"""SCT2430 3.8-40 V -> 5 V / 3.5 A synchronous buck (datasheet Table 3 design: 500 kHz, 7.8 uH, 2x 47 uF),
instantiated twice on the `power` board: +5V for the electronics and +5V_HEAT for the heaters."""

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
ESOP8 = "Package_SO:SOIC-8-1EP_3.9x4.9mm_P1.27mm_EP2.514x3.2mm"
L_SWPA6045 = "Inductor_SMD:L_Sunlord_SWPA6045S"


def res(value, source, ref):
    return part("Device", "R", value, R0603, src(source=source), ref=ref)


def cap(value, source, ref, footprint=C0603):
    return part("Device", "C", value, footprint, src(source=source), ref=ref)


def sct2430(name, vin, vout, gnd, u, l, c, r):
    """One SCT2430 5.0 V buck. `c` = 8 capacitor refs, `r` = 6 resistor refs (explicit, stable layout)."""
    block(f"buck_{name}")
    tag = name.upper()
    sw, bst, en, fb, rt, comp, cc = (Net(f"{tag}_{n}") for n in ("SW", "BST", "EN", "FB", "RT", "COMP", "CC"))
    ic = part("gateway", "SCT2430", "SCT2430STER", ESOP8,
              src(mpn="SCT2430STER", mfr="SCT", lcsc="C509397",
                  source=f"SCT2430 3.8-40 V input, 3.5 A synchronous buck converter ({name} 5 V rail)"),
              ref=u)
    for pin, net in (("VIN", vin), ("GND", gnd), ("EP", gnd), ("EN", en), ("BOOT", bst), ("SW", sw),
                     ("FB", fb), ("RT/CLK", rt), ("COMP", comp)):
        ic[pin] += net
    ind = part("Device", "L", "8.2uH", L_SWPA6045,
               src(mpn="SWPA6045S8R2MT", mfr="Sunlord", lcsc="C96963",
                   source="8.2 uH shielded power inductor, Isat 3.9 A, Irms 2.6 A, 56 mOhm (6 x 6 x 4.5 mm)"),
               ref=l)
    ind[1] += sw
    ind[2] += vout
    caps = (("10u 50V", vin, gnd, "SCT2430 input decoupling", C0805),
            ("100n 50V", vin, gnd, "SCT2430 input high-frequency decoupling (at VIN/GND pins)", C0603),
            ("100n 50V", bst, sw, "SCT2430 bootstrap capacitor", C0603),
            ("47u 10V", vout, gnd, "SCT2430 output capacitor 1 (datasheet 2x 47 uF)", C1206),
            ("47u 10V", vout, gnd, "SCT2430 output capacitor 2", C1206),
            ("100n 50V", vout, gnd, "SCT2430 output high-frequency decoupling", C0603),
            ("3n3 50V", cc, gnd, "SCT2430 COMP capacitor (datasheet Table 3, 5 V)", C0603),
            ("22p C0G 50V", comp, gnd, "SCT2430 COMP high-frequency pole capacitor", C0603))
    for ref, (val, a, b, source, fp) in zip(c, caps):
        x = cap(val, f"{source} [{name}]", ref, fp)
        x[1] += a
        x[2] += b
    ress = (("51k", vin, en, "SCT2430 EN UVLO top (51k/10k -> 7.1 V on / 6.4 V off)"),
            ("10k", en, gnd, "SCT2430 EN UVLO bottom"),
            ("53k6", vout, fb, "SCT2430 FB top: VOUT = 0.8 * (1 + 53.6k / 10.2k) = 5.00 V"),
            ("10k2", fb, gnd, "SCT2430 FB bottom"),
            ("200k", rt, gnd, "SCT2430 RT: 500 kHz switching frequency"),
            ("33k2", comp, cc, "SCT2430 COMP series resistor (datasheet Table 3, 5 V / 2x 47 uF)"))
    for ref, (val, a, b, source) in zip(r, ress):
        x = res(val, f"{source} [{name}]", ref)
        x[1] += a
        x[2] += b
