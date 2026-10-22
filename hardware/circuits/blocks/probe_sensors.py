"""Pagoda Probe board circuit blocks (vertical stem of the T-assembly):
  - U3 (bottom intake zone, between the two milled slots): Sensirion SHT40-AD1F-R2 +/-0.2 C / +/-1.8 % RH sensor
    (I2C 0x44; same DFN-4 footprint as the SHT45)
  - U1 (middle zone, above milled slot): Sensirion SCD41-D-R2 photoacoustic true CO2 sensor (I2C 0x62)
  - Backside Closed-Loop Regulated 0.42 W Local Heater (B.Cu directly behind U1 SCD41 pad 21):
    * R1, R2: 2x 680R 1206 0.5 W resistors in PARALLEL on the +12V_HEAT rail (340 Ohm, 0.42 W at 12 V, 42 % of rating each);
      the heater current returns to main over CO2_HEAT_RTN through the 1 ohm monitor shunt (ADS1115 on main)
    * TH1: SDNT1608X103F3950FTF 10k 1% B=3950K NTC thermistor right between R1 & R2 on back of SCD41
    * U4 (LM2903DR2G on +12V_HEAT, 2-36 V) + Q1 (AO3400A, gate pulled to +3V3): analog thermostat holding SCD41 at +8 C
      during extreme cold (R7 470k positive feedback gives ~1.4 K hysteresis so the comparator does not chatter)
  - U2 (upper zone): Sensirion SGP41-D-R4 MOX multi-gas VOC Index + NOx Index sensor (I2C 0x59)
  - U5 (upper zone, next to U2): Bosch BMP585 gel-filled barometer (I2C 0x47) in the vented probe chamber,
    close to outside air for thunder pressure-wave timing
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
FP_SGP41 = "gateway_fp:Sensirion_SGP41_DFN-6"
FP_BMP585 = "gateway_fp:Bosch_BMP585_LGA-9"


def res(value, fields=None, footprint=R0603, ref=None):
    return part("Device", "R", value, footprint, fields, ref=ref)


def cap(value, fields=None, footprint=C0603, ref=None):
    return part("Device", "C", value, footprint, fields, ref=ref)


def build(gnd, v3, sda, scl, v5, heat_rtn):
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

    # --- Backside 0.42 W Parallel Redundant Heater (+12V_HEAT) + Closed-Loop NTC Thermostat Behind SCD41 ---
    block("co2_back_heater")
    co2_sw = Net("CO2_HEAT_SW")
    co2_ntc = Net("CO2_NTC")
    co2_vset = Net("CO2_VSET")
    co2_gate = Net("CO2_GATE")

    # 2x 680R 1206 0.5 W resistors in PARALLEL on +12V_HEAT (340 Ohm, 0.42 W total at 12 V, 0.21 W each)
    r1 = res("680R", src(source="Parallel 0.5 W 1206 backside heater 1 directly behind SCD41 pad 21"), footprint=R1206)
    r2 = res("680R", src(source="Parallel 0.5 W 1206 backside heater 2 directly behind SCD41 pad 21"), footprint=R1206)
    r1[1] += v5
    r1[2] += co2_sw
    r2[1] += v5
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

    # Closed-loop thermostat comparator U4 (LM2903, -40..+125 C) + low-side MOSFET switch Q1 (AO3400A).
    # Powered from +12V_HEAT (10.4-12 V): its input common-mode range (0..VCC-2.0 V over temperature) covers the
    # 2.27 V NTC/setpoint node; the open-collector output is pulled to +3V3 so Q1's gate never sees the 12 V rail.
    u4 = part("gateway", "LM393", "LM2903DR2G", FP_LM393,
              src(mpn="LM2903DR2G", mfr="onsemi", lcsc="C57474",
                  source="Closed-loop analog thermostat comparator (-40..+125 C) regulating SCD41 backside heater via TH1"),
              ref="U4")
    u4["8"] += v5
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
    c_u4 = cap("100n 50V", src(source="LM2903 thermostat decoupling (+12V_HEAT)"))
    c_u4[1] += v5
    c_u4[2] += gnd

    q1 = part("gateway", "AO3400A", "AO3400A", FP_AO3400,
              src(mpn="AO3400A", mfr="Alpha & Omega Semicon", lcsc="C20917",
                  source="Low-side MOSFET switching the parallel 0.42 W SCD41 backside heater array"),
              ref="Q1")
    q1["1"] += co2_gate
    q1["2"] += heat_rtn   # source -> CO2_HEAT_RTN -> 1 ohm monitor shunt on main
    q1["3"] += co2_sw
    r_hyst = res("470k", src(source="Thermostat positive feedback (OUT -> IN+, ~48 mV = ~1.4 K hysteresis)"), ref="R7")
    r_hyst[1] += co2_gate
    r_hyst[2] += co2_ntc

    # --- U2: Sensirion SGP41-D-R4 VOC + NOx Index Sensor (I2C 0x59) ---
    block("air_quality")
    u2 = part("gateway", "SGP41", "SGP41-D-R4", FP_SGP41,
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
    c_sht = cap("100n 50V", src(source="SHT40 decoupling"))
    c_sht[1] += v3
    c_sht[2] += gnd

    # --- U5: Bosch BMP585 Barometer (I2C 0x47: SDO=VDDIO, CSB=VDDIO -> I2C mode) ---
    block("baro")
    u5 = part("gateway", "BMP585", "BMP585", FP_BMP585,
              src(mpn="BMP585", mfr="Bosch Sensortec", lcsc="C18184976",
                  source="Bosch BMP585 gel-filled liquid-resistant barometer (-40..+85 C, <0.1 Pa RMS, I2C 0x47)"),
              ref="U5")
    u5["1"] += scl    # SCX
    u5["2"] += sda    # SDX
    u5["3"] += v3     # SDO -> high = 0x47
    u5["4"] += v3     # VDDIO
    u5["5"] += NC     # INT (FIFO polled over I2C)
    u5["6"] += gnd    # VSSIO/VSS
    u5["7"] += v3     # CSB -> high = I2C mode
    u5["8"] += v3     # VDD
    u5["9"] += NC     # laser-mark pad, solder-resist covered
    c_bmp = cap("100n 50V", src(source="BMP585 VDD/VDDIO decoupling"))
    c_bmp[1] += v3
    c_bmp[2] += gnd
