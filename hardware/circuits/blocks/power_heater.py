"""12-32 V DC roof input protection, VIN_ADC supply monitor, and de-icing heater MOSFET driver (AO3400A).
The two supply rails (+5V electronics, +12V_HEAT heaters / transducer drive) come from the separate `power` board
(two 60 V TPS54360 bucks) over main J5; no 12-32 V conductor leaves the base bay. Every part on VIN is rated >= 60 V (TVS clamp 58.1 V)."""

from common import block, part, power_flag, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
D_SMA = "Diode_SMD:D_SMA"
FUSE2920 = "Fuse:Fuse_2920_7451Metric"
TERM_KF250_4P = "gateway_fp:TerminalBlock_Kefa_KF250-3.5-4P"
SOT23_AO3400 = "gateway_fp:AOS_AO3400A_SOT-23"


def res(value, fields=None, footprint=R0603, ref=None):
    return part("Device", "R", value, footprint, fields, ref=ref)


def cap(value, fields=None, footprint=C0603, ref=None):
    return part("Device", "C", value, footprint, fields, ref=ref)


def build(gnd, heat_pwm):
    """Build the 12-32 V + RS-485 terminal J1 with input protection, the VIN_ADC divider and the heater switch."""
    vin_raw = Net("VIN_RAW")
    vin_fused = Net("VIN_FUSED")
    vin_prot = Net("VIN_PROT")
    rs485_a = Net("RS485_A")
    rs485_b = Net("RS485_B")
    v5 = Net("+5V")
    heat5 = Net("+12V_HEAT")
    vin_adc = Net("VIN_ADC")
    heat_sw = Net("HEAT_SW")
    heat_gate = Net("HEAT_GATE")
    heat_sense = Net("HEAT_SENSE")
    for n in (vin_raw, vin_fused, vin_prot, v5, heat5):
        power_flag(n)

    # --- 4-pole push-in terminal J1 (1=VIN+ Red, 2=GND Black, 3=RS485_A+ White, 4=RS485_B- Yellow) ---
    block("power_in")
    j1 = part("Connector", "Screw_Terminal_01x04", "12-32V + RS485", TERM_KF250_4P,
              src(mpn="KF250-3.5-4P-1", mfr="Cixi Kefa Elec", lcsc="C475156",
                  source="4-pole 3.5 mm push-in spring terminal (1:VIN+ Red, 2:GND Black, 3:RS485_A+ White, 4:RS485_B- Yellow)"),
              ref="J1")
    j1[1] += vin_raw
    j1[2] += gnd
    j1[3] += rs485_a
    j1[4] += rs485_b

    f1 = part("Device", "Polyfuse", "1.1A", FUSE2920,
              src(mpn="2920L110/60V", mfr="Littelfuse", lcsc="C49318488",
                  source="1.1 A hold / 60 V resettable PTC fuse (rated above the 58 V TVS clamp)"),
              ref="F1")
    f1[1] += vin_raw
    f1[2] += vin_fused

    # SMAJ36A: 36 V stand-off (> 32 V max input), 40 V min breakdown, 58.1 V clamp < 60 V TPS54360 rating
    d_tvs = part("Device", "D_Zener", "SMAJ36A", D_SMA,
                 src(mpn="SMAJ36A", mfr="MDD", lcsc="C113967",
                     source="36 V unidirectional TVS surge suppressor on roof DC input (58.1 V clamp)"),
                 ref="D1")
    d_tvs["K"] += vin_prot
    d_tvs["A"] += gnd

    d_rev = part("Device", "D_Schottky", "SS16", D_SMA,
                 src(mpn="SS16", mfr="MDD", lcsc="C2481",
                     source="Reverse-polarity protection Schottky diode (60 V, 1 A)"),
                 ref="D2")
    d_rev["A"] += vin_fused
    d_rev["K"] += vin_prot

    # 100 V X7R MLCC input reservoir: rated above the 58 V TVS clamp (a 50 V part would be over-stressed in a surge)
    c_bulk = part("Device", "C", "2u2 100V", C1206,
                  src(source="100 V X7R ceramic input reservoir on VIN_PROT (-55..+125 C, no electrolyte dry-out)"),
                  ref="C1")
    c_bulk[1] += vin_prot
    c_bulk[2] += gnd

    # --- VIN_ADC 12-32 V Supply Voltage Monitor (100k / 10k -> 11:1 ratio) ---
    block("vin_sense")
    r_vin_top = res("100k", src(source="VIN_PROT 11:1 ADC divider top (32 V -> 2.91 V)"))
    r_vin_top[1] += vin_prot
    r_vin_top[2] += vin_adc
    r_vin_bot = res("10k", src(source="VIN_PROT 11:1 ADC divider bottom"))
    r_vin_bot[1] += vin_adc
    r_vin_bot[2] += gnd
    c_vin_adc = cap("100n 50V", src(source="VIN_ADC filter capacitor"))
    c_vin_adc[1] += vin_adc
    c_vin_adc[2] += gnd

    # --- Constant-Wattage De-Icing Heater PWM Switch (AO3400A) ---
    block("heater_drv")
    q1 = part("gateway", "AO3400A", "AO3400A", SOT23_AO3400,
              src(mpn="AO3400A", mfr="Alpha & Omega Semicon", lcsc="C20917",
                  source="30 V 5.7 A N-MOSFET low-side PWM switch for reflector de-icing heater"),
              ref="Q1")
    r_gate = res("330R", src(source="Heater MOSFET gate series resistor"))
    r_gate[1] += heat_pwm
    r_gate[2] += heat_gate
    r_gpd = res("100k", src(source="Heater MOSFET gate pull-down (off during boot)"))
    r_gpd[1] += heat_gate
    r_gpd[2] += gnd
    q1["1"] += heat_gate
    q1["2"] += heat_sense   # source -> R16 0.1 ohm current shunt -> GND (main_sensors heater_mon block)
    q1["3"] += heat_sw

    d_fly = part("Device", "D_Schottky", "SS16", D_SMA,
                 src(mpn="SS16", mfr="MDD", lcsc="C2481",
                     source="Freewheeling flyback Schottky across heater cable inductance (to the +12V_HEAT rail)"),
                 ref="D3")
    d_fly["A"] += heat_sw
    d_fly["K"] += heat5

    return {
        "vin_prot": vin_prot,
        "v5": v5,
        "heat5": heat5,
        "vin_adc": vin_adc,
        "heat_sw": heat_sw,
        "heat_sense": heat_sense,
        "rs485_a": rs485_a,
        "rs485_b": rs485_b,
    }
