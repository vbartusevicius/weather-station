"""Build a single breakaway mouse-bite PCB panel combining all boards of a variant."""

from __future__ import annotations

import csv
import itertools
import json
import re
import shutil
import subprocess
from pathlib import Path

import fab_outputs
import jlc_parts as jp
import pcbnew

REPO = Path("/work")
SLOT_W = 2.0
TAB_H = 3.2
HOLE_DIA = 0.5
HOLE_Y_OFFS = (-0.9, -0.3, 0.3, 0.9)

# Boards ordered on their own instead of in the panel (none: all six pack into one panel, see pack_columns).
SEPARATE_BOARDS: tuple[str, ...] = ()

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


def edge_segments(pcb: pcbnew.BOARD, horizontal: bool, coord: float, lo: float, hi: float) -> list:
    """Edge.Cuts line segments lying on y = coord (horizontal) or x = coord, overlapping [lo, hi]: (item, a, b)."""
    out = []
    for d in pcb.GetDrawings():
        if d.GetLayer() != pcbnew.Edge_Cuts or not isinstance(d, pcbnew.PCB_SHAPE) or d.GetShape() != pcbnew.SHAPE_T_SEGMENT:
            continue
        sx, sy = pcbnew.ToMM(d.GetStart().x), pcbnew.ToMM(d.GetStart().y)
        ex, ey = pcbnew.ToMM(d.GetEnd().x), pcbnew.ToMM(d.GetEnd().y)
        if horizontal and abs(sy - coord) < 0.08 and abs(ey - coord) < 0.08:
            a, b = sorted((sx, ex))
        elif not horizontal and abs(sx - coord) < 0.08 and abs(ex - coord) < 0.08:
            a, b = sorted((sy, ey))
        else:
            continue
        if b > lo - 0.2 and a < hi + 0.2:
            out.append((d, round(a, 4), round(b, 4)))
    return out


def cut_edge(pcb: pcbnew.BOARD, horizontal: bool, coord: float, centre: float, hw: float) -> bool:
    """Open a TAB_H gap at `centre` in the edge segment covering it (with 0.3 mm margin); False if none does."""
    for d, a, b in edge_segments(pcb, horizontal, coord, centre - hw, centre + hw):
        if a <= centre - hw - 0.3 and b >= centre + hw + 0.3:
            pcb.Delete(d)
            if horizontal:
                add_edge_line(pcb, a, coord, centre - hw, coord)
                add_edge_line(pcb, centre + hw, coord, b, coord)
            else:
                add_edge_line(pcb, coord, a, coord, centre - hw)
                add_edge_line(pcb, coord, centre + hw, coord, b)
            return True
    return False


def part_boxes(pcb: pcbnew.BOARD) -> list[tuple[float, float, float, float]]:
    """Part extents (x0, y0, x1, y1) in mm from the pads (+0.3 mm body overhang), panel helpers excluded."""
    out = []
    for fp in pcb.GetFootprints():
        if fp.GetReference() in ("MOUSE_BITES", "TOOLING") or fp.GetReference().startswith("FID"):
            continue
        pads = list(fp.Pads())
        if not pads:
            continue
        bbs = [p.GetBoundingBox() for p in pads]   # copper extent + 0.3 mm for the part body overhang
        out.append((min(pcbnew.ToMM(b.GetLeft()) for b in bbs) - 0.3, min(pcbnew.ToMM(b.GetTop()) for b in bbs) - 0.3,
                    max(pcbnew.ToMM(b.GetRight()) for b in bbs) + 0.3, max(pcbnew.ToMM(b.GetBottom()) for b in bbs) + 0.3))
    return out


def tab_clearance(boxes, horizontal: bool, coord: float, centre: float, margin: float = 1.0) -> float:
    """Distance from a tab (its TAB_H mouth on an edge at `coord`) to the nearest part, 0 if within `margin`."""
    hw = TAB_H / 2
    if horizontal:   # tab mouth along x at y = coord
        mx0, mx1, my0, my1 = centre - hw, centre + hw, coord, coord
    else:
        mx0, mx1, my0, my1 = coord, coord, centre - hw, centre + hw
    best = 1e9
    for x0, y0, x1, y1 in boxes:
        dx = max(x0 - mx1, mx0 - x1, 0.0)
        dy = max(y0 - my1, my0 - y1, 0.0)
        best = min(best, (dx * dx + dy * dy) ** 0.5)
    return 0.0 if best < margin else best


def pick_tabs(boxes, horizontal: bool, coords: tuple[float, ...], a: float, b: float, n: int) -> list[float]:
    """Up to n tab centres in [a, b] whose mouths on every edge in `coords` are clear of parts, spread apart."""
    step = 0.25
    cands = [a + i * step for i in range(int((b - a) / step) + 1)]
    ok = [c for c in cands if all(tab_clearance(boxes, horizontal, k, c) > 0.0 for k in coords)]
    if not ok:
        return []
    if n == 1 or len(ok) == 1:
        return [round(min(ok, key=lambda c: abs(c - (a + b) / 2.0)), 4)]
    picks = [round(min(ok, key=lambda c: abs(c - (a + 0.2 * (b - a)))), 4),
             round(min(ok, key=lambda c: abs(c - (a + 0.8 * (b - a)))), 4)]
    return picks if picks[1] - picks[0] >= TAB_H + 2.0 else picks[:1]


def bridge(pcb: pcbnew.BOARD, horizontal: bool, c: float, a: float, b: float, mb: list) -> None:
    """Mouse-bite tab of width TAB_H centred at `c` across the gap between two facing straight edges at a and b
    (horizontal edges at y = a / y = b, or vertical edges at x = a / x = b), with 4 breakaway holes on each side."""
    hw = TAB_H / 2
    if not (cut_edge(pcb, horizontal, a, c, hw) and cut_edge(pcb, horizontal, b, c, hw)):
        raise RuntimeError(f"tab at {c:.2f} does not land on both edges ({a:.2f}, {b:.2f})")
    s = 1.0 if b > a else -1.0
    for side in (-hw, hw):
        if horizontal:
            add_edge_line(pcb, c + side, a, c + side, b)
        else:
            add_edge_line(pcb, a, c + side, b, c + side)
    for d in HOLE_Y_OFFS:
        mb += [(c + d, a + s * 0.15), (c + d, b - s * 0.15)] if horizontal else \
              [(a + s * 0.15, c + d), (b - s * 0.15, c + d)]


def pack_columns(sizes: dict[str, tuple[float, float]]) -> dict[str, tuple[float, float, bool]]:
    """Smallest-area column packing of the boards: every 0 / 90 deg rotation combination, panel heights from the
    tallest board's short side up, first-fit-decreasing into columns of that height (SLOT_W gaps).
    Returns name -> (x0, y0, rotated)."""
    names = list(sizes)
    hi = sum(max(w, h) for w, h in sizes.values())
    best = None
    for bits in range(2 ** len(names)):
        dims = {n: (sizes[n][1], sizes[n][0]) if bits >> i & 1 else sizes[n] for i, n in enumerate(names)}
        tallest = max(h for _w, h in dims.values())
        for step in range(int((min(hi, tallest + 40.0) - tallest) / 0.1) + 1):
            height = round(tallest + 0.1 * step, 1)
            cols: list[list] = []   # [width, used height, [names]]
            for n in sorted(names, key=lambda n: (-dims[n][1], -dims[n][0])):
                w, h = dims[n]
                col = next((c for c in cols if c[1] + SLOT_W + h <= height + 1e-6), None)
                if col:
                    col[0], col[1] = max(col[0], w), col[1] + SLOT_W + h
                    col[2].append(n)
                else:
                    cols.append([w, h, [n]])
            width = sum(c[0] for c in cols) + SLOT_W * (len(cols) - 1)
            used = max(c[1] for c in cols)
            area = width * used
            if best is None or area < best[0] - 1e-6:
                best = (area, cols, dims, bits)
    _area, cols, dims, bits = best
    return [(w, members) for w, _used, members in cols], {n: bool(bits >> names.index(n) & 1) for n in names}


def place_columns(cols, dims) -> dict[str, tuple[float, float, float, float]]:
    """Column structure -> board rectangles (x0, y0, x1, y1), columns left to right, boards stacked top down."""
    out, x = {}, 0.0
    for width, members in cols:
        y = 0.0
        for n in members:
            w, h = dims[n]
            out[n] = (round(x, 4), round(y, 4), round(x + w, 4), round(y + h, 4))
            y += h + SLOT_W
        x += width + SLOT_W
    return out


def board_geometry(brd: pcbnew.BOARD):
    """Straight outline segments and part boxes of a loaded board, relative to its outline's top-left corner."""
    x0, y0, _w, _h = board_edge_bounds(brd)
    segs = []
    for d in brd.GetDrawings():
        if d.GetLayer() != pcbnew.Edge_Cuts or not isinstance(d, pcbnew.PCB_SHAPE) or d.GetShape() != pcbnew.SHAPE_T_SEGMENT:
            continue
        sx, sy = pcbnew.ToMM(d.GetStart().x) - x0, pcbnew.ToMM(d.GetStart().y) - y0
        ex, ey = pcbnew.ToMM(d.GetEnd().x) - x0, pcbnew.ToMM(d.GetEnd().y) - y0
        if abs(sy - ey) < 0.05:
            segs.append((True, sy, min(sx, ex), max(sx, ex)))
        elif abs(sx - ex) < 0.05:
            segs.append((False, sx, min(sy, ey), max(sy, ey)))
    boxes = [(bx0 - x0, by0 - y0, bx1 - x0, by1 - y0) for bx0, by0, bx1, by1 in part_boxes(brd)]
    return segs, boxes


def flip_geometry(g, dims):
    """Board geometry (segments, part boxes) turned 180 deg inside its w x h outline box."""
    w, h = dims
    segs = [(hz, (h if hz else w) - c, (w if hz else h) - b, (w if hz else h) - a) for hz, c, a, b in g[0]]
    boxes = [(w - x1, h - y1, w - x0, h - y0) for x0, y0, x1, y1 in g[1]]
    return segs, boxes


def tab_plan(rects, geo) -> tuple[list, list[str]]:
    """Tabs for a board arrangement: (horizontal, centre, edge a, edge b, board a, board b) for every facing pair
    of straight edges with part-free tab positions, and the boards left loose."""
    boxes = [(bx0 + r[0], by0 + r[1], bx1 + r[0], by1 + r[1])
             for n, r in rects.items() for bx0, by0, bx1, by1 in geo[n][1]]

    def segs(n, horizontal, coord):
        r = rects[n]
        off_c, off_s = (r[1], r[0]) if horizontal else (r[0], r[1])
        return [(a + off_s, b + off_s) for h, c, a, b in geo[n][0] if h == horizontal and abs(c + off_c - coord) < 0.08]

    parent = {n: n for n in rects}

    def root(n):
        while parent[n] != n:
            n = parent[n]
        return n

    tabs = []
    for a, (ax0, ay0, ax1, ay1) in rects.items():
        for b, (bx0, by0, bx1, by1) in rects.items():
            if a == b:
                continue
            if 0.0 < bx0 - ax1 <= 6.0 and min(ay1, by1) - max(ay0, by0) > TAB_H:     # b right of a
                horizontal, ea, eb, lo, hi = False, ax1, bx0, max(ay0, by0), min(ay1, by1)
            elif 0.0 < by0 - ay1 <= 6.0 and min(ax1, bx1) - max(ax0, bx0) > TAB_H:   # b below a
                horizontal, ea, eb, lo, hi = True, ay1, by0, max(ax0, bx0), min(ax1, bx1)
            else:
                continue
            spans = [(max(a1, a2, lo) + TAB_H / 2 + 0.5, min(b1, b2, hi) - TAB_H / 2 - 0.5)
                     for a1, b1 in segs(a, horizontal, ea) for a2, b2 in segs(b, horizontal, eb)]
            spans = sorted(((s0, s1) for s0, s1 in spans if s1 >= s0), key=lambda s: s[0] - s[1])
            placed = []
            for s0, s1 in spans:
                for c in pick_tabs(boxes, horizontal, (ea, eb), s0, s1, 2 if s1 - s0 > 20.0 else 1):
                    if len(placed) < 2 and all(abs(c - q) > TAB_H + 2.0 for q in placed):
                        tabs.append((horizontal, c, ea, eb, a, b))
                        placed.append(c)
            if placed:
                parent[root(a)] = root(b)
    first = next(iter(rects))
    return tabs, sorted(n for n in rects if root(n) != root(first))


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

    sizes = {}
    for bname in boards:
        _x0, _y0, w, h = board_edge_bounds(pcbnew.LoadBoard(str(REPO / "hardware/layout" / variant / bname / f"{bname}.kicad_pcb")))
        sizes[bname] = (w, h)
    cols, rotated = pack_columns(sizes)

    loaded: dict[str, tuple[pcbnew.BOARD, float, float, float, float]] = {}
    for bname in boards:
        brd = pcbnew.LoadBoard(str(REPO / "hardware/layout" / variant / bname / f"{bname}.kicad_pcb"))
        if rotated[bname]:
            rotate_board_deg(brd, -90.0)
        loaded[bname] = (brd, *board_edge_bounds(brd))
    dims = {n: (v[3], v[4]) for n, v in loaded.items()}
    geo = {n: board_geometry(v[0]) for n, v in loaded.items()}
    # neither the column order nor the order inside a column changes the area: take the arrangement whose tabs
    # hold every board (parts sit too close to some board edges for mouse-bites), then the most boards with >= 2
    best = None
    variants = [list(itertools.permutations(members)) for _w, members in cols]
    for stacks in itertools.product(*variants):
        for order in itertools.permutations(zip((w for w, _m in cols), stacks)):
            rects = place_columns(order, dims)
            tabs, loose = tab_plan(rects, geo)
            per_board = {n: sum(n in (tb[4], tb[5]) for tb in tabs) for n in rects}
            score = (-len(loose), min(per_board.values()), sum(v >= 2 for v in per_board.values()), len(tabs))
            if best is None or score > best[0]:
                best = (score, rects, tabs, loose)
    # then try each board turned 180 deg (other edges face the neighbours: parts sit close to some edges)
    flipped = set()
    for n in list(geo):
        trial = dict(geo)
        trial[n] = flip_geometry(geo[n], dims[n])
        tabs, loose = tab_plan(best[1], trial)
        per_board = {m: sum(m in (tb[4], tb[5]) for tb in tabs) for m in best[1]}
        score = (-len(loose), min(per_board.values()), sum(v >= 2 for v in per_board.values()), len(tabs))
        if score > best[0]:
            geo, best = trial, (score, best[1], tabs, loose)
            flipped ^= {n}
    for n in flipped:
        rotate_board_deg(loaded[n][0], 180.0)
        loaded[n] = (loaded[n][0], *board_edge_bounds(loaded[n][0]))
    _score, rects, tabs, loose = best
    if loose:
        raise RuntimeError(f"no column order holds every board with part-free tabs: {loose} loose")
    layout = {n: (r[0], r[1], rotated[n]) for n, r in rects.items()}

    base_name = boards[0]
    base_path = REPO / "hardware/layout" / variant / base_name / f"{base_name}.kicad_pcb"
    for suffix in (".kicad_pro", ".kicad_dru"):
        shutil.copy(base_path.with_suffix(suffix), panel_dir / f"panel{suffix}")
    panel = loaded[base_name][0]

    placements: list[tuple[str, float, float, float, float, float, float]] = []
    combined_parts: list[dict] = []
    for bname in boards:
        src, bx0, by0, bw, bh = loaded[bname]
        target_x0, target_y0, _rot = layout[bname]
        dx, dy = round(target_x0 - bx0, 4), round(target_y0 - by0, 4)
        vec = pcbnew.VECTOR2I_MM(dx, dy)
        if bname == base_name:   # the panel is the first board's own PCB: move its items in place
            for item in [*src.GetFootprints(), *src.GetTracks(), *src.Zones(), *src.GetDrawings()]:
                item.Move(vec)
        else:
            net_prefix = f"{bname.upper()}_"
            net_map = {"": panel.FindNet("")}
            for net_name in src.GetNetInfo().NetsByName():
                sname = str(net_name)
                if not sname:
                    continue
                pname = f"{net_prefix}{sname}"
                if not panel.FindNet(pname):
                    panel.Add(pcbnew.NETINFO_ITEM(panel, pname))
                net_map[sname] = panel.FindNet(pname)
            for fp in list(src.GetFootprints()):
                src.Remove(fp)
                fp.SetReference(offset_ref(bname, fp.GetReference()))
                for pad in fp.Pads():
                    if net_map.get(pad.GetNetname()):
                        pad.SetNet(net_map[pad.GetNetname()])
                fp.Move(vec)
                panel.Add(fp)
            for item in list(src.GetTracks()) + list(src.Zones()):
                src.Remove(item)
                if net_map.get(item.GetNetname()):
                    item.SetNet(net_map[item.GetNetname()])
                item.Move(vec)
                panel.Add(item)
            for drw in list(src.GetDrawings()):
                src.Remove(drw)
                drw.Move(vec)
                panel.Add(drw)
        placements.append((bname, dx, dy, target_x0, target_y0, bw, bh))
        for p in json.loads((REPO / "hardware/build" / variant / bname / "parts.json").read_text()):
            cp = dict(p)
            cp["ref"] = offset_ref(bname, p["ref"])
            combined_parts.append(cp)

    mb_coords: list[tuple[float, float]] = []
    for horizontal, c, ea, eb, _a, _b in tabs:
        bridge(panel, horizontal, c, ea, eb, mb_coords)

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
    # ODB++, Gerbers + drills + IPC-D-356, generic BOM / CPL (same set as every board)
    fab_outputs.export_fab(panel_pcb_path, combined_parts, panel_dir, "panel", layers)

    renders_dir = panel_dir / "renders"
    renders_dir.mkdir(exist_ok=True)
    run("kicad-cli", "pcb", "export", "svg", "--output", str(renders_dir / "top.svg"),
        "--layers", "F.Cu,F.Mask,F.SilkS,Edge.Cuts", "--exclude-drawing-sheet", "--fit-page-to-board", str(panel_pcb_path))
    run("kicad-cli", "pcb", "export", "svg", "--output", str(renders_dir / "bottom.svg"),
        "--layers", "B.Cu,B.Mask,B.SilkS,Edge.Cuts", "--mirror", "--exclude-drawing-sheet", "--fit-page-to-board", str(panel_pcb_path))

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
