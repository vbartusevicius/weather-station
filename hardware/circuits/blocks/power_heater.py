"""12-32 V DC roof input protection, 40 V -> 5 V buck (SCT2400), VIN_ADC supply
monitor, and constant-wattage de-icing heater MOSFET driver (AO3400A)."""

from common import block, part, power_flag, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
D_SMA = "Diode_SMD:D_SMA"
TSOT23_6 = "Package_TO_SOT_SMD:TSOT-23-6"
FUSE1812 = "Fuse:Fuse_1812_4532Metric"
TERM_KF250_4P = "gateway_fp:TerminalBlock_Kefa_KF250-3.5-4P"
L_ANR4012 = "Inductor_SMD:L_APV_ANR4012"
SOT23_AO3400 = "gateway_fp:AOS_AO3400A_SOT-23"


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def build(gnd, heat_pwm):
    """Build 12-32 V + RS-485 4-pole terminal J1, 5 V buck, VIN_ADC divider, and heater switch."""
    vin_raw = Net("VIN_RAW")
    vin_fused = Net("VIN_FUSED")
    vin_prot = Net("VIN_PROT")
    rs485_a = Net("RS485_A")
    rs485_b = Net("RS485_B")
    v5 = Net("+5V")
    vin_adc = Net("VIN_ADC")
    heat_sw = Net("HEAT_SW")
    buck_sw = Net("BUCK5_SW")
    buck_bst = Net("BUCK5_BST")
    buck_en = Net("BUCK5_EN")
    buck_fb = Net("BUCK5_FB")
    heat_gate = Net("HEAT_GATE")
    for n in (vin_raw, vin_fused, vin_prot, v5):
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

    f1 = part("Device", "Polyfuse", "1.1A", FUSE1812,
              src(mpn="1812L110/33MR", mfr="Littelfuse", lcsc="C142747",
                  source="1.1 A hold / 33 V resettable PTC fuse"),
              ref="F1")
    f1[1] += vin_raw
    f1[2] += vin_fused

    d_tvs = part("Device", "D_Zener", "SMAJ33A", D_SMA,
                 src(mpn="SMAJ33A", mfr="MDD", lcsc="C113966",
                     source="33 V unidirectional TVS surge suppressor on roof DC input"),
                 ref="D1")
    d_tvs["K"] += vin_prot
    d_tvs["A"] += gnd

    d_rev = part("Device", "D_Schottky", "SS16", D_SMA,
                 src(mpn="SS16", mfr="MDD", lcsc="C2481",
                     source="Reverse-polarity protection Schottky diode (60 V, 1 A)"),
                 ref="D2")
    d_rev["A"] += vin_fused
    d_rev["K"] += vin_prot

    # Solid-state 50 V X7R MLCC ceramic input bank (paired with C4 10u 50V on VIN_PROT; -55..+125 C, 10-year roof longevity)
    c_bulk = part("Device", "C", "10u 50V", C0805,
                  src(source="Solid-state 50 V X7R ceramic input reservoir (-55..+125 C, zero electrolyte dry-out)"),
                  ref="C1")
    c_bulk[1] += vin_prot
    c_bulk[2] += gnd

    # --- SCT2400 12-32 V -> +5V Buck Converter ---
    block("buck5v")
    u1 = part("gateway", "SCT2400", "SCT2400TVBR", TSOT23_6,
              src(mpn="SCT2400TVBR", mfr="SCT", lcsc="C2693819",
                  source="SCT2400 4.5-40 V input, 600 mA synchronous buck converter"),
              ref="U1")
    u1["VIN"] += vin_prot
    u1["GND"] += gnd
    u1["EN"] += buck_en
    u1["BST"] += buck_bst
    u1["SW"] += buck_sw
    u1["FB"] += buck_fb

    # EN UVLO divider: 51k / 10k -> turn-on at 1.21 * 6.1 = 7.4 V (supports 12-32 V)
    r_en_top = res("51k", src(source="SCT2400 EN UVLO top (51k/10k -> 7.4 V turn-on)"))
    r_en_top[1] += vin_prot
    r_en_top[2] += buck_en
    r_en_bot = res("10k", src(source="SCT2400 EN UVLO bottom"))
    r_en_bot[1] += buck_en
    r_en_bot[2] += gnd

    c_bst = cap("100n 50V", src(source="SCT2400 bootstrap capacitor"))
    c_bst[1] += buck_bst
    c_bst[2] += buck_sw

    l1 = part("Device", "L", "4.7uH", L_ANR4012,
              src(mpn="SMNR4012-4R7MT", mfr="Sunna", lcsc="C135248",
                  source="4.7 uH 1.15 A shielded power inductor"),
              ref="L1")
    l1[1] += buck_sw
    l1[2] += v5

    r_fb_top = res("51k", src(source="SCT2400 FB top: VOUT = 0.81*(1+51k/10k) = 4.94 V"))
    r_fb_top[1] += v5
    r_fb_top[2] += buck_fb
    r_fb_bot = res("10k", src(source="SCT2400 FB bottom"))
    r_fb_bot[1] += buck_fb
    r_fb_bot[2] += gnd

    c_cin = cap("10u 50V", src(source="SCT2400 input decoupling"), footprint=C0805)
    c_cin[1] += vin_prot
    c_cin[2] += gnd
    for val in ("22u 25V", "22u 25V", "100n 50V"):
        c_out = cap(val, src(source="SCT2400 output decoupling"),
                    footprint=C0805 if "22u" in val else C0603)
        c_out[1] += v5
        c_out[2] += gnd

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
    q1["2"] += gnd
    q1["3"] += heat_sw

    d_fly = part("Device", "D_Schottky", "SS16", D_SMA,
                 src(mpn="SS16", mfr="MDD", lcsc="C2481",
                     source="Freewheeling flyback Schottky across heater cable inductance"),
                 ref="D3")
    d_fly["A"] += heat_sw
    d_fly["K"] += vin_prot

    return {
        "vin_prot": vin_prot,
        "v5": v5,
        "vin_adc": vin_adc,
        "heat_sw": heat_sw,
        "rs485_a": rs485_a,
        "rs485_b": rs485_b,
    }
