"""Crown v2: STM32G431-based ultrasonic anemometer, piezo rain, lightning and compass board.

The crown digitises everything locally and talks to the main ESP32 as an I2C peripheral (I2C2, address 0x2A); the
ESP32 can reflash it through the STM32 ROM I2C bootloader (BOOT0 + NRST lines; AN2606: I2C2 on PC4 / PA8, which
only the LQFP-64 package bonds out).

1. Wind (4 x 40 kHz closed waterproof transducers, N/E/S/W, V-path via the reflector):
   - TX: TIM8_CH1 / CH1N (complementary, dead-time) drive INA / INB of four UCC27524 dual low-side drivers (one per
     transducer, 4.5-18 V) powered from +12V_HEAT through a 10R / 22 uF filter. Each transducer sits between OUTA and
     OUTB (H-bridge): 2 x 12 V = 24 Vpp minus the isolation diodes (~21 Vpp). Only the driver whose EN is high fires;
     the UCC27524 EN pins are pulled up to VDD (12 V) through 200 kOhm (SLUSFA9, typ.), so a 4.7k pull-down holds an
     undriven EN at 12 V x 4.7k / 204.7k = 0.28 V (< 0.8 V min low threshold) and all drivers stay off while the STM32
     is in reset or being flashed; the STM32 drives EN to 3.3 V (> 2.3 V max high threshold) and sinks <= 60 uA.
   - T/R isolation: a BAV99 wired anti-parallel (pins 1+2 tied) in series with every driver output: the drive pulse
     passes with ~0.7 V loss, the millivolt echo on the listening transducer cannot reach its idle (low) driver.
     The transducer B terminal is referenced to GND through 330R during receive.
   - RX: transducer A terminal -> 47k -> 10 nF -> 1M bias to VREF (1.65 V) per channel -> 74HC4052 4:1 mux (3.3 V)
     -> TLV9062 non-inverting band-pass (gain 7.8 at 40 kHz, 1.6 kHz HP, 106 kHz LP) -> STM32 OPAMP1 PGA (x2..x32,
     automatic gain) -> ADC1 at 2 MSPS (DMA). The transducers must stay at 0 V DC: any bias on them would forward-bias
     the T/R diodes into the idle (low) driver and short the echo (found in hardware/sim/crown_us_link.py), hence the
     AC coupling. During a 24 Vpp burst the 47k limits the mux input-diode current to ~0.2 mA (74HC4052 +/-20 mA).
     Timing: 8 MHz +/-10 ppm crystal.
2. Rain: 4 x 20 mm Murata 7BB-20-6L0 piezo discs in parallel -> BAV99 clamp -> 1k -> TLV9062 charge amplifier
   (22 nF || 4.7 MOhm, biased at VREF, 1.5 Hz high-pass, ~1.8 V per uC) -> 1k / 1 nF -> STM32 ADC2 (continuous).
3. Lightning: ScioSense AS3935 on SPI1 (SI = GND selects SPI) + Coilcraft MA5532-AE 100 uH ferrite-rod antenna and
   2 x 470 pF C0G tank (AS3935-AN04), IRQ to the STM32 (EXTI). No copper under the antenna (layout keepout).
4. Compass: MEMSIC MMC5603NJ on the main I2C bus (0x30), unchanged.
Power: XC6206 3.3 V LDO from +5V (crown, dome sensors); VDDA through 10R / 1 uF.
"""

from builtins import NC

from common import block, part, power_flag, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
FP_JST2 = "gateway_fp:JST_BM02B-SRSS-TB"
FP_LQFP64 = "Package_QFP:LQFP-64_10x10mm_P0.5mm"
FP_SOIC8 = "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm"
FP_CD4052 = "gateway_fp:TI_CD4052BM96_SOIC-16"
FP_TLV9062 = "gateway_fp:TI_TLV9062IDR_SOIC-8"
FP_MMC5603 = "gateway_fp:MEMSIC_MMC5603NJ_WLCSP-4"
FP_AS3935 = "gateway_fp:ScioSense_AS3935_QFN-16"
FP_SOT23 = "Package_TO_SOT_SMD:SOT-23"
FP_XTAL = "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm"
L_MA5532 = "RF_Antenna:Coilcraft_MA5532-AE_RFID"
TP_PAD = "TestPoint:TestPoint_Pad_D1.0mm"

AXES = ("N", "E", "S", "W")
# STM32G431RBT6 LQFP-64 pin -> net name (None = unused, left unconnected). Pin functions per DS12589 / KiCad symbol.
MCU_PIN_NETS = {
    "1": "+3V3",            # VBAT
    "5": "XTAL_IN",         # PF0 RCC_OSC_IN (8 MHz HSE)
    "6": "XTAL_OUT",        # PF1 RCC_OSC_OUT
    "7": "CROWN_NRST",      # PG10-NRST
    "13": "US_AMP_OUT",     # PA1 OPAMP1_VINP -> PGA -> ADC1 (internal VOPAMP1 channel)
    "15": "GND", "16": "+3V3",
    "18": "RAIN_ADC",       # PA4 ADC2_IN17
    "22": "I2C_SCL",        # PC4 I2C2_SCL (app + ROM bootloader)
    "27": "GND",            # VSSA
    "28": "+3V3A",          # VREF+
    "29": "+3V3A",          # VDDA
    "31": "GND", "32": "+3V3",
    "38": "TX_P",           # PC6 TIM8_CH1
    "39": "XD_EN_N",        # PC7
    "40": "XD_EN_E",        # PC8
    "41": "XD_EN_S",        # PC9
    "42": "I2C_SDA",        # PA8 I2C2_SDA (app + ROM bootloader)
    "47": "GND", "48": "+3V3",
    "49": "SWDIO",          # PA13
    "50": "SWCLK",          # PA14
    "51": "AS_CS",          # PA15 GPIO
    "52": "TX_N",           # PC10 TIM8_CH1N
    "53": "XD_EN_W",        # PC11
    "54": "CROWN_IRQ",      # PC12 data-ready / lightning interrupt to the ESP32
    "56": "AS_SCK",         # PB3 SPI1_SCK
    "57": "AS_MISO",        # PB4 SPI1_MISO
    "58": "AS_MOSI",        # PB5 SPI1_MOSI
    "59": "MUX_S0",         # PB6
    "60": "MUX_S1",         # PB7
    "61": "CROWN_BOOT0",    # PB8-BOOT0
    "62": "AS_IRQ",         # PB9 EXTI
    "63": "GND", "64": "+3V3",
}


def res(value, source, ref, footprint=R0603, **kw):
    return part("Device", "R", value, footprint, src(source=source, **kw), ref=ref)


def cap(value, source, ref, footprint=C0603):
    return part("Device", "C", value, footprint, src(source=source), ref=ref)


def two(p, a, b):
    p[1] += a
    p[2] += b
    return p


def jst2(label, ref_name, desc):
    return part("gateway", "BM02B_SRSS", label, FP_JST2,
                src(mpn="BM02B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160388", source=desc),
                ref=ref_name)


def bav99(ref, desc):
    """NXP BAV99 (SOT-23): pin 1 = anode D1, pin 2 = cathode D2, pin 3 = cathode D1 / anode D2."""
    return part("Diode", "BAV99", "BAV99", FP_SOT23,
                src(mpn="BAV99,215", mfr="Nexperia", lcsc="C2500", source=desc), ref=ref)


def build(nets):
    """`nets`: dict of the shared nets (GND, +3V3, +5V, +12V_HEAT, I2C_SDA, I2C_SCL, CROWN_IRQ, CROWN_NRST,
    CROWN_BOOT0). Every part has an explicit reference so the layout plan stays stable."""
    gnd, v3, v5, v12 = nets["GND"], nets["+3V3"], nets["+5V"], nets["+12V_HEAT"]
    n = dict(nets)
    for name in ("+3V3A", "XTAL_IN", "XTAL_OUT", "US_AMP_OUT", "RAIN_ADC", "TX_P", "TX_N", "SWDIO", "SWCLK",
                 "AS_CS", "AS_SCK", "AS_MISO", "AS_MOSI", "AS_IRQ", "MUX_S0", "MUX_S1", "VDRV", "US_VREF",
                 "US_RX_MUX", "US_AMP_FB", "US_AMP_G", "PIEZO_IN", "PIEZO_INV", "PIEZO_OUT", "AS_ACG", "AS_INP"):
        if name not in n:
            n[name] = Net(name)
    for ax in AXES:
        for suffix in ("A", "B", "OUT_A", "OUT_B", "RX"):
            n[f"XD_{ax}_{suffix}"] = Net(f"XD_{ax}_{suffix}")
        n[f"XD_EN_{ax}"] = Net(f"XD_EN_{ax}")
    v3a, vref = n["+3V3A"], n["US_VREF"]
    power_flag(v3a)
    power_flag(n["VDRV"])

    # ---------------------------------------------------------------- power
    block("crown_power")
    u10 = part("Regulator_Linear", "XC6206PxxxMR", "XC6206P332MR", FP_SOT23,
               src(mpn="XC6206P332MR-G", mfr="Torex", lcsc="C5446",
                   source="3.3 V 200 mA LDO for the crown MCU / AFE and the dome sensors (from +5V)"), ref="U10")
    u10["3"] += v5    # VI
    u10["2"] += v3    # VO
    u10["1"] += gnd
    two(cap("1u 50V", "XC6206 input capacitor", "C1"), v5, gnd)
    two(cap("10u 10V", "XC6206 output capacitor", "C2"), v3, gnd)
    two(res("10R", "VDDA / VREF+ filter resistor", "R27"), v3, v3a)
    two(cap("1u 50V", "VDDA / VREF+ bulk", "C8"), v3a, gnd)
    two(cap("100n 50V", "VDDA / VREF+ decoupling", "C9"), v3a, gnd)

    # ---------------------------------------------------------------- MCU
    block("crown_mcu")
    u1 = part("MCU_ST_STM32G4", "STM32G431RBTx", "STM32G431RBT6", FP_LQFP64,
              src(mpn="STM32G431RBT6", mfr="STMicroelectronics", lcsc="C431633",
                  source="STM32G431RBT6 Cortex-M4F 170 MHz, 2x 4 MSPS ADC, OPAMP PGA: crown wind / rain / lightning MCU"),
              ref="U1")
    for pin in (str(i) for i in range(1, 65)):
        net = MCU_PIN_NETS.get(pin)
        u1[pin] += n[net] if net else NC
    for ref in ("C3", "C4", "C5", "C6"):
        two(cap("100n 50V", "STM32 VDD decoupling (one per VDD pin)", ref), v3, gnd)
    two(cap("10u 10V", "STM32 VDD bulk", "C7"), v3, gnd)
    two(cap("100n 50V", "STM32 NRST filter (ST AN5093)", "C10"), n["CROWN_NRST"], gnd)
    two(res("10k", "BOOT0 pull-down: boots the application unless the ESP32 requests the ROM bootloader", "R25"),
        n["CROWN_BOOT0"], gnd)
    y1 = part("Device", "Crystal_GND24", "8MHz", FP_XTAL,
              src(mpn="3225 8M 20PF 10PPM", mfr="YXC", lcsc="C5181477",
                  source="8 MHz +/-10 ppm HSE crystal (CL 20 pF): time-of-flight timebase"), ref="Y1")
    y1[1] += n["XTAL_IN"]
    y1[3] += n["XTAL_OUT"]
    y1[2] += gnd
    y1[4] += gnd
    two(cap("33p C0G 50V", "HSE load capacitor (2 x (CL 20 pF - 5 pF stray))", "C11"), n["XTAL_IN"], gnd)
    two(cap("33p C0G 50V", "HSE load capacitor", "C12"), n["XTAL_OUT"], gnd)
    # SWD programming / debug pads (bench recovery; field updates go through the ROM I2C bootloader)
    for ref, net, what in (("TP1", "+3V3", "3V3"), ("TP2", "SWDIO", "SWDIO"), ("TP3", "SWCLK", "SWCLK"),
                           ("TP4", "CROWN_NRST", "NRST"), ("TP5", "GND", "GND")):
        tp = part("Connector", "TestPoint", what, TP_PAD,
                  src(source=f"SWD programming pad ({what})", board_only=True), ref=ref)
        tp[1] += n[net]

    # ---------------------------------------------------------------- ultrasonic TX (H-bridge per transducer)
    block("us_tx")
    two(res("10R", "Transducer driver supply filter from +12V_HEAT (decouples heater PWM from the drive)", "R17"),
        v12, n["VDRV"])
    two(cap("22u 25V", "Transducer driver supply reservoir (8-cycle burst at constant voltage)", "C13", C1206),
        n["VDRV"], gnd)
    for i, ax in enumerate(AXES):
        drv = part("Driver_FET", "UCC27524D", "UCC27524ADR", FP_SOIC8,
                   src(mpn="UCC27524ADR", mfr="Texas Instruments", lcsc="C185857",
                       source=f"Dual 5 A low-side driver, H-bridge for the {ax} transducer (OUTA/OUTB anti-phase)"),
                   ref=f"U{2 + i}")
        en = n[f"XD_EN_{ax}"]
        for pin, net in (("1", en), ("8", en), ("2", n["TX_P"]), ("4", n["TX_N"]), ("6", n["VDRV"]), ("3", gnd),
                         ("7", n[f"XD_{ax}_OUT_A"]), ("5", n[f"XD_{ax}_OUT_B"])):
            drv[pin] += net   # 1 ENA, 8 ENB, 2 INA, 4 INB, 6 VDD, 3 GND, 7 OUTA, 5 OUTB
        two(cap("1u 50V", f"UCC27524 {ax} local VDD decoupling", f"C{14 + i}"), n["VDRV"], gnd)
        two(res("4k7", f"{ax} driver enable pull-down vs the 200k internal pull-up to 12 V (off in reset)", f"R{13 + i}"),
            en, gnd)
        for j, side in enumerate(("A", "B")):
            d = bav99(f"D{1 + 2 * i + j}",
                      f"Anti-parallel T/R isolation diodes (BAV99 pins 1+2 tied) in the {ax} transducer {side} lead")
            d[1] += n[f"XD_{ax}_OUT_{side}"]
            d[2] += n[f"XD_{ax}_OUT_{side}"]
            d[3] += n[f"XD_{ax}_{side}"]
        two(res("330R", f"{ax} transducer B-terminal receive reference to GND", f"R{1 + i}"), n[f"XD_{ax}_B"], gnd)

    # ---------------------------------------------------------------- ultrasonic RX
    block("us_rx")
    for i, ax in enumerate(AXES):
        j_xd = jst2(f"XD_{ax}", f"J{2 + i}", f"40 kHz waterproof ultrasonic transducer ({ax} port, 1 = A, 2 = B)")
        j_xd[1] += n[f"XD_{ax}_A"]
        j_xd[2] += n[f"XD_{ax}_B"]
        rx_c = Net(f"XD_{ax}_RXC")
        two(res("47k", f"{ax} receive series resistor (limits mux input-diode current to ~0.2 mA during TX)",
                f"R{5 + i}"), n[f"XD_{ax}_A"], rx_c)
        two(cap("10n 50V", f"{ax} receive DC block (keeps the transducer at 0 V so its T/R diodes stay off)",
                f"C{30 + i}"), rx_c, n[f"XD_{ax}_RX"])
        two(res("1M", f"{ax} receive DC bias to VREF (1.65 V)", f"R{9 + i}"), n[f"XD_{ax}_RX"], vref)
    two(res("10k", "VREF divider top (+3V3A)", "R18"), v3a, vref)
    two(res("10k", "VREF divider bottom", "R19"), vref, gnd)
    two(cap("1u 50V", "VREF bypass", "C18"), vref, gnd)

    u7 = part("gateway", "74HC4052", "74HC4052D", FP_CD4052,
              src(mpn="74HC4052D,653", mfr="Nexperia", lcsc="C11350",
                  source="3.3 V dual 4:1 analog mux selecting the listening transducer (X section; Y unused)"),
              ref="U7")
    u7["16"] += v3
    u7["8"] += gnd
    u7["7"] += gnd    # VEE
    u7["6"] += gnd    # /E enable
    u7["10"] += n["MUX_S0"]
    u7["9"] += n["MUX_S1"]
    u7["13"] += n["US_RX_MUX"]   # X common
    for pin, ax in (("12", "N"), ("14", "E"), ("15", "S"), ("11", "W")):   # X0..X3
        u7[pin] += n[f"XD_{ax}_RX"]
    for pin in ("3", "1", "5", "2", "4"):   # Y common, Y0..Y3 unused -> GND
        u7[pin] += gnd
    two(cap("100n 50V", "74HC4052 decoupling", "C24"), v3, gnd)

    u8 = part("gateway", "TLV9062", "TLV9062IDR", FP_TLV9062,
              src(mpn="TLV9062IDR", mfr="Texas Instruments", lcsc="C398355",
                  source="Dual 10 MHz RRIO op-amp: A = 40 kHz receive band-pass (G 7.8), B = piezo charge amplifier"),
              ref="U8")
    u8["8"] += v3
    u8["4"] += gnd
    two(cap("100n 50V", "TLV9062 decoupling", "C23"), v3, gnd)
    u8["3"] += n["US_RX_MUX"]   # A+ (biased at VREF through the 100k of the selected channel)
    u8["2"] += n["US_AMP_FB"]
    u8["1"] += n["US_AMP_OUT"]
    two(res("1k", "RX stage gain resistor Rg", "R20"), n["US_AMP_FB"], n["US_AMP_G"])
    two(cap("100n 50V", "RX stage AC ground (1.6 kHz high-pass with Rg)", "C19"), n["US_AMP_G"], vref)
    two(res("6k8", "RX stage feedback resistor Rf (G = 1 + 6.8k / 1k = 7.8)", "R21"), n["US_AMP_FB"], n["US_AMP_OUT"])
    two(cap("220p C0G 50V", "RX stage low-pass (106 kHz)", "C20"), n["US_AMP_FB"], n["US_AMP_OUT"])

    # ---------------------------------------------------------------- rain
    block("rain_afe")
    for i, quad in enumerate(("NE", "NW", "SW", "SE")):
        j_pz = jst2(f"PIEZO_{quad}", f"J{6 + i}",
                    f"20 mm Murata 7BB-20-6L0 piezo rain disc ({quad} quadrant, wired in parallel)")
        j_pz[1] += n["PIEZO_IN"]
        j_pz[2] += gnd
    d13 = bav99("D9", "Piezo input clamp to GND / +3V3 (hail impacts generate tens of volts)")
    d13[1] += gnd
    d13[2] += v3
    d13[3] += n["PIEZO_IN"]
    two(res("1k", "Piezo series resistor into the charge amplifier", "R22"), n["PIEZO_IN"], n["PIEZO_INV"])
    u8["6"] += n["PIEZO_INV"]   # B-
    u8["5"] += vref             # B+
    u8["7"] += n["PIEZO_OUT"]
    two(cap("22n 50V", "Charge amplifier feedback capacitor (~1.8 V per uC)", "C21"), n["PIEZO_INV"], n["PIEZO_OUT"])
    two(res("4M7", "Charge amplifier DC feedback (1.5 Hz high-pass)", "R23"), n["PIEZO_INV"], n["PIEZO_OUT"])
    two(res("1k", "Rain ADC anti-alias resistor", "R24"), n["PIEZO_OUT"], n["RAIN_ADC"])
    two(cap("1n 50V", "Rain ADC anti-alias capacitor (160 kHz)", "C22"), n["RAIN_ADC"], gnd)

    # ---------------------------------------------------------------- lightning (AS3935 on SPI1)
    block("lightning")
    u9 = part("gateway", "AS3935", "AS3935-BQFT", FP_AS3935,
              src(mpn="AS3935-BQFT", mfr="ScioSense", hand=True, footprint_ref="C3178344",
                  source="ScioSense AS3935 Franklin lightning sensor IC (1-40 km range, SPI mode)"),
              ref="U9")
    for pin, net in (("1", n["AS_ACG"]), ("2", gnd), ("3", n["AS_INP"]), ("4", gnd), ("5", v3), ("6", v3),
                     ("7", gnd), ("8", n["AS_CS"]), ("9", gnd), ("10", n["AS_IRQ"]), ("11", n["AS_SCK"]),
                     ("12", n["AS_MISO"]), ("13", n["AS_MOSI"]), ("15", gnd), ("16", gnd), ("17", gnd)):
        u9[pin] += net    # 6 VREG = 3.3 V direct (EN_VREG = GND); 9 SI = GND selects SPI
    u9["14"] += NC
    two(cap("1u 50V", "AS3935 ACG analog reference bypass", "C26"), n["AS_ACG"], gnd)
    two(cap("100n 50V", "AS3935 VDD decoupling", "C25"), v3, gnd)
    l1 = part("Device", "L", "100uH", L_MA5532,
              src(mpn="MA5532-AE", mfr="Coilcraft", hand=True, footprint_ref="C5361233",
                  source="AS3935 500 kHz lightning antenna (Coilcraft MA5532-AE 100 uH open ferrite rod, Q 30)"),
              ref="L1")
    two(l1, n["AS_INP"], gnd)
    for ref in ("C27", "C28"):
        two(cap("470p C0G 50V", "AS3935 500 kHz tank capacitor (2x 470 pF C0G = 940 pF, AN04)", ref), n["AS_INP"], gnd)
    two(res("10k", "AS3935 tank Q-factor damping resistor (AN04)", "R26"), n["AS_INP"], gnd)

    # ---------------------------------------------------------------- compass
    block("compass")
    u6 = part("gateway", "MMC5603", "MMC5603NJ", FP_MMC5603,
              src(mpn="MMC5603NJ", mfr="MEMSIC", lcsc="C404328",
                  source="20-bit 3-axis AMR magnetometer with on-chip degaussing for Digital North auto-alignment (I2C 0x30)"),
              ref="U6")
    u6["A1"] += gnd
    u6["A2"] += n["I2C_SCL"]
    u6["B1"] += v3
    u6["B2"] += n["I2C_SDA"]
    two(cap("100n 50V", "MMC5603NJ VDD decoupling", "C29"), v3, gnd)
    return n
