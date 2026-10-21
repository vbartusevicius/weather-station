"""V1 pagoda probe board (14.0 x 50.0 mm, 4 layers, 1.2 mm, vertical stem of the T-assembly):
  - J1 (bottom): 10-pin JST-SH from main J3
  - U3 (bottom intake zone, below milled slot): Sensirion SHT45-AD1F-R2 precision thermo-hygrometer
  - U1 (middle zone, above milled slot): Sensirion SCD41-D-R2 photoacoustic true CO2 sensor
  - Backside Closed-Loop 0.42 W Local Heater (B.Cu behind U1): 2x 120R 1206 in parallel on +5V + TH1 10k NTC + LM393 + AO3400A
  - U2 + U5 (upper zone): Sensirion SGP41-D-R4 VOC/NOx sensor + Bosch BMP585 barometer (thunder pressure waves)
  - J2 (top T-junction): 4-pin 2.54 mm male pin header (KH-2.54PH180-1X4P-L11.5, C2905435)
    plugging directly into the bottom 4-pin female socket (J1) of the horizontal `reflector` PCB
"""

from blocks import probe_sensors
from common import block, part, power, power_flag, src
from skidl import Net

from variants.v1.main import PROBE_LINK, jst_sh10, wire

SOCKET_1X4_RA_SMD = "gateway_fp:HX_PM2.54-1x4P-RA-SMD"
REFLECTOR_LINK = ("+5V_HEAT", "HEAT_SW", "NTC_SENSE", "GND")


def build():
    gnd = power("GND")
    v3 = power("+3V3")
    v5 = Net("+5V_HEAT")   # the probe only uses 5 V for its heater
    heat_rtn = Net("CO2_HEAT_RTN")
    heat_sw = Net("HEAT_SW")
    ntc_sense = Net("NTC_SENSE")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    for n in (gnd, v3, v5):
        power_flag(n)

    nets = {
        "HEAT_SW": heat_sw, "GND": gnd, "+5V_HEAT": v5, "CO2_HEAT_RTN": heat_rtn,
        "NTC_SENSE": ntc_sense, "+3V3": v3, "I2C_SDA": sda, "I2C_SCL": scl,
    }
    block("int_main")
    wire(jst_sh10("Main board", "J1", "10-pin JST-SH cable from main J3"), PROBE_LINK, nets)

    probe_sensors.build(gnd, v3, sda, scl, v5, heat_rtn)

    block("int_reflector")
    j2 = part("Connector", "Conn_01x04_Socket", "Reflector T-Socket SMD", SOCKET_1X4_RA_SMD,
              src(mpn="HX PM2.54-1x4P WT", mfr="HANBO", lcsc="C46061768",
                  source="4-pin 2.54 mm right-angle SMD female socket at top of probe receiving the vertical SMD male header J1 of horizontal reflector PCB"),
              ref="J2")
    wire(j2, REFLECTOR_LINK, nets)


NET_CLASSES = {
    "SELV-PROBE": [
        "GND", "+3V3", "+5V_HEAT", "HEAT_SW", "CO2_HEAT_SW", "CO2_HEAT_RTN", "CO2_NTC",
        "CO2_VSET", "CO2_GATE", "NTC_SENSE", "I2C_SDA", "I2C_SCL", "Net-*",
    ],
    "PROBE-Power": ["HEAT_SW", "CO2_HEAT_SW", "CO2_HEAT_RTN", "+5V_HEAT", "+3V3"],
}
