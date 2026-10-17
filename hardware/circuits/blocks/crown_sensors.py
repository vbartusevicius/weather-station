"""Top Crown Ultrasonic, 4-Quadrant Piezo Rain & Compass board circuit blocks:
  1. 4-Channel 40 kHz Ultrasonic Anemometer Transducer Interface & AFE:
     - J2 (North), J3 (East), J4 (South), J5 (West): 4x BM02B-SRSS-TB 2-pin JST-SH headers
     - U1: TI SN74LVC2G04DBVR dual inverter in parallel as high-current 40 kHz TX burst driver
     - U2: TI CD4052BM96 dual 4-channel analog multiplexer routing TX/RX across N<->S and E<->W
     - U3: TI TLV9062IDR 10 MHz dual op-amp 2-stage 40 kHz bandpass amplifier (gain ~520x)
     - U4: onsemi LM393DR2G dual comparator (Comp A zero-crossing phase + Comp B envelope threshold gate)
  2. 4-Quadrant Piezoelectric Rain Impact AFE:
     - J6 (NE), J7 (NW), J8 (SW), J9 (SE): 4x BM02B-SRSS-TB 2-pin JST-SH headers wired in parallel for the
       4x 20 mm Murata 7BB-20-6L0 piezo discs glued directly to the crown roof ceiling
     - U5: TI TLV9062IDR dual op-amp (Stage A charge amplifier + Stage B 206 mV linear-biased envelope follower)
  3. U6: ST LIS2MDLTR 3-axis magnetometer for Digital True/Magnetic North auto-alignment (I2C 0x1E)
"""

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
FP_JST2 = "gateway_fp:JST_BM02B-SRSS-TB"
FP_LVC2G04 = "gateway_fp:TI_SN74LVC2G04_SOT-23-6"
FP_CD4052 = "gateway_fp:TI_CD4052BM96_SOIC-16"
FP_TLV9062 = "gateway_fp:TI_TLV9062IDR_SOIC-8"
FP_LM393 = "gateway_fp:ON_LM393DR2G_SOIC-8"
FP_LIS2MDL = "gateway_fp:ST_LIS2MDLTR_LGA-12"


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def jst2(label, ref_name, desc):
    return part("gateway", "BM02B_SRSS", label, FP_JST2,
                src(mpn="BM02B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160388", source=desc),
                ref=ref_name)


def build(gnd, v5, v3, sda, scl, us_sel_a, us_sel_b, us_tx, us_rx, rain_adc):
    # =========================================================================
    # 1. 4-Way 40 kHz Ultrasonic Anemometer Multiplexer & AFE
    # =========================================================================
    block("us_transducers")
    xd_n = Net("XD_N")
    xd_e = Net("XD_E")
    xd_s = Net("XD_S")
    xd_w = Net("XD_W")

    for idx, (axis_name, net_sig) in enumerate(
        [("North", xd_n), ("East", xd_e), ("South", xd_s), ("West", xd_w)], start=2
    ):
        j_xd = jst2(f"XD_{axis_name}", f"J{idx}",
                    f"40 kHz waterproof ultrasonic transducer ({axis_name} port)")
        j_xd["1"] += net_sig
        j_xd["2"] += gnd

    block("us_afe")
    u1 = part("gateway", "SN74LVC2G04", "SN74LVC2G04DBVR", FP_LVC2G04,
              src(mpn="SN74LVC2G04DBVR", mfr="Texas Instruments", lcsc="C10428",
                  source="Dual CMOS inverter in parallel driving 40 kHz ultrasonic TX bursts"),
              ref="U1")
    tx_drv = Net("US_TX_DRV")
    tx_ac = Net("US_TX_AC")
    u1["1"] += us_tx
    u1["3"] += us_tx
    u1["2"] += gnd
    u1["5"] += v5
    u1["4"] += tx_drv
    u1["6"] += tx_drv
    c_u1 = cap("100n 50V", src(source="U1 TX driver supply decoupling"))
    c_u1[1] += v5
    c_u1[2] += gnd

    c_tx_ac = cap("100n 50V", src(source="Ultrasonic TX AC coupling capacitor"))
    r_tx_ser = res("1k", src(source="Reciprocal 1k TX source termination matching 1k RX load"))
    c_tx_ac[1] += tx_drv
    c_tx_ac[2] += r_tx_ser[1]
    r_tx_ser[2] += tx_ac

    u2 = part("gateway", "CD4052", "CD4052BM96", FP_CD4052,
              src(mpn="CD4052BM96", mfr="Texas Instruments", lcsc="C6521",
                  source="Dual 4:1 analog mux steering 40 kHz TX & RX across N/S/E/W transducers"),
              ref="U2")
    rx_mux = Net("US_RX_MUX")
    u2["16"] += v5
    u2["8"] += gnd
    u2["7"] += gnd
    u2["6"] += gnd
    u2["10"] += us_sel_a
    u2["9"] += us_sel_b
    u2["13"] += tx_ac
    u2["12"] += xd_n
    u2["14"] += xd_s
    u2["15"] += xd_e
    u2["11"] += xd_w
    u2["3"] += rx_mux
    u2["1"] += xd_s
    u2["5"] += xd_n
    u2["2"] += xd_w
    u2["4"] += xd_e
    c_u2 = cap("100n 50V", src(source="CD4052 supply decoupling"))
    c_u2[1] += v5
    c_u2[2] += gnd

    vref = Net("US_VREF")
    r_vref1 = res("10k", src(source="Ultrasonic AFE mid-supply VREF divider top"))
    r_vref2 = res("10k", src(source="Ultrasonic AFE mid-supply VREF divider bottom"))
    c_vref = cap("100n 50V", src(source="Ultrasonic AFE VREF bypass capacitor"))
    r_vref1[1] += v5
    r_vref1[2] += vref
    r_vref2[1] += vref
    r_vref2[2] += gnd
    c_vref[1] += vref
    c_vref[2] += gnd

    u3 = part("gateway", "TLV9062", "TLV9062IDR", FP_TLV9062,
              src(mpn="TLV9062IDR", mfr="Texas Instruments", lcsc="C398355",
                  source="Dual 10 MHz op-amp 2-stage 40 kHz ultrasonic bandpass amplifier"),
              ref="U3")
    u3["8"] += v5
    u3["4"] += gnd
    c_u3 = cap("100n 50V", src(source="TLV9062 ultrasonic AFE decoupling"))
    c_u3[1] += v5
    c_u3[2] += gnd

    amp1_in = Net("US_AMP1_IN")
    amp1_fb = Net("US_AMP1_FB")
    amp1_out = Net("US_AMP1_OUT")
    c_rx_in = cap("47n 50V", src(source="RX input high-pass coupling"))
    r_rx_bias = res("1k", src(source="Reciprocal 1k RX load termination matching 1k TX source"))
    c_rx_in[1] += rx_mux
    c_rx_in[2] += amp1_in
    r_rx_bias[1] += amp1_in
    r_rx_bias[2] += vref

    u3["3"] += amp1_in
    u3["2"] += amp1_fb
    u3["1"] += amp1_out
    r_g1 = res("1k", src(source="Stage 1 gain resistor"))
    c_g1 = cap("47n 50V", src(source="Stage 1 AC ground return"))
    r_f1 = res("22k", src(source="Stage 1 feedback resistor (G = 23x)"))
    c_f1 = cap("18p C0G 50V", src(source="Stage 1 high-frequency roll-off"))
    r_g1[1] += amp1_fb
    r_g1[2] += c_g1[1]
    c_g1[2] += vref
    r_f1[1] += amp1_fb
    r_f1[2] += amp1_out
    c_f1[1] += amp1_fb
    c_f1[2] += amp1_out

    amp2_in = Net("US_AMP2_IN")
    amp2_fb = Net("US_AMP2_FB")
    amp2_out = Net("US_AMP2_OUT")
    c_stg2 = cap("47n 50V", src(source="Stage 1->2 AC coupling"))
    r_bias2 = res("10k", src(source="Stage 2 VREF bias"))
    c_stg2[1] += amp1_out
    c_stg2[2] += amp2_in
    r_bias2[1] += amp2_in
    r_bias2[2] += vref

    u3["5"] += amp2_in
    u3["6"] += amp2_fb
    u3["7"] += amp2_out
    r_g2 = res("1k", src(source="Stage 2 gain resistor"))
    r_f2 = res("22k", src(source="Stage 2 feedback resistor (G = 23x)"))
    r_g2[1] += amp2_fb
    r_g2[2] += vref
    r_f2[1] += amp2_fb
    r_f2[2] += amp2_out

    vth = Net("US_VTH")
    r_vth_top = res("100k", src(source="Envelope threshold bias top (+5V -> US_VTH)"))
    r_vth_bot = res("10k", src(source="Envelope threshold offset above VREF (~230 mV)"))
    r_vth_top[1] += v5
    r_vth_top[2] += vth
    r_vth_bot[1] += vth
    r_vth_bot[2] += vref

    u4 = part("gateway", "LM393", "LM393DR2G", FP_LM393,
              src(mpn="LM393DR2G", mfr="onsemi", lcsc="C7955",
                  source="Dual comparator: Comp A zero-crossing phase + Comp B envelope threshold gate"),
              ref="U4")
    u4["8"] += v5
    u4["4"] += gnd
    u4["3"] += amp2_out
    u4["2"] += vref
    u4["1"] += us_rx
    u4["5"] += amp2_out
    u4["6"] += vth
    u4["7"] += us_rx
    r_rx_pu = res("4k7", src(source="LM393 open-collector pull-up to +3V3"))
    r_rx_pu[1] += v3
    r_rx_pu[2] += us_rx
    c_u4 = cap("100n 50V", src(source="LM393 decoupling"))
    c_u4[1] += v5
    c_u4[2] += gnd

    # =========================================================================
    # 2. 4-Quadrant Piezoelectric Rain Impact AFE (4x 20 mm Murata 7BB-20-6L0 in parallel)
    # =========================================================================
    block("rain_afe")
    piezo_in = Net("PIEZO_IN")
    piezo_inv = Net("PIEZO_INV")
    piezo_stg1 = Net("PIEZO_STG1")

    for idx, quad in enumerate(("NE", "NW", "SW", "SE"), start=6):
        j_pz = jst2(f"PIEZO_{quad}", f"J{idx}",
                    f"20 mm Murata 7BB-20-6L0 piezo rain disc ({quad} quadrant, wired in parallel)")
        j_pz["1"] += piezo_in
        j_pz["2"] += gnd

    u5 = part("gateway", "TLV9062", "TLV9062IDR", FP_TLV9062,
              src(mpn="TLV9062IDR", mfr="Texas Instruments", lcsc="C398355",
                  source="Dual op-amp: Stage A 4-piezo parallel charge amp + Stage B 206 mV envelope follower"),
              ref="U5")
    u5["8"] += v5
    u5["4"] += gnd
    c_u5 = cap("100n 50V", src(source="Piezo AFE op-amp decoupling"))
    c_u5[1] += v5
    c_u5[2] += gnd

    r_p_in = res("1k", src(source="Piezo input series current-limiting resistor"))
    r_p_fb = res("100k", src(source="Piezo charge amp DC feedback bleed resistor"))
    c_p_fb = cap("18p C0G 50V", src(source="Piezo charge integration capacitor"))
    r_p_in[1] += piezo_in
    r_p_in[2] += piezo_inv
    u5["2"] += piezo_inv
    u5["3"] += gnd
    u5["1"] += piezo_stg1
    r_p_fb[1] += piezo_inv
    r_p_fb[2] += piezo_stg1
    c_p_fb[1] += piezo_inv
    c_p_fb[2] += piezo_stg1

    piezo_env = Net("PIEZO_ENV")
    r_env1 = res("10k", src(source="Rain envelope low-pass & 5V->3.3V scaling top"))
    r_env2 = res("20k", src(source="Rain envelope 5V->3.3V scaling bottom"))
    r_env_bias = res("100k", src(source="206 mV DC baseline pull-up lifting U5B out of 0 V rail dead-zone"))
    c_env = cap("220n 50V", src(source="Rain impulse envelope hold capacitor"))
    r_env1[1] += piezo_stg1
    r_env1[2] += piezo_env
    r_env2[1] += piezo_env
    r_env2[2] += gnd
    r_env_bias[1] += v3
    r_env_bias[2] += piezo_env
    c_env[1] += piezo_env
    c_env[2] += gnd

    u5["5"] += piezo_env
    u5["6"] += rain_adc
    u5["7"] += rain_adc

    # =========================================================================
    # 3. Digital Auto-North Magnetometer (LIS2MDLTR, I2C 0x1E)
    # =========================================================================
    block("compass")
    u6 = part("gateway", "LIS2MDL", "LIS2MDLTR", FP_LIS2MDL,
              src(mpn="LIS2MDLTR", mfr="STMicroelectronics", lcsc="C919695",
                  source="3-axis magnetometer for Digital True/Magnetic North auto-alignment (I2C 0x1E)"),
              ref="U6")
    lis_c1 = Net("LIS_C1")
    u6["1"] += scl
    u6["2"] += gnd
    u6["3"] += v3
    u6["4"] += sda
    u6["5"] += v3
    u6["6"] += lis_c1
    u6["7"] += gnd
    u6["8"] += gnd
    u6["9"] += v3
    u6["10"] += v3
    u6["11"] += gnd
    u6["12"] += gnd
    c_lis1 = cap("220n 50V", src(source="LIS2MDL C1 charge-pump reservoir capacitor"))
    c_lis1[1] += lis_c1
    c_lis1[2] += gnd
    c_lis2 = cap("100n 50V", src(source="LIS2MDL VDD decoupling"))
    c_lis2[1] += v3
    c_lis2[2] += gnd
