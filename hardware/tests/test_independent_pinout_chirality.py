"""Test 1 — Independent Datasheet Pinout, Chirality & Polarity Gate.

Operates strictly on the final routed `.kicad_pcb` (via `pcbnew`) and the exported production BOM (`<board>_bom.csv`),
with ZERO imports from `hardware/circuits/` or `hardware/scripts/`.

Verifies:
  1. Footprint chirality (`F.Cu` and `B.Cu`): signed shoelace area of pins `1..N` in each component's top mounting-face
     view proves counter-clockwise JEDEC top-view winding and catches footprints accidentally mirrored by `Flip()`.
  2. Independent MPN pin-role table: every IC, regulator, MCU, op-amp, comparator, mux, driver, sensor, MOSFET and
     crystal pin's connected net role matches a standalone datasheet-audited rule table (including asymmetric dual
     op-amps / comparators where Ch 1 is `2=IN-, 3=IN+` and Ch 2 is `5=IN+, 6=IN-` with passive feedback verification).
  3. Bus integrity & unique I2C addresses: non-swapped `SDA`/`SCL` + unique 7-bit addresses derived from physical
     address-strap pins across mated boards, SPI1 (`SCK`/`MISO`/`MOSI`/`CS` + `SI=GND`), UART/RS-485 (`TX->DI`,
     `RX<-RO`, `DE`, `A+`/`B-`), and complementary ultrasonic drive (`TX_P`/`TX_N`).
  4. Zero open or self-shorted 2-pin passives (`R`, `C`, `L`, `F`, `TH`, `Y`).
  5. DC `Anode` vs `Cathode` bias polarity + physical silkscreen cathode-mark proximity on every diode, Schottky,
     TVS, BAV99 and photodiode.
"""

from __future__ import annotations

import ast
import csv
import json
import math
import re
import subprocess
import tempfile
from pathlib import Path

import pcbnew
import pytest

REPO = Path(__file__).resolve().parents[2]
RELEASE_ROOT = REPO / "hardware/releases/v1"
BOARDS = ("main", "crown", "reflector", "dome", "dome_legacy", "probe", "power")

# Board-only footprints excluded from BOM (copper debug pads).
BOARD_ONLY_REFS = {("crown", "J12")}


def _is_nc(net: str) -> bool:
    return not net or net.startswith("unconnected-")


def _is_gnd(net: str) -> bool:
    return net == "GND"


def _is_rail(net: str, *allowed: str) -> bool:
    return net in allowed


def _has_prefix(net: str, *prefixes: str) -> bool:
    return any(net.startswith(p) for p in prefixes)


def _non_power_signal(net: str) -> bool:
    return not _is_nc(net) and net != "GND" and not net.startswith("+")


# Standalone datasheet-audited pin-role predicates keyed by production BOM MPN.
# Each entry maps pin_number -> predicate(net_name) or exact net / set of allowed net names.
DATASHEET_MPN_RULES: dict[str, dict] = {
    # TI TPS54360DDAR (SLVSBB4G, HSOIC-8 PowerPAD): 60 V 3.5 A buck
    "TPS54360DDAR": {
        "pins": {
            "1": lambda n: n.endswith("_BST"),
            "2": {"VIN_PROT"},
            "3": lambda n: n.endswith("_EN"),
            "4": lambda n: n.endswith("_RT"),
            "5": lambda n: n.endswith("_FB"),
            "6": lambda n: n.endswith("_COMP"),
            "7": {"GND"},
            "8": lambda n: n.endswith("_SW"),
            "9": {"GND"},
        },
    },
    # Diodes Inc AP2112K-3.3TRG1 (DS37425, SOT-23-5): 600 mA 3.3 V LDO
    "AP2112K-3.3TRG1": {
        "pins": {
            "1": {"+5V"},          # VIN
            "2": {"GND"},          # GND
            "3": {"+5V"},          # EN (active high)
            "4": _is_nc,           # NC
            "5": {"+3V3_PROBE"},   # VOUT
        },
    },
    # Torex XC6206P332MR-G (SOT-23): 3.3 V LDO (1=VSS, 2=VOUT, 3=VIN)
    "XC6206P332MR-G": {
        "pins": {
            "1": {"GND"},
            "2": {"+3V3"},
            "3": {"+5V"},
        },
    },
    # Seeed Studio XIAO ESP32-C6 (102010636, 2x7 module)
    "102010636": {
        "pins": {
            "1": {"VIN_ADC"},       # D0 / GPIO0 ADC1_CH0
            "2": {"NTC_SENSE"},     # D1 / GPIO1 ADC1_CH1
            "3": {"CROWN_BOOT0"},   # D2 / GPIO2
            "4": {"HEAT_PWM"},      # D3 / GPIO21
            "5": {"I2C_SDA"},       # D4 / GPIO22
            "6": {"I2C_SCL"},       # D5 / GPIO23
            "7": {"RS485_TX"},      # D6 / GPIO16 UART1 TX
            "8": {"RS485_RX"},      # D7 / GPIO17 UART1 RX
            "9": {"RS485_DE"},      # D8 / GPIO19 UART1 RTS
            "10": {"CROWN_IRQ"},    # D9 / GPIO20
            "11": {"CROWN_NRST"},   # D10 / GPIO18
            "12": {"+3V3"},         # 3V3 out
            "13": {"GND"},          # GND
            "14": {"+5V_XIAO"},     # 5V in
        },
    },
    # MaxLinear SP3485EN-L/TR (SOIC-8): 3.3 V RS-485 transceiver
    "SP3485EN-L/TR": {
        "pins": {
            "1": {"RS485_RX"},   # RO -> MCU RX
            "2": {"RS485_DE"},   # /RE
            "3": {"RS485_DE"},   # DE
            "4": {"RS485_TX"},   # DI <- MCU TX
            "5": {"GND"},
            "6": {"RS485_A"},    # non-inverting A+
            "7": {"RS485_B"},    # inverting B-
            "8": {"+3V3"},       # VCC
        },
    },
    # TI ADS1115IDGSR (SBAS444D, VSSOP-10): 16-bit I2C ADC
    "ADS1115IDGSR": {
        "pins": {
            "1": {"GND", "+3V3"},   # ADDR strap (GND=0x48, VDD=0x49)
            "2": _is_nc,            # ALERT/RDY unused (polled)
            "3": {"GND"},
            "4": _non_power_signal, # AIN0
            "5": _non_power_signal, # AIN1
            "6": lambda n: not _is_nc(n) and not n.startswith("+"),  # AIN2 (signal or GND)
            "7": _non_power_signal, # AIN3
            "8": {"+3V3"},          # VDD
            "9": {"I2C_SDA"},       # SDA
            "10": {"I2C_SCL"},      # SCL
        },
    },
    # ST LSM6DS3TR-C (DS10850, LGA-14): 6-axis IMU in Mode 1 I2C (CS=VDDIO, SA0=GND -> 0x6A)
    "LSM6DS3TR-C": {
        "pins": {
            "1": {"GND"},        # SDO/SA0 -> 0x6A
            "2": {"GND"},        # SDx tied to GND in Mode 1
            "3": {"GND"},        # SCx tied to GND in Mode 1
            "4": _is_nc,         # INT1
            "5": {"+3V3"},       # VDDIO
            "6": {"GND"},
            "7": {"GND"},
            "8": {"+3V3"},       # VDD
            "9": _is_nc,         # INT2
            "10": _is_nc,
            "11": _is_nc,
            "12": {"+3V3"},      # CS high -> I2C enabled
            "13": {"I2C_SCL"},   # SCL
            "14": {"I2C_SDA"},   # SDA
        },
    },
    # ST STM32G431RBT6 (DS12589, LQFP-64): Crown MCU
    "STM32G431RBT6": {
        "pins": {
            "1": {"+3V3"},           # VBAT
            "5": {"XTAL_IN"},        # PF0-OSC_IN
            "6": {"XTAL_OUT"},       # PF1-OSC_OUT
            "7": {"CROWN_NRST"},     # PG10-NRST
            "13": {"US_AMP_OUT"},    # PA1 OPAMP1_VINP
            "15": {"GND"},           # VSS
            "16": {"+3V3"},          # VDD
            "17": {"US_VREF"},       # PA3 OPAMP1_VINM0 (bias input!)
            "18": {"RAIN_ADC"},      # PA4 ADC2_IN17
            "22": {"I2C_SCL"},       # PC4 I2C2_SCL
            "27": {"GND"},           # VSSA
            "28": {"+3V3A"},         # VREF+
            "29": {"+3V3A"},         # VDDA
            "31": {"GND"},           # VSS
            "32": {"+3V3"},          # VDD
            "33": {"BL_PB12"},       # PB12 SPI2_NSS pull-up
            "38": {"TX_P"},          # PC6 TIM8_CH1
            "39": {"XD_EN_N"},       # PC7
            "40": {"XD_EN_E"},       # PC8
            "41": {"XD_EN_S"},       # PC9
            "42": {"I2C_SDA"},       # PA8 I2C2_SDA
            "44": {"BL_PA10"},       # PA10 USART1_RX pull-up
            "47": {"GND"},           # VSS
            "48": {"+3V3"},          # VDD
            "49": {"SWDIO"},         # PA13
            "50": {"SWCLK"},         # PA14
            "51": {"AS_CS"},         # PA15
            "52": {"TX_N"},          # PC10 TIM8_CH1N
            "53": {"XD_EN_W"},       # PC11
            "54": {"CROWN_IRQ"},     # PC12
            "56": {"AS_SCK"},        # PB3 SPI1_SCK
            "57": {"AS_MISO"},       # PB4 SPI1_MISO
            "58": {"AS_MOSI"},       # PB5 SPI1_MOSI
            "59": {"MUX_S0"},        # PB6
            "60": {"MUX_S1"},        # PB7
            "61": {"CROWN_BOOT0"},   # PB8-BOOT0
            "62": {"AS_IRQ"},        # PB9
            "63": {"GND"},           # VSS
            "64": {"+3V3"},          # VDD
        },
        "default_unused": _is_nc,
    },
    # TI UCC27524ADR (SLUSFA9, SOIC-8): Dual 5 A low-side MOSFET/transducer driver
    "UCC27524ADR": {
        "pins": {
            "1": lambda n: n.startswith("XD_EN_"),                 # ENA
            "2": {"TX_P"},                                         # INA
            "3": {"GND"},                                          # GND
            "4": {"TX_N"},                                         # INB
            "5": lambda n: n.startswith("XD_") and n.endswith("_OUT_B"),  # OUTB
            "6": {"VDRV"},                                         # VDD
            "7": lambda n: n.startswith("XD_") and n.endswith("_OUT_A"),  # OUTA
            "8": lambda n: n.startswith("XD_EN_"),                 # ENB
        },
        "custom": lambda pads: (
            [] if (pads["1"] == pads["8"] and pads["5"] != pads["7"])
            else [f"ENA({pads['1']})!=ENB({pads['8']}) or OUTA==OUTB"]
        ),
    },
    # Nexperia 74HC4052D,653 (SOIC-16): Dual 4:1 analog multiplexer
    "74HC4052D,653": {
        "pins": {
            "1": {"GND"},           # Y0 unused -> GND
            "2": {"GND"},           # Y2 unused -> GND
            "3": {"GND"},           # Y_COM unused -> GND
            "4": {"GND"},           # Y3 unused -> GND
            "5": {"GND"},           # Y1 unused -> GND
            "6": {"GND"},           # /E active-low enable
            "7": {"GND"},           # VEE
            "8": {"GND"},           # GND
            "9": {"MUX_S1"},        # S1
            "10": {"MUX_S0"},       # S0
            "11": {"XD_W_RX"},      # X3
            "12": {"XD_N_RX"},      # X0
            "13": {"US_RX_MUX"},    # X_COM
            "14": {"XD_E_RX"},      # X1
            "15": {"XD_S_RX"},      # X2
            "16": {"+3V3"},         # VCC
        },
    },
    # TI TLV9062IDR (SOIC-8) & TLV9062IDGKR (VSSOP-8): Dual RRIO op-amp
    # Asymmetric dual pinout: Ch 1 = (1:OUT1, 2:IN1-, 3:IN1+), Ch 2 = (5:IN2+, 6:IN2-, 7:OUT2), 4=V-, 8=V+
    "TLV9062IDR": {
        "pins": {
            "1": _non_power_signal,  # OUT1
            "2": _non_power_signal,  # IN1- (inverting feedback node)
            "3": _non_power_signal,  # IN1+ (non-inverting input / bias)
            "4": {"GND"},            # V-
            "5": _non_power_signal,  # IN2+ (non-inverting bias)
            "6": _non_power_signal,  # IN2- (inverting summing node)
            "7": _non_power_signal,  # OUT2
            "8": {"+3V3"},           # V+
        },
        "dual_opamp_feedback": True,
    },
    "TLV9062IDGKR": {
        "pins": {
            "1": _non_power_signal,
            "2": _non_power_signal,
            "3": _non_power_signal,
            "4": {"GND"},
            "5": _non_power_signal,
            "6": _non_power_signal,
            "7": _non_power_signal,
            "8": {"+3V3"},
        },
        "dual_opamp_feedback": True,
    },
    # onsemi LM2903DR2G (SOIC-8): Dual differential comparator
    # Asymmetric dual pinout: Ch 1 = (1:OUT1, 2:IN1-, 3:IN1+), Ch 2 = (5:IN2+, 6:IN2-, 7:OUT2), 4=GND, 8=VCC
    "LM2903DR2G": {
        "pins": {
            "1": {"CO2_GATE"},      # OUT1 open-collector
            "2": {"CO2_VSET"},      # IN1- (-7.6 C reference threshold)
            "3": {"CO2_NTC"},       # IN1+ (NTC + positive hysteresis feedback)
            "4": {"GND"},           # GND
            "5": {"CO2_VSET"},      # Unused Ch2 IN2+ parked in CMR
            "6": {"GND"},           # Unused Ch2 IN2- parked at GND != IN2+
            "7": _is_nc,            # Unused Ch2 open-collector output
            "8": {"+12V_HEAT"},     # VCC (keeps 2.76 V inputs within 0..VCC-2V CMR)
        },
    },
    # ScioSense AS3935-BQFT (QFN-16 + EP): Lightning sensor in SPI mode (SI=GND)
    "AS3935-BQFT": {
        "pins": {
            "1": {"AS_ACG"},
            "2": {"GND"},        # INM
            "3": {"AS_INP"},     # INP (500 kHz tank)
            "4": {"GND"},
            "5": {"+3V3"},       # VDD
            "6": {"+3V3"},       # VREG (EN_VREG=GND -> direct 3.3 V)
            "7": {"GND"},        # EN_VREG
            "8": {"AS_CS"},      # CS
            "9": {"GND"},        # SI=GND selects SPI
            "10": {"AS_IRQ"},    # IRQ
            "11": {"AS_SCK"},    # SCLK
            "12": {"AS_MISO"},   # MISO
            "13": {"AS_MOSI"},   # MOSI
            "14": _is_nc,
            "15": {"GND"},       # ADD0
            "16": {"GND"},       # ADD1
            "17": {"GND"},       # Exposed thermal pad
        },
    },
    # MEMSIC MMC5983MA (LGA-16): 3-axis AMR magnetometer in I2C mode (CS=VDDIO -> 0x30)
    "MMC5983MA": {
        "pins": {
            "1": {"I2C_SCL"},
            "2": {"+3V3"},       # VDD
            "4": {"+3V3"},       # CS high -> I2C mode
            "9": {"GND"},
            "10": {"MAG_CAP"},   # 10 uF SET/RESET reservoir pin
            "11": {"GND"},
            "13": {"+3V3"},      # VDDIO
            "16": {"I2C_SDA"},
        },
        "default_unused": _is_nc,
    },
    # Sensirion SCD41-D-R2 (LGA-20 + pad 21): Photoacoustic CO2 sensor (0x62)
    "SCD41-D-R2": {
        "pins": {
            "6": {"GND"},
            "7": {"+3V3_PROBE"},
            "9": {"I2C_SCL"},
            "10": {"I2C_SDA"},
            "19": {"+3V3_PROBE"},
            "20": {"GND"},
            "21": {"GND"},
        },
        "default_unused": _is_nc,
    },
    # Sensirion SGP41-D-R4 (DFN-6 + EP): VOC/NOx MOX sensor (0x59)
    "SGP41-D-R4": {
        "pins": {
            "1": {"+3V3_PROBE"},   # VDD
            "2": {"GND"},          # VSS
            "3": {"I2C_SDA"},      # SDA
            "4": {"GND"},          # R -> GND per datasheet
            "5": {"+3V3_PROBE"},   # VDDH
            "6": {"I2C_SCL"},      # SCL
            "7": {"GND"},          # Center die pad
        },
    },
    # Sensirion SHT40-AD1F-R2 (DFN-4 + EP): Temp/RH sensor (0x44)
    "SHT40-AD1F-R2": {
        "pins": {
            "1": {"I2C_SDA"},
            "2": {"I2C_SCL"},
            "3": {"+3V3_PROBE"},
            "4": {"GND"},
            "5": {"GND"},
        },
    },
    # Bosch BMP585 (LGA-9): Gel-filled barometer in I2C mode (CSB=VDDIO, SDO=VDDIO -> 0x47)
    "BMP585": {
        "pins": {
            "1": {"I2C_SCL"},      # SCX
            "2": {"I2C_SDA"},      # SDX
            "3": {"+3V3_PROBE"},   # SDO -> 0x47
            "4": {"+3V3_PROBE"},   # VDDIO
            "5": _is_nc,           # INT
            "6": {"GND"},          # VSS
            "7": {"+3V3_PROBE"},   # CSB -> I2C
            "8": {"+3V3_PROBE"},   # VDD
            "9": _is_nc,           # Solder-mask covered laser-mark pad
        },
    },
    # ams OSRAM TSL25911FN (6-pin FN): Vis+NIR light sensor (0x29)
    "TSL25911FN": {
        "pins": {
            "1": {"I2C_SCL"},
            "2": _is_nc,      # INT
            "3": {"GND"},
            "4": {"GND"},
            "5": {"+3V3"},    # VDD
            "6": {"I2C_SDA"},
        },
    },
    # Lite-On LTR-390UV-01 (6-pin): UV/ALS sensor (0x53)
    "LTR-390UV-01": {
        "pins": {
            "1": {"+3V3"},    # VDD
            "2": {"GND"},     # NC/GND
            "3": {"GND"},     # GND
            "4": {"I2C_SCL"},
            "5": _is_nc,      # INT
            "6": {"I2C_SDA"},
        },
    },
    # GenUV GUVB-C31SM (4-pin): Digital UV-B sensor (0x39)
    "GUVB-C31SM": {
        "pins": {
            "1": {"GND"},
            "2": {"I2C_SDA"},
            "3": {"I2C_SCL"},
            "4": {"+3V3"},
        },
    },
    # Alpha & Omega AO3400A (SOT-23 N-MOSFET: 1=Gate, 2=Source, 3=Drain)
    "AO3400A": {
        "pins": {
            "1": lambda n: n.endswith("_GATE"),
            "2": {"HEAT_SENSE", "CO2_HEAT_RTN"},   # low-side source to current shunt
            "3": {"HEAT_SW", "CO2_HEAT_SW"},       # switched drain load
        },
        "custom": lambda pads: (
            [] if len({pads["1"], pads["2"], pads["3"]}) == 3 else ["G/S/D shorted"]
        ),
    },
    # YXC X49SM8MSD2SI (HC-49S-SMD 8 MHz crystal)
    "X49SM8MSD2SI": {
        "pins": {
            "1": {"XTAL_IN"},
            "2": {"XTAL_OUT"},
        },
    },
    # Omron B3U-1000P tactile service push-button
    "B3U-1000P": {
        "pins": {
            "1": {"NTC_SENSE"},
            "2": {"GND"},
        },
    },
}


def load_bom_by_ref(board: str) -> dict[str, dict[str, str]]:
    bom_path = RELEASE_ROOT / board / f"{board}_bom.csv"
    assert bom_path.exists(), f"missing exported BOM: {bom_path}"
    by_ref: dict[str, dict[str, str]] = {}
    with open(bom_path, newline="") as f:
        for row in csv.DictReader(f):
            for ref in [r.strip() for r in row["Designators"].split(",") if r.strip()]:
                assert ref not in by_ref, f"{board}: duplicate ref {ref} in BOM"
                by_ref[ref] = row
    return by_ref


def load_routed_pcb(board: str) -> pcbnew.BOARD:
    pcb_path = RELEASE_ROOT / board / f"{board}.kicad_pcb"
    assert pcb_path.exists(), f"missing released PCB: {pcb_path}"
    return pcbnew.LoadBoard(str(pcb_path))


def ref_prefix(ref: str) -> str:
    m = re.match(r"^([A-Za-z]+)", ref)
    return m.group(1) if m else ""


def test_zero_generator_imports():
    """Guard: this gate file must have zero imports from `skidl`, `hardware/circuits/`, or `hardware/scripts/`."""
    tree = ast.parse(Path(__file__).read_text())
    imported = [
        alias.name for node in ast.walk(tree)
        if isinstance(node, ast.Import) for alias in node.names
    ] + [
        node.module or "" for node in ast.walk(tree)
        if isinstance(node, ast.ImportFrom)
    ]
    forbidden = [m for m in imported if m in ("skidl", "common", "product_spec", "layout", "gen_circuits", "jlc_parts")
                 or m.startswith(("variants", "blocks"))]
    assert not forbidden, f"independent gate imported generator modules: {forbidden}"


@pytest.mark.parametrize("board", BOARDS)
def test_bom_covers_every_populated_footprint(board: str):
    pcb = load_routed_pcb(board)
    bom = load_bom_by_ref(board)
    pcb_refs = {fp.GetReference() for fp in pcb.Footprints() if (board, fp.GetReference()) not in BOARD_ONLY_REFS}
    assert pcb_refs == set(bom.keys()), (
        f"{board}: PCB vs BOM designator mismatch: only_pcb={sorted(pcb_refs - set(bom))}, "
        f"only_bom={sorted(set(bom) - pcb_refs)}"
    )


@pytest.mark.parametrize("board", BOARDS)
def test_footprint_chirality_jedec_top_view_ccw(board: str):
    """Signed shoelace area of pins `1..N` in each component's top mounting-face view must be > 0 (CCW) on both
    `F.Cu` and `B.Cu`, catching mirrored footprints or inverted `Flip()` transforms."""
    pcb = load_routed_pcb(board)
    problems = []

    for fp in sorted(pcb.Footprints(), key=lambda item: item.GetReference()):
        ref = fp.GetReference()
        fpid = str(fp.GetFPID().GetLibItemName())
        flipped = fp.IsFlipped()

        # Collect numbered signal pads (1..N), converting to top-mounting-face view:
        # F.Cu: (x_right, y_up) = (+ToMM(x), -ToMM(y)); B.Cu viewed through its top face flips x -> -x.
        numbered: dict[int, tuple[float, float]] = {}
        for pad in fp.Pads():
            num = pad.GetNumber()
            if num.isdigit():
                px = pcbnew.ToMM(pad.GetPosition().x)
                py = -pcbnew.ToMM(pad.GetPosition().y)
                mx = -px if flipped else px
                numbered.setdefault(int(num), (mx, py))

        if len(numbered) < 3:
            continue
        nums = sorted(numbered)
        if nums != list(range(1, len(nums) + 1)):
            continue

        # Exclude central exposed thermal/mark pad (`N`) when it sits near the centroid of `1..N-1`.
        if len(nums) >= 5:
            cx = sum(numbered[k][0] for k in nums[:-1]) / (len(nums) - 1)
            cy = sum(numbered[k][1] for k in nums[:-1]) / (len(nums) - 1)
            if math.hypot(numbered[nums[-1]][0] - cx, numbered[nums[-1]][1] - cy) < 0.65:
                nums = nums[:-1]

        pts = [numbered[k] for k in nums]

        # Check collinearity (single-row 1xN headers/connectors).
        vx, vy = pts[-1][0] - pts[0][0], pts[-1][1] - pts[0][1]
        span = math.hypot(vx, vy)
        if span > 0 and all(abs((px - pts[0][0]) * vy - (py - pts[0][1]) * vx) / span < 0.03 for px, py in pts):
            # Single-row connector: assert uniform monotonic pitch along 1..N.
            steps = [math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]) for i in range(len(pts) - 1)]
            if min(steps) < 0.8 * max(steps):
                problems.append(f"{ref} ({fpid}): non-uniform 1xN pin pitch {steps}")
            for i in range(len(pts) - 1):
                dot = (pts[i + 1][0] - pts[i][0]) * vx + (pts[i + 1][1] - pts[i][1]) * vy
                if dot <= 0:
                    problems.append(f"{ref} ({fpid}): non-monotonic pin order at pin {i + 1}")
            continue

        # Dual-row odd/even header (Tag-Connect TC2030 2x03: odd row 1,3,5; even row 2,4,6).
        if "Odd_Even" in fpid or "TC2030" in fpid:
            r_vec = (numbered[2][0] - numbered[1][0], numbered[2][1] - numbered[1][1])
            c_vec = (numbered[3][0] - numbered[1][0], numbered[3][1] - numbered[1][1])
            cross = r_vec[0] * c_vec[1] - r_vec[1] * c_vec[0]
            dot = r_vec[0] * c_vec[0] + r_vec[1] * c_vec[1]
            if cross >= -0.5 or abs(dot) > 0.05:
                problems.append(f"{ref} ({fpid}): bad TC2030 row/col chirality cross={cross:.3f} dot={dot:.3f}")
            continue

        # Staggered (zig-zag) 1xN connectors (e.g. HX_PZ2.54-1x4P-SMD, KF250-3.5-4P): odd pins (1,3,..) on one side
        # of the connector axis and even pins (2,4,..) on the other form a zero-shoelace bowtie, but `(p2-p1) x (p3-p1)`
        # strictly encodes top-view zig-zag chirality (> 0) and flips sign if mirrored!
        if ref_prefix(ref) == "J" and len(pts) >= 4:
            axis = (pts[2][0] - pts[0][0], pts[2][1] - pts[0][1])
            axis_len = math.hypot(*axis)
            if axis_len > 0:
                projs = [((px - pts[0][0]) * axis[0] + (py - pts[0][1]) * axis[1]) / axis_len for px, py in pts]
                steps = [projs[i + 1] - projs[i] for i in range(len(projs) - 1)]
                if min(steps) > 0.85 * max(steps) > 0:
                    zig_cross = (pts[1][0] - pts[0][0]) * axis[1] - (pts[1][1] - pts[0][1]) * axis[0]
                    if zig_cross <= 0.1:
                        side = "B.Cu" if flipped else "F.Cu"
                        problems.append(f"{ref} ({fpid} on {side}): mirrored staggered connector (cross={zig_cross:+.3f} <= 0)")
                    continue

        # Standard JEDEC perimeter / DIL / quad / SOT package: signed shoelace 2*Area > 0 proves CCW top-view winding.
        area2 = sum(
            pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1]
            for i in range(len(pts))
        )
        if area2 <= 0.1:
            side = "B.Cu" if flipped else "F.Cu"
            problems.append(f"{ref} ({fpid} on {side}): mirrored/CW pin winding (2*area={area2:+.3f} mm^2 <= 0)")

    assert not problems, f"{board} chirality errors: " + "; ".join(problems)


@pytest.mark.parametrize("board", BOARDS)
def test_independent_datasheet_mpn_pin_roles(board: str):
    """Every active IC, regulator, transistor, sensor, crystal and switch pin must satisfy its standalone datasheet
    pin-role rule, including dual op-amp negative-feedback topology."""
    pcb = load_routed_pcb(board)
    bom = load_bom_by_ref(board)
    problems = []

    # Build passive 2-pin adjacency `{frozenset({netA, netB})}` to verify op-amp/comparator feedback networks.
    passive_bridges: set[frozenset[str]] = set()
    for fp in pcb.Footprints():
        if ref_prefix(fp.GetReference()) in ("R", "C"):
            pnets = {p.GetNetname() for p in fp.Pads() if p.GetNumber() and not _is_nc(p.GetNetname())}
            if len(pnets) == 2:
                passive_bridges.add(frozenset(pnets))

    for fp in sorted(pcb.Footprints(), key=lambda item: item.GetReference()):
        ref = fp.GetReference()
        if (board, ref) in BOARD_ONLY_REFS:
            continue
        pfx = ref_prefix(ref)
        if pfx in ("R", "C", "L", "F", "TH", "J", "D"):
            continue

        mpn = bom[ref]["MPN"]
        assert mpn in DATASHEET_MPN_RULES, f"{board}:{ref} active/IC MPN {mpn!r} missing from DATASHEET_MPN_RULES"
        rule = DATASHEET_MPN_RULES[mpn]
        pin_rules = rule["pins"]
        default_unused = rule.get("default_unused")

        pads = {p.GetNumber(): p.GetNetname() for p in fp.Pads() if p.GetNumber()}
        for pnum, pred in pin_rules.items():
            if pnum not in pads:
                problems.append(f"{ref} ({mpn}): missing pad {pnum}")
                continue
            net = pads[pnum]
            ok = (net in pred) if isinstance(pred, set) else bool(pred(net))
            if not ok:
                problems.append(f"{ref} ({mpn}) pin {pnum}: net {net!r} violates datasheet role")

        if default_unused is not None:
            for pnum, net in pads.items():
                if pnum not in pin_rules and not default_unused(net):
                    problems.append(f"{ref} ({mpn}) unused pin {pnum}: expected NC, got {net!r}")

        if "custom" in rule:
            for msg in rule["custom"](pads):
                problems.append(f"{ref} ({mpn}): {msg}")

        if rule.get("dual_opamp_feedback"):
            # Ch 1: OUT1=1, IN1-=2, IN1+=3; Ch 2: IN2+=5, IN2-=6, OUT2=7 (mirrored!).
            for ch, out_p, inm_p, inp_p in (("Ch1", "1", "2", "3"), ("Ch2", "7", "6", "5")):
                n_out, n_inm, n_inp = pads[out_p], pads[inm_p], pads[inp_p]
                if n_inm == n_inp:
                    problems.append(f"{ref} ({mpn}) {ch}: IN- and IN+ shorted on {n_inm!r}")
                if frozenset((n_out, n_inm)) not in passive_bridges:
                    problems.append(
                        f"{ref} ({mpn}) {ch}: missing negative feedback R/C between OUT({n_out!r}) and IN-({n_inm!r}) "
                        f"(check asymmetric dual pinout 2=IN1-,3=IN1+ vs 5=IN2+,6=IN2-)"
                    )

    # Probe LM2903 positive hysteresis check: R7 must bridge OUT1 (pin 1) and IN1+ (pin 3), NOT IN1- (pin 2)!
    if board == "probe":
        u4 = pcb.FindFootprintByReference("U4")
        u4_pads = {p.GetNumber(): p.GetNetname() for p in u4.Pads()}
        assert frozenset((u4_pads["1"], u4_pads["3"])) in passive_bridges, (
            "probe:U4 LM2903 missing positive hysteresis resistor between OUT1 (pin 1) and IN1+ (pin 3)"
        )
        assert frozenset((u4_pads["1"], u4_pads["2"])) not in passive_bridges, (
            "probe:U4 LM2903 hysteresis resistor wired to IN1- instead of IN1+"
        )

    assert not problems, f"{board} datasheet pin-role violations: " + "; ".join(problems)


@pytest.mark.parametrize("board", BOARDS)
def test_zero_shorted_or_open_two_pin_passives(board: str):
    pcb = load_routed_pcb(board)
    problems = []
    for fp in sorted(pcb.Footprints(), key=lambda item: item.GetReference()):
        ref = fp.GetReference()
        if ref_prefix(ref) not in ("R", "C", "L", "F", "TH", "Y"):
            continue
        pads = [p for p in fp.Pads() if p.GetNumber()]
        assert len(pads) == 2, f"{board}:{ref} expected 2 pads, got {len(pads)}"
        n1, n2 = pads[0].GetNetname(), pads[1].GetNetname()
        if _is_nc(n1) or _is_nc(n2):
            problems.append(f"{ref}: open pad ({n1!r}, {n2!r})")
        elif n1 == n2:
            problems.append(f"{ref}: shorted across single net {n1!r}")
    assert not problems, f"{board} passive connectivity errors: " + "; ".join(problems)


# Expected diode DC bias `(anode_net, cathode_net)` or multi-junction rule per `(board, ref)`.
DIODE_POLARITY_RULES: dict[tuple[str, str], dict] = {
    # Main board:
    # D1 SMAJ36A unidirectional 36V TVS: Anode(2)=GND, Cathode(1)=VIN_PROT (reverse biased at +12..32 V)
    ("main", "D1"): {"kind": "2pin", "anode_pad": "2", "cathode_pad": "1", "anode": "GND", "cathode": "VIN_PROT"},
    # D2 SS16 reverse-polarity series Schottky: Anode(2)=VIN_FUSED -> Cathode(1)=VIN_PROT (forward biased)
    ("main", "D2"): {"kind": "2pin", "anode_pad": "2", "cathode_pad": "1", "anode": "VIN_FUSED", "cathode": "VIN_PROT"},
    # D3 SS16 heater flyback Schottky: Anode(2)=HEAT_SW -> Cathode(1)=+12V_HEAT (reverse biased when off/on, clamps flyback)
    ("main", "D3"): {"kind": "2pin", "anode_pad": "2", "cathode_pad": "1", "anode": "HEAT_SW", "cathode": "+12V_HEAT"},
    # D4 PSM712 SOT-23 asymmetric RS-485 surge TVS: 1=RS485_A, 2=RS485_B, 3=GND common anode
    ("main", "D4"): {"kind": "sot23_tvs", "pins": {"1": "RS485_A", "2": "RS485_B", "3": "GND"}},
    # D5 SS14 USB back-feed block Schottky: Anode(2)=+5V -> Cathode(1)=+5V_XIAO
    ("main", "D5"): {"kind": "2pin", "anode_pad": "2", "cathode_pad": "1", "anode": "+5V", "cathode": "+5V_XIAO"},
    # Power board:
    # D1, D2 B360A buck catch Schottkys: Anode(2)=GND -> Cathode(1)=SW (reverse biased when high-side FET conducts)
    ("power", "D1"): {"kind": "2pin", "anode_pad": "2", "cathode_pad": "1", "anode": "GND", "cathode": "LOGIC_SW"},
    ("power", "D2"): {"kind": "2pin", "anode_pad": "2", "cathode_pad": "1", "anode": "GND", "cathode": "HEAT_SW"},
    # Dome board:
    # D1 GUVB-C21SD & D2 GUVA-C22SD zero-bias photodiodes (COB2418: pad 1=Anode on UV_VB, pad 2=Cathode on TIA IN-)
    ("dome", "D1"): {"kind": "2pin", "anode_pad": "1", "cathode_pad": "2", "anode": "UV_VB", "cathode": "UVB_IN"},
    ("dome", "D2"): {"kind": "2pin", "anode_pad": "1", "cathode_pad": "2", "anode": "UV_VB", "cathode": "UVA_IN"},
    # Crown board:
    # D1..D8 BAV99 SOT-23 anti-parallel T/R isolation diodes (pins 1+2 tied on driver OUT, pin 3 on transducer lead)
    ("crown", "D1"): {"kind": "bav99_antiparallel", "drv": "XD_N_OUT_A", "xd": "XD_N_A"},
    ("crown", "D2"): {"kind": "bav99_antiparallel", "drv": "XD_N_OUT_B", "xd": "XD_N_B"},
    ("crown", "D3"): {"kind": "bav99_antiparallel", "drv": "XD_E_OUT_A", "xd": "XD_E_A"},
    ("crown", "D4"): {"kind": "bav99_antiparallel", "drv": "XD_E_OUT_B", "xd": "XD_E_B"},
    ("crown", "D5"): {"kind": "bav99_antiparallel", "drv": "XD_S_OUT_A", "xd": "XD_S_A"},
    ("crown", "D6"): {"kind": "bav99_antiparallel", "drv": "XD_S_OUT_B", "xd": "XD_S_B"},
    ("crown", "D7"): {"kind": "bav99_antiparallel", "drv": "XD_W_OUT_A", "xd": "XD_W_A"},
    ("crown", "D8"): {"kind": "bav99_antiparallel", "drv": "XD_W_OUT_B", "xd": "XD_W_B"},
    # D9 BAV99 SOT-23 series rail clamp on piezo input: 1(A1)=GND, 2(K2)=+3V3, 3(K1/A2)=PIEZO_IN
    ("crown", "D9"): {"kind": "sot23_clamp", "pins": {"1": "GND", "2": "+3V3", "3": "PIEZO_IN"}},
}


@pytest.mark.parametrize("board", BOARDS)
def test_diode_dc_polarity_and_silkscreen_cathode_mark(board: str):
    """Every diode, Schottky, TVS and photodiode must have the right DC Anode/Cathode polarity AND a physical
    silkscreen polarity mark on its mounting face strictly closer to Cathode (or Pin 1 on SOT-23) than Anode."""
    pcb = load_routed_pcb(board)
    problems = []

    for fp in sorted(pcb.Footprints(), key=lambda item: item.GetReference()):
        ref = fp.GetReference()
        if ref_prefix(ref) != "D":
            continue
        key = (board, ref)
        assert key in DIODE_POLARITY_RULES, f"unverified diode {board}:{ref}"
        rule = DIODE_POLARITY_RULES[key]

        pads = {p.GetNumber(): p for p in fp.Pads() if p.GetNumber()}
        pad_nets = {k: p.GetNetname() for k, p in pads.items()}
        pad_xy = {
            k: (pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y))
            for k, p in pads.items()
        }

        silk_layer = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
        silk_pts = []
        for gi in fp.GraphicalItems():
            if gi.GetLayer() == silk_layer and isinstance(gi, pcbnew.PCB_SHAPE):
                bb = gi.GetBoundingBox()
                silk_pts.append((pcbnew.ToMM(bb.GetCenter().x), -pcbnew.ToMM(bb.GetCenter().y), gi.ShowShape()))

        if not silk_pts:
            problems.append(f"{ref}: zero silkscreen polarity graphics on {'B.SilkS' if fp.IsFlipped() else 'F.SilkS'}")
            continue

        if rule["kind"] == "2pin":
            ap, kp = rule["anode_pad"], rule["cathode_pad"]
            if pad_nets.get(ap) != rule["anode"] or pad_nets.get(kp) != rule["cathode"]:
                problems.append(
                    f"{ref}: polarity mismatch got A({ap})={pad_nets.get(ap)!r}, K({kp})={pad_nets.get(kp)!r}; "
                    f"expected A={rule['anode']!r}, K={rule['cathode']!r}"
                )
                continue
            ax, ay = pad_xy[ap]
            kx, ky = pad_xy[kp]
            # Both minimum silk-feature distance and silk centroid must sit closer to Cathode than Anode.
            min_k = min(math.hypot(sx - kx, sy - ky) for sx, sy, _ in silk_pts)
            min_a = min(math.hypot(sx - ax, sy - ay) for sx, sy, _ in silk_pts)
            cx = sum(sx for sx, _, _ in silk_pts) / len(silk_pts)
            cy = sum(sy for _, sy, _ in silk_pts) / len(silk_pts)
            cent_k = math.hypot(cx - kx, cy - ky)
            cent_a = math.hypot(cx - ax, cy - ay)
            if not (min_k < min_a and cent_k < cent_a):
                problems.append(
                    f"{ref}: silkscreen mark closer to Anode than Cathode "
                    f"(min_k={min_k:.2f} vs min_a={min_a:.2f}, cent_k={cent_k:.2f} vs cent_a={cent_a:.2f})"
                )
        elif rule["kind"] == "bav99_antiparallel":
            expected = {"1": rule["drv"], "2": rule["drv"], "3": rule["xd"]}
            if pad_nets != expected:
                problems.append(f"{ref}: BAV99 nets {pad_nets} != {expected}")
        elif rule["kind"] in ("sot23_tvs", "sot23_clamp"):
            if pad_nets != rule["pins"]:
                problems.append(f"{ref}: SOT-23 diode nets {pad_nets} != {rule['pins']}")

        if rule["kind"] != "2pin":
            # SOT-23 pin-1 marker (polygon/dot) must sit strictly closer to pad 1 than pad 2 or pad 3.
            p1x, p1y = pad_xy["1"]
            p2x, p2y = pad_xy["2"]
            p3x, p3y = pad_xy["3"]
            markers = [(sx, sy) for sx, sy, kind in silk_pts if kind == "Polygon"] or [(sx, sy) for sx, sy, _ in silk_pts]
            d1 = min(math.hypot(sx - p1x, sy - p1y) for sx, sy in markers)
            d2 = min(math.hypot(sx - p2x, sy - p2y) for sx, sy in markers)
            d3 = min(math.hypot(sx - p3x, sy - p3y) for sx, sy in markers)
            if not (d1 < d2 and d1 < d3):
                problems.append(f"{ref}: SOT-23 pin-1 silkscreen marker not closest to pad 1 (d1={d1:.2f}, d2={d2:.2f}, d3={d3:.2f})")

    assert not problems, f"{board} diode polarity/silkscreen errors: " + "; ".join(problems)


def _derive_i2c_devices(board: str) -> list[tuple[str, str, int]]:
    """Derive `(board:ref, mpn, 7bit_addr)` strictly from BOM MPN and physical address-strap pad nets on the PCB."""
    pcb = load_routed_pcb(board)
    bom = load_bom_by_ref(board)
    found: list[tuple[str, str, int]] = []

    for fp in sorted(pcb.Footprints(), key=lambda item: item.GetReference()):
        ref = fp.GetReference()
        if (board, ref) in BOARD_ONLY_REFS:
            continue
        mpn = bom[ref]["MPN"]
        pads = {p.GetNumber(): p.GetNetname() for p in fp.Pads() if p.GetNumber()}

        if mpn == "ADS1115IDGSR":
            # SBAS444D Table 4: ADDR -> GND=0x48, VDD=0x49, SDA=0x4A, SCL=0x4B
            addr_map = {"GND": 0x48, "+3V3": 0x49, "I2C_SDA": 0x4A, "I2C_SCL": 0x4B}
            assert pads["1"] in addr_map, f"{board}:{ref} invalid ADS1115 ADDR strap {pads['1']!r}"
            found.append((f"{board}:{ref}", mpn, addr_map[pads["1"]]))
        elif mpn == "LSM6DS3TR-C":
            addr = 0x6A if pads["1"] == "GND" else 0x6B
            found.append((f"{board}:{ref}", mpn, addr))
        elif mpn == "BMP585":
            addr = 0x46 if pads["3"] == "GND" else 0x47
            found.append((f"{board}:{ref}", mpn, addr))
        elif mpn == "STM32G431RBT6":
            found.append((f"{board}:{ref}", mpn, 0x2A))
        elif mpn == "MMC5983MA":
            found.append((f"{board}:{ref}", mpn, 0x30))
        elif mpn == "SCD41-D-R2":
            found.append((f"{board}:{ref}", mpn, 0x62))
        elif mpn == "SGP41-D-R4":
            found.append((f"{board}:{ref}", mpn, 0x59))
        elif mpn == "SHT40-AD1F-R2":
            found.append((f"{board}:{ref}", mpn, 0x44))
        elif mpn == "TSL25911FN":
            found.append((f"{board}:{ref}", mpn, 0x29))
        elif mpn == "LTR-390UV-01":
            found.append((f"{board}:{ref}", mpn, 0x53))
        elif mpn == "GUVB-C31SM":
            found.append((f"{board}:{ref}", mpn, 0x39))
    return found


@pytest.mark.parametrize("dome_board", ("dome", "dome_legacy"))
def test_non_swapped_buses_and_unique_i2c_addresses(dome_board: str):
    """Verify I2C SDA/SCL continuity, pull-ups and unique 7-bit addresses across the whole station, plus non-swapped
    SPI1, RS-485 UART/A-B, and board-to-board connector pinouts."""
    station_boards = ("main", "crown", "probe", dome_board)
    devices = [dev for b in station_boards for dev in _derive_i2c_devices(b)]
    seen_addrs: dict[int, str] = {}
    collisions = []
    for tag, mpn, addr in devices:
        if addr in seen_addrs:
            collisions.append(f"0x{addr:02X} shared by {seen_addrs[addr]} and {tag} ({mpn})")
        seen_addrs[addr] = f"{tag} ({mpn})"
    assert not collisions, "I2C address collisions: " + "; ".join(collisions)

    # Verify main I2C pull-ups exist on both I2C_SDA and I2C_SCL to +3V3, and SDA != SCL.
    main_pcb = load_routed_pcb("main")
    main_bom = load_bom_by_ref("main")
    pullups = {}
    for fp in main_pcb.Footprints():
        ref = fp.GetReference()
        if ref_prefix(ref) == "R":
            pnets = {p.GetNetname() for p in fp.Pads() if p.GetNumber()}
            if "+3V3" in pnets and ("I2C_SDA" in pnets or "I2C_SCL" in pnets):
                bus = "I2C_SDA" if "I2C_SDA" in pnets else "I2C_SCL"
                pullups[bus] = main_bom[ref]["Value"]
    assert pullups == {"I2C_SDA": "2k2", "I2C_SCL": "2k2"}, f"missing/wrong I2C pull-ups on main: {pullups}"

    # Verify RS-485 terminal J1 polarity & fail-safe bias: J1.3=RS485_A (pulled to +3V3), J1.4=RS485_B (pulled to GND).
    j1_pads = {p.GetNumber(): p.GetNetname() for p in main_pcb.FindFootprintByReference("J1").Pads() if p.GetNumber()}
    assert j1_pads == {"1": "VIN_RAW", "2": "GND", "3": "RS485_A", "4": "RS485_B"}, f"main J1 wrong pinout: {j1_pads}"
    r_nets = [
        ({p.GetNetname() for p in fp.Pads() if p.GetNumber()}, main_bom[fp.GetReference()]["Value"])
        for fp in main_pcb.Footprints() if ref_prefix(fp.GetReference()) == "R"
    ]
    assert ({"RS485_A", "+3V3"}, "390R") in r_nets, "RS485_A missing 390R fail-safe pull-up to +3V3"
    assert ({"RS485_B", "GND"}, "390R") in r_nets, "RS485_B missing 390R fail-safe pull-down to GND"

    # Verify board-to-board connector pin-for-pin signal parity directly from PCB pads (no spec.yaml import).
    mated_pairs = (
        ("main", "J2", "crown", "J1"),
        ("main", "J3", "probe", "J1"),
        ("main", "J5", "power", "J1"),
        ("crown", "J10", dome_board, "J1"),
        ("probe", "J2", "reflector", "J1"),
    )
    for b_a, r_a, b_b, r_b in mated_pairs:
        fp_a = load_routed_pcb(b_a).FindFootprintByReference(r_a)
        fp_b = load_routed_pcb(b_b).FindFootprintByReference(r_b)
        pins_a = {p.GetNumber(): p.GetNetname() for p in fp_a.Pads() if p.GetNumber().isdigit()}
        pins_b = {p.GetNumber(): p.GetNetname() for p in fp_b.Pads() if p.GetNumber().isdigit()}
        assert pins_a == pins_b, f"connector mismatch {b_a}:{r_a} {pins_a} != {b_b}:{r_b} {pins_b}"


@pytest.mark.parametrize("board", BOARDS)
def test_every_ic_has_pin1_silkscreen_indicator_and_zero_silk_over_pads(board: str):
    """Every multi-pin IC (`U*`) must have a physical silkscreen Pin-1 indicator closer to Pin 1 than to the opposite
    corner pin, and `kicad-cli pcb drc --severity-all` must report zero `silk_over_copper` / `silk_overlap` violations."""
    pcb_path = RELEASE_ROOT / board / f"{board}.kicad_pcb"
    pcb = pcbnew.LoadBoard(str(pcb_path))
    problems = []

    for fp in sorted(pcb.Footprints(), key=lambda item: item.GetReference()):
        ref = fp.GetReference()
        if ref_prefix(ref) != "U":
            continue
        pads = {
            int(p.GetNumber()): (pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y))
            for p in fp.Pads() if p.GetNumber().isdigit()
        }
        if len(pads) < 4 or 1 not in pads:
            continue
        silk_layer = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
        # Prefer explicit pin-1 dot/polygon/arc markers; fall back to silk shapes if package uses chamfer/notch silk.
        dots = []
        any_silk = []
        for gi in fp.GraphicalItems():
            if gi.GetLayer() == silk_layer and isinstance(gi, pcbnew.PCB_SHAPE):
                bb = gi.GetBoundingBox()
                pt = (pcbnew.ToMM(bb.GetCenter().x), -pcbnew.ToMM(bb.GetCenter().y))
                any_silk.append(pt)
                if gi.ShowShape() in ("Circle", "Polygon", "Arc"):
                    dots.append(pt)
        markers = dots or any_silk
        if not markers:
            problems.append(f"{ref} ({fp.GetFPID().GetLibItemName()}): missing Pin-1 silkscreen indicator")
            continue
        p1 = pads[1]
        # Opposite corner pad = farthest pad from Pin 1.
        opp_pin, p_opp = max(pads.items(), key=lambda kv: math.hypot(kv[1][0] - p1[0], kv[1][1] - p1[1]))
        d1 = min(math.hypot(mx - p1[0], my - p1[1]) for mx, my in markers)
        d_opp = min(math.hypot(mx - p_opp[0], my - p_opp[1]) for mx, my in markers)
        if not (d1 < d_opp):
            problems.append(
                f"{ref} ({fp.GetFPID().GetLibItemName()}): Pin-1 silk mark not closer to Pin 1 ({d1:.2f} mm) "
                f"than Pin {opp_pin} ({d_opp:.2f} mm)"
            )

    with tempfile.TemporaryDirectory() as tmp:
        rep = Path(tmp) / "silk_drc.json"
        subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--severity-all",
                        "--output", str(rep), str(pcb_path)], check=True, capture_output=True)
        drc = json.loads(rep.read_text())
        for v in drc["violations"]:
            if v["type"] in ("silk_over_copper", "silk_overlap", "silk_edge_clearance"):
                desc = ", ".join(i["description"] for i in v.get("items", []))
                problems.append(f"{v['type']}: {desc}")

    assert not problems, f"{board} IC Pin-1 / silkscreen-over-pad errors: " + "; ".join(problems)

