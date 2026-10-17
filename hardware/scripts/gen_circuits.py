"""Generate KiCad netlists and schematics for the active variant's boards.

Runs inside the `eda` container:
    python3 hardware/scripts/gen_circuits.py
"""

import builtins
import importlib
import io
import json
import os
import sys
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "circuits"))
(Path(__file__).resolve().parents[1] / "build").mkdir(exist_ok=True)
os.chdir(Path(__file__).resolve().parents[1] / "build")

import common
from product_spec import load_spec
from skidl import (
    ERC,
    KICAD9,
    generate_netlist,
    reset,
    set_default_tool,
)

BUILD = common.REPO_ROOT / "hardware" / "build"
SPEC = common.SPEC_PATH
DERIVED_BEGIN = "# --- BEGIN GENERATED (hardware/scripts/gen_circuits.py) ---"
DERIVED_END = "# --- END GENERATED ---"


def generate_board(variant, name, build_fn, net_classes):
    reset()
    set_default_tool(KICAD9)
    bdir = BUILD / variant / name
    bdir.mkdir(parents=True, exist_ok=True)
    import skidl as skidl_mod
    skidl_mod.config.backup_lib_file_name = str(BUILD / "gen_circuits_sklib.py")
    import logging
    buf = io.StringIO()
    handler = logging.StreamHandler(buf)
    handler.setFormatter(logging.Formatter("%(levelname)s %(message)s"))
    skidl_logger = logging.getLogger("skidl")
    skidl_logger.addHandler(handler)
    with redirect_stdout(buf), redirect_stderr(buf):
        build_fn()
        ERC()
        generate_netlist(file_=str(bdir / f"{name}.net"))
    skidl_logger.removeHandler(handler)
    out = buf.getvalue()
    bdir.mkdir(parents=True, exist_ok=True)
    (bdir / "skidl_log.txt").write_text(out)
    parts = [
        {
            "ref": p.ref,
            "value": p.value,
            "footprint": p.footprint,
            "place": p.fields.get("Place", "misc"),
            "lcsc": p.fields.get("LCSC", ""),
            "mpn": p.fields.get("Manufacturer_Part_Number", ""),
            "mfr": p.fields.get("Manufacturer", ""),
            "assembly": p.fields.get("Assembly", "jlc"),
            "source": p.fields.get("Source", ""),
            "footprint_ref": p.fields.get("Footprint_Reference", ""),
            "pins": {str(pin.num): pin.name for pin in p.pins},
        }
        for p in builtins.default_circuit.parts if not p.ref.startswith("#")
    ]
    (bdir / "parts.json").write_text(json.dumps(parts, indent=1))
    return out


def write_derived(net_classes_by_board):
    from blocks import mcu_xiao
    text = SPEC.read_text()
    derived = {
        "mcu_pins": mcu_xiao.MCU_PINS,
        "net_classes": net_classes_by_board,
    }
    block = DERIVED_BEGIN + "\n" + yaml.safe_dump(
        {"derived": derived}, sort_keys=False) + DERIVED_END + "\n"
    if DERIVED_BEGIN in text and DERIVED_END in text:
        pre = text[: text.index(DERIVED_BEGIN)]
        text = pre + block
    else:
        if not text.endswith("\n"):
            text += "\n"
        text += "\n" + block
    SPEC.write_text(text)


def collect_assembly_parts(variant, spec):
    """Automatically build the off-board + hand-fitted assembly_parts list from
    parts.json of every board in `variant` and `spec.yaml`."""
    boards = spec["variants"][variant]["boards"]
    hand_by_mpn = {}
    for b in boards:
        for p in json.loads((BUILD / variant / b / "parts.json").read_text()):
            if p["assembly"] == "hand":
                entry = hand_by_mpn.setdefault(p["mpn"], {"p": p, "locs": []})
                entry["locs"].append(f"{b} {p['ref']}")

    items = []
    if "102010636" in hand_by_mpn:
        locs = ", ".join(hand_by_mpn["102010636"]["locs"])
        items.append({
            "item": "MCU module (hand-fitted into sockets)",
            "qty": len(hand_by_mpn["102010636"]["locs"]),
            "part": "Seeed Studio XIAO ESP32C6 Pre-Soldered (SKU 102010636)",
            "description": f"ESP32-C6 module with pre-soldered 2x7 pin headers; plugs into two 5.0 mm female sockets ({locs})",
            "source": "Seeed Studio / Digi-Key / Mouser",
        })
        items.append({
            "item": "XIAO sockets (hand-soldered)",
            "qty": 2 * len(hand_by_mpn["102010636"]["locs"]),
            "part": "1x7 2.54 mm female header, 5.0 mm tall (e.g. Hong Cheng HC-PM254-5.0H)",
            "description": f"1x7 female header, 2.54 mm, 5.0 mm tall, soldered into {locs}; XIAO stays detachable",
            "source": "LCSC / Mouser",
        })
    if "LTR-390UV-01" in hand_by_mpn:
        locs = ", ".join(hand_by_mpn["LTR-390UV-01"]["locs"])
        items.append({
            "item": "UV-A/UV-B Index sensor IC (hand-reflowed)",
            "qty": len(hand_by_mpn["LTR-390UV-01"]["locs"]),
            "part": "Lite-On LTR-390UV-01 (LCSC C492374)",
            "description": f"2x2 mm 6-pin optical UV-A/UV-B + ALS sensor ({locs}) under the top fused-silica UV window",
            "source": "Digi-Key / Mouser / LCSC",
        })
    if "AS3935-BQFT" in hand_by_mpn:
        locs = ", ".join(hand_by_mpn["AS3935-BQFT"]["locs"])
        items.append({
            "item": "Franklin Lightning Sensor IC (hand-reflowed)",
            "qty": len(hand_by_mpn["AS3935-BQFT"]["locs"]),
            "part": "ScioSense AS3935-BQFT (LCSC C3178344)",
            "description": f"4x4 mm MLPQ-16 lightning strike distance & energy detector ({locs})",
            "source": "Digi-Key / Mouser / LCSC",
        })

    # Off-board sensors & transducers
    items.append({
        "item": "Waterproof 40 kHz Ultrasonic Transducers",
        "qty": 4,
        "part": "EU10AIF40H07T/R or NU40A10TR-1 (10 mm closed-face aluminum IP67 40 kHz transducer)",
        "description": "4 downward-facing transducers pressed into the N/E/S/W crown pockets and plugged into crown J2..J5 via 2-pin JST-SH pigtails",
        "source": "LCSC / AliExpress / Audiowell",
    })
    items.append({
        "item": "Piezoelectric Haptic Rain Disc",
        "qty": 1,
        "part": "27 mm brass/ceramic piezoelectric disc element with 2-pin JST-SH lead",
        "description": "Bonded with thin structural acrylic/epoxy to the underside of the top crown rain dome; plugs into crown J6",
        "source": "Murata / Steminc / LCSC",
    })
    items.append({
        "item": "Particulate Matter Sensor Module (PM1.0/2.5/4.0/10)",
        "qty": 1,
        "part": "Sensirion SEN55-SDN-T (or SEN54-SDN-T) + 6-pin JST-ZH cable",
        "description": "Laser-scattering PM1.0/PM2.5/PM4.0/PM10 environmental module seated in the lower intake bay; plugs into main J4",
        "source": "Sensirion / Digi-Key / Mouser",
    })
    items.append({
        "item": "Board-to-board 10-pin JST-SH cables",
        "qty": 2,
        "part": "JST A10SR10SR30K152A (150 mm main J2 <-> crown J1) + A10SR10SR30K051A (50 mm main J3 <-> probe J1)",
        "description": "1:1 10-pin 1.0 mm JST-SH jumper cables connecting main to crown (through the NE airfoil strut conduit) and main to probe",
        "source": "Digi-Key / Mouser / LCSC",
    })
    items.append({
        "item": "3D-printed ASA roof enclosure parts (8-part modular set)",
        "qty": 8,
        "part": "enclosure/{base,mast_clamp,probe_sleeve_back,probe_sleeve_front,pagoda,wind_tunnel,strut_cover,crown}.stl",
        "description": "UV-stabilized white/light-grey ASA, 0.2 mm layers, vapor-smoothed + UV polyurethane topcoat, joined with 4x M3x12 316-stainless screws and clamped to mast with 4x M4x16 bolts",
        "source": "printed",
    })
    items.append({
        "item": "Non-siloxane polyurethane conformal coating & potting resin",
        "qty": 1,
        "part": "Electrolube URC / Humiseal 1A33 polyurethane coating (-65..+130 C) + 2-part polyurethane resin",
        "description": "Mask the 6 silkscreened NO-COAT sensor windows (SHT45, SCD41, SGP41, BMP581, LTR-390UV, VEML7700) with Kapton tape; coat both sides of main/crown/probe and pot the probe J1 connector cofferdam cup (NEVER use RTV silicone near SGP41)",
        "source": "Mouser / Digi-Key / TME",
    })
    return items


def sync_assembly_parts(variant, spec):
    target = (common.REPO_ROOT / "hardware/product/assembly_parts.yaml"
              if variant == "v1" else common.REPO_ROOT / f"hardware/circuits/variants/{variant}/assembly_parts.yaml")
    header = ("# Automatically generated by hardware/scripts/gen_circuits.py from parts.json + spec.yaml.\n"
              "# Copied by hardware/scripts/release.py into the release as assembly_parts.csv.\n")
    target.write_text(header + yaml.safe_dump(collect_assembly_parts(variant, spec), sort_keys=False))


def main():
    spec = load_spec()
    variant = spec["active_variant"]
    BUILD.mkdir(parents=True, exist_ok=True)

    pkg = f"variants.{variant}"
    modules = {name: importlib.import_module(f"{pkg}.{name}")
               for name in spec["variants"][variant]["boards"]}
    logs = {name: generate_board(variant, name, module.build, module.NET_CLASSES)
            for name, module in modules.items()}
    write_derived({name: module.NET_CLASSES for name, module in modules.items()})
    sync_assembly_parts(variant, spec)

    erc_errors = 0
    netlist_errors = 0
    for name, log in logs.items():
        print(f"=== {name} ===")
        for line in log.splitlines():
            if "ERC" in line or "error" in line.lower():
                print(" ", line)
        erc_errors += sum(1 for line in log.splitlines() if "ERC ERROR" in line)
        netlist_errors += sum(
            1 for line in log.splitlines()
            if line.startswith(("ERROR", "CRITICAL"))
            and "ERC" not in line
            and not ("No footprint for" in line and "/#" in line))
    (BUILD / "skidl_erc.txt").write_text(
        "\n\n".join(f"=== {n} ===\n{l}" for n, l in logs.items()))
    print(f"SKiDL ERC errors: {erc_errors}, non-waived netlist errors: {netlist_errors}")
    return 1 if (erc_errors or netlist_errors) else 0


if __name__ == "__main__":
    sys.exit(main())
