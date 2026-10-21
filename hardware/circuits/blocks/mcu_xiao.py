"""MCU & RS-485 block: Seeed XIAO ESP32-C6 on 2x7 female headers, powered from +5V via a TI LM66100 ideal diode,
onboard service button, SP3485EN-L/TR half-duplex RS-485 transceiver (UART1 TX / RX + driver-enable on separate pins)
and the crown-MCU control lines (reset, boot-mode select, data-ready interrupt).

The wind / rain front ends are digitised on the crown board by its own STM32G431 (I2C peripheral), so the XIAO needs
no analog or 40 kHz timing pins any more: those GPIOs now carry RS-485 RX / DE and the crown control lines."""

from builtins import NC

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
SC70_6 = "Package_TO_SOT_SMD:SOT-363_SC-70-6"
XIAO_FP = "gateway_fp:XIAO_ESP32C6_Header"
SW_B3U = "Button_Switch_SMD:SW_SPST_B3U-1000P"
FP_SOIC8 = "gateway_fp:ON_LM393DR2G_SOIC-8"

MCU_PINS = {
    "vin_adc": {"pin": "D0/GPIO0", "net": "VIN_ADC", "note": "ADC1_CH0: 12-32 V supply sense (100k/10k divider)"},
    "ntc_sense": {"pin": "D1/GPIO1", "net": "NTC_SENSE", "note": "ADC1_CH1: reflector NTC thermistor + service button"},
    "crown_boot0": {"pin": "D2/GPIO2", "net": "CROWN_BOOT0", "note": "Crown STM32 BOOT0 (high + reset = ROM I2C bootloader)"},
    "heat_pwm": {"pin": "D3/GPIO21", "net": "HEAT_PWM", "note": "LEDC PWM: constant-wattage de-icing heater MOSFET drive"},
    "i2c_sda": {"pin": "D4/GPIO22", "net": "I2C_SDA", "note": "I2C bus SDA (sensors, crown STM32 peripheral 0x2A)"},
    "i2c_scl": {"pin": "D5/GPIO23", "net": "I2C_SCL", "note": "I2C bus SCL"},
    "rs485_tx": {"pin": "D6/GPIO16", "net": "RS485_TX", "note": "UART1 TX -> SP3485 DI"},
    "rs485_rx": {"pin": "D7/GPIO17", "net": "RS485_RX", "note": "UART1 RX <- SP3485 RO"},
    "rs485_de": {"pin": "D8/GPIO19", "net": "RS485_DE", "note": "UART1 RTS (RS-485 half-duplex mode) -> SP3485 DE + /RE"},
    "crown_irq": {"pin": "D9/GPIO20", "net": "CROWN_IRQ", "note": "Crown STM32 data-ready / lightning interrupt (input)"},
    "crown_nrst": {"pin": "D10/GPIO18", "net": "CROWN_NRST", "note": "Crown STM32 NRST (open-drain output)"},
}


def res(value, fields=None, footprint=R0603, ref=None):
    return part("Device", "R", value, footprint, fields, ref=ref)


def cap(value, fields=None, footprint=C0603, ref=None):
    return part("Device", "C", value, footprint, fields, ref=ref)


def build(gnd, v3, v5, vin_adc, heat_pwm, rs485_a, rs485_b):
    """Build the XIAO ESP32-C6 MCU block, service button, RS-485 transceiver and crown control lines."""
    v5_xiao = Net("+5V_XIAO")
    ntc_sense = Net("NTC_SENSE")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    rs485_tx, rs485_rx, rs485_de = Net("RS485_TX"), Net("RS485_RX"), Net("RS485_DE")
    crown_boot0, crown_irq, crown_nrst = Net("CROWN_BOOT0"), Net("CROWN_IRQ"), Net("CROWN_NRST")

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
    c_v5 = cap("10u 10V", src(source="XIAO 5V decoupling"), ref="C4")
    c_v5[1] += v5_xiao
    c_v5[2] += gnd

    # --- Seeed XIAO ESP32-C6 ---
    block("mcu")
    u3 = part("gateway", "XIAO_ESP32C6", "XIAO ESP32C6", XIAO_FP,
              src(mpn="102010636", mfr="Seeed Studio", hand=True, footprint_ref="C9900124963",
                  source="XIAO ESP32C6 Pre-Soldered on 2x 1x7 5.0 mm female socket strips"),
              ref="U3")
    for pin, net in enumerate((vin_adc, ntc_sense, crown_boot0, heat_pwm, sda, scl, rs485_tx, rs485_rx, rs485_de,
                               crown_irq, crown_nrst, v3, gnd, v5_xiao), start=1):
        u3[str(pin)] += net

    # I2C pull-ups: 2.2k keeps the rise time < 300 ns on the ~150-200 pF bus (sensors + 260/60/100 mm cables)
    for ref_r, bus_net in (("R5", sda), ("R6", scl)):
        r = res("2k2", src(source="I2C bus pull-up (2.2k, ~1.5 mA sink at 3.3 V)"), ref=ref_r)
        r[1] += v3
        r[2] += bus_net

    # NTC_SENSE 10k pull-up + filter cap + Service Button (SW1)
    r_ntc_pu = res("10k", src(source="10k 1% reference pull-up for reflector 10k NTC thermistor"), ref="R7")
    r_ntc_pu[1] += v3
    r_ntc_pu[2] += ntc_sense
    c_ntc = cap("100n 50V", src(source="NTC_SENSE ADC anti-aliasing filter"), ref="C5")
    c_ntc[1] += ntc_sense
    c_ntc[2] += gnd

    sw1 = part("Switch", "SW_Push", "SERVICE", SW_B3U,
               src(mpn="B3U-1000P", mfr="Omron", lcsc="C231329",
                   source="Tactile service button: tap = status wake, hold 5s = WiFi portal, hold 10s = factory reset"),
               ref="SW1")
    sw1[1] += ntc_sense
    sw1[2] += gnd

    # --- RS-485 (SP3485EN-L/TR -> J1 pins 3 A+ / 4 B-): ESP-IDF UART_MODE_RS485_HALF_DUPLEX drives DE from RTS ---
    block("rs485")
    u7 = part("gateway", "SP3485", "SP3485EN-L/TR", FP_SOIC8,
              src(mpn="SP3485EN-L/TR", mfr="MaxLinear", lcsc="C8963",
                  source="3.3 V half-duplex RS-485 transceiver wired to J1 Pins 3 (A+) and 4 (B-)"),
              ref="U7")
    u7["1"] += rs485_rx   # RO
    u7["2"] += rs485_de   # /RE (low = receive)
    u7["3"] += rs485_de   # DE
    u7["4"] += rs485_tx   # DI
    u7["5"] += gnd
    u7["6"] += rs485_a
    u7["7"] += rs485_b
    u7["8"] += v3
    c_u7 = cap("100n 50V", src(source="SP3485 decoupling"), ref="C7")
    c_u7[1] += v3
    c_u7[2] += gnd
    r_de = res("10k", src(source="RS-485 DE/RE pull-down: receiver on, driver off while the ESP32 boots"), ref="R8")
    r_de[1] += rs485_de
    r_de[2] += gnd

    # Fail-safe bias 390R / 390R: >= 236 mV idle differential even with 120 R terminators at BOTH bus ends
    # (3.3 V x 60 / (390 + 60 + 390)); 440 mV with one terminator. RS-485 receivers need >= 200 mV.
    r_fs_a = res("390R", src(source="RS485_A fail-safe bias pull-up to +3V3 (390R)"), ref="R9")
    r_fs_a[1] += v3
    r_fs_a[2] += rs485_a
    r_fs_b = res("390R", src(source="RS485_B fail-safe bias pull-down to GND (390R)"), ref="R10")
    r_fs_b[1] += rs485_b
    r_fs_b[2] += gnd
    c_bias = cap("100n 50V", src(source="RS-485 bus bias rail high-frequency bypass"), ref="C8")
    c_bias[1] += v3
    c_bias[2] += gnd
    # Optional 120R bus terminator across A/B: hand-fitted ONLY if this station sits at a physical end of the bus
    r_term = res("120R", src(source="Optional RS-485 120R terminator: solder only at a bus end (not JLC-placed)",
                             hand=True), ref="R26")
    r_term[1] += rs485_a
    r_term[2] += rs485_b
    # Surge/ESD clamp for the outdoor RS-485 pair, right at the J1 terminal (-7 V / +12 V asymmetric TVS)
    d_485 = part("gateway", "SM712", "PSM712", "Package_TO_SOT_SMD:SOT-23",
                 src(mpn="PSM712-LF-T7", mfr="Protek", lcsc="C32677",
                     source="RS-485 A/B surge and ESD TVS (-7/+12 V, 600 W 8/20 us) at the roof-cable terminal"),
                 ref="D4")
    d_485["1"] += rs485_a
    d_485["2"] += rs485_b
    d_485["3"] += gnd

    return {
        "ntc_sense": ntc_sense,
        "heat_pwm": heat_pwm,
        "sda": sda,
        "scl": scl,
        "crown_boot0": crown_boot0,
        "crown_irq": crown_irq,
        "crown_nrst": crown_nrst,
    }
