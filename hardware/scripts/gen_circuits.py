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
            "item": "UV-A/UV-B Index & High-Noon ALS sensor IC (hand-reflowed)",
            "qty": len(hand_by_mpn["LTR-390UV-01"]["locs"]),
            "part": "Lite-On LTR-390UV-01 (LCSC C492374)",
            "description": f"2x2 mm 6-pin optical UV-A/UV-B + 157k lux ALS sensor ({locs}) under the top fused-silica UV window",
            "source": "Digi-Key / Mouser / LCSC",
        })
    if "GUVB-C31SM" in hand_by_mpn:
        locs = ", ".join(hand_by_mpn["GUVB-C31SM"]["locs"])
        items.append({
            "item": "AlGaN True UV-B Sensor IC (hand-reflowed; optional DNP)",
            "qty": len(hand_by_mpn["GUVB-C31SM"]["locs"]),
            "part": "Genicom / GenUV GUVB-C31SM (2.0x2.3 mm 4-pin OLGA I2C 0x39)",
            "description": f"Intrinsic wide-bandgap AlGaN UV-B (240-320 nm) digital I2C sensor on dome PCB ({locs}; can be left unpopulated if using U1 alone)",
            "source": "GenUV / Digi-Key / Mouser",
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
        "item": "Waterproof 40 kHz Closed-Face Ultrasonic Transceivers + 2-Pin JST-SH Pigtails",
        "qty": 4,
        "part": "STEMINC SMATR10H40X80 / TaiMI TM-A1040H07TR (Ø 10.0x7.0 mm closed-face aluminum 40 kHz transceiver) OR INGHAi GU14095B-40TR (LCSC C242186) / Same Sky CUSA-TR65-065-2200-W68 / CUSA-TR65-065-2200-TH68 (IP68 Ø 14x9 mm) + 4x 2-Pin JST-SH (SHR-02V-S-B) Pigtails",
        "description": "4 inward-angled (29.0 deg V-beam) transceivers seated in the N/E/S/W covered barrels of crown_base.stl, locked from the back by the 4 slotted press-fit retaining rings (in struts.stl), and plugged into crown J2..J5 (BM02B-SRSS-TB, LCSC C160388)",
        "source": "LCSC (C242186) / Digi-Key & Mouser (Same Sky CUSA-TR65) / STEMINC (SMATR10H40X80)",
    })
    items.append({
        "item": "20 mm Piezoelectric Haptic Rain Discs (4-Quadrant Symmetric Array)",
        "qty": 4,
        "part": "Murata 7BB-20-6L0 (Ø 20 mm brass/ceramic piezoelectric disc element)",
        "description": "4 discs bonded (brass face only, using thin rigid cyanoacrylate Loctite 401) to the 4 coplanar Ø 20.4 mm ceiling pockets of crown.stl (NE, NW, SW, SE at r=26.8 mm) and plugged into crown J6..J9",
        "source": "Murata / Mouser / Digi-Key / LCSC",
    })
    items.append({
        "item": "JGS1 Fused Silica UV & Solar Optical Dome (or Ø 20x1.5 mm JGS1 Flat Disc)",
        "qty": 1,
        "part": "JGS1/JGS2 UV Fused Silica Hemispherical Dome (Ø 18.0 mm OD / Ø 15.6 mm ID x 9.0 mm height with Ø 20.5 mm flange, Bena Optics DW-series / AliExpress) OR standard Ø 20.0 x 1.5 mm JGS1 Quartz Glass Disc",
        "description": "185-2500 nm UV-B/UV-A/Visible/NIR transmission (>=92%); seats from underneath against the top crimson O-ring inside crown.stl and is clamped by the M22x1.0 threaded retainer nut",
        "source": "AliExpress / Bena Optics / Crystran / Edmund Optics",
    })
    items.append({
        "item": "Particulate Matter Sensor Module (PM1.0/2.5/4.0/10)",
        "qty": 1,
        "part": "Sensirion SEN55-SDN-T (or SEN54-SDN-T) + 6-pin JST-ZH cable",
        "description": "Laser-scattering PM1.0/PM2.5/PM4.0/10 environmental module seated in the lower intake bay; plugs into main J4",
        "source": "Sensirion / Digi-Key / Mouser",
    })
    items.append({
        "item": "Round Jacketed 10-Core Dismountable JST-SH Cable + Probe Jumper",
        "qty": 2,
        "part": "260 mm (250-300 mm) Ø 4.2 mm Round Jacketed 10-Core 30AWG Cable with 10-pin 1.0 mm JST-SH (SHR-10V-S-B) plugs (main J2 <-> crown J1, 224.2 mm routed path + 35 mm service loop) + 60 mm 10-pin JST-SH jumper (main J3 <-> probe J1, 32.0 mm direct + 28 mm slack)",
        "description": "Passes through the 13x11 mm umbrella cutout, the 12.6x3.35 mm rear conduit of probe_sleeve_back, and the 45-deg NE split-C strut with the SHR-10V-S-B connector attached (or crimp pins removable via housing lance tabs)",
        "source": "Digi-Key / Mouser / LCSC / AliExpress",
    })
    items.append({
        "item": "Stainless Steel M6 Fastener Set (Pagoda Through-Bolts & Mast Clamp Bolts)",
        "qty": 1,
        "part": "4x M6x55 mm Socket Cap Bolts + 4x M6 Hex Nuts (Pagoda stack) + 4x M6x35 mm Hex Bolts + 8x M6 Washers + 4x M6 Locknuts (Mast Clamp)",
        "description": "A4-70 / 316 stainless steel fasteners clamping the 5-plate pagoda stack (through the Ø 6.2 mm column bores) and clamping the mast saddle onto Ø 25-34 mm roof poles",
        "source": "Local hardware / McMaster-Carr / Mouser",
    })
    items.append({
        "item": "Elastomeric O-Ring Seal Set (5 Rings)",
        "qty": 1,
        "part": "3x Ø 88x1.5 mm EPDM/NBR O-Rings (base<->pagoda, pagoda<->wind_tunnel, crown_base<->crown) + 1x Ø 21x1.5 mm O-Ring (probe_sleeve bottom) + 1x Ø 17x1.5 mm O-Ring (JGS1 UV dome)",
        "description": "Standard metric EPDM/nitrile O-rings (or 3D-printed in TPU 95A from orings.stl) sealing all 3 bayonet joints, the bottom pagoda bulkhead, and the JGS1 quartz dome",
        "source": "Local hardware / McMaster-Carr / TPU 95A print",
    })
    items.append({
        "item": "IP68 Waterproof Compression Cable Gland (M12x1.5 / PG7)",
        "qty": 1,
        "part": "M12x1.5 or PG7 IP68 Nylon Cable Gland with EPDM rubber seal & 15 mm hex locknut (Ø 3.5-6.5 mm cable)",
        "description": "Mounts in the single center Ø 12.2 mm bulkhead hole of base.stl (locknut seats in the 15.4 mm internal hex recess; knurled compression dome sits inside the Ø 26 mm protected upper mast chamber above the pole stop)",
        "source": "Mouser / Digi-Key / local electrical",
    })
    items.append({
        "item": "Dual-Band 2.4 / 5.8 GHz U.FL (IPEX-1) Flexible PCB Dipole Antenna",
        "qty": 1,
        "part": "Molex 1461530100 / Taoglas FXP830 (42x12 mm, +4.5 dBi, 100 mm U.FL coax)",
        "description": "Plugs into XIAO ESP32-C6 U.FL jack (activated via GPIO3=LOW, GPIO14=HIGH) and snaps horizontally into the upper inner-wall cradle of base.stl so the main lobe radiates downward through the roof",
        "source": "Mouser / Digi-Key / LCSC",
    })
    items.append({
        "item": "3D-printed ASA roof enclosure parts (9-part modular set)",
        "qty": 9,
        "part": "enclosure/{base,mast_clamp,orings,probe_sleeve_back,probe_sleeve_front,pagoda,wind_tunnel,strut_cover,crown}.stl",
        "description": "UV-stabilized white/light-grey ASA (plus TPU 95A for orings.stl), 0.2 mm layers, vapor-smoothed + UV polyurethane topcoat, clamped to mast with 4x M6x25 316-stainless bolts & captured M6 hex nuts",
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
