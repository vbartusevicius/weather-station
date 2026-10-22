"""V1 crown board v2 (50 x 50 mm octagon with four transducer notches, 4 layers, 1.2 mm, y = 182.8 mm):
  - U1 STM32G431RBT6: digitises the wind echoes, rain impacts and lightning events; I2C peripheral 0x2A for main,
    reflashed by the ESP32 through the ROM I2C bootloader (CROWN_BOOT0 + CROWN_NRST from main)
  - U2..U5 + D1..D8: UCC27524 H-bridge transducer drivers on +12V_HEAT with BAV99 T/R isolation
  - D9..D12 + U7 + U8A: receive clamps, 74HC4052 mux and 40 kHz band-pass preamp into the STM32 PGA / ADC1
  - U8B + D13: piezo rain charge amplifier into ADC2
  - U9 + L1: AS3935 lightning detector (SPI) with its MA5532-AE ferrite-rod antenna
  - U6: MMC5603NJ compass (main I2C bus); U10: 3.3 V LDO (crown + dome)
  - J1: 12-pin JST-SH from main J2; J2..J5 transducers (1 = A, 2 = B); J6..J9 piezo discs; J10 dome riser;
    J11 spare +12V_HEAT outlet; J12 SWD debug port (Tag-Connect TC2030-NL footprint)
"""

from blocks import crown_sensors
from common import block, part, power, power_flag, src
from skidl import Net

from variants.v1.main import CROWN_LINK, jst_sh12, wire

B2B_HDR4 = "gateway_fp:HX_PZ2.54-1x4P-SMD"
JST_SH2 = "Connector_JST:JST_SH_BM02B-SRSS-TB_1x02-1MP_P1.00mm_Vertical"
DOME_B2B_LINK = ("+3V3", "GND", "I2C_SDA", "I2C_SCL")


def build():
    gnd = power("GND")
    v3 = power("+3V3")
    v5 = power("+5V")
    heat12 = Net("+12V_HEAT")
    for n in (gnd, v5, heat12):   # +3V3 is driven by the crown LDO U10
        power_flag(n)
    nets = {
        "+5V": v5, "+3V3": v3, "GND": gnd, "+12V_HEAT": heat12,
        "I2C_SDA": Net("I2C_SDA"), "I2C_SCL": Net("I2C_SCL"),
        "CROWN_IRQ": Net("CROWN_IRQ"), "CROWN_NRST": Net("CROWN_NRST"), "CROWN_BOOT0": Net("CROWN_BOOT0"),
    }
    block("int_main")
    wire(jst_sh12("Main board", "J1", "12-pin JST-SH cable from main J2 (+12V_HEAT, +5V, I2C, crown MCU control)"),
         CROWN_LINK, nets)
    block("aux_heat")
    j11 = part("Connector", "Conn_01x02_Pin", "AUX 12V_HEAT", JST_SH2,
               src(mpn="BM02B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160388",
                   source="Spare 2-pin JST-SH +12V_HEAT / GND outlet for a future crown / dome heater (unused in V1)"),
               ref="J11")
    j11[1] += heat12
    j11[2] += gnd

    crown_sensors.build(nets)

    block("int_dome")
    j10 = part("Connector_Generic", "Conn_01x04", "DOME_SOCKET", B2B_HDR4,
               src(mpn="HX PM2.54-1x4P TP-YQ", mfr="HCTL", lcsc="C42379197",
                   source="4-pin 2.54 mm vertical SMD female socket receiving the `dome` Micro-PCB male riser pins"),
               ref="J10")
    wire(j10, DOME_B2B_LINK, nets)


NET_CLASSES = {
    "SELV-CROWN": [
        "GND", "+3V3", "+3V3A", "+5V", "+12V_HEAT", "VDRV", "I2C_SDA", "I2C_SCL", "CROWN_IRQ", "CROWN_NRST",
        "CROWN_BOOT0", "XTAL_*", "SWDIO", "SWCLK", "AS_*", "TX_P", "TX_N", "MUX_*", "XD_*", "US_*", "PIEZO_*", "RAIN_ADC",
        "Net-*",
    ],
    "CROWN-Power": ["+5V", "+12V_HEAT", "+3V3", "VDRV"],
}
