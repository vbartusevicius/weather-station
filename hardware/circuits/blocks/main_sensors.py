"""Main-board environmental & safety sensors:
  - U4: Bosch BMP581 precision barometric pressure sensor (I2C 0x47)
  - U5: ST LSM6DS3TR-C 6-axis IMU for mast tilt compensation & rain vibration filtering (I2C 0x6A)
  - U6: ScioSense AS3935-BQFT Franklin lightning detector (I2C 0x03) + 500 kHz resonant tank
  - J4: 6-pin JST-ZH connector (B6B-ZR-SM4-TF) for Sensirion SEN55-SDN-T PM1.0/2.5/4.0/10 module
"""

from builtins import NC

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
FP_BMP581 = "gateway_fp:Bosch_BMP581_LGA-10"
FP_LSM6DS3 = "gateway_fp:ST_LSM6DS3TR-C_LGA-14"
FP_AS3935 = "gateway_fp:ScioSense_AS3935_QFN-16"
FP_B6B_ZR = "gateway_fp:JST_B6B-ZR-SM4-TF"
L_SWPA6045 = "Inductor_SMD:L_Sunlord_SWPA6045S"


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def build(gnd, v3, v5, sda, scl):
    # --- U4: Bosch BMP581 Barometer (I2C 0x47: SDO=VDDIO, CSB=VDDIO) ---
    block("baro")
    u4 = part("gateway", "BMP581", "BMP581", FP_BMP581,
              src(mpn="BMP581", mfr="Bosch Sensortec", lcsc="C5362283",
                  source="Bosch BMP581 precision barometer (+/-0.03 hPa relative, I2C 0x47)"),
              ref="U4")
    u4["1"] += v3     # VDDIO
    u4["2"] += scl    # SCK
    u4["3"] += gnd    # VSS
    u4["4"] += sda    # SDI
    u4["5"] += v3     # SDO -> high = 0x47
    u4["6"] += v3     # CSB -> high = I2C mode
    u4["7"] += NC     # INT
    u4["8"] += gnd    # VSS
    u4["9"] += gnd    # VSS
    u4["10"] += v3    # VDD
    c_bmp = cap("100n 50V", src(source="BMP581 VDD/VDDIO decoupling"))
    c_bmp[1] += v3
    c_bmp[2] += gnd

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

    # --- J4: Sensirion SEN55-SDN-T PM1.0/2.5/4.0/10 Module Header (JST-ZH 6P) ---
    block("pm_sensor")
    j4 = part("gateway", "B6B_ZR", "SEN55 PM", FP_B6B_ZR,
              src(mpn="B6B-ZR-SM4-TF(LF)(SN)", mfr="JST", lcsc="C471473",
                  source="6-pin JST-ZH SMD header for Sensirion SEN55-SDN-T particulate sensor (1:+5V, 2:GND, 3:SDA, 4:SCL, 5:SEL=GND, 6:NC)"),
              ref="J4")
    j4["1"] += v5
    j4["2"] += gnd
    j4["3"] += sda
    j4["4"] += scl
    j4["5"] += gnd
    j4["6"] += NC
    c_sen = cap("10u 10V", src(source="SEN55 +5V supply bypass"))
    c_sen[1] += v5
    c_sen[2] += gnd
