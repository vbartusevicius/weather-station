"""V1 main board (41.0 x 56.0 mm, 4 layers, 1.2 mm, horizontal in the sealed lower bay):
  - J1: KF142R-5.08-2P 12-32 V DC push-in roof terminal + PTC fuse + SMAJ33A TVS + SS16 Schottky + 470 uF cap
  - U1: SCT2400 12-32 V -> +5V buck converter
  - Q1: AO3400A low-side PWM de-icing heater MOSFET switch + SS16 flyback diode
  - U2, U3: LM66100 ideal diode + Seeed XIAO ESP32-C6 on 5 mm female headers + SW1 service button + D4/D5 RGB LEDs
  - U4: Bosch BMP581 barometer, U5: ST LSM6DS3TR-C 6-axis IMU, U6: ScioSense AS3935 lightning detector
  - J2: 10-pin JST-SH to crown J1; J3: 10-pin JST-SH to probe J1; J4: 6-pin JST-ZH to Sensirion SEN55 PM module
"""

from builtins import NC

from blocks import main_sensors, mcu_xiao, power_heater
from common import block, part, power, power_flag, src
from skidl import Net

JST_SH10 = "Connector_JST:JST_SH_BM10B-SRSS-TB_1x10-1MP_P1.00mm_Vertical"
CROWN_LINK = ("+5V", "+3V3", "GND", "I2C_SDA", "I2C_SCL", "US_SEL_A", "US_SEL_B", "US_TX", "US_RX", "RAIN_ADC")
PROBE_LINK = ("VIN_PROT", "VIN_PROT", "HEAT_SW", "HEAT_SW", "GND", "NTC_SENSE", "+3V3", "GND", "I2C_SDA", "I2C_SCL")


def jst_sh10(value, ref, source):
    return part("Connector", "Conn_01x10_Pin", value, JST_SH10,
                src(mpn="BM10B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160396", source=source), ref=ref)


def wire(conn, names, nets):
    for pin, name in enumerate(names, start=1):
        conn[pin] += NC if name is None else nets[name]


def build():
    gnd = power("GND")
    v3 = power("+3V3")
    power_flag(gnd)
    power_flag(v3)
    heat_pwm = Net("HEAT_PWM")

    pwr = power_heater.build(gnd, heat_pwm)
    vin_prot, v5, vin_adc, heat_sw = pwr["vin_prot"], pwr["v5"], pwr["vin_adc"], pwr["heat_sw"]
    rs485_a, rs485_b = pwr["rs485_a"], pwr["rs485_b"]

    mcu = mcu_xiao.build(gnd, v3, v5, vin_adc, heat_pwm, rs485_a, rs485_b)
    ntc_sense = mcu["ntc_sense"]
    rain_adc = mcu["rain_adc"]
    sda, scl = mcu["sda"], mcu["scl"]
    us_sel_a, us_sel_b = mcu["us_sel_a"], mcu["us_sel_b"]
    us_tx, us_rx = mcu["us_tx"], mcu["us_rx"]

    main_sensors.build(gnd, v3, v5, sda, scl)

    nets = {
        "GND": gnd, "+3V3": v3, "+5V": v5,
        "VIN_PROT": vin_prot, "HEAT_SW": heat_sw,
        "NTC_SENSE": ntc_sense, "RAIN_ADC": rain_adc,
        "I2C_SDA": sda, "I2C_SCL": scl,
        "US_SEL_A": us_sel_a, "US_SEL_B": us_sel_b,
        "US_TX": us_tx, "US_RX": us_rx,
    }
    block("int_crown")
    wire(jst_sh10("Crown board", "J2", "10-pin JST-SH cable to top crown board J1"), CROWN_LINK, nets)
    block("int_probe")
    wire(jst_sh10("Probe board", "J3", "10-pin JST-SH cable to pagoda probe board J1"), PROBE_LINK, nets)


NET_CLASSES = {
    "SELV-MAIN": [
        "GND", "+3V3", "+5V", "+5V_XIAO", "VIN_ADC", "NTC_SENSE", "RAIN_ADC",
        "HEAT_PWM", "HEAT_GATE", "I2C_SDA", "I2C_SCL", "US_SEL_A", "US_SEL_B",
        "US_TX", "US_RX", "RS485_IO", "RS485_DE", "RS485_RO", "RS485_A", "RS485_B",
        "BUCK5_SW", "BUCK5_BST", "BUCK5_EN", "BUCK5_FB", "AS_ACG", "AS_INP", "Net-*",
    ],
    "MAIN-Power": ["VIN_RAW", "VIN_FUSED", "VIN_PROT", "HEAT_SW", "+5V", "+5V_XIAO"],
}
