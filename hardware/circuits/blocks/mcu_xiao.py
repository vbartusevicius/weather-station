"""MCU & RS-485 block: Seeed XIAO ESP32-C6 on 2x7 female headers, powered from +5V via
a TI LM66100 ideal diode, plus onboard service button and auto-direction SP3485EN-L/TR
RS-485 transceiver (`J5`: White = RS485_A+, Yellow = RS485_B-)."""

from builtins import NC

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
SC70_6 = "Package_TO_SOT_SMD:SOT-363_SC-70-6"
XIAO_FP = "gateway_fp:XIAO_ESP32C6_Header"
SW_B3U = "Button_Switch_SMD:SW_SPST_B3U-1000P"
FP_SOIC8 = "gateway_fp:ON_LM393DR2G_SOIC-8"
FP_LVC2G04 = "gateway_fp:TI_SN74LVC2G04_SOT-23-6"
FP_JST2 = "gateway_fp:JST_BM02B-SRSS-TB"

MCU_PINS = {
    "vin_adc": {"pin": "D0/GPIO0", "net": "VIN_ADC", "note": "ADC1_CH0: 12-32 V supply sense (100k/10k divider)"},
    "ntc_sense": {"pin": "D1/GPIO1", "net": "NTC_SENSE", "note": "ADC1_CH1: reflector NTC thermistor + service button"},
    "rain_adc": {"pin": "D2/GPIO2", "net": "RAIN_ADC", "note": "ADC1_CH2: hybrid piezo impact & capacitive rain envelope"},
    "heat_pwm": {"pin": "D3/GPIO21", "net": "HEAT_PWM", "note": "LEDC PWM: constant-wattage de-icing heater MOSFET drive"},
    "i2c_sda": {"pin": "D4/GPIO22", "net": "I2C_SDA", "note": "I2C bus SDA (all environmental, UV, IMU, gas & PM sensors)"},
    "i2c_scl": {"pin": "D5/GPIO23", "net": "I2C_SCL", "note": "I2C bus SCL"},
    "us_sel_a": {"pin": "D6/GPIO16", "net": "US_SEL_A", "note": "Ultrasonic transducer mux select bit A"},
    "us_sel_b": {"pin": "D7/GPIO17", "net": "US_SEL_B", "note": "Ultrasonic transducer mux select bit B"},
    "us_tx": {"pin": "D8/GPIO19", "net": "US_TX", "note": "RMT TX: 40 kHz ultrasonic burst output"},
    "us_rx": {"pin": "D9/GPIO20", "net": "US_RX", "note": "RMT RX / GPTimer capture: ultrasonic echo zero-cross"},
    "rs485_io": {"pin": "D10/GPIO18", "net": "RS485_IO", "note": "UART1 TX/RX: Auto-direction RS-485 transceiver (SP3485EN)"},
}


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def build(gnd, v3, v5, vin_adc, heat_pwm, rs485_a, rs485_b):
    """Build the XIAO ESP32-C6 MCU block, service button, and auto-direction RS-485 transceiver."""
    v5_xiao = Net("+5V_XIAO")
    ntc_sense = Net("NTC_SENSE")
    rain_adc = Net("RAIN_ADC")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    us_sel_a = Net("US_SEL_A")
    us_sel_b = Net("US_SEL_B")
    us_tx = Net("US_TX")
    us_rx = Net("US_RX")
    rs485_io = Net("RS485_IO")

    # --- LM66100 ideal diode: +5V -> +5V_XIAO ---
    block("mcu_power")
    u2 = part("Power_Management", "LM66100DCK", "LM66100", SC70_6,
              src(mpn="LM66100DCKR", mfr="TI", lcsc="C2869734",
                  source="TI LM66100 ideal diode preventing USB-C back-feed into +5V rail"),
              ref="U2")
    u2["VIN"] += v5
    u2["~{CE}"] += gnd
    u2["GND"] += gnd
    u2["VOUT"] += v5_xiao
    u2["ST"] += NC
    c_v5 = cap("10u 10V", src(source="XIAO 5V decoupling"))
    c_v5[1] += v5_xiao
    c_v5[2] += gnd

    # --- Seeed XIAO ESP32-C6 ---
    block("mcu")
    u3 = part("gateway", "XIAO_ESP32C6", "XIAO ESP32C6", XIAO_FP,
              src(mpn="102010636", mfr="Seeed Studio", hand=True, footprint_ref="C9900124963",
                  source="XIAO ESP32C6 Pre-Soldered on 2x 1x7 5.0 mm female socket strips"),
              ref="U3")
    u3["1"] += vin_adc
    u3["2"] += ntc_sense
    u3["3"] += rain_adc
    u3["4"] += heat_pwm
    u3["5"] += sda
    u3["6"] += scl
    u3["7"] += us_sel_a
    u3["8"] += us_sel_b
    u3["9"] += us_tx
    u3["10"] += us_rx
    u3["11"] += rs485_io
    u3["12"] += v3
    u3["13"] += gnd
    u3["14"] += v5_xiao

    # I2C pull-ups
    for bus_net in (sda, scl):
        r = res("4k7", src(source="I2C bus pull-up"))
        r[1] += v3
        r[2] += bus_net

    # NTC_SENSE 10k pull-up + filter cap + Service Button (SW1)
    r_ntc_pu = res("10k", src(source="10k 1% reference pull-up for reflector 10k NTC thermistor"))
    r_ntc_pu[1] += v3
    r_ntc_pu[2] += ntc_sense
    c_ntc = cap("100n 50V", src(source="NTC_SENSE ADC anti-aliasing filter"))
    c_ntc[1] += ntc_sense
    c_ntc[2] += gnd

    sw1 = part("Switch", "SW_Push", "SERVICE", SW_B3U,
               src(mpn="B3U-1000P", mfr="Omron", lcsc="C231329",
                   source="Tactile service button: tap = status wake, hold 5s = WiFi portal, hold 10s = factory reset"),
               ref="SW1")
    sw1[1] += ntc_sense
    sw1[2] += gnd

    # --- Auto-Direction RS-485 Transceiver (SP3485EN-L/TR + SN74LVC2G04 -> J1 Pins 3 & 4) ---
    block("rs485")
    rs485_de = Net("RS485_DE")
    rs485_ro = Net("RS485_RO")

    u8 = part("gateway", "SN74LVC2G04", "SN74LVC2G04DBVR", FP_LVC2G04,
              src(mpn="SN74LVC2G04DBVR", mfr="Texas Instruments", lcsc="C10428",
                  source="Auto-direction inverter driving SP3485 DE/RE# high during low bits"),
              ref="U8")
    u8["1"] += rs485_io
    u8["3"] += gnd
    u8["2"] += gnd
    u8["5"] += v3
    u8["6"] += rs485_de
    u8["4"] += NC
    c_u8 = cap("100n 50V", src(source="U8 auto-direction inverter decoupling"))
    c_u8[1] += v3
    c_u8[2] += gnd

    u7 = part("gateway", "SP3485", "SP3485EN-L/TR", FP_SOIC8,
              src(mpn="SP3485EN-L/TR", mfr="MaxLinear", lcsc="C8963",
                  source="3.3 V half-duplex RS-485 transceiver wired to J1 Pins 3 (A+) and 4 (B-)"),
              ref="U7")
    u7["1"] += rs485_ro
    u7["2"] += rs485_de
    u7["3"] += rs485_de
    u7["4"] += gnd
    u7["5"] += gnd
    u7["6"] += rs485_a
    u7["7"] += rs485_b
    u7["8"] += v3
    c_u7 = cap("100n 50V", src(source="SP3485 decoupling"))
    c_u7[1] += v3
    c_u7[2] += gnd

    # 1k series feedback from RO -> RS485_IO + fail-safe A/B bus bias resistors (4k7)
    r_ro = res("1k", src(source="RS485 RO -> GPIO18 half-duplex RX series resistor"))
    r_ro[1] += rs485_ro
    r_ro[2] += rs485_io
    r_fs_a = res("4k7", src(source="RS485_A fail-safe pull-up to +3V3"))
    r_fs_a[1] += v3
    r_fs_a[2] += rs485_a
    r_fs_b = res("4k7", src(source="RS485_B fail-safe pull-down to GND"))
    r_fs_b[1] += rs485_b
    r_fs_b[2] += gnd
    c_j5 = cap("100n 50V", src(source="RS-485 bus bias rail high-frequency bypass"))
    c_j5[1] += v3
    c_j5[2] += gnd

    return {
        "ntc_sense": ntc_sense,
        "rain_adc": rain_adc,
        "heat_pwm": heat_pwm,
        "sda": sda,
        "scl": scl,
        "us_sel_a": us_sel_a,
        "us_sel_b": us_sel_b,
        "us_tx": us_tx,
        "us_rx": us_rx,
    }
