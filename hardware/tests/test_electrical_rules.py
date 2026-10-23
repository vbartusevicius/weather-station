"""Checklist T6: electrical rules that DRC / ERC cannot see.

1. Every purchased part is rated for the -40 C outdoor minimum (JLCPCB "Operating Temperature", the MLCC dielectric
   class, or a datasheet value below), or is listed in ACCEPTED_COLD with the reason the station tolerates it.
2. Logic levels and comparator input ranges across supply domains, with datasheet limits (worst case).
"""

import json
import re
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
OUTDOOR_MIN_C = -40

# Datasheet operating ranges for parts whose JLCPCB record has no temperature attribute.
DATASHEET_MIN_C = {
    "SS16": -55,              # MDD SS12..SS16 datasheet: Tj / Tstg -55..+150 C
    "BAV99,215": -65,         # Nexperia BAV99: Tamb -65..+150 C
    "B360A-13-F": -65,        # Diodes Inc. B360A: -65..+150 C
    "AS3935-BQFT": -40,       # ScioSense AS3935 datasheet: -40..+85 C
    "SHT40-AD1F-R2": -40,     # Sensirion SHT4x: -40..+125 C
    "SWPA6045S220MT": -40,    # Sunlord SWPA series: -40..+125 C
    "SWPA6045S8R2MT": -40,
    "MA5532-AEB": -40,        # Coilcraft MA5532-AE: -40..+85 C
    "XIAO-ESP32-C6": -40,     # Seeed XIAO ESP32-C6 product brief (Digi-Key 102010574): working -40..+85 C
    "HX PM2.54-1x4P WT": -40, # PBT header, reflector <-> probe (heated zone)
}
# MLCC temperature characteristic -> lower limit (EIA): X7R / X5R / C0G (NP0) are all -55 C.
MLCC_CODE = {"X7R": -55, "X5R": -55, "C0G": -55, "NP0": -55}

# Parts below -40 C that the design tolerates, with the reason. Anything new must be justified here.
ACCEPTED_COLD = {
    "BM02B-SRSS-TB(LF)(SN)": "JST SH -25 C: passive contacts inside the sealed, sun-warmed enclosure; S5",
    "BM08B-SRSS-TB(LF)(SN)": "JST SH -25 C (base bay next to the bucks); S5",
    "BM10B-SRSS-TB(LF)(SN)": "JST SH -25 C (base bay next to the bucks); S5",
    "BM12B-SRSS-TB(LF)(SN)": "JST SH -25 C: passive contacts; S5",
    "B6B-ZR-SM4-TF(LF)(SN)": "JST ZH -25 C: passive contacts in the base bay; S5",
    "B3U-1000P": "Omron B3U -25 C: service button, only pressed during maintenance",
    "TSL25911FN": "-30 C: optical sensor under the dome, measurement degrades only (S4)",
    "SCD41-D-R2": "-10 C: probe heater holds the SCD41 zone at -7.6 C or warmer (S5)",
    "SGP41-D-R4": "datasheet -20 C: next to the heated SCD41 zone; readings flagged in deep cold (S5)",
    "GUVB-C31SM": "GenUV -30 C (distributor data): UV measurement only, inside the sun-warmed dome (S4)",
}


def _min_temp(record):
    attr = next((a["attribute_value_name"] for a in record.get("attributes") or []
                 if a["attribute_name_en"] == "Operating Temperature"), None)
    if attr:
        nums = [int(n) for n in re.findall(r"[-+]?\d+", attr)]
        return nums[0] if nums else None
    model = record.get("componentModelEn", "")
    if model in DATASHEET_MIN_C:
        return DATASHEET_MIN_C[model]
    for code, t in MLCC_CODE.items():   # Samsung CL..A/B/C, YAGEO CC0603K/J R X7R
        if code in model or (model.startswith("CL") and model[4:5] in "ABC") or "KRX7R" in model:
            return t
    return None


def test_every_part_is_rated_for_outdoor_cold():
    ref = json.loads((REPO / "hardware/product/jlc_reference.json").read_text())["parts"]
    problems = []
    for parts_json in sorted((REPO / "hardware/build/v1").glob("*/parts.json")):
        if parts_json.parent.name == "panel":
            continue
        for p in json.loads(parts_json.read_text()):
            if p.get("assembly") == "none":
                continue
            key = p["lcsc"] or p.get("footprint_ref", "")
            record = ref.get(key, {}).get("record", {})
            model = record.get("componentModelEn") or p.get("mpn", "")
            if model in ACCEPTED_COLD or p.get("mpn", "") in ACCEPTED_COLD:
                continue
            t = _min_temp(record)
            if t is None or t > OUTDOOR_MIN_C:
                problems.append(f"{parts_json.parent.name}:{p['ref']} {model or p['value']} min {t} C")
    assert not problems, "parts not rated for -40 C (rate them or justify in ACCEPTED_COLD): " + "; ".join(problems)


# (description, driver worst-case level, receiver threshold, "high" = driver must exceed it / "low" = stay below)
LEVELS = [
    # crown: STM32G431 (3.3 V, VOH >= VDD - 0.4 V at 8 mA, DS12589) -> UCC27524 EN / IN (SLUSFA9: VEN_H, VIN_H <= 2.3 V)
    ("STM32 -> UCC27524 ENx high", 3.3 - 0.4, 2.3, "high"),
    ("STM32 -> UCC27524 INx high", 3.3 - 0.4, 2.3, "high"),
    # undriven EN: 200 kOhm internal pull-up to 12.3 V (max regulated) vs the 4.7 kOhm pull-down; VEN_L >= 0.8 V
    ("UCC27524 EN undriven (reset)", 12.3 * 4.7 / (200 + 4.7), 0.8, "low"),
    ("UCC27524 EN undriven, R_EN -25 %", 12.3 * 4.7 / (150 + 4.7), 0.8, "low"),
    # 74HC4052 at 3.3 V: VIH = 0.7 VCC = 2.31 V (Nexperia 74HC4052, 2..6 V CMOS inputs)
    ("STM32 -> 74HC4052 S0/S1 high", 3.3 - 0.4, 0.7 * 3.3, "high"),
    # main: ESP32-C6 VOH >= 0.8 VDD -> SP3485 DI / DE VIH 2.0 V; SP3485 RO VOH >= VCC - 0.4 -> ESP32 VIH 0.75 VDD
    ("ESP32 -> SP3485 DI/DE high", 0.8 * 3.3, 2.0, "high"),
    ("SP3485 RO -> ESP32 high", 3.3 - 0.4, 0.75 * 3.3, "high"),
    # probe: LM2903 open-collector output pulled to +3V3 drives the AO3400A gate (VGS(th) <= 1.45 V, RDS(on) spec at 2.5 V)
    ("LM2903 -> AO3400A gate (10k pull-up, no static gate load)", 3.3 - 0.1, 2.5, "high"),
    # main: ESP32 GPIO -> 330R -> AO3400A gate with 100k pull-down. The gate draws no static current, so the CMOS
    # output sits within ~0.1 V of VDD (the 0.8 VDD datasheet VOH is at full IOH); RDS(on) is specified at 2.5 V
    ("ESP32 -> AO3400A heater gate", (3.3 - 0.1) * 100 / (100 + 0.33), 2.5, "high"),
]

# (description, input node voltage, allowed input range low, high) for comparators / op-amps
INPUT_RANGES = [
    # probe LM2903 on +12V_HEAT: CMR 0 .. VCC - 2.0 V over temperature (onsemi LM2903); worst VCC = 9 V heater UVLO
    ("LM2903 NTC / setpoint node at 9 V supply", 2.76, 0.0, 9.0 - 2.0),
    # crown TLV9062 rail-to-rail inputs on +3V3A: VREF 1.65 V biased receive and charge amplifier
    ("TLV9062 receive / rain inputs", 1.65, -0.1, 3.3 + 0.1),
    # crown 74HC4052 analog inputs biased at VREF inside its 0 .. 3.3 V rails (AC-coupled receive)
    ("74HC4052 receive inputs (bias)", 1.65, 0.0, 3.3),
]


@pytest.mark.parametrize("name,level,threshold,kind", LEVELS)
def test_logic_levels_cross_domains(name, level, threshold, kind):
    margin = level - threshold if kind == "high" else threshold - level
    assert margin >= 0.2, f"{name}: {level:.2f} V vs {threshold:.2f} V threshold ({margin:+.2f} V margin)"


@pytest.mark.parametrize("name,node,lo,hi", INPUT_RANGES)
def test_comparator_and_opamp_inputs_in_range(name, node, lo, hi):
    assert lo + 0.2 <= node <= hi - 0.2, f"{name}: {node:.2f} V outside {lo:.2f}..{hi:.2f} V (0.2 V margin)"
