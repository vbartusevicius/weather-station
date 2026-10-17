"""MCU block: Seeed XIAO ESP32-C6 on 2x7 female headers, powered from +5V via
a TI LM66100 ideal diode, plus onboard service button and two SK6805-EC15 RGB
status LEDs."""

from builtins import NC

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
SC70_6 = "Package_TO_SOT_SMD:SOT-363_SC-70-6"
SOT23_5 = "Package_TO_SOT_SMD:SOT-23-5"
XIAO_FP = "gateway_fp:XIAO_ESP32C6_Header"
LED_RGB = "gateway_fp:LED_SK6805-EC15_1.5x1.5mm"
SW_B3U = "Button_Switch_SMD:SW_SPST_B3U-1000P"

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
    "led_data": {"pin": "D10/GPIO18", "net": "LED_DATA", "note": "RMT TX: SK6805-EC15 RGB status LEDs via 5 V buffer"},
}


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def build(gnd, v3, v5, vin_adc, heat_pwm):
    """Build the XIAO ESP32-C6 MCU block, service button, and RGB status LEDs."""
    v5_xiao = Net("+5V_XIAO")
    ntc_sense = Net("NTC_SENSE")
    rain_adc = Net("RAIN_ADC")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    us_sel_a = Net("US_SEL_A")
    us_sel_b = Net("US_SEL_B")
    us_tx = Net("US_TX")
    us_rx = Net("US_RX")
    led_data = Net("LED_DATA")

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
    u3["11"] += led_data
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

    # --- Status RGB LEDs (2x SK6805-EC15 via SN74AHCT1G125 5V level shifter) ---
    block("status_leds")
    u7 = part("74xGxx", "74AHCT1G125", "74AHCT1G125", SOT23_5,
              src(mpn="SN74AHCT1G125DBVR", mfr="TI", lcsc="C7484",
                  source="5 V CMOS buffer lifting 3.3 V LED_DATA to 5 V"),
              ref="U7")
    u7[1] += gnd
    u7[2] += led_data
    u7[3] += gnd
    u7[5] += v5
    c_buf = cap("100n 50V", src(source="74AHCT1G125 decoupling"))
    c_buf[1] += v5
    c_buf[2] += gnd
    r_led = res("330R", src(source="SK6805 DIN series resistor"))
    u7[4] += r_led[1]
    led_din1 = r_led[2]
    led_chain = Net("LED_CHAIN1")

    d4 = part("gateway", "SK6805-EC15", "RGB STATUS", LED_RGB,
              src(mpn="SK6805-EC15", mfr="OPSCO", lcsc="C2890035",
                  source="Status RGB LED 1 (System / Wi-Fi / Heater)"),
              ref="D4")
    d4["DIN"] += led_din1
    d4["VDD"] += v5
    d4["GND"] += gnd
    d4["DOUT"] += led_chain
    c_d4 = cap("100n 50V", src(source="SK6805 decoupling"))
    c_d4[1] += v5
    c_d4[2] += gnd

    d5 = part("gateway", "SK6805-EC15", "RGB WEATHER", LED_RGB,
              src(mpn="SK6805-EC15", mfr="OPSCO", lcsc="C2890035",
                  source="Status RGB LED 2 (Weather / AQI / Lightning Alert)"),
              ref="D5")
    d5["DIN"] += led_chain
    d5["VDD"] += v5
    d5["GND"] += gnd
    d5["DOUT"] += NC
    c_d5 = cap("100n 50V", src(source="SK6805 decoupling"))
    c_d5[1] += v5
    c_d5[2] += gnd

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
