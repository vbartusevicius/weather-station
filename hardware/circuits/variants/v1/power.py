"""V1 `power` board (28.5 x 30.0 mm, 4 layers, 1.2 mm) lying flat 1 mm above the base-bay floor under `main`:
  - U1 + L1: SCT2430 12-32 V -> +5V / 3.5 A for the electronics (XIAO, sensors, PMSA003I fan, crown AFE)
  - U2 + L2: SCT2430 12-32 V -> +5V_HEAT / 3.5 A for the heaters (reflector 3.5 W PWM, probe 0.42 W, spare crown pair)
    Separate rails: heater PWM steps and heater / cable faults cannot disturb or brown out the electronics.
  - J1: 8-pin JST-SH (BM08B) next to the board's outer edge; a short cable rises past main's edge to main J5.
"""

from blocks import power_supply
from common import block, part, power, power_flag, src
from skidl import Net

JST_SH8 = "Connector_JST:JST_SH_BM08B-SRSS-TB_1x08-1MP_P1.00mm_Vertical"
# 2 contacts per rail (JST-SH 1 A each): VIN_PROT <= 0.7 A at 12 V, +5V <= 0.6 A, +5V_HEAT <= 0.9 A; GND carries the
# difference between the 5 V load returns and the input return (< 1.2 A).
POWER_LINK = ("VIN_PROT", "VIN_PROT", "GND", "GND", "+5V", "+5V", "+5V_HEAT", "+5V_HEAT")


def build():
    from variants.v1.main import wire
    gnd = power("GND")
    vin = Net("VIN_PROT")
    v5 = power("+5V")
    heat5 = Net("+5V_HEAT")
    for n in (gnd, vin, v5, heat5):
        power_flag(n)
    block("int_main")
    j1 = part("Connector", "Conn_01x08_Pin", "Main board", JST_SH8,
              src(mpn="BM08B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160394",
                  source="8-pin JST-SH power cable to main J5 (VIN_PROT in, +5V / +5V_HEAT out)"),
              ref="J1")
    wire(j1, POWER_LINK, {"VIN_PROT": vin, "GND": gnd, "+5V": v5, "+5V_HEAT": heat5})
    power_supply.sct2430("logic", vin, v5, gnd, "U1", "L1",
                         [f"C{i}" for i in range(1, 9)], [f"R{i}" for i in range(1, 7)])
    power_supply.sct2430("heat", vin, heat5, gnd, "U2", "L2",
                         [f"C{i}" for i in range(9, 17)], [f"R{i}" for i in range(7, 13)])


NET_CLASSES = {
    "SELV-POWER": ["GND", "VIN_PROT", "+5V", "+5V_HEAT", "LOGIC_*", "HEAT_*", "Net-*"],
    "POWER-Power": ["VIN_PROT", "+5V", "+5V_HEAT", "LOGIC_SW", "HEAT_SW"],
}
