"""V1 horizontal reflector de-icing heater board (34.0 x 56.0 mm, 4 layers, 1.2 mm,
horizontal top bar of the T-assembly at y = 123.8 mm directly under the ultrasonic
reflector cone):
  - J1 (back side): 4-pin 2.54 mm female socket strip (KH-2.54FH-1X4P-H8.5, C2905415)
    plugging directly onto the top 4-pin 2.54 mm male header (J2) of the vertical `probe` PCB
  - R1, R2, R3, R4 (top side): 4x 100R 2512 1 W power resistors wired 100% in PARALLEL
    (25.0 Ohm net, 4.0 W continuous rated capacity, full fault-tolerant redundancy)
  - TH1 (top center): SDNT1608X103F3950FTF 10k 1% B=3950K NTC thermistor measuring the
    true temperature of the horizontal reflector plate
  - C1: 100n 50V NTC sense noise filter capacitor
"""

from common import block, part, power, power_flag, src
from skidl import Net

from variants.v1.main import wire
from variants.v1.probe import REFLECTOR_LINK

R0603 = "Resistor_SMD:R_0603_1608Metric"
R2512 = "Resistor_SMD:R_2512_6332Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
HDR_1X4_SMD = "gateway_fp:HX_PZ2.54-1x4P-SMD"


def build():
    gnd = power("GND")
    vin_prot = Net("VIN_PROT")
    heat_sw = Net("HEAT_SW")
    ntc_sense = Net("NTC_SENSE")
    for n in (gnd, vin_prot):
        power_flag(n)

    nets = {
        "VIN_PROT": vin_prot,
        "HEAT_SW": heat_sw,
        "NTC_SENSE": ntc_sense,
        "GND": gnd,
    }
    block("int_probe")
    j1 = part("Connector", "Conn_01x04_Pin", "Probe T-Header SMD", HDR_1X4_SMD,
              src(mpn="HX PZ2.54-1x4P TP-YQ", mfr="HANBO", lcsc="C41417361",
                  source="4-pin 2.54 mm vertical SMD male pin header on bottom (B.Cu) of reflector plugging into top right-angle SMD female socket J2 of vertical probe PCB"),
              ref="J1")
    wire(j1, REFLECTOR_LINK, nets)

    # 4x 100R 2512 1 W resistors wired 100% in PARALLEL between VIN_PROT and HEAT_SW (25.0 Ohm net, 4.0 W rated, full redundancy)
    block("reflector_heater")
    for idx, quad in enumerate(("NW", "NE", "SW", "SE"), start=1):
        r = part("Device", "R", "100R", R2512,
                 src(source=f"Parallel horizontal reflector heater {quad} quadrant (100 Ohm 1 W 2512)"),
                 ref=f"R{idx}")
        r[1] += vin_prot
        r[2] += heat_sw

    th1 = part("Device", "Thermistor_NTC", "10k NTC", R0603,
               src(mpn="SDNT1608X103F3950FTF", mfr="Sunlord", lcsc="C279936",
                   source="10k 1% B=3950K NTC thermistor at centre of horizontal reflector plate"),
               ref="TH1")
    th1[1] += ntc_sense
    th1[2] += gnd

    c1 = part("Device", "C", "100n 50V", C0603,
              src(source="Reflector NTC sense high-frequency filter capacitor"), ref="C1")
    c1[1] += ntc_sense
    c1[2] += gnd


NET_CLASSES = {
    "SELV-REFLECTOR": ["GND", "VIN_PROT", "HEAT_SW", "NTC_SENSE", "Net-*"],
    "REFLECTOR-Power": ["VIN_PROT", "HEAT_SW"],
}
