"""Build a single breakaway mouse-bite PCB panel combining all boards of a variant."""

from __future__ import annotations

import csv
import json
import re
import shutil
import subprocess
from pathlib import Path

import jlc_parts as jp
import pcbnew

REPO = Path("/work")
SLOT_W = 2.0
TAB_H = 3.2
HOLE_DIA = 0.5
HOLE_Y_OFFS = (-0.9, -0.3, 0.3, 0.9)

# Boards ordered on their own instead of in the panel: the 60 x 60 mm notched crown v2 outline does not fit the
# rectangular row layout below (its per-board release has Gerbers, BOM and CPL).
SEPARATE_BOARDS = ("crown",)

BOARD_REF_OFFSET = {
    "main": 0,
    "crown": 100,
    "reflector": 200,
    "dome": 300,
    "probe": 400,
    "power": 500,
}


def offset_ref(board_name: str, ref: str) -> str:
    off = BOARD_REF_OFFSET.get(board_name, 0)
    if off == 0:
        return ref
    m = re.match(r"^([A-Za-z]+)(\d+)$", ref)
    if not m:
        return f"{board_name.upper()}_{ref}"
    return f"{m.group(1)}{int(m.group(2)) + off}"


def run(*cmd: str) -> None:
    subprocess.run(cmd, check=True, capture_output=True, text=True)


def add_edge_line(pcb: pcbnew.BOARD, x0: float, y0: float, x1: float, y1: float) -> None:
    if abs(x0 - x1) < 1e-4 and abs(y0 - y1) < 1e-4:
        return
    seg = pcbnew.PCB_SHAPE(pcb)
    seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
    seg.SetLayer(pcbnew.Edge_Cuts)
    seg.SetWidth(pcbnew.FromMM(0.1))
    seg.SetStart(pcbnew.VECTOR2I_MM(x0, y0))
    seg.SetEnd(pcbnew.VECTOR2I_MM(x1, y1))
    pcb.Add(seg)


def add_mouse_bite_holes(pcb: pcbnew.BOARD, coords: list[tuple[float, float]]) -> None:
    if not coords:
        return
    fp = pcbnew.FOOTPRINT(pcb)
    fp.SetReference("MOUSE_BITES")
    fp.SetValue("DNP")
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    fp.SetDNP(True)
    fp.SetExcludedFromBOM(True)
    fp.SetExcludedFromPosFiles(True)
    fp.SetPosition(pcbnew.VECTOR2I_MM(0, 0))
    for idx, (x_mm, y_mm) in enumerate(coords, start=1):
        pad = pcbnew.PAD(fp)
        pad.SetNumber(str(idx))
        pad.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
        pad.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
        pad.SetSize(pcbnew.VECTOR2I_MM(HOLE_DIA, HOLE_DIA))
        pad.SetDrillSize(pcbnew.VECTOR2I_MM(HOLE_DIA, HOLE_DIA))
        pad.SetLayerSet(pad.UnplatedHoleMask())
        pad.SetPosition(pcbnew.VECTOR2I_MM(x_mm, y_mm))
        fp.Add(pad)
    pcb.Add(fp)


def is_vertical_edge_at_x(drw: pcbnew.BOARD_ITEM, x_target: float, y_min: float = -1e6, y_max: float = 1e6) -> bool:
    if drw.GetLayer() != pcbnew.Edge_Cuts or not isinstance(drw, pcbnew.PCB_SHAPE):
        return False
    if drw.GetShape() != pcbnew.SHAPE_T_SEGMENT:
        return False
    sx = pcbnew.ToMM(drw.GetStart().x)
    ex = pcbnew.ToMM(drw.GetEnd().x)
    sy = pcbnew.ToMM(drw.GetStart().y)
    ey = pcbnew.ToMM(drw.GetEnd().y)
    return abs(sx - x_target) < 0.08 and abs(ex - x_target) < 0.08 and min(sy, ey) >= y_min - 0.2 and max(sy, ey) <= y_max + 0.2


def is_horizontal_edge_at_y(drw: pcbnew.BOARD_ITEM, y_target: float, x_min: float, x_max: float) -> bool:
    if drw.GetLayer() != pcbnew.Edge_Cuts or not isinstance(drw, pcbnew.PCB_SHAPE):
        return False
    if drw.GetShape() != pcbnew.SHAPE_T_SEGMENT:
        return False
    sy = pcbnew.ToMM(drw.GetStart().y)
    ey = pcbnew.ToMM(drw.GetEnd().y)
    sx = pcbnew.ToMM(drw.GetStart().x)
    ex = pcbnew.ToMM(drw.GetEnd().x)
    return abs(sy - y_target) < 0.08 and abs(ey - y_target) < 0.08 and min(sx, ex) >= x_min - 0.2 and max(sx, ex) <= x_max + 0.2


def rotate_board_deg(brd: pcbnew.BOARD, deg: float) -> None:
    center = pcbnew.VECTOR2I_MM(0, 0)
    angle = pcbnew.EDA_ANGLE(deg, pcbnew.DEGREES_T)
    for fp in brd.GetFootprints():
        fp.Rotate(center, angle)
    for trk in brd.GetTracks():
        trk.Rotate(center, angle)
    for zone in brd.Zones():
        zone.Rotate(center, angle)
    for drw in brd.GetDrawings():
        drw.Rotate(center, angle)


def board_edge_bounds(brd: pcbnew.BOARD) -> tuple[float, float, float, float]:
    xs, ys = [], []
    for d in brd.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts and isinstance(d, pcbnew.PCB_SHAPE):
            xs.extend((round(pcbnew.ToMM(d.GetStart().x), 4), round(pcbnew.ToMM(d.GetEnd().x), 4)))
            ys.extend((round(pcbnew.ToMM(d.GetStart().y), 4), round(pcbnew.ToMM(d.GetEnd().y), 4)))
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    return x0, y0, x1 - x0, y1 - y0


def build_panel(
    variant: str,
    boards: tuple[str, ...],
    root: Path,
    board_checks: dict[str, list],
) -> list[str]:
    boards = tuple(b for b in boards if b not in SEPARATE_BOARDS)
    panel_dir = root / "panel"
    shutil.rmtree(panel_dir, ignore_errors=True)
    panel_dir.mkdir(parents=True)

    loaded: dict[str, tuple[pcbnew.BOARD, float, float, float, float]] = {}
    for bname in boards:
        bpath = REPO / "hardware/layout" / variant / bname / f"{bname}.kicad_pcb"
        brd = pcbnew.LoadBoard(str(bpath))
        if bname == "probe":
            # Rotate probe 90 deg so it is 50.0 x 14.0 mm and nests horizontally above crown!
            rotate_board_deg(brd, -90.0)
        x0, y0, w, h = board_edge_bounds(brd)
        loaded[bname] = (brd, x0, y0, w, h)

    base_name = boards[0]
    base_path = REPO / "hardware/layout" / variant / base_name / f"{base_name}.kicad_pcb"
    for suffix in (".kicad_pro", ".kicad_dru"):
        shutil.copy(base_path.with_suffix(suffix), panel_dir / f"panel{suffix}")

    panel, x0_0, y0_0, w0, h0 = loaded[base_name]
    placements: list[tuple[str, float, float, float, float, float, float]] = [
        (base_name, 0.0, 0.0, x0_0, y0_0, w0, h0)
    ]
    cur_right = x0_0 + w0

    combined_parts: list[dict] = []
    for p in json.loads((REPO / "hardware/build" / variant / base_name / "parts.json").read_text()):
        cp = dict(p)
        cp["ref"] = offset_ref(base_name, p["ref"])
        combined_parts.append(cp)

    for bname in boards[1:]:
        net_prefix = f"{bname.upper()}_"
        src, bx0, by0, bw, bh = loaded[bname]

        if bname == "dome" and any(p[0] == "reflector" for p in placements):
            # Stack the round `dome` PCB directly ABOVE `reflector` in the same column
            _, _, _, ref_x0, ref_y0, ref_w, _ref_h = next(p for p in placements if p[0] == "reflector")
            target_x0 = round(ref_x0 + (ref_w - bw) / 2.0, 4)
            target_y1 = round(ref_y0 - SLOT_W, 4)
            dx = round(target_x0 - bx0, 4)
            dy = round(target_y1 - (by0 + bh), 4)
            target_y0 = round(by0 + dy, 4)
            advance_right = False
        elif bname == "probe" and any(p[0] == "crown" for p in placements):
            # Place rotated 90-deg `probe` (50.0 x 14.0 mm) horizontally ABOVE `crown` and `SLOT_W` (2.0 mm) above `dome`!
            _, _, _, cr_x0, _cr_y0, _cr_w, _cr_h = next(p for p in placements if p[0] == "crown")
            dome_p = next((p for p in placements if p[0] == "dome"), None)
            target_x0 = round(cr_x0, 4)
            target_y1 = round(dome_p[4] - SLOT_W, 4) if dome_p else round(y0_0 + bh, 4)
            dx = round(target_x0 - bx0, 4)
            dy = round(target_y1 - (by0 + bh), 4)
            target_y0 = round(by0 + dy, 4)
            advance_right = False
        else:
            target_x0 = round(cur_right + SLOT_W, 4)
            dx = round(target_x0 - bx0, 4)
            dy = round((y0_0 + h0) - (by0 + bh), 4)
            target_y0 = round(by0 + dy, 4)
            advance_right = True
        vec = pcbnew.VECTOR2I_MM(dx, dy)

        net_map = {"": panel.FindNet("")}
        for net_name in src.GetNetInfo().NetsByName():
            sname = str(net_name)
            if not sname:
                continue
            pname = f"{net_prefix}{sname}"
            existing = panel.FindNet(pname)
            if not existing:
                ni = pcbnew.NETINFO_ITEM(panel, pname)
                panel.Add(ni)
                existing = panel.FindNet(pname)
            net_map[sname] = existing

        for fp in list(src.GetFootprints()):
            src.Remove(fp)
            fp.SetReference(offset_ref(bname, fp.GetReference()))
            for pad in fp.Pads():
                orig_net = pad.GetNetname()
                if net_map.get(orig_net):
                    pad.SetNet(net_map[orig_net])
            fp.Move(vec)
            panel.Add(fp)

        for trk in list(src.GetTracks()):
            src.Remove(trk)
            orig_net = trk.GetNetname()
            if net_map.get(orig_net):
                trk.SetNet(net_map[orig_net])
            trk.Move(vec)
            panel.Add(trk)

        for zone in list(src.Zones()):
            src.Remove(zone)
            orig_net = zone.GetNetname()
            if net_map.get(orig_net):
                zone.SetNet(net_map[orig_net])
            zone.Move(vec)
            panel.Add(zone)

        for drw in list(src.GetDrawings()):
            src.Remove(drw)
            drw.Move(vec)
            panel.Add(drw)

        placements.append((bname, dx, dy, target_x0, target_y0, bw, bh))
        if advance_right:
            cur_right = target_x0 + bw

        for p in json.loads((REPO / "hardware/build" / variant / bname / "parts.json").read_text()):
            cp = dict(p)
            cp["ref"] = offset_ref(bname, p["ref"])
            combined_parts.append(cp)

    mb_coords: list[tuple[float, float]] = []
    # 1. Horizontal mouse-bite tabs along bottom row: main <-> crown <-> reflector <-> power
    horiz_placements = [p for p in placements if p[0] in ("main", "crown", "reflector", "power")
                        or p[0] == "probe" and not any(q[0] == "crown" for q in placements)]
    for i in range(len(horiz_placements) - 1):
        l_name, _, _, lx0, ly0, lw, lh = horiz_placements[i]
        _, _, _, rx0, ry0, _rw, rh = horiz_placements[i + 1]
        lx1 = round(lx0 + lw, 4)
        ly1 = round(ly0 + lh, 4)
        ry1 = round(ry0 + rh, 4)
        l_ys, r_ys = [], []
        for drw in list(panel.GetDrawings()):
            if is_vertical_edge_at_x(drw, lx1, ly0, ly1):
                l_ys.extend((round(pcbnew.ToMM(drw.GetStart().y), 4), round(pcbnew.ToMM(drw.GetEnd().y), 4)))
                panel.Delete(drw)
            elif is_vertical_edge_at_x(drw, rx0, ry0, ry1):
                r_ys.extend((round(pcbnew.ToMM(drw.GetStart().y), 4), round(pcbnew.ToMM(drw.GetEnd().y), 4)))
                panel.Delete(drw)
        ly_s0, ly_s1 = (min(l_ys), max(l_ys)) if l_ys else (ly0, ly1)
        ry_s0, ry_s1 = (min(r_ys), max(r_ys)) if r_ys else (ry0, ry1)
        ov_y0 = max(ly_s0, ry_s0)
        ov_y1 = min(ly_s1, ry_s1)
        ov_h = ov_y1 - ov_y0
        hw = TAB_H / 2
        t1 = round(ov_y0 + ov_h * 0.28, 4)
        t2 = round(ov_y0 + ov_h * 0.72, 4)
        t_centres = (t1, t2)

        # If left board is `main` and `probe` is above `crown`, also add a mouse-bite tab from `main` to `probe`!
        probe_pl = next((p for p in placements if p[0] == "probe"), None)
        if l_name == "main" and probe_pl and any(q[0] == "crown" for q in placements):
            _, _, _, px0, py0, _pw, ph = probe_pl
            py1 = round(py0 + ph, 4)
            for drw in list(panel.GetDrawings()):
                if is_vertical_edge_at_x(drw, px0, py0, py1):
                    panel.Delete(drw)
            tp = round(py0 + ph / 2.0, 4)
            add_edge_line(panel, lx1, ly_s0, lx1, tp - hw)
            add_edge_line(panel, lx1, tp + hw, lx1, t1 - hw)
            add_edge_line(panel, lx1, t1 + hw, lx1, t2 - hw)
            add_edge_line(panel, lx1, t2 + hw, lx1, ly_s1)
            add_edge_line(panel, px0, py0, px0, tp - hw)
            add_edge_line(panel, px0, tp + hw, px0, py1)
            add_edge_line(panel, lx1, tp - hw, px0, tp - hw)
            add_edge_line(panel, lx1, tp + hw, px0, tp + hw)
            for dy_h in HOLE_Y_OFFS:
                mb_coords.append((lx1 + 0.15, tp + dy_h))
                mb_coords.append((px0 - 0.15, tp + dy_h))
        else:
            add_edge_line(panel, lx1, ly_s0, lx1, t1 - hw)
            add_edge_line(panel, lx1, t1 + hw, lx1, t2 - hw)
            add_edge_line(panel, lx1, t2 + hw, lx1, ly_s1)

        add_edge_line(panel, rx0, ry_s0, rx0, t1 - hw)
        add_edge_line(panel, rx0, t1 + hw, rx0, t2 - hw)
        add_edge_line(panel, rx0, t2 + hw, rx0, ry_s1)

        hole_off_x = 0.15
        for tc in t_centres:
            add_edge_line(panel, lx1, tc - hw, rx0, tc - hw)
            add_edge_line(panel, lx1, tc + hw, rx0, tc + hw)
            for dy_h in HOLE_Y_OFFS:
                mb_coords.append((lx1 + hole_off_x, tc + dy_h))
                mb_coords.append((rx0 - hole_off_x, tc + dy_h))

    # 2. Vertical mouse-bite tabs connecting `probe` (bottom edge) down to BOTH `crown` (top flat edge) AND `dome` (top flat chord)!
    probe_pl = next((p for p in placements if p[0] == "probe"), None)
    crown_pl = next((p for p in placements if p[0] == "crown"), None)
    dome_pl = next((p for p in placements if p[0] == "dome"), None)
    refl_pl = next((p for p in placements if p[0] == "reflector"), None)
    if probe_pl and crown_pl and dome_pl:
        _, _, _, px0, py0, pw, ph = probe_pl
        _, _, _, cx0, cy0, cw, _ch = crown_pl
        _, _, _, dx0, dy0, dw, dh = dome_pl
        py1 = round(py0 + ph, 4)
        top_xs, cr_xs, dm_top_xs = [], [], []
        for drw in list(panel.GetDrawings()):
            if is_horizontal_edge_at_y(drw, py1, px0, px0 + pw):
                top_xs.extend((round(pcbnew.ToMM(drw.GetStart().x), 4), round(pcbnew.ToMM(drw.GetEnd().x), 4)))
                panel.Delete(drw)
            elif is_horizontal_edge_at_y(drw, cy0, cx0, cx0 + cw):
                cr_xs.extend((round(pcbnew.ToMM(drw.GetStart().x), 4), round(pcbnew.ToMM(drw.GetEnd().x), 4)))
                panel.Delete(drw)
            elif is_horizontal_edge_at_y(drw, dy0, dx0, dx0 + dw):
                dm_top_xs.extend((round(pcbnew.ToMM(drw.GetStart().x), 4), round(pcbnew.ToMM(drw.GetEnd().x), 4)))
                panel.Delete(drw)
        tx0, tx1 = (min(top_xs), max(top_xs)) if top_xs else (px0, px0 + pw)
        bx0_c, bx1_c = (min(cr_xs), max(cr_xs)) if cr_xs else (cx0 + 7.0, cx0 + cw - 7.0)
        bx0_d, bx1_d = (min(dm_top_xs), max(dm_top_xs)) if dm_top_xs else (dx0 + 4.6, dx0 + dw - 4.6)

        xc_crown = round(cx0 + cw / 2.0, 4)
        xc_dome = round(dx0 + dw / 2.0, 4)
        hw = TAB_H / 2

        # Split probe's bottom horizontal edge at BOTH xc_crown and xc_dome!
        add_edge_line(panel, tx0, py1, xc_crown - hw, py1)
        add_edge_line(panel, xc_crown + hw, py1, xc_dome - hw, py1)
        add_edge_line(panel, xc_dome + hw, py1, tx1, py1)

        # Tab 2A: probe <-> crown top flat edge
        add_edge_line(panel, bx0_c, cy0, xc_crown - hw, cy0)
        add_edge_line(panel, xc_crown + hw, cy0, bx1_c, cy0)
        add_edge_line(panel, xc_crown - hw, py1, xc_crown - hw, cy0)
        add_edge_line(panel, xc_crown + hw, py1, xc_crown + hw, cy0)
        for dx_h in HOLE_Y_OFFS:
            mb_coords.append((xc_crown + dx_h, py1 + 0.15))
            mb_coords.append((xc_crown + dx_h, cy0 - 0.15))

        # Tab 2B: probe <-> dome top flat chord (anchors probe's right tip to dome's top!)
        add_edge_line(panel, bx0_d, dy0, xc_dome - hw, dy0)
        add_edge_line(panel, xc_dome + hw, dy0, bx1_d, dy0)
        add_edge_line(panel, xc_dome - hw, py1, xc_dome - hw, dy0)
        add_edge_line(panel, xc_dome + hw, py1, xc_dome + hw, dy0)
        for dx_h in HOLE_Y_OFFS:
            mb_coords.append((xc_dome + dx_h, py1 + 0.15))
            mb_coords.append((xc_dome + dx_h, dy0 - 0.15))

    # 3. Vertical mouse-bite tab connecting `dome` (bottom chord) to the top edge of `reflector`
    if dome_pl and refl_pl:
        _, _, _, dx0, dy0, dw, dh = dome_pl
        _, _, _, rx0, ry0, rw, _rh = refl_pl
        dy1 = round(dy0 + dh, 4)
        top_xs, bot_xs = [], []
        for drw in list(panel.GetDrawings()):
            if is_horizontal_edge_at_y(drw, dy1, dx0, dx0 + dw):
                top_xs.extend((round(pcbnew.ToMM(drw.GetStart().x), 4), round(pcbnew.ToMM(drw.GetEnd().x), 4)))
                panel.Delete(drw)
            elif is_horizontal_edge_at_y(drw, ry0, rx0, rx0 + rw):
                bot_xs.extend((round(pcbnew.ToMM(drw.GetStart().x), 4), round(pcbnew.ToMM(drw.GetEnd().x), 4)))
                panel.Delete(drw)
        tx0, tx1 = (min(top_xs), max(top_xs)) if top_xs else (dx0 + 4.6, dx0 + dw - 4.6)
        bx0_s, bx1_s = (min(bot_xs), max(bot_xs)) if bot_xs else (rx0, rx0 + rw)
        xc = round(dx0 + dw / 2.0, 4)
        hw = TAB_H / 2
        add_edge_line(panel, tx0, dy1, xc - hw, dy1)
        add_edge_line(panel, xc + hw, dy1, tx1, dy1)
        add_edge_line(panel, bx0_s, ry0, xc - hw, ry0)
        add_edge_line(panel, xc + hw, ry0, bx1_s, ry0)
        add_edge_line(panel, xc - hw, dy1, xc - hw, ry0)
        add_edge_line(panel, xc + hw, dy1, xc + hw, ry0)
        for dx_h in HOLE_Y_OFFS:
            y_arc = round(dy0 + dh / 2.0 + (6.65 ** 2 - dx_h ** 2) ** 0.5, 4)
            mb_coords.append((xc + dx_h, y_arc))
            mb_coords.append((xc + dx_h, ry0 - 0.15))

    panel_checks = jp.check_board(panel, combined_parts)
    add_mouse_bite_holes(panel, mb_coords)

    panel_pcb_path = panel_dir / "panel.kicad_pcb"
    pcbnew.SaveBoard(str(panel_pcb_path), panel)

    build_panel_dir = REPO / "hardware/build" / variant / "panel"
    build_panel_dir.mkdir(parents=True, exist_ok=True)
    (build_panel_dir / "parts.json").write_text(json.dumps(combined_parts, indent=2))

    drc_report = panel_dir / "drc.json"
    run("kicad-cli", "pcb", "drc", "--format", "json", "--output", str(drc_report), str(panel_pcb_path))
    drc = json.loads(drc_report.read_text())
    errors = [v for v in drc["violations"] if v["severity"] == "error"]
    unrouted = drc["unconnected_items"]
    problems = (
        [f"panel: {len(errors)} DRC errors: {[e['description'] for e in errors[:5]]}"] * bool(errors)
        + [f"panel: {len(unrouted)} unrouted"] * bool(unrouted)
    )
    if problems:
        return problems

    copper = [pcbnew.LayerName(layer) for layer in panel.GetEnabledLayers().CuStack()]
    layers = copper + ["F.Mask", "B.Mask", "F.Paste", "B.Paste", "F.SilkS", "B.SilkS", "Edge.Cuts"]
    gtarget = panel_dir / "gerbers"
    gtarget.mkdir()
    run("kicad-cli", "pcb", "export", "gerbers", "--output", f"{gtarget}/",
        "--layers", ",".join(layers), "--no-protel-ext", str(panel_pcb_path))
    run("kicad-cli", "pcb", "export", "drill", "--output", f"{gtarget}/",
        "--format", "excellon", "--excellon-separate-th", str(panel_pcb_path))
    shutil.make_archive(str(panel_dir / "panel_gerbers"), "zip", gtarget)
    shutil.rmtree(gtarget)

    renders_dir = panel_dir / "renders"
    renders_dir.mkdir(exist_ok=True)
    run("kicad-cli", "pcb", "export", "svg", "--output", str(renders_dir / "top.svg"),
        "--layers", "F.Cu,F.Mask,F.SilkS,Edge.Cuts", "--page-size-mode", "2", str(panel_pcb_path))
    run("kicad-cli", "pcb", "export", "svg", "--output", str(renders_dir / "bottom.svg"),
        "--layers", "B.Cu,B.Mask,B.SilkS,Edge.Cuts", "--mirror", "--page-size-mode", "2", str(panel_pcb_path))

    by_ref = {p["ref"]: p for p in combined_parts}
    groups: dict[tuple[str, str, str], list[str]] = {}
    for p in by_ref.values():
        if p["assembly"] == "jlc":
            key = (p["value"], p["footprint"].split(":")[1], p["lcsc"])
            groups.setdefault(key, []).append(p["ref"])
    with open(panel_dir / "panel_bom_jlc.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (value, footprint, lcsc), refs in sorted(groups.items()):
            w.writerow([value, ",".join(sorted(refs)), footprint, lcsc])

    cpl_rows = []
    check_rows = []
    for c in panel_checks:
        if c.assembly != "jlc":
            continue
        pref = c.ref
        fp = panel.FindFootprintByReference(pref)
        layer = "Bottom" if fp.IsFlipped() else "Top"
        kicad_rot = round(fp.GetOrientationDegrees()) % 360
        cpl_rows.append([pref, f"{c.placement.x:.3f}mm", f"{c.placement.y:.3f}mm", layer, c.placement.rotation])
        check_rows.append([
            pref, c.lcsc, by_ref[pref]["mpn"], c.jlc_footprint, layer,
            kicad_rot, c.placement.rotation, c.stock, c.library, "lands pin for pin",
        ])

    with open(panel_dir / "panel_cpl_jlc.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        w.writerows(cpl_rows)

    with open(panel_dir / "panel_assembly_check.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "LCSC", "Manufacturer part", "JLCPCB footprint", "Layer",
                    "KiCad rotation", "JLCPCB rotation", "Stock", "Library", "Result"])
        w.writerows(check_rows)

    with open(panel_dir / "panel_hand_fit.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Value", "Manufacturer part", "Notes"])
        for p in sorted(by_ref.values(), key=lambda item: item["ref"]):
            if p["assembly"] == "hand":
                w.writerow([p["ref"], p["value"], p["mpn"], p["source"]])

    pbb = panel.GetBoardEdgesBoundingBox()
    pw_mm = pcbnew.ToMM(pbb.GetWidth())
    ph_mm = pcbnew.ToMM(pbb.GetHeight())
    print(f"panel ({'+'.join(boards)}): released {pw_mm:.1f}x{ph_mm:.1f}mm breakaway mouse-bite panel "
          f"(0 DRC errors, {len(groups)} consolidated BOM lines, {len(cpl_rows)} SMT placements)")
    return []
