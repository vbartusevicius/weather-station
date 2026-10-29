"""V1 elevated Ø 13.6 mm UV-A / UV-B / light micro-PCB (`dome`, 4 layers, 1.6 mm) on the same 4-pin riser
(`crown J10 <-> dome J1`: +3V3, GND, I2C_SDA, I2C_SCL) and outline as `dome_legacy` (LTR-390UV + GUVB-C31SM).

Top (sky side, under the PTFE dome / JGS1 window):
  - D1 Genicom GUVB-C21SD: AlGaN UV-B photodiode (240-320 nm, 0.11 A/W at 300 nm, 1.4 nA per UV index in sunlight)
  - D2 Genicom GUVA-C22SD: GaN UV-A photodiode (240-370 nm, 0.14 A/W at 350 nm, 17 nA per UV index + 87 nA)
  - U2 ams OSRAM TSL25911FN: visible + NIR light, 188 uLux night sky (I2C 0x29), unchanged from dome_legacy
  Both photodiodes are COB2418 (1.8 x 2.4 x 1.0 mm), so the sensor tops stay under the JGS1 window limit.
Bottom (beside the riser):
  - U1 TI TLV9062 dual CMOS op-amp (0.5 pA input bias): one zero-bias transimpedance stage per photodiode. Each
    anode sits on VB (~0.30 V from R3 / R4), the cathode on the inverting input, so the output rises above VB:
    V(UVB) = VB + I * 10 MOhm (R1 || C4 10 nF, 0.1 s), V(UVA) = VB + I * 4.7 MOhm (R2 || C5 22 nF, 0.1 s).
    Range: UV-B 46 pA (UV index 0.3 under the PTFE dome) .. 13 nA (UV index 10 under the window) = 0.46 .. 130 mV;
    UV-A ~2 nA (overcast winter, PTFE) .. ~260 nA (summer, window) = 9 mV .. 1.2 V.
  - U3 TI ADS1115 16-bit ADC (I2C 0x49: ADDR = VDD; main U4 is 0x48): AIN0 - AIN3 = UV-B, AIN1 - AIN3 = UV-A,
    differential against VB (AIN3), so offsets of the bias divider cancel; AIN2 measures VB against GND.
The firmware zeroes both channels at night (sun below -6 deg) and ranges the ADS1115 PGA per channel."""

from builtins import NC

from common import block, part, power, power_flag, src
from skidl import Net

from variants.v1.crown import DOME_B2B_LINK
from variants.v1.main import wire

C0603 = "Capacitor_SMD:C_0603_1608Metric"
R0603 = "Resistor_SMD:R_0603_1608Metric"
B2B_HDR4 = "gateway_fp:HX_PZ2.54-1x4P-SMD"
FP_TSL25911 = "gateway_fp:AMS_TSL25911FN"
FP_PD = "gateway_fp:Genicom_COB2418"
FP_WSON8 = "Package_SON:Texas_DSG0008A_WSON-8-1EP_2x2mm_P0.5mm_EP0.9x1.6mm"
FP_ADS_X2QFN = "gateway_fp:TI_ADS1115_X2QFN-10"


def cap(value, source, ref):
    return part("Device", "C", value, C0603, src(source=source), ref=ref)


def res(value, source, ref):
    return part("Device", "R", value, R0603, src(source=source), ref=ref)


def two(p, a, b):
    p[1] += a
    p[2] += b
    return p


def build():
    gnd = power("GND")
    v3 = power("+3V3")
    sda = Net("I2C_SDA")
    scl = Net("I2C_SCL")
    for n in (gnd, v3):
        power_flag(n)

    nets = {"+3V3": v3, "GND": gnd, "I2C_SDA": sda, "I2C_SCL": scl}
    block("int_crown")
    j1 = part("Connector_Generic", "Conn_01x04", "CROWN_RISER", B2B_HDR4,
              src(mpn="HX PZ2.54-1x4P TP-YQ", mfr="HCTL", lcsc="C41417361",
                  source="4-pin 2.54 mm SMD riser receptor on bottom of Ø13.6 mm `dome` Micro-PCB"),
              ref="J1")
    wire(j1, DOME_B2B_LINK, nets)

    block("light")
    # U2: ams OSRAM TSL25911FN (I2C 0x29): UV-rejection package, visible + NIR (700-1100 nm) + 188 uLux night sky
    u2 = part("gateway", "TSL25911", "TSL25911FN", FP_TSL25911,
              src(mpn="TSL25911FN", mfr="ams OSRAM", lcsc="C176812",
                  source="UV-rejection dual-diode Visible + Near-IR (700-1100 nm) & 188 uLux night-sky sensor (I2C 0x29)"),
              ref="U2")
    for pin, net in (("1", scl), ("2", NC), ("3", gnd), ("4", gnd), ("5", v3), ("6", sda)):
        u2[pin] += net
    two(cap("100n 50V", "TSL25911FN decoupling", "C1"), v3, gnd)

    block("uv")
    vb, uvb_in, uva_in, uvb_out, uva_out = (Net(n) for n in ("UV_VB", "UVB_IN", "UVA_IN", "UVB_OUT", "UVA_OUT"))
    d1 = part("gateway", "GUV_PD", "GUVB-C21SD", FP_PD,
              src(mpn="GUVB-C21SD", mfr="Genicom", hand=True, footprint_ref="C6565190",
                  source="AlGaN UV-B photodiode 240-320 nm (COB2418), zero-bias into U1A"),
              ref="D1")
    d1["A"] += vb
    d1["K"] += uvb_in
    d2 = part("gateway", "GUV_PD", "GUVA-C22SD", FP_PD,
              src(mpn="GUVA-C22SD", mfr="Genicom", hand=True, footprint_ref="C6565190",
                  source="GaN UV-A photodiode 240-370 nm (COB2418), zero-bias into U1B"),
              ref="D2")
    d2["A"] += vb
    d2["K"] += uva_in

    u1 = part("gateway", "TLV9062", "TLV9062IDSGR", FP_WSON8,
              src(mpn="TLV9062IDSGR", mfr="Texas Instruments", lcsc="C2058009",
                  source="Dual 0.5 pA CMOS op-amp (WSON-8 2x2): UV-B / UV-A photodiode transimpedance amplifiers"),
              ref="U1")
    for pin, net in (("1", uvb_out), ("2", uvb_in), ("3", vb), ("4", gnd),
                     ("5", vb), ("6", uva_in), ("7", uva_out), ("8", v3)):
        u1[pin] += net
    two(cap("100n 50V", "TLV9062 decoupling", "C2"), v3, gnd)
    two(res("10M", "UV-B transimpedance gain 10 MOhm (1 nA -> 10 mV)", "R1"), uvb_in, uvb_out)
    two(res("4M7", "UV-A transimpedance gain 4.7 MOhm (1 nA -> 4.7 mV)", "R2"), uva_in, uva_out)
    two(cap("10n 50V", "UV-B TIA feedback capacitor (10 MOhm x 10 nF = 0.1 s)", "C4"), uvb_in, uvb_out)
    two(cap("22n 50V", "UV-A TIA feedback capacitor (4.7 MOhm x 22 nF = 0.1 s)", "C5"), uva_in, uva_out)
    two(res("100k", "Photodiode bias divider top (VB = 3.3 V x 10k / 110k = 0.30 V)", "R3"), v3, vb)
    two(res("10k", "Photodiode bias divider bottom", "R4"), vb, gnd)
    two(cap("1u 50V", "Photodiode bias VB reservoir", "C6"), vb, gnd)

    u3 = part("gateway", "ADS1115", "ADS1115IRUGR", FP_ADS_X2QFN,
              src(mpn="ADS1115IRUGR", mfr="Texas Instruments", lcsc="C2651328",
                  source="16-bit I2C ADC (X2QFN-10), I2C 0x49: UV-B / UV-A transimpedance outputs against VB"),
              ref="U3")
    for pin, net in (("1", v3), ("2", NC), ("3", gnd), ("4", uvb_out), ("5", uva_out), ("6", gnd),
                     ("7", vb), ("8", v3), ("9", sda), ("10", scl)):
        u3[pin] += net   # 1 ADDR = VDD -> 0x49; 2 ALERT/RDY unused (polled); AIN2 grounded
    two(cap("100n 50V", "ADS1115 VDD decoupling", "C3"), v3, gnd)


NET_CLASSES = {
    "SELV-DOME": ["GND", "+3V3", "I2C_SDA", "I2C_SCL", "Net-*", "UV_VB", "UVB_IN", "UVA_IN", "UVB_OUT", "UVA_OUT"],
    "DOME-Power": ["+3V3"],
}
