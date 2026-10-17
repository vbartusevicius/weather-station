"""V1 top crown board (41.0 x 56.0 mm, 4 layers, 1.2 mm, horizontal inside top crown):
  - J1: 10-pin JST-SH from main J2
  - J2..J5: 4x 2-pin JST-SH for N, E, S, W 40 kHz waterproof ultrasonic transducers
  - U1..U4: SN74LVC2G04 TX burst driver, CD4052 cross-strapped mux, TLV9062 bandpass amp, LM393 zero-cross comparator
  - J6, U5: 2-pin JST-SH for 27 mm Piezo rain disc + TLV9062 charge/envelope amplifier
  - U6, U7, U8: LTR-390UV-01 UV sensor, VEML7700-TT solar irradiance sensor, LIS2MDLTR 3-axis magnetometer
"""

from blocks import crown_sensors
from common import block, power, power_flag
from skidl import Net

from variants.v1.main import CROWN_LINK, jst_sh10, wire


def build():
    gnd = power("GND")
    v3 = power("+3V3")
    v5 = power("+5V")
    for n in (gnd, v3, v5):
        power_flag(n)

    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    us_sel_a = Net("US_SEL_A")
    us_sel_b = Net("US_SEL_B")
    us_tx = Net("US_TX")
    us_rx = Net("US_RX")
    rain_adc = Net("RAIN_ADC")

    nets = {
        "+5V": v5, "+3V3": v3, "GND": gnd,
        "I2C_SDA": sda, "I2C_SCL": scl,
        "US_SEL_A": us_sel_a, "US_SEL_B": us_sel_b,
        "US_TX": us_tx, "US_RX": us_rx, "RAIN_ADC": rain_adc,
    }
    block("int_main")
    wire(jst_sh10("Main board", "J1", "10-pin JST-SH cable from main J2"), CROWN_LINK, nets)

    crown_sensors.build_ultrasonic(gnd, v3, v5, us_sel_a, us_sel_b, us_tx, us_rx)
    crown_sensors.build_rain_uv_compass(gnd, v3, v5, sda, scl, rain_adc)


NET_CLASSES = {
    "SELV-CROWN": [
        "GND", "+3V3", "+5V", "I2C_SDA", "I2C_SCL", "US_SEL_A", "US_SEL_B",
        "US_TX", "US_RX", "RAIN_ADC", "XD_N", "XD_E", "XD_S", "XD_W",
        "US_TX_DRV", "US_TX_AC", "US_RX_MUX", "US_VREF", "US_VTH", "US_AMP1_IN",
        "US_AMP1_FB", "US_AMP1_OUT", "US_AMP2_IN", "US_AMP2_FB", "US_AMP2_OUT",
        "PIEZO_IN", "PIEZO_INV", "PIEZO_STG1", "PIEZO_ENV", "LIS_C1", "Net-*",
    ],
    "CROWN-Power": ["+5V", "+3V3"],
}
