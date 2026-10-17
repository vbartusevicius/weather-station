"""V1 elevated 12.0 x 12.0 mm UV/Light Micro-PCB (`dome`, 4 layers, 1.2 mm)
raised on a vertical 4-pin board-to-board riser (`crown J10 <-> dome J1`) at y = 195.2 mm
so U1 (LTR-390UV-01) and U2 (VEML7700-TT) sit directly inside the hollow JGS1 Fused Silica
Hemispherical Dome above the roof peak with a full 180-deg (0-deg horizon) sky view!"""

from builtins import NC

from common import block, part, power, power_flag, src
from skidl import Net

from variants.v1.crown import DOME_B2B_LINK
from variants.v1.main import wire

C0603 = "Capacitor_SMD:C_0603_1608Metric"
B2B_HDR4 = "gateway_fp:HX_PZ2.54-1x4P-SMD"
FP_LTR390 = "gateway_fp:LiteOn_LTR-390UV-01"
FP_VEML7700 = "gateway_fp:Vishay_VEML7700-TT"


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
                  source="4-pin 2.54 mm SMD riser receptor on bottom of 12x12 mm `dome` Micro-PCB"),
              ref="J1")
    wire(j1, DOME_B2B_LINK, nets)

    block("uv_als")
    u1 = part("gateway", "LTR390UV", "LTR-390UV-01", FP_LTR390,
              src(mpn="LTR-390UV-01", mfr="Lite-On", hand=True, footprint_ref="C492374",
                  source="Direct UV-A/UV-B index sensor elevated inside hollow JGS1 hemisphere (I2C 0x53)"),
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

    u2 = part("gateway", "VEML7700", "VEML7700-TT", FP_VEML7700,
              src(mpn="VEML7700-TT", mfr="Vishay", lcsc="C1850416",
                  source="High-accuracy ambient light / solar irradiance sensor elevated inside JGS1 hemisphere (I2C 0x10)"),
              ref="U2")
    u2["1"] += sda
    u2["2"] += v3
    u2["3"] += gnd
    u2["4"] += scl
    c2 = cap("100n 50V", src(source="VEML7700 decoupling"))
    c2[1] += v3
    c2[2] += gnd


NET_CLASSES = {
    "SELV-DOME": ["GND", "+3V3", "I2C_SDA", "I2C_SCL", "Net-*"],
    "DOME-Power": ["+3V3"],
}
