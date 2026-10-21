"""V1 `power` board (28.5 x 21.5 mm, 4 layers, 1.2 mm) lying flat under `main`:
  - U1 + L1 + D1: TPS54360 (60 V) 12-32 V -> +5V for the electronics (XIAO, sensors, PMSA003I fan, crown MCU / AFE)
  - U2 + L2 + D2: TPS54360 (60 V) 12-32 V -> +12V_HEAT (12.0 V regulated above ~13.5 V input, VIN - ~1 V below it):
    reflector heater 4.8 W PWM, probe heater 0.42 W, and the crown transducer H-bridge drivers (via crown J1)
    Both switch at 743 kHz (away from the AS3935 500 kHz band) and survive the 58 V SMAJ36A surge clamp.
    Separate rails: heater PWM steps and heater / cable faults cannot disturb or brown out the electronics.
  - J1: 8-pin JST-SH (BM08B) along the top edge; a short cable rises past main's edge to main J5.
"""

from blocks import power_supply
from common import block, part, power, power_flag, src
from skidl import Net

JST_SH8 = "Connector_JST:JST_SH_BM08B-SRSS-TB_1x08-1MP_P1.00mm_Vertical"
# 2 contacts per rail (JST-SH 1 A each): VIN_PROT <= 0.9 A at 12 V, +5V <= 0.6 A, +12V_HEAT <= 0.6 A; GND carries the
# difference between the load returns and the input return (< 1.2 A).
POWER_LINK = ("VIN_PROT", "VIN_PROT", "GND", "GND", "+5V", "+5V", "+12V_HEAT", "+12V_HEAT")


def build():
    from variants.v1.main import wire
    gnd = power("GND")
    vin = Net("VIN_PROT")
    v5 = power("+5V")
    heat5 = Net("+12V_HEAT")
    for n in (gnd, vin, v5, heat5):
        power_flag(n)
    block("int_main")
    j1 = part("Connector", "Conn_01x08_Pin", "Main board", JST_SH8,
              src(mpn="BM08B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160394",
                  source="8-pin JST-SH power cable to main J5 (VIN_PROT in, +5V / +12V_HEAT out)"),
              ref="J1")
    wire(j1, POWER_LINK, {"VIN_PROT": vin, "GND": gnd, "+5V": v5, "+12V_HEAT": heat5})
    power_supply.tps54360("logic", vin, v5, gnd, "U1", "L1", "D1",
                          [f"C{i}" for i in range(1, 10)], [f"R{i}" for i in range(1, 7)])
    power_supply.tps54360("heat", vin, heat5, gnd, "U2", "L2", "D2",
                          [f"C{i}" for i in range(10, 19)], [f"R{i}" for i in range(7, 13)], volts=12.0)


NET_CLASSES = {
    "SELV-POWER": ["GND", "VIN_PROT", "+5V", "+12V_HEAT", "LOGIC_*", "HEAT_*", "Net-*"],
    "POWER-Power": ["VIN_PROT", "+5V", "+12V_HEAT", "LOGIC_SW", "HEAT_SW"],
}
