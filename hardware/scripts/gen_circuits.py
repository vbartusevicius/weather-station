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
            "item": "AlGaN True UV-B Sensor IC (hand-reflowed)",
            "qty": len(hand_by_mpn["GUVB-C31SM"]["locs"]),
            "part": "Genicom / GenUV GUVB-C31SM (2.0x2.3 mm 4-pin OLGA I2C 0x39)",
            "description": f"Intrinsic wide-bandgap AlGaN UV-B (240-320 nm) digital I2C sensor on dome PCB ({locs}); primary UV-index source, fused 80/20 with U1",
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
    if "MA5532-AE" in hand_by_mpn:
        locs = ", ".join(hand_by_mpn["MA5532-AE"]["locs"])
        items.append({
            "item": "AS3935 lightning antenna (hand-soldered)",
            "qty": len(hand_by_mpn["MA5532-AE"]["locs"]),
            "part": "Coilcraft MA5532-AED (100 uH +/-2 %, 11.7 x 3.5 mm open ferrite rod)",
            "description": f"ScioSense AS3935-AN04 recommended antenna ({locs}); tuned with 2x 470 pF C0G + the AS3935 internal 0..120 pF array",
            "source": "Coilcraft / Digi-Key / Mouser (not stocked at JLCPCB)",
        })

    # Off-board sensors & transducers
    items.append({
        "item": "Waterproof 40 kHz Closed-Face Ultrasonic Transceivers + 2-Pin JST-SH Pigtails",
        "qty": 4,
        "part": "Ø 10.0 x 7.0 mm closed-face aluminium waterproof 40 kHz transceiver, e.g. EU10AIF40H07T/R (the part proven in the open-source QingStation V-path anemometer) + 4x 2-pin JST-SH (SHR-02V-S-B) pigtails. Only Ø 10 mm parts fit the Ø 10.5 mm barrels: do NOT buy Ø 14/16 mm types",
        "description": "4 inward-angled (29.0 deg V-beam) transceivers seated in the N/E/S/W covered barrels of crown_base.stl, locked from the back by the 4 slotted press-fit retaining rings (in struts.stl), and plugged into crown J2..J5 (BM02B-SRSS-TB, LCSC C160388)",
        "source": "AliExpress / Taobao (EU10AIF40H07T/R or equivalent Ø 10 x 7 mm, >= 106 dB SPL @ 10 V, >= -75 dB sensitivity)",
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
        "description": "185-2500 nm UV-B/UV-A/Visible/NIR transmission (>=92%); seats from underneath against the 17 x 1.5 mm O-ring inside crown.stl and is clamped by the printed M22x1.5 threaded retainer (flange must be <= 1.2 mm thick: get the supplier drawing before ordering)",
        "source": "AliExpress / Bena Optics / Crystran / Edmund Optics",
    })
    items.append({
        "item": "Particulate Matter Sensor Module (PM1.0/2.5/10)",
        "qty": 1,
        "part": "Plantower PMSA003I (I2C 0x12, 38.0x35.0x11.8 mm, inlet and fan exhaust on the same face, e.g. Adafruit 4505)",
        "description": "Lies face-down on the left half of the base.stl bay beside main, openings edge outward (fan toward the rear): the inlet/fan face seals onto two duct chambers whose floor slits draw and exhaust outside air only; held by two snap hooks",
        "source": "Adafruit / Digi-Key / Mouser / LCSC",
    })
    items.append({
        "item": "PMSA003I harness + duct gasket",
        "qty": 1,
        "part": "100 mm 6-wire harness: 6-pin JST-ZH 1.5 mm (ZHR-6) to 2x5 1.27 mm IDC socket (module); 1.5 mm closed-cell EPDM foam strip (1.2 mm wide, on the duct chamber wall tops)",
        "description": "Harness: main J4 pin 1 +5V -> module pins 1+2 (VCC), 2 GND -> 3+4, 3 SDA -> 9, 4 SCL -> 7; module pins 5/10 (RESET/SET) and 6/8 left open (internal pull-ups). Optional 47 uF/10 V across VCC/GND at the module end (datasheet). The module's connector faces up, next to main J4; the gasket is squeezed to 1.0 mm by the base snap hooks",
        "source": "LCSC / AliExpress / local hardware",
    })
    items.append({
        "item": "Crown cable (12-pin JST-SH) + probe jumper (10-pin JST-SH)",
        "qty": 2,
        "part": "260 mm 12-conductor 30 AWG cable, OD <= 3.2 mm (or 12 loose 30 AWG wires in a PTFE sleeve), SHR-12V-S-B plug on both ends wired pin 1 <-> pin 1 (main J2 <-> crown J1, 224 mm routed path + 35 mm service loop) + 60 mm 10-wire SHR-10V-S-B jumper, pin 1 <-> pin 1 (main J3 <-> probe J1)",
        "description": "main J2 and crown J1 are 12-pin BM12B-SRSS-TB. The crown cable passes the 13x11 mm umbrella cutout, the 12.6 x 3.35 mm rear conduit of probe_sleeve_back (a Ø 4.2 mm cable does NOT fit) and the 45-deg NE split-C strut; crimp one end after threading it through the strut nut",
        "source": "Digi-Key / Mouser / LCSC / AliExpress",
    })
    items.append({
        "item": "Stainless Steel M6 Fastener Set (Pagoda Through-Bolts & Mast Clamp Bolts)",
        "qty": 1,
        "part": "4x M6x55 DIN 7984 LOW-HEAD socket cap screws (head 10 x 3.5 mm; a standard ISO 4762 head hits the reflector board) + 4x ISO 4032 M6 nuts (pagoda stack) + 6x M6x35 hex bolts + 12x M6 washers (DIN 9021 large) + 6x M6 locknuts (mast clamp, 3 rows over the 50 mm pole engagement)",
        "description": "A4-70 / 316 stainless steel fasteners clamping the 5-plate pagoda stack (through the Ø 6.2 mm column bores) and clamping the mast saddle: direct grip on Ø 32-34 mm poles, Ø 24-32 mm poles with the printed mast_shims (4 sizes, 50 mm tall)",
        "source": "Local hardware / McMaster-Carr / Mouser",
    })
    items.append({
        "item": "Elastomeric O-Ring Seal Set (5 Rings)",
        "qty": 1,
        "part": "3x 88 x 2.0 mm EPDM/NBR 70A O-rings (base<->pagoda, pagoda<->wind_tunnel, crown_base<->crown: glands sized for 2.0 mm, a 1.5 mm ring does not seal) + 1x 21 x 1.5 mm O-ring (probe_sleeve bottom) + 1x 17 x 1.5 mm O-ring (JGS1 UV dome)",
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
        "description": "Plugs into XIAO ESP32-C6 U.FL jack (activated via GPIO3=LOW, GPIO14=HIGH); stick it (its own adhesive) horizontally on the inner bay wall above the umbrella, away from the main board (V1 has no printed cradle)",
        "source": "Mouser / Digi-Key / LCSC",
    })
    items.append({
        "item": "3D-printed roof enclosure parts (11-part modular set)",
        "qty": 11,
        "part": "enclosure/print/<part>.stl (one print plate per part) — assembled-position references in enclosure/{base,mast_clamp,orings,probe_sleeve_back,probe_sleeve_front,pagoda,wind_tunnel,struts,crown_base,crown,mast_shims}.stl",
        "description": "Order/print enclosure/print/<part>.stl: each part's bodies laid out >= 6 mm apart, so captive and stacked bodies (umbrella, reset button, louver plates, strut halves, nuts, shims) are never printed fused. MJF PA12 or UV-stabilised ASA (TPU 95A only if printing orings); clamped to the mast with 6x M6x35 bolts",
        "source": "printed",
    })
    items.append({
        "item": "Non-siloxane polyurethane conformal coating & potting resin",
        "qty": 1,
        "part": "Electrolube URC / Humiseal 1A33 polyurethane coating (-65..+130 C) + 2-part polyurethane resin",
        "description": "Mask the silkscreened NO-COAT sensor windows (SHT40, SCD41, SGP41, BMP585, dome optics) with Kapton tape; coat both sides of main/crown/probe and pot the probe J1 connector cofferdam cup (NEVER use RTV silicone near the SGP41: its vapours poison the MOx gas sensor)",
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
