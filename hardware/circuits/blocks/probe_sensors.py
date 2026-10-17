"""Pagoda Probe board circuit blocks (vertical stem of the T-assembly):
  - U3 (bottom intake zone, below milled slot): Sensirion SHT45-AD1F-R2 +/-0.1 C / +/-1.0% RH sensor (I2C 0x44)
  - U1 (middle zone, above milled slot): Sensirion SCD41-D-R2 photoacoustic true CO2 sensor (I2C 0x62)
  - Backside Closed-Loop Regulated 0.5 W Local Heater (B.Cu directly behind U1 SCD41 pad 21):
    * R1, R2: 2x 470R 1206 0.25 W resistors wired in PARALLEL (0.50 W rated total, 235 Ohm net, full redundancy)
    * TH1: SDNT1608X103F3950FTF 10k 1% B=3950K NTC thermistor right between R1 & R2 on back of SCD41
    * U4 (LM393DR2G) + Q1 (AO3400A): Closed-loop analog thermostat regulating SCD41 backside to +8 C during extreme cold
  - U2 (upper zone): Sensirion SGP41-D-R4 MOX multi-gas VOC Index + NOx Index sensor (I2C 0x59)
"""

from builtins import NC

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
FP_SCD41 = "gateway_fp:Sensirion_SCD41_LGA-20"
FP_SHT45 = "gateway_fp:Sensirion_SHT45_DFN-4"
FP_LM393 = "gateway_fp:ON_LM393DR2G_SOIC-8"
FP_AO3400 = "gateway_fp:AOS_AO3400A_SOT-23"


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def build(gnd, v3, sda, scl, vin_prot):
    # --- U1: Sensirion SCD41-D-R2 True CO2 Sensor (I2C 0x62) ---
    block("air_quality")
    u1 = part("gateway", "SCD41", "SCD41-D-R2", FP_SCD41,
              src(mpn="SCD41-D-R2", mfr="Sensirion", lcsc="C3659294",
                  source="Sensirion SCD41-D-R2 photoacoustic CO2 sensor (400-5000 ppm, I2C 0x62)"),
              ref="U1")
    for dnc_pin in ("1", "2", "3", "4", "5", "8", "11", "12", "13", "14", "15", "16", "17", "18"):
        u1[dnc_pin] += NC
    for gnd_pin in ("6", "20", "21"):
        u1[gnd_pin] += gnd
    u1["7"] += v3
    u1["19"] += v3
    u1["9"] += scl
    u1["10"] += sda
    c_scd1 = cap("10u 10V", src(source="SCD41 VDDH bulk decoupling"))
    c_scd1[1] += v3
    c_scd1[2] += gnd
    c_scd2 = cap("100n 50V", src(source="SCD41 VDD high-frequency decoupling"))
    c_scd2[1] += v3
    c_scd2[2] += gnd

    # --- Backside 0.5 W Parallel Redundant Heater + Closed-Loop NTC Thermostat Behind SCD41 ---
    block("co2_back_heater")
    co2_sw = Net("CO2_HEAT_SW")
    co2_ntc = Net("CO2_NTC")
    co2_vset = Net("CO2_VSET")
    co2_gate = Net("CO2_GATE")

    # 2x 470R 1206 0.25 W resistors in PARALLEL (0.50 W total rated capacity, 235 Ohm net, full redundancy)
    r1 = res("470R", src(source="Parallel 0.25 W 1206 backside heater 1 directly behind SCD41 pad 21"), footprint=R1206)
    r2 = res("470R", src(source="Parallel 0.25 W 1206 backside heater 2 directly behind SCD41 pad 21"), footprint=R1206)
    r1[1] += vin_prot
    r1[2] += co2_sw
    r2[1] += vin_prot
    r2[2] += co2_sw

    # TH1: 10k 1% B3950 NTC thermistor on B.Cu right between R1 and R2 measuring SCD41 backside temperature
    th1 = part("Device", "Thermistor_NTC", "10k NTC", R0603,
               src(mpn="SDNT1608X103F3950FTF", mfr="Sunlord", lcsc="C279936",
                   source="10k 1% B=3950K NTC thermistor on B.Cu measuring SCD41 backside temperature"),
               ref="TH1")
    r_ntc_pu = res("10k", src(source="10k 1% reference pull-up for SCD41 backside NTC thermistor"))
    r_ntc_pu[1] += v3
    r_ntc_pu[2] += co2_ntc
    th1[1] += co2_ntc
    th1[2] += gnd

    # Setpoint divider (10k / 22k -> V_SET = 2.27 V = +8.0 C threshold)
    r_set_top = res("10k", src(source="SCD41 thermostat +8 C setpoint divider top"))
    r_set_bot = res("22k", src(source="SCD41 thermostat +8 C setpoint divider bottom"))
    r_set_top[1] += v3
    r_set_top[2] += co2_vset
    r_set_bot[1] += co2_vset
    r_set_bot[2] += gnd

    # Closed-loop thermostat comparator U4 (LM393) + low-side MOSFET switch Q1 (AO3400A)
    u4 = part("gateway", "LM393", "LM393DR2G", FP_LM393,
              src(mpn="LM393DR2G", mfr="onsemi", lcsc="C7955",
                  source="Closed-loop analog thermostat comparator regulating SCD41 backside heater via TH1"),
              ref="U4")
    u4["8"] += v3
    u4["4"] += gnd
    u4["3"] += co2_ntc   # IN/A+ (rises above V_SET when T_SCD41 < +8 C)
    u4["2"] += co2_vset  # IN/A- (+8 C threshold)
    u4["1"] += co2_gate  # OUT/A (releases high when T_SCD41 < +8 C -> turns Q1 ON)
    u4["5"] += co2_vset
    u4["6"] += gnd
    u4["7"] += NC
    r_gate_pu = res("10k", src(source="Thermostat open-collector gate pull-up"))
    r_gate_pu[1] += v3
    r_gate_pu[2] += co2_gate
    c_u4 = cap("100n 50V", src(source="LM393 thermostat decoupling"))
    c_u4[1] += v3
    c_u4[2] += gnd

    q1 = part("gateway", "AO3400A", "AO3400A", FP_AO3400,
              src(mpn="AO3400A", mfr="Alpha & Omega Semicon", lcsc="C20917",
                  source="Low-side MOSFET switching the parallel 0.5 W SCD41 backside heater array"),
              ref="Q1")
    q1["1"] += co2_gate
    q1["2"] += gnd
    q1["3"] += co2_sw

    # --- U2: Sensirion SGP41-D-R4 VOC + NOx Index Sensor (I2C 0x59) ---
    block("air_quality")
    u2 = part("gateway", "SGP41", "SGP41-D-R4", "gateway_fp:Sensirion_SGP41_DFN-6",
              src(mpn="SGP41-D-R4", mfr="Sensirion", lcsc="C3659325",
                  source="Sensirion SGP41-D-R4 MOX VOC + NOx multi-gas sensor (I2C 0x59)"),
              ref="U2")
    u2["1"] += v3
    u2["2"] += gnd
    u2["3"] += sda
    u2["4"] += gnd
    u2["5"] += v3
    u2["6"] += scl
    u2["7"] += gnd
    c_sgp = cap("100n 50V", src(source="SGP41 decoupling"))
    c_sgp[1] += v3
    c_sgp[2] += gnd

    # --- U3: Sensirion SHT40-AD1F-R2 Temp & Humidity Sensor (I2C 0x44) ---
    block("thermo_hygro")
    u3 = part("gateway", "SHT45", "SHT40-AD1F-R2", FP_SHT45,
              src(mpn="SHT40-AD1F-R2", mfr="Sensirion", lcsc="C7461846",
                  source="Sensirion SHT40-AD1F-R2 +/-0.2 C, +/-1.8% RH sensor with IP67 PTFE membrane (I2C 0x44)"),
              ref="U3")
    u3["1"] += sda
    u3["2"] += scl
    u3["3"] += v3
    u3["4"] += gnd
    u3["5"] += gnd
    c_sht = cap("100n 50V", src(source="SHT45 decoupling"))
    c_sht[1] += v3
    c_sht[2] += gnd
