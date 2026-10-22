"""V1 `power` board (28.5 x 21.5 mm, 4 layers, 1.2 mm) lying flat under `main`:
  - U1 + L1 + D1: TPS54360 (60 V) 12-32 V -> +5V for the electronics (XIAO, sensors, PMSA003I fan, crown MCU / AFE)
  - U2 + L2 + D2: TPS54360 (60 V) 12-32 V -> +12V_HEAT (12.0 V regulated above ~13.5 V input, VIN - ~1 V below it):
    reflector heater 4.8 W PWM, probe heater 0.42 W, and the crown transducer H-bridge drivers (via crown J1)
    Both switch at 743 kHz (away from the AS3935 500 kHz band) and survive the 58 V SMAJ36A surge clamp.
    Separate rails: heater PWM steps and heater / cable faults cannot disturb or brown out the electronics.
  - U3: AP2112K-3.3 600 mA LDO from +5V -> +3V3_PROBE, a dedicated rail for the probe board (SCD41 205 mA
    measurement peaks, SGP41, SHT40, BMP585) so they do not load the XIAO's 3V3 regulator during Wi-Fi TX peaks.
  - J1: 10-pin JST-SH (BM10B) along the top edge; a short cable rises past main's edge to main J5.
"""

from blocks import power_supply
from common import block, part, power, power_flag, src
from skidl import Net

JST_SH10 = "Connector_JST:JST_SH_BM10B-SRSS-TB_1x10-1MP_P1.00mm_Vertical"
FP_SOT23_5 = "Package_TO_SOT_SMD:SOT-23-5"
# 2 contacts per rail (JST-SH 1 A each): VIN_PROT <= 0.9 A at 12 V, +5V <= 0.6 A, +12V_HEAT <= 0.6 A; GND (3 contacts)
# carries the difference between the load returns and the input return (< 1.2 A); +3V3_PROBE <= 0.25 A on one.
POWER_LINK = ("VIN_PROT", "VIN_PROT", "GND", "GND", "+5V", "+5V", "+12V_HEAT", "+12V_HEAT", "GND", "+3V3_PROBE")


def build():
    from variants.v1.main import wire
    gnd = power("GND")
    vin = Net("VIN_PROT")
    v5 = power("+5V")
    heat5 = Net("+12V_HEAT")
    v3p = Net("+3V3_PROBE")
    for n in (gnd, vin, v5, heat5, v3p):
        power_flag(n)
    block("int_main")
    j1 = part("Connector", "Conn_01x10_Pin", "Main board", JST_SH10,
              src(mpn="BM10B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160396",
                  source="10-pin JST-SH power cable to main J5 (VIN_PROT in, +5V / +12V_HEAT / +3V3_PROBE out)"),
              ref="J1")
    wire(j1, POWER_LINK, {"VIN_PROT": vin, "GND": gnd, "+5V": v5, "+12V_HEAT": heat5, "+3V3_PROBE": v3p})
    block("ldo_probe")
    u3 = part("Regulator_Linear", "AP2112K-3.3", "AP2112K-3.3", FP_SOT23_5,
              src(mpn="AP2112K-3.3TRG1", mfr="Diodes Inc.", lcsc="C51118",
                  source="600 mA 3.3 V LDO, -40..+85 C: dedicated probe rail from +5V (0.25 V dropout at 600 mA)"),
              ref="U3")
    u3["1"] += v5     # VIN
    u3["2"] += gnd
    u3["3"] += v5     # EN: always on
    u3["5"] += v3p    # VOUT
    c_in = power_supply.cap("1u 50V", "AP2112K input capacitor (datasheet >= 1 uF)", "C19")
    c_in[1] += v5
    c_in[2] += gnd
    c_out = power_supply.cap("10u 10V", "AP2112K output capacitor: holds the SCD41 205 mA pulses at the far end of the probe cable", "C20")
    c_out[1] += v3p
    c_out[2] += gnd
    power_supply.tps54360("logic", vin, v5, gnd, "U1", "L1", "D1",
                          [f"C{i}" for i in range(1, 10)], [f"R{i}" for i in range(1, 7)])
    power_supply.tps54360("heat", vin, heat5, gnd, "U2", "L2", "D2",
                          [f"C{i}" for i in range(10, 19)], [f"R{i}" for i in range(7, 13)], volts=12.0)


NET_CLASSES = {
    "SELV-POWER": ["GND", "VIN_PROT", "+5V", "+12V_HEAT", "+3V3_PROBE", "LOGIC_*", "HEAT_*", "Net-*"],
    "POWER-Power": ["VIN_PROT", "+5V", "+12V_HEAT", "+3V3_PROBE", "LOGIC_SW", "HEAT_SW"],
}
