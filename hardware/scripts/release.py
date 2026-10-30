"""Package a fabrication release from the stored layouts (`eda` container)."""

import csv
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import fab_outputs
import jlc_parts
import order_parts
import panelize
import pcbnew
import yaml

REPO = Path("/work")
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

SPEC = load_spec()
VARIANT = SPEC["active_variant"]
BOARDS = tuple(SPEC["variants"][VARIANT]["boards"])
LEGACY = tuple(SPEC["variants"][VARIANT].get("legacy_boards", ()))   # alternatives: own release folder + on the panel
RELEASE = SPEC["product"]["release"]


def assembly_parts_path():
    variant_list = REPO / "hardware/circuits/variants" / VARIANT / "assembly_parts.yaml"
    return variant_list if variant_list.exists() else REPO / "hardware/product/assembly_parts.yaml"


def run(*cmd):
    subprocess.run(cmd, check=True, capture_output=True, text=True)


def layout_path(board):
    return REPO / "hardware/layout" / VARIANT / board / f"{board}.kicad_pcb"


def parts(board):
    return json.loads((REPO / "hardware/build" / VARIANT / board / "parts.json").read_text())


# Live = fresh JLCPCB / EasyEDA records and stock (local releases). CI sets JLC_LIVE=0 and checks against the
# committed snapshot hardware/product/jlc_reference.json: GitHub runners get blocked by lceda.cn / JLCPCB, and stock
# changes daily, so a live CI check fails at random.
LIVE = os.environ.get("JLC_LIVE", "1") != "0"


def assembly_checks(board, *, live=LIVE):
    return jlc_parts.check_board(pcbnew.LoadBoard(str(layout_path(board))), parts(board), live=live)


def gate(board, out, checks):
    report = out / "drc.json"
    run("kicad-cli", "pcb", "drc", "--format", "json", "--output", str(report),
        str(layout_path(board)))
    drc = json.loads(report.read_text())
    errors = [v for v in drc["violations"] if v["severity"] == "error"]
    unsourced = [p["ref"] for p in parts(board)
                 if not (p["assembly"] == "jlc" and p["lcsc"].startswith("C")
                         or p["assembly"] == "hand" and p["mpn"] or p["assembly"] == "none")]
    problems = [f"{len(errors)} DRC errors"] * bool(errors) \
        + [f"{len(drc['unconnected_items'])} unrouted"] * bool(drc["unconnected_items"]) \
        + [f"unsourced parts {unsourced}"] * bool(unsourced) \
        + [f"{c.ref} ({c.lcsc}): {problem}" for c in checks for problem in c.problems]
    return problems


def gerber_layers(pcb):
    copper = [pcbnew.LayerName(layer) for layer in pcb.GetEnabledLayers().CuStack()]
    return copper + ["F.Mask", "B.Mask", "F.Paste", "B.Paste", "F.SilkS", "B.SilkS", "Edge.Cuts"]


def fab_files(board, out):
    """ODB++, Gerbers + drills + IPC-D-356, generic BOM / CPL (fab_outputs.py)."""
    fab_outputs.export_fab(layout_path(board), parts(board), out, board,
                           gerber_layers(pcbnew.LoadBoard(str(layout_path(board)))))


def assembly_files(board, out, checks):
    checks = [c for c in checks if c.assembly == "jlc"]
    by_ref = {p["ref"]: p for p in parts(board)}
    groups = {}
    for p in by_ref.values():
        if p["assembly"] == "jlc":
            key = (p["value"], p["footprint"].split(":")[1], p["lcsc"])
            groups.setdefault(key, []).append(p["ref"])
    with open(out / f"{board}_bom_jlc.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (value, footprint, lcsc), refs in sorted(groups.items()):
            w.writerow([value, ",".join(sorted(refs)), footprint, lcsc])
    pcb = pcbnew.LoadBoard(str(layout_path(board)))
    with open(out / f"{board}_cpl_jlc.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for c in checks:
            layer = "Bottom" if pcb.FindFootprintByReference(c.ref).IsFlipped() else "Top"
            w.writerow([c.ref, f"{c.placement.x:.3f}mm", f"{c.placement.y:.3f}mm", layer, c.placement.rotation])
    turned, bottom = [], []
    with open(out / f"{board}_assembly_check.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "LCSC", "Manufacturer part", "JLCPCB footprint", "Layer",
                    "KiCad rotation", "JLCPCB rotation", "Stock", "Library", "Result"])
        for c in checks:
            fp = pcb.FindFootprintByReference(c.ref)
            kicad = round(fp.GetOrientationDegrees()) % 360
            layer = "Bottom" if fp.IsFlipped() else "Top"
            w.writerow([c.ref, c.lcsc, by_ref[c.ref]["mpn"], c.jlc_footprint, layer, kicad,
                        c.placement.rotation, c.stock, c.library, "lands pin for pin"])
            if c.placement.rotation != kicad:
                turned.append(c.ref)
            if fp.IsFlipped():
                bottom.append(c.ref)
    with open(out / f"{board}_hand_fit.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Value", "Manufacturer part", "Notes"])
        for p in sorted(by_ref.values(), key=lambda p: p["ref"]):
            if p["assembly"] == "hand":
                w.writerow([p["ref"], p["value"], p["mpn"], p["source"]])
    return turned, bottom


def release_board(board, root):
    out = root / board
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    checks = assembly_checks(board)
    problems = gate(board, out, checks)
    if problems:
        shutil.rmtree(out)
        return problems, ([], []), []
    for suffix in (".kicad_pcb", ".kicad_pro", ".kicad_dru"):
        shutil.copy(layout_path(board).with_suffix(suffix), out)
    fab_files(board, out)
    placement = assembly_files(board, out, checks)
    step = REPO / "hardware/build" / VARIANT / board / f"{board}.step"
    run("kicad-cli", "pcb", "export", "step", "--grid-origin", "--subst-models", "--force",
        "--output", str(step), str(layout_path(board)))
    shutil.copy(step, out)
    run("python3", str(REPO / "hardware/scripts/render_board.py"), board, "release")
    renders = REPO / "hardware/build" / VARIANT / board / "render/release"
    shutil.copytree(renders, out / "renders", ignore=shutil.ignore_patterns("drc.json", "zoom_*"))
    return [], placement, checks


def main(version, boards=BOARDS + LEGACY):
    root = REPO / "hardware/releases" / version
    root.mkdir(parents=True, exist_ok=True)
    failed = False
    board_checks = {}
    for board in boards:
        problems, (turned, bottom), checks = release_board(board, root)
        board_checks[board] = checks
        if problems:
            failed = True
            print(f"{board}: NOT released:", flush=True)
            for problem in problems:
                print(f"  {problem}", flush=True)
        else:
            print(f"{board}: released; every JLC part lands pin for pin. Rotation differs from KiCad's "
                  f"for {', '.join(turned) or 'none'}; bottom side: {', '.join(bottom) or 'none'} "
                  "(confirm these in JLCPCB's placement preview)")
    if not failed and set(BOARDS) <= set(boards):
        # the legacy boards ride along on the panel (break them off, or skip their assembly) when released together
        panel_boards = BOARDS + tuple(b for b in LEGACY if b in boards)
        panel_problems = panelize.build_panel(VARIANT, panel_boards, root, board_checks)
        if panel_problems:
            failed = True
            for problem in panel_problems:
                print(f"  {problem}")
    with open(root / "assembly_parts.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Item", "Qty", "Category", "Part", "Description", "Source"])
        for p in yaml.safe_load(assembly_parts_path().read_text()):
            w.writerow([p["item"], p["qty"], p["category"], p["part"], p["description"], p["source"]])
    enclosure = REPO / "mechanical/build" / VARIANT
    if enclosure.exists():
        # print plates are regrouped over time (PRINT_GROUPS): drop plates the CAD build no longer produces
        shutil.rmtree(root / "enclosure" / "print", ignore_errors=True)
        shutil.rmtree(root / "enclosure" / "drawings", ignore_errors=True)
        # parts / renders renamed or retired in the CAD build (e.g. crown_dome -> crown) must not linger in the release
        for sub in ("", "renders"):
            for old in (root / "enclosure" / sub).glob("*.*"):
                if old.suffix in (".stl", ".step", ".png") and not (enclosure / sub / old.name).exists():
                    old.unlink()
        shutil.copytree(enclosure, root / "enclosure", dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("fit_*.png", "front_legend.png", "*.3mf", "*_obj.zip"))
        for colour_model in (*(root / "enclosure").glob("*.3mf"), *(root / "enclosure").glob("*_obj.zip")):
            colour_model.unlink()   # colour 3D-print formats are no longer produced
    if not failed:
        order_parts.write(root)   # order_lcsc / order_elsewhere.csv for self-assembly
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else RELEASE, tuple(sys.argv[2:]) or BOARDS + LEGACY))
