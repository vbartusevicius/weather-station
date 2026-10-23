"""V1 main board (32.0 x 52.0 mm, 4 layers, 1.6 mm, horizontal in the lower bay):
  - J1: KF250-3.5-4P push-in roof terminal (12-32 V + RS-485) + 60 V PTC + SMAJ36A TVS + SS16 Schottky + 100 V MLCC
  - J5: 10-pin JST-SH to the `power` board (VIN_PROT out, +5V / +12V_HEAT from two TPS54360 bucks, +3V3_PROBE from
    its LDO straight through to the probe connector J3)
  - Q1: AO3400A low-side PWM de-icing heater MOSFET switch + SS16 flyback diode
  - U2, U3: LM66100 ideal diode + Seeed XIAO ESP32-C6 on 5 mm female headers + SW1 service button
  - U7/D4: SP3485 RS-485 (UART1 TX / RX + RTS-driven DE), 390R fail-safe bias, SM712 surge TVS, optional 120R (R26)
  - U4: ADS1115 heater monitor, U5: LSM6DS3TR-C IMU (the AS3935 lightning detector moved to the crown board)
  - J2: 12-pin JST-SH to crown J1; J3: 10-pin JST-SH to probe J1; J4: 6-pin JST-ZH to Plantower PMSA003I PM module
"""

from builtins import NC

from blocks import main_sensors, mcu_xiao, power_heater
from common import block, part, power, power_flag, src
from skidl import Net

JST_SH10 = "Connector_JST:JST_SH_BM10B-SRSS-TB_1x10-1MP_P1.00mm_Vertical"
JST_SH12 = "Connector_JST:JST_SH_BM12B-SRSS-TB_1x12-1MP_P1.00mm_Vertical"
# Crown cable: +12V_HEAT (transducer H-bridge drivers, spare heater outlet) on 2 contacts with 2 GND returns,
# +5V for the crown 3.3 V LDO, I2C to the crown STM32 (+ compass, AS3935 behind it, dome sensors) with GND
# contacts between the 12 V burst pair and the bus, and the STM32 reset / boot-mode / interrupt lines.
CROWN_LINK = ("+12V_HEAT", "+12V_HEAT", "GND", "GND", "+5V", "GND", "I2C_SDA", "GND", "I2C_SCL",
              "CROWN_IRQ", "CROWN_NRST", "CROWN_BOOT0")
# Both heaters run from +12V_HEAT (12 V regulated, never the raw 12-32 V input): 2 contacts each for +12V_HEAT and
# HEAT_SW (reflector heater 0.4 A), the probe heater (35 mA) returns over CO2_HEAT_RTN to the main-board shunt, and
# logic GND needs only one contact (heater currents return over HEAT_SW / CO2_HEAT_RTN).
# The probe sensors run from the dedicated +3V3_PROBE LDO on the power board (not the XIAO 3V3 regulator).
PROBE_LINK = ("+12V_HEAT", "+12V_HEAT", "HEAT_SW", "HEAT_SW", "GND", "NTC_SENSE", "+3V3_PROBE", "CO2_HEAT_RTN", "I2C_SDA", "I2C_SCL")


def jst_sh12(value, ref, source):
    return part("Connector", "Conn_01x12_Pin", value, JST_SH12,
                src(mpn="BM12B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160398", source=source), ref=ref)


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
    sda, scl = mcu["sda"], mcu["scl"]

    co2_heat_rtn = Net("CO2_HEAT_RTN")
    main_sensors.build(gnd, v3, v5, sda, scl, pwr["heat_sense"], co2_heat_rtn, pwr["heat5"])

    nets = {
        "GND": gnd, "+3V3": v3, "+5V": v5, "+12V_HEAT": pwr["heat5"],
        "VIN_PROT": vin_prot, "HEAT_SW": heat_sw, "CO2_HEAT_RTN": co2_heat_rtn,
        "NTC_SENSE": ntc_sense, "+3V3_PROBE": Net("+3V3_PROBE"),
        "I2C_SDA": sda, "I2C_SCL": scl,
        "CROWN_IRQ": mcu["crown_irq"], "CROWN_NRST": mcu["crown_nrst"], "CROWN_BOOT0": mcu["crown_boot0"],
    }
    block("int_crown")
    wire(jst_sh12("Crown board", "J2", "12-pin JST-SH cable to top crown board J1"), CROWN_LINK, nets)
    block("int_probe")
    wire(jst_sh10("Probe board", "J3", "10-pin JST-SH cable to pagoda probe board J1"), PROBE_LINK, nets)
    block("int_power")
    from variants.v1.power import POWER_LINK
    j5 = jst_sh10("Power board", "J5",
                  "10-pin JST-SH power cable from the power board J1 (VIN_PROT out, +5V / +12V_HEAT / +3V3_PROBE in)")
    wire(j5, POWER_LINK, nets)


NET_CLASSES = {
    "SELV-MAIN": [
        "GND", "+3V3", "+3V3_PROBE", "+5V", "+5V_XIAO", "VIN_ADC", "NTC_SENSE",
        "HEAT_PWM", "HEAT_GATE", "I2C_SDA", "I2C_SCL", "CROWN_IRQ", "CROWN_NRST", "CROWN_BOOT0",
        "RS485_TX", "RS485_RX", "RS485_DE", "RS485_A", "RS485_B",
        "+12V_HEAT", "HEAT_SENSE", "CO2_HEAT_RTN", "MON_REFL", "MON_PROBE", "MON_5V", "MON_5V_LOGIC", "Net-*",
    ],
    "MAIN-Power": ["VIN_RAW", "VIN_FUSED", "VIN_PROT", "HEAT_SW", "HEAT_SENSE", "CO2_HEAT_RTN", "+5V", "+12V_HEAT", "+5V_XIAO"],
}
