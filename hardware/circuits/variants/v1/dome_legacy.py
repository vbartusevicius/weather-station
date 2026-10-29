"""V1 LEGACY dome (`dome_legacy`): the original elevated Ø 13.6 mm 3-sensor UV/Light/NIR micro-PCB, kept as a backup
for the photodiode `dome` (same outline, same 4-pin riser `crown J10 <-> dome J1`). U1 (LTR-390UV-01, discontinued),
U2 (ams OSRAM TSL25911FN Vis+NIR+188uLux) and U3 (GenUV GUVB-C31SM AlGaN UV-B, obsolete). The station firmware
detects which dome is fitted (ADS1115 at 0x49 = photodiode dome; LTR-390 0x53 / GUVB-C31SM 0x39 = this one)."""

from builtins import NC

from common import block, part, power, power_flag, src
from skidl import Net

from variants.v1.crown import DOME_B2B_LINK
from variants.v1.main import wire

C0603 = "Capacitor_SMD:C_0603_1608Metric"
B2B_HDR4 = "gateway_fp:HX_PZ2.54-1x4P-SMD"
FP_LTR390 = "gateway_fp:LiteOn_LTR-390UV-01"
FP_TSL25911 = "gateway_fp:AMS_TSL25911FN"
FP_GUVB = "gateway_fp:GenUV_GUVB-C31SM"


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def build():
    gnd = power("GND")
    v3 = power("+3V3")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    for n in (gnd, v3):
        power_flag(n)

    nets = {"+3V3": v3, "GND": gnd, "I2C_SDA": sda, "I2C_SCL": scl}
    block("int_crown")
    j1 = part("Connector_Generic", "Conn_01x04", "CROWN_RISER", B2B_HDR4,
              src(mpn="HX PZ2.54-1x4P TP-YQ", mfr="HCTL", lcsc="C41417361",
                  source="4-pin 2.54 mm SMD riser receptor on bottom of Ø13.6 mm `dome` Micro-PCB"),
              ref="J1")
    wire(j1, DOME_B2B_LINK, nets)

    block("uv_als")
    # U1: Lite-On LTR-390UV-01 (I2C 0x53) — UV-A/upper-UV-B + 157,000 lux high-noon ALS
    u1 = part("gateway", "LTR390UV", "LTR-390UV-01", FP_LTR390,
              src(mpn="LTR-390UV-01", mfr="Lite-On", hand=True, footprint_ref="C492374",
                  source="UV-A/B + 157k lux ALS sensor elevated inside hollow JGS1 hemisphere (I2C 0x53)"),
              ref="U1")
    u1["1"] += v3
    u1["2"] += gnd
    u1["3"] += gnd
    u1["4"] += scl
    u1["5"] += NC
    u1["6"] += sda
    c1 = cap("100n 50V", src(source="LTR-390UV decoupling"))
    c1[1] += v3
    c1[2] += gnd

    # U2: ams OSRAM TSL25911FN (LCSC C176812, I2C 0x29) — UV-rejection package, Visible + Near-IR (700-1100 nm) + 188 uLux night sky
    u2 = part("gateway", "TSL25911", "TSL25911FN", FP_TSL25911,
              src(mpn="TSL25911FN", mfr="ams OSRAM", lcsc="C176812",
                  source="UV-rejection dual-diode Visible + Near-IR (700-1100 nm) & 188 uLux night-sky sensor (I2C 0x29)"),
              ref="U2")
    u2["1"] += scl
    u2["2"] += NC
    u2["3"] += gnd
    u2["4"] += gnd
    u2["5"] += v3
    u2["6"] += sda
    c2 = cap("100n 50V", src(source="TSL25911FN decoupling"))
    c2[1] += v3
    c2[2] += gnd

    # U3: GenUV GUVB-C31SM (I2C 0x39, hand-populated, fitted by default) — True AlGaN wide-bandgap UV-B (240-320 nm) sensor
    u3 = part("gateway", "GUVB_C31SM", "GUVB-C31SM", FP_GUVB,
              src(mpn="GUVB-C31SM", mfr="Genicom", hand=True,
                  source="AlGaN true UV-B (240-320 nm) digital I2C sensor on dome PCB (I2C 0x39)"),
              ref="U3")
    u3["1"] += gnd
    u3["2"] += sda
    u3["3"] += scl
    u3["4"] += v3
    # Datasheet application circuit: C_VCC = 1 uF placed next to pin 4
    c3 = cap("1u 50V", src(source="GUVB-C31SM VCC decoupling (1 uF per datasheet Table 5.1)"))
    c3[1] += v3
    c3[2] += gnd


NET_CLASSES = {
    "SELV-DOME": ["GND", "+3V3", "I2C_SDA", "I2C_SCL", "Net-*"],
    "DOME-Power": ["+3V3"],
}
