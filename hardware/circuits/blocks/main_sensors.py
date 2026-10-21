"""Main-board sensors (the BMP585 barometer lives on the probe, the AS3935 lightning detector on the crown):
  - U4: TI ADS1115 16-bit I2C ADC (0x48) measuring both heaters: reflector heater current (R16 0.1 ohm),
    probe heater current (R17 1 ohm in its CO2_HEAT_RTN return) and the +5V probe heater supply
  - U5: ST LSM6DS3TR-C 6-axis IMU for mast tilt compensation & rain vibration filtering (I2C 0x6A)
  - J4: 6-pin JST-ZH connector (B6B-ZR-SM4-TF) for the Plantower PMSA003I PM1.0/2.5/10 module (harness)
"""

from builtins import NC

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
FP_LSM6DS3 = "gateway_fp:ST_LSM6DS3TR-C_LGA-14"
FP_B6B_ZR = "gateway_fp:JST_B6B-ZR-SM4-TF"
FP_MSOP10 = "gateway_fp:TI_ADS1115_VSSOP-10"


def res(value, fields=None, footprint=R0603, ref=None):
    return part("Device", "R", value, footprint, fields, ref=ref)


def cap(value, fields=None, footprint=C0603, ref=None):
    return part("Device", "C", value, footprint, fields, ref=ref)


def build(gnd, v3, v5, sda, scl, heat_sense, co2_heat_rtn, heat5):
    # --- U5: ST LSM6DS3TR-C 6-Axis IMU (I2C 0x6A: SA0=GND, CS=VDDIO) ---
    block("imu")
    u5 = part("gateway", "LSM6DS3", "LSM6DS3TR-C", FP_LSM6DS3,
              src(mpn="LSM6DS3TR-C", mfr="STMicroelectronics", lcsc="C967633",
                  source="ST LSM6DS3TR-C 6-axis IMU for mast tilt & rain vibration cancellation (I2C 0x6A)"),
              ref="U5")
    u5["1"] += gnd    # SDO/SA0 -> 0x6A
    u5["2"] += gnd    # SDx (Mode 1: tied to GND)
    u5["3"] += gnd    # SCx (Mode 1: tied to GND)
    u5["4"] += NC     # INT1
    u5["5"] += v3     # VDDIO
    u5["6"] += gnd    # GND
    u5["7"] += gnd    # GND
    u5["8"] += v3     # VDD
    u5["9"] += NC     # INT2
    u5["10"] += NC    # NC
    u5["11"] += NC    # NC
    u5["12"] += v3    # CS -> high = I2C mode
    u5["13"] += scl   # SCL
    u5["14"] += sda   # SDA
    c_imu = cap("100n 50V", src(source="LSM6DS3TR-C decoupling"), ref="C9")
    c_imu[1] += v3
    c_imu[2] += gnd

    # --- J4: Plantower PMSA003I PM1.0/2.5/10 Module Header (JST-ZH 6P -> harness -> module 2x5 1.27 mm) ---
    # PMSA003I: 5 V fan supply, 3.3 V open-drain I2C (0x12, 100 kHz); SET/RESET have internal pull-ups and stay open.
    block("pm_sensor")
    j4 = part("gateway", "B6B_ZR", "PMSA003I PM", FP_B6B_ZR,
              src(mpn="B6B-ZR-SM4-TF(LF)(SN)", mfr="JST", lcsc="C471473",
                  source="6-pin JST-ZH SMD header for the Plantower PMSA003I particulate sensor harness (1:+5V, 2:GND, 3:SDA, 4:SCL, 5:NC, 6:NC)"),
              ref="J4")
    j4["1"] += v5
    j4["2"] += gnd
    j4["3"] += sda
    j4["4"] += scl
    j4["5"] += NC
    j4["6"] += NC
    c_sen = cap("10u 10V", src(source="PMSA003I +5V fan supply bypass (datasheet: 47 uF/10 V bulk at the module end of the harness)"),
                ref="C13")
    c_sen[1] += v5
    c_sen[2] += gnd

    # --- U4: TI ADS1115 heater power monitor (I2C 0x48: ADDR=GND) ---
    # AIN0: reflector heater shunt R16 (0.1 ohm, PWM current averaged by R18/C19 and the ADC's integrating conversion)
    # AIN1: probe heater shunt R17 (1 ohm in the CO2_HEAT_RTN return from the probe), filtered by R19/C20
    # AIN2: +12V_HEAT heater rail via R20/R21 (30k/10k, 1:4)  AIN3: +5V electronics rail via R24/R25 (1:2)
    block("heater_mon")
    mon_refl, mon_probe, mon_5v, mon_5v_logic = Net("MON_REFL"), Net("MON_PROBE"), Net("MON_5V"), Net("MON_5V_LOGIC")
    u4 = part("gateway", "ADS1115", "ADS1115IDGSR", FP_MSOP10,
              src(mpn="ADS1115IDGSR", mfr="Texas Instruments", lcsc="C37593",
                  source="16-bit 4-channel I2C ADC measuring reflector + probe heater current and the +5V rail (I2C 0x48)"),
              ref="U4")
    u4["1"] += gnd        # ADDR -> 0x48
    u4["2"] += NC         # ALERT/RDY (polled)
    u4["3"] += gnd
    u4["4"] += mon_refl   # AIN0
    u4["5"] += mon_probe  # AIN1
    u4["6"] += mon_5v     # AIN2
    u4["7"] += mon_5v_logic  # AIN3
    u4["8"] += v3
    u4["9"] += sda
    u4["10"] += scl
    c_mon = cap("100n 50V", src(source="ADS1115 VDD decoupling"), ref="C21")
    c_mon[1] += v3
    c_mon[2] += gnd
    r_refl = res("0R1", src(source="Reflector heater current shunt (Q1 source to GND, 0.25 W, <= 16 mW at 0.4 A)"),
                 footprint=R1206, ref="R16")
    r_refl[1] += heat_sense
    r_refl[2] += gnd
    r_probe = res("1R", src(source="Probe heater current shunt (CO2_HEAT_RTN to GND, 1.2 mW at 35 mA)"), ref="R17")
    r_probe[1] += co2_heat_rtn
    r_probe[2] += gnd
    for ref_r, ref_c, node, out in (("R18", "C19", heat_sense, mon_refl), ("R19", "C20", co2_heat_rtn, mon_probe)):
        r_f = res("1k", src(source="Heater shunt anti-alias filter"), ref=ref_r)
        r_f[1] += node
        r_f[2] += out
        c_f = cap("1u 50V", src(source="Heater shunt anti-alias filter"), ref=ref_c)
        c_f[1] += out
        c_f[2] += gnd
    r_top = res("30k", src(source="+12V_HEAT monitor divider top (1:4 -> 3.0 V at 12 V)"), ref="R20")
    r_top[1] += heat5
    r_top[2] += mon_5v
    r_bot = res("10k", src(source="+12V_HEAT monitor divider bottom"), ref="R21")
    r_bot[1] += mon_5v
    r_bot[2] += gnd
    r_ltop = res("10k", src(source="+5V electronics rail monitor divider top"), ref="R24")
    r_ltop[1] += v5
    r_ltop[2] += mon_5v_logic
    r_lbot = res("10k", src(source="+5V electronics rail monitor divider bottom"), ref="R25")
    r_lbot[1] += mon_5v_logic
    r_lbot[2] += gnd
