"""Top-Crown board circuit blocks:
  1. 4-Channel Bidirectional 40 kHz Ultrasonic Anemometer Transceiver:
     - J2..J5: 4x 2-pin JST-SH connectors (BM02B-SRSS-TB) for N, E, S, W waterproof transducers
     - U1: SN74LVC2G04 dual parallel CMOS inverter burst driver
     - U2: CD4052BM96 dual 4:1 analog mux cross-strapping (N<->S, E<->W)
     - U3: TLV9062IDR 2-stage active 40 kHz bandpass amplifier
     - U4: LM393DR2G zero-crossing & threshold comparator outputting US_RX
  2. Hybrid Haptic Piezoelectric + Capacitive Rain Sensor:
     - J6: 2-pin JST-SH connector for 27 mm Piezo acoustic disc + capacitive grid
     - U5: TLV9062IDR charge amplifier & envelope integrator outputting RAIN_ADC
  3. Top Optical Window & Auto-North Magnetometer:
     - U6: Lite-On LTR-390UV-01 true UV-A/UV-B index sensor (I2C 0x53)
     - U7: Vishay VEML7700-TT solar irradiance / ambient light sensor (I2C 0x10)
     - U8: ST LIS2MDLTR 3-axis magnetometer for Auto-North alignment (I2C 0x1E)
"""

from builtins import NC

from common import block, part, src
from skidl import Net

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
FP_BM02B = "gateway_fp:JST_BM02B-SRSS-TB"
FP_LVC2G04 = "gateway_fp:TI_SN74LVC2G04_SOT-23-6"
FP_CD4052 = "gateway_fp:TI_CD4052BM96_SOIC-16"
FP_TLV9062 = "gateway_fp:TI_TLV9062IDR_SOIC-8"
FP_LM393 = "gateway_fp:ON_LM393DR2G_SOIC-8"
FP_LTR390 = "gateway_fp:LiteOn_LTR-390UV-01"
FP_VEML7700 = "gateway_fp:Vishay_VEML7700-TT"
FP_LIS2MDL = "gateway_fp:ST_LIS2MDLTR_LGA-12"


def res(value, fields=None, footprint=R0603):
    return part("Device", "R", value, footprint, fields)


def cap(value, fields=None, footprint=C0603):
    return part("Device", "C", value, footprint, fields)


def build_ultrasonic(gnd, v3, v5, us_sel_a, us_sel_b, us_tx, us_rx):
    xd_n = Net("XD_N")
    xd_e = Net("XD_E")
    xd_s = Net("XD_S")
    xd_w = Net("XD_W")
    tx_drv = Net("US_TX_DRV")
    tx_ac = Net("US_TX_AC")
    rx_mux = Net("US_RX_MUX")
    vref = Net("US_VREF")
    amp1_in = Net("US_AMP1_IN")
    amp1_fb = Net("US_AMP1_FB")
    amp1_out = Net("US_AMP1_OUT")
    amp2_in = Net("US_AMP2_IN")
    amp2_fb = Net("US_AMP2_FB")
    amp2_out = Net("US_AMP2_OUT")

    # --- J2..J5: 4x 40 kHz Waterproof Ultrasonic Transducer Connectors ---
    block("us_transducers")
    for idx, (label, net_) in enumerate((("XD_NORTH", xd_n), ("XD_EAST", xd_e),
                                         ("XD_SOUTH", xd_s), ("XD_WEST", xd_w)), start=2):
        j = part("gateway", "BM02B_SRSS", label, FP_BM02B,
                 src(mpn="BM02B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160388",
                     source=f"2-pin JST-SH header for 40 kHz waterproof transducer ({label})"),
                 ref=f"J{idx}")
        j["1"] += net_
        j["2"] += gnd

    # --- U1: SN74LVC2G04 Parallel CMOS Burst Driver ---
    block("us_afe")
    u1 = part("gateway", "SN74LVC2G04", "SN74LVC2G04", FP_LVC2G04,
              src(mpn="SN74LVC2G04DBVR", mfr="TI", lcsc="C10428",
                  source="Dual parallel CMOS inverter driving 40 kHz ultrasonic burst"),
              ref="U1")
    u1["1"] += us_tx
    u1["3"] += us_tx
    u1["2"] += gnd
    u1["5"] += v5
    u1["4"] += tx_drv
    u1["6"] += tx_drv
    c_u1 = cap("100n 50V", src(source="SN74LVC2G04 decoupling"))
    c_u1[1] += v5
    c_u1[2] += gnd

    c_tx_ac = cap("100n 50V", src(source="Ultrasonic TX AC coupling capacitor"))
    r_tx_ser = res("1k", src(source="Reciprocal 1k TX source termination matching 1k RX load"))
    c_tx_ac[1] += tx_drv
    c_tx_ac[2] += r_tx_ser[1]
    r_tx_ser[2] += tx_ac

    # --- U2: CD4052BM96 Cross-Strapped TX/RX Multiplexer ---
    # (B,A) = 00: X0(N)->Y0(S), 01: X1(S)->Y1(N), 10: X2(E)->Y2(W), 11: X3(W)->Y3(E)
    u2 = part("gateway", "CD4052", "CD4052BM96", FP_CD4052,
              src(mpn="CD4052BM96", mfr="TI", lcsc="C6521",
                  source="Dual 4:1 analog mux steering TX burst & RX echo across N/S/E/W transducers"),
              ref="U2")
    u2["16"] += v5       # VDD
    u2["6"] += gnd       # INH (enabled)
    u2["7"] += gnd       # VEE
    u2["8"] += gnd       # VSS
    u2["10"] += us_sel_a # A
    u2["9"] += us_sel_b  # B
    u2["13"] += tx_ac    # X_COM (TX burst source)
    u2["12"] += xd_n     # X_CH_0
    u2["14"] += xd_s     # X_CH_1
    u2["15"] += xd_e     # X_CH_2
    u2["11"] += xd_w     # X_CH_3
    u2["3"] += rx_mux    # Y_COM (RX echo output)
    u2["1"] += xd_s      # Y_CH_0
    u2["5"] += xd_n      # Y_CH_1
    u2["2"] += xd_w      # Y_CH_2
    u2["4"] += xd_e      # Y_CH_3
    c_u2 = cap("100n 50V", src(source="CD4052 decoupling"))
    c_u2[1] += v5
    c_u2[2] += gnd

    # --- U3: TLV9062IDR 2-Stage 40 kHz Bandpass Amplifier ---
    # Mid-rail VREF = +2.5V
    r_vref1 = res("10k", src(source="Ultrasonic AFE mid-supply VREF divider top"))
    r_vref2 = res("10k", src(source="Ultrasonic AFE mid-supply VREF divider bottom"))
    c_vref = cap("100n 50V", src(source="Ultrasonic AFE VREF bypass"))
    r_vref1[1] += v5
    r_vref1[2] += vref
    r_vref2[1] += vref
    r_vref2[2] += gnd
    c_vref[1] += vref
    c_vref[2] += gnd

    c_rx_in = cap("47n 50V", src(source="RX input high-pass coupling"))
    r_rx_bias = res("1k", src(source="Reciprocal 1k RX load termination matching 1k TX source"))
    c_rx_in[1] += rx_mux
    c_rx_in[2] += amp1_in
    r_rx_bias[1] += amp1_in
    r_rx_bias[2] += vref

    u3 = part("gateway", "TLV9062", "TLV9062IDR", FP_TLV9062,
              src(mpn="TLV9062IDR", mfr="TI", lcsc="C398355",
                  source="Dual 10 MHz CMOS op-amp for 2-stage 40 kHz ultrasonic RX amplification (gain ~480x)"),
              ref="U3")
    u3["8"] += v5
    u3["4"] += gnd
    c_u3 = cap("100n 50V", src(source="TLV9062 decoupling"))
    c_u3[1] += v5
    c_u3[2] += gnd

    # Stage 1 (G = 1 + 22k/1k = 23x)
    u3["3"] += amp1_in
    u3["2"] += amp1_fb
    u3["1"] += amp1_out
    r_g1_in = res("1k", src(source="Stage 1 gain input resistor"))
    r_g1_fb = res("22k", src(source="Stage 1 gain feedback resistor (23x)"))
    r_g1_in[1] += vref
    r_g1_in[2] += amp1_fb
    r_g1_fb[1] += amp1_fb
    r_g1_fb[2] += amp1_out

    # Stage 2 (G = 1 + 22k/1k = 23x)
    c_stg2 = cap("47n 50V", src(source="Inter-stage AC coupling"))
    r_stg2_bias = res("10k", src(source="Stage 2 VREF bias"))
    c_stg2[1] += amp1_out
    c_stg2[2] += amp2_in
    r_stg2_bias[1] += amp2_in
    r_stg2_bias[2] += vref

    u3["5"] += amp2_in
    u3["6"] += amp2_fb
    u3["7"] += amp2_out
    r_g2_in = res("1k", src(source="Stage 2 gain input resistor"))
    r_g2_fb = res("22k", src(source="Stage 2 gain feedback resistor (23x)"))
    r_g2_in[1] += vref
    r_g2_in[2] += amp2_fb
    r_g2_fb[1] += amp2_fb
    r_g2_fb[2] += amp2_out

    # --- U4: LM393DR2G Envelope-Gated Zero-Crossing Dual Comparator ---
    # Comp A: zero-crossing phase detector at VREF (2.50 V)
    # Comp B: echo envelope arrival threshold detector (VTH = VREF + 230 mV via 100k/10k divider)
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
    u4["3"] += amp2_out  # IN/A+ (40 kHz signal)
    u4["2"] += vref      # IN/A- (zero-crossing reference = 2.50 V)
    u4["1"] += us_rx     # OUT/A (zero-crossing open-collector output)
    u4["5"] += amp2_out  # IN/B+ (40 kHz signal)
    u4["6"] += vth       # IN/B- (envelope threshold = 2.73 V)
    u4["7"] += us_rx     # OUT/B (wired-AND open-collector envelope gate)
    r_rx_pu = res("4k7", src(source="LM393 open-collector pull-up to +3V3"))
    r_rx_pu[1] += v3
    r_rx_pu[2] += us_rx
    c_u4 = cap("100n 50V", src(source="LM393 decoupling"))
    c_u4[1] += v5
    c_u4[2] += gnd


def build_rain_uv_compass(gnd, v3, v5, sda, scl, rain_adc):
    piezo_in = Net("PIEZO_IN")
    piezo_inv = Net("PIEZO_INV")
    piezo_stg1 = Net("PIEZO_STG1")
    piezo_env = Net("PIEZO_ENV")
    lis_c1 = Net("LIS_C1")

    # --- J6 + U5: Hybrid Haptic Piezo + Capacitive Rain Charge/Envelope AFE ---
    block("rain_afe")
    j6 = part("gateway", "BM02B_SRSS", "PIEZO_RAIN", FP_BM02B,
              src(mpn="BM02B-SRSS-TB(LF)(SN)", mfr="JST", lcsc="C160388",
                  source="2-pin JST-SH header for 27 mm piezoelectric acoustic rain disc under top dome"),
              ref="J6")
    j6["1"] += piezo_in
    j6["2"] += gnd

    u5 = part("gateway", "TLV9062", "TLV9062IDR", FP_TLV9062,
              src(mpn="TLV9062IDR", mfr="TI", lcsc="C398355",
                  source="Dual op-amp: Stage A piezo charge amplifier + Stage B drop-energy integrator"),
              ref="U5")
    u5["8"] += v5
    u5["4"] += gnd
    c_u5 = cap("100n 50V", src(source="Rain AFE op-amp decoupling"))
    c_u5[1] += v5
    c_u5[2] += gnd

    r_pz_in = res("1k", src(source="Piezo ESD current-limiting input resistor"))
    r_pz_in[1] += piezo_in
    r_pz_in[2] += piezo_inv
    r_pz_fb = res("100k", src(source="Piezo charge amplifier DC bleed resistor"))
    c_pz_fb = cap("47n 50V", src(source="Piezo charge amplifier integration capacitor"))
    r_pz_fb[1] += piezo_inv
    r_pz_fb[2] += piezo_stg1
    c_pz_fb[1] += piezo_inv
    c_pz_fb[2] += piezo_stg1
    u5["3"] += gnd
    u5["2"] += piezo_inv
    u5["1"] += piezo_stg1

    # Stage B follower + RC envelope filter to RAIN_ADC with 206 mV resting DC bias (eliminates 0 V op-amp rail dead-zone)
    r_env_top = res("10k", src(source="Rain envelope low-pass & 5V->3.3V scaling top"))
    r_env_bot = res("20k", src(source="Rain envelope 5V->3.3V scaling bottom"))
    r_env_bias = res("100k", src(source="206 mV DC baseline pull-up lifting U5B out of 0 V rail dead-zone for drizzle"))
    c_env = cap("220n 50V", src(source="Rain impulse envelope hold capacitor"))
    r_env_top[1] += piezo_stg1
    r_env_top[2] += piezo_env
    r_env_bot[1] += piezo_env
    r_env_bot[2] += gnd
    r_env_bias[1] += v3
    r_env_bias[2] += piezo_env
    c_env[1] += piezo_env
    c_env[2] += gnd
    u5["5"] += piezo_env
    u5["6"] += rain_adc
    u5["7"] += rain_adc

    # --- U6: Lite-On LTR-390UV-01 UV-A/UV-B Index Sensor (I2C 0x53) ---
    block("uv_als")
    u6 = part("gateway", "LTR390UV", "LTR-390UV-01", FP_LTR390,
              src(mpn="LTR-390UV-01", mfr="Lite-On", hand=True, footprint_ref="C492374",
                  source="Lite-On LTR-390UV-01 direct UV-A/UV-B index sensor under top fused-silica window (I2C 0x53)"),
              ref="U6")
    u6["1"] += v3
    u6["2"] += NC
    u6["3"] += gnd
    u6["4"] += scl
    u6["5"] += NC
    u6["6"] += sda
    c_u6 = cap("100n 50V", src(source="LTR-390UV decoupling"))
    c_u6[1] += v3
    c_u6[2] += gnd

    # --- U7: Vishay VEML7700-TT Solar Irradiance / Ambient Light Sensor (I2C 0x10) ---
    u7 = part("gateway", "VEML7700", "VEML7700-TT", FP_VEML7700,
              src(mpn="VEML7700-TT", mfr="Vishay", lcsc="C1850416",
                  source="Vishay VEML7700-TT 16-bit 0-140 klx solar irradiance sensor (I2C 0x10)"),
              ref="U7")
    u7["1"] += scl
    u7["2"] += v3
    u7["3"] += gnd
    u7["4"] += sda
    c_u7 = cap("100n 50V", src(source="VEML7700 decoupling"))
    c_u7[1] += v3
    c_u7[2] += gnd

    # --- U8: ST LIS2MDLTR 3-Axis Magnetometer for Auto-North Alignment (I2C 0x1E) ---
    block("compass")
    u8 = part("gateway", "LIS2MDL", "LIS2MDLTR", FP_LIS2MDL,
              src(mpn="LIS2MDLTR", mfr="STMicroelectronics", lcsc="C919695",
                  source="ST LIS2MDLTR 3-axis magnetometer for automatic True/Magnetic North wind alignment (I2C 0x1E)"),
              ref="U8")
    u8["1"] += scl     # SCL/SPC
    u8["2"] += NC      # NC
    u8["3"] += v3      # CS -> high = I2C mode
    u8["4"] += sda     # SDA/SDI/SDO
    u8["5"] += lis_c1  # C1 tank cap
    u8["6"] += gnd     # GND
    u8["7"] += NC      # INT/DRDY
    u8["8"] += gnd     # GND
    u8["9"] += v3      # Vdd
    u8["10"] += v3     # Vdd_IO
    u8["11"] += NC     # NC
    u8["12"] += NC     # NC
    c_lis_c1 = cap("220n 50V", src(source="LIS2MDLTR C1 capacitor"))
    c_lis_c1[1] += lis_c1
    c_lis_c1[2] += gnd
    c_lis_vdd = cap("100n 50V", src(source="LIS2MDLTR VDD decoupling"))
    c_lis_vdd[1] += v3
    c_lis_vdd[2] += gnd
