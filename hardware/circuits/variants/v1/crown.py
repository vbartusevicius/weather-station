"""V1 top crown ultrasonic, 4-quadrant piezo rain & compass board
(41.0 x 56.0 mm, 4 layers, 1.2 mm, lowered to y = 181.5 mm with full roof clearance):
  - J1: 10-pin JST-SH from main J2
  - J2..J5 (bottom): 4x 2-pin JST-SH headers for N/E/S/W 40 kHz waterproof ultrasonic transducers
  - J6..J9 (top): 4x 2-pin JST-SH headers wired in parallel for the 4x 20 mm Murata piezo rain discs
  - J10 (top center 0,0): 4-pin 2.54 mm vertical SMD header feeding the elevated 12x12 mm `dome` Micro-PCB
    right inside the hollow JGS1 Fused Silica Hemispherical Dome
  - U1..U4: 40 kHz Ultrasonic ToF Multiplexer & Bandpass/Zero-Cross AFE
  - U5: TLV9062IDR 4-Piezo Charge Amplifier & 206 mV Linear Envelope Follower
  - U6 (LIS2MDLTR): 3-axis magnetometer for Digital North auto-alignment
"""

from blocks import crown_sensors
from common import block, part, power, power_flag, src
from skidl import Net

from variants.v1.main import CROWN_LINK, jst_sh10, wire

B2B_HDR4 = "gateway_fp:HX_PZ2.54-1x4P-SMD"
DOME_B2B_LINK = ("+3V3", "GND", "I2C_SDA", "I2C_SCL")


def build():
    gnd = power("GND")
    v3 = power("+3V3")
    v5 = power("+5V")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    us_sel_a = Net("US_SEL_A")
    us_sel_b = Net("US_SEL_B")
    us_tx = Net("US_TX")
    us_rx = Net("US_RX")
    rain_adc = Net("RAIN_ADC")
    for n in (gnd, v3, v5):
        power_flag(n)

    nets = {
        "+5V": v5, "+3V3": v3, "GND": gnd,
        "I2C_SDA": sda, "I2C_SCL": scl,
        "US_SEL_A": us_sel_a, "US_SEL_B": us_sel_b,
        "US_TX": us_tx, "US_RX": us_rx,
        "RAIN_ADC": rain_adc,
    }
    block("int_main")
    wire(jst_sh10("Main board", "J1", "10-pin JST-SH cable from main J2"), CROWN_LINK, nets)

    crown_sensors.build(gnd, v5, v3, sda, scl, us_sel_a, us_sel_b, us_tx, us_rx, rain_adc)

    block("int_dome")
    j10 = part("Connector_Generic", "Conn_01x04", "DOME_SOCKET", B2B_HDR4,
               src(mpn="HX PM2.54-1x4P TP-YQ", mfr="HCTL", lcsc="C42379197",
                   source="4-pin 2.54 mm vertical SMD female socket receiving the `dome` Micro-PCB male riser pins"),
               ref="J10")
    wire(j10, DOME_B2B_LINK, nets)


NET_CLASSES = {
    "SELV-CROWN": [
        "GND", "+3V3", "+5V", "I2C_SDA", "I2C_SCL", "US_SEL_A", "US_SEL_B",
        "US_TX", "US_RX", "RAIN_ADC", "XD_N", "XD_E", "XD_S", "XD_W",
        "US_TX_DRV", "US_TX_AC", "US_RX_MUX", "US_VREF", "US_VTH", "US_AMP1_IN",
        "US_AMP1_FB", "US_AMP1_OUT", "US_AMP2_IN", "US_AMP2_FB", "US_AMP2_OUT",
        "PIEZO_IN", "PIEZO_INV", "PIEZO_STG1", "PIEZO_ENV", "Net-*",
    ],
    "CROWN-Power": ["+5V", "+3V3"],
}
