"""Main-board environmental & safety sensors (the BMP585 barometer lives on the probe):
  - U4: TI ADS1115 16-bit I2C ADC (0x48) measuring both heaters: reflector heater current (R16 0.1 ohm),
    probe heater current (R17 1 ohm in its CO2_HEAT_RTN return) and the +5V probe heater supply
  - U5: ST LSM6DS3TR-C 6-axis IMU for mast tilt compensation & rain vibration filtering (I2C 0x6A)
  - U6: ScioSense AS3935-BQFT Franklin lightning detector (I2C 0x03) + 500 kHz resonant tank
  - J4: 6-pin JST-ZH connector (B6B-ZR-SM4-TF) for the Plantower PMSA003I PM1.0/2.5/10 module (harness)
"""

from builtins import NC

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
FP_LSM6DS3 = "gateway_fp:ST_LSM6DS3TR-C_LGA-14"
FP_AS3935 = "gateway_fp:ScioSense_AS3935_QFN-16"
FP_B6B_ZR = "gateway_fp:JST_B6B-ZR-SM4-TF"
FP_MSOP10 = "gateway_fp:TI_ADS1115_VSSOP-10"
L_SWPA6045 = "Inductor_SMD:L_Sunlord_SWPA6045S"


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
    c_imu = cap("100n 50V", src(source="LSM6DS3TR-C decoupling"))
    c_imu[1] += v3
    c_imu[2] += gnd

    # --- U6: ScioSense AS3935-BQFT Lightning Detector (I2C 0x03) ---
    block("lightning")
    as_acg = Net("AS_ACG")
    as_inp = Net("AS_INP")
    u6 = part("gateway", "AS3935", "AS3935-BQFT", FP_AS3935,
              src(mpn="AS3935-BQFT", mfr="ScioSense", hand=True, footprint_ref="C3178344",
                  source="ScioSense AS3935 Franklin lightning sensor IC (1-40 km range, I2C 0x03)"),
              ref="U6")
    u6["1"] += as_acg  # ACG
    u6["2"] += gnd     # INN
    u6["3"] += as_inp  # INP
    u6["4"] += gnd     # GND
    u6["5"] += v3      # VDD
    u6["6"] += v3      # VREG (3.3 V direct operation with EN_VREG=GND)
    u6["7"] += gnd     # EN_VREG
    u6["8"] += gnd     # CS
    u6["9"] += v3      # SI -> high selects I2C interface
    u6["10"] += NC     # IRQ (polled over I2C)
    u6["11"] += scl    # I2CL/SCL
    u6["12"] += gnd    # MISO
    u6["13"] += sda    # I2CD/MOSI (SDA)
    u6["14"] += NC     # NC
    u6["15"] += v3     # ADD0 -> 1
    u6["16"] += v3     # ADD1 -> 1 (I2C addr 0x03)
    u6["17"] += gnd    # EP

    c_acg = cap("1u 50V", src(source="AS3935 ACG analog reference bypass"))
    c_acg[1] += as_acg
    c_acg[2] += gnd
    c_as_vdd = cap("100n 50V", src(source="AS3935 VDD decoupling"))
    c_as_vdd[1] += v3
    c_as_vdd[2] += gnd

    # 500 kHz resonant antenna tank: L2 = 220 uH || C18 = 470 pF C0G (f0 = 495.0 kHz, trimmed to 500.0 kHz by AS3935 TUN_CAP)
    l2 = part("Device", "L", "220uH", L_SWPA6045,
              src(mpn="SWPA6045S221MT", mfr="Sunlord", lcsc="C91047",
                  source="AS3935 500 kHz RF tank inductor (220 uH)"),
              ref="L2")
    l2[1] += as_inp
    l2[2] += gnd
    c_tank = cap("470p C0G 50V", src(source="AS3935 500 kHz tank tuning capacitor (470 pF C0G/NP0, f0 = 495.0 kHz)"))
    c_tank[1] += as_inp
    c_tank[2] += gnd
    r_q = res("10k", src(source="AS3935 tank Q-factor damping resistor"))
    r_q[1] += as_inp
    r_q[2] += gnd

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
    c_sen = cap("10u 10V", src(source="PMSA003I +5V fan supply bypass (datasheet: 47 uF/10 V bulk at the module end of the harness)"))
    c_sen[1] += v5
    c_sen[2] += gnd

    # --- U4: TI ADS1115 heater power monitor (I2C 0x48: ADDR=GND) ---
    # AIN0: reflector heater shunt R16 (0.1 ohm, PWM current averaged by R18/C19 and the ADC's integrating conversion)
    # AIN1: probe heater shunt R17 (1 ohm in the CO2_HEAT_RTN return from the probe), filtered by R19/C20
    # AIN2: +5V_HEAT heater rail via R20/R21 (1:2)           AIN3: +5V electronics rail via R24/R25 (1:2)
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
    r_refl = res("0R1", src(source="Reflector heater current shunt (Q1 source to GND, 0.25 W, <= 41 mW average)"),
                 footprint=R1206, ref="R16")
    r_refl[1] += heat_sense
    r_refl[2] += gnd
    r_probe = res("1R", src(source="Probe heater current shunt (CO2_HEAT_RTN to GND, 7 mW at 85 mA)"), ref="R17")
    r_probe[1] += co2_heat_rtn
    r_probe[2] += gnd
    for ref_r, ref_c, node, out in (("R18", "C19", heat_sense, mon_refl), ("R19", "C20", co2_heat_rtn, mon_probe)):
        r_f = res("1k", src(source="Heater shunt anti-alias filter"), ref=ref_r)
        r_f[1] += node
        r_f[2] += out
        c_f = cap("1u 50V", src(source="Heater shunt anti-alias filter"), ref=ref_c)
        c_f[1] += out
        c_f[2] += gnd
    r_top = res("10k", src(source="+5V_HEAT monitor divider top"), ref="R20")
    r_top[1] += heat5
    r_top[2] += mon_5v
    r_bot = res("10k", src(source="+5V_HEAT monitor divider bottom"), ref="R21")
    r_bot[1] += mon_5v
    r_bot[2] += gnd
    r_ltop = res("10k", src(source="+5V electronics rail monitor divider top"), ref="R24")
    r_ltop[1] += v5
    r_ltop[2] += mon_5v_logic
    r_lbot = res("10k", src(source="+5V electronics rail monitor divider bottom"), ref="R25")
    r_lbot[1] += mon_5v_logic
    r_lbot[2] += gnd
