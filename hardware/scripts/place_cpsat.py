"""Deterministic 2-stage Google OR-Tools CP-SAT PCB placer (`eda` container).

Stage 1 (Anchors & Active/Large Parts):
  - Fixes mechanical connectors, switches, optical/thermal-zone anchors (`locked: true`), board outline pockets,
    milled slots, antenna keepouts, enclosure posts/clips, and cylinder radius limits.
  - Solves all unlocked ICs, inductors, fuses, crystals, transistors and diodes simultaneously on an integer grid
    (`0.05 mm`), enforcing `AddNoOverlap2D` on `F.Cu`, `B.Cu`, and cross-face `F<->B` THT drill + exposed-pad
    via-escape shadows.
  - Minimizes weighted L1 pad-to-pad Manhattan wirelength (`AddAbsEquality`) with discrete `0/90/180/270` orientations,
    fixed `random_seed`, single worker (deterministic), and bounded solver time.

Stage 2 (Batched Passives):
  - Freezes Stage 1 parts with routing-escape court halos (wider around fine-pitch ICs and connectors).
  - Solves 2-pin passives (`R`, `C`, `TH`) in logical batches (`block` / parent IC) using boolean `is_vert` (`0/90`
    width/height swap via `OnlyEnforceIf`) and `is_flip` (`0/180` polarity) anchored to exact rotated pad coordinates
    of their parent ICs, down-weighting global `GND` and power rails so decoupling capacitors hug their target IC pins.

Usage:
    python3 hardware/scripts/place_cpsat.py <board> [--write] [--eval-dir DIR] [--unlock-power-stage]
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

import pcbnew
import yaml
from ortools.sat.python import cp_model
from shapely.geometry import Polygon, box
from shapely.ops import unary_union

REPO = Path("/work") if Path("/work/hardware").exists() else Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

import layout

SPEC = load_spec()
VARIANT = SPEC["active_variant"]

GRID_MM = 0.05
ANGLES = (0, 90, 180, 270)
PASSIVE_PREFIXES = ("R", "C", "TH")
GLOBAL_GND = {"GND"}
HIGH_PRIORITY_PATTERNS = ("SW", "BST", "FB", "COMP", "CC", "RT", "IN", "INV", "XTAL", "AS_INP", "GATE", "SENSE")


def g_lo(mm_val: float) -> int:
    return math.floor(round(mm_val / GRID_MM, 6))


def g_hi(mm_val: float) -> int:
    return math.ceil(round(mm_val / GRID_MM, 6))


def g_near(mm_val: float) -> int:
    return round(mm_val / GRID_MM)


def ref_prefix(ref: str) -> str:
    m = re.match(r"^([A-Za-z]+)", ref)
    return m.group(1) if m else ""


def is_small_passive(sp: PartSpec) -> bool:
    if ref_prefix(sp.ref) not in PASSIVE_PREFIXES or sp.pad_count != 2:
        return False
    og = sp.oris[0]
    area_mm2 = ((og.dx1 - og.dx0) * GRID_MM) * ((og.dy1 - og.dy0) * GRID_MM)
    # 0805 courtyard is ~7.4 mm^2 -> Stage 2; 1206 (~10.4 mm^2) and 2512 bulk/power parts -> Stage 1!
    return area_mm2 < 9.0


@dataclass
class OriGeom:
    rot: int
    dx0: int
    dy0: int
    dx1: int
    dy1: int
    pads: dict[str, tuple[int, int, str]]       # pad_num -> (pdx_g, pdy_g, net_name)
    tht_boxes: list[tuple[int, int, int, int]]  # relative (dx0, dy0, dx1, dy1) in grid units
    via_shadow: list[tuple[int, int, int, int]] # opposite-side exposed-pad via shadow


@dataclass
class PartSpec:
    ref: str
    side: str               # "front" | "back"
    block: str
    locked: bool
    init_x: float
    init_y: float
    init_rot: int
    fpid: str
    pad_count: int
    fine_pitch: bool
    raised: bool
    oris: dict[int, OriGeom] = field(default_factory=dict)


def extract_part_geoms(board_name: str, plan: dict, *, unlock_power_stage: bool = False) -> dict[str, PartSpec]:
    """Measure exact courtyard, pad, THT drill and thermal-pad shadow geometry for all 4 rotations in pcbnew."""
    fresh = layout.fresh_board(board_name)
    raised_refs = set(plan["frame"].get("raised_modules", []))
    parts_cfg = plan["parts"]
    specs: dict[str, PartSpec] = {}

    for ref, cfg in parts_cfg.items():
        fp = fresh.FindFootprintByReference(ref)
        if fp is None:
            continue
        side = cfg.get("side", "front")
        locked = bool(cfg.get("locked", False))
        # On crown, L1 sits inside a hard-coded multi-layer antenna copper keepout window in layout_crown.yaml.
        if board_name == "crown" and ref == "L1":
            locked = True
        # On power, F.Cu priority-1 hot-loop pours are drawn at fixed coordinates around U1/U2/L1/L2/D1/D2/C1/C2/C5/C6/C10/C11/C14/C15
        # unless `--unlock-power-stage` is requested for pure from-scratch power placement evaluation.
        if board_name == "power" and not unlock_power_stage and side == "front" and cfg.get("block") in ("buck_logic", "buck_heat"):
            locked = True

        pads_list = [p for p in fp.Pads() if p.GetNumber()]
        fpid = str(fp.GetFPID().GetLibItemName())
        # Detect fine-pitch (< 0.85 mm min pad centre pitch) for escape halo sizing.
        min_pitch = 99.0
        pts0 = [(pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y)) for p in pads_list]
        for i in range(len(pts0)):
            for j in range(i + 1, len(pts0)):
                d = math.hypot(pts0[i][0] - pts0[j][0], pts0[i][1] - pts0[j][1])
                if d > 0.05:
                    min_pitch = min(min_pitch, d)
        fine_pitch = min_pitch < 0.85 or len(pads_list) >= 8

        init_rot = round(cfg.get("rot", 0)) % 360
        angles = sorted(set(ANGLES) | {init_rot})
        oris: dict[int, OriGeom] = {}
        for rot in angles:
            layout.set_side(fp, side == "back")
            fp.SetPosition(layout.pos(0.0, 0.0))
            fp.SetOrientationDegrees(rot)
            bb = layout.body_box(fp)
            dx0 = g_lo(pcbnew.ToMM(bb.GetLeft()))
            dx1 = g_hi(pcbnew.ToMM(bb.GetRight()))
            dy0 = g_lo(-pcbnew.ToMM(bb.GetBottom()))
            dy1 = g_hi(-pcbnew.ToMM(bb.GetTop()))

            pads: dict[str, tuple[int, int, str]] = {}
            tht_boxes: list[tuple[int, int, int, int]] = []
            via_shadow: list[tuple[int, int, int, int]] = []
            for p in fp.Pads():
                num = p.GetNumber()
                px = pcbnew.ToMM(p.GetPosition().x)
                py = -pcbnew.ToMM(p.GetPosition().y)
                net = p.GetNetname()
                if num and net:
                    # Multiple pads can share a pin number (e.g. SOT-223 or shield MP); keep numbered signal/power pads.
                    key = num if num not in pads else f"{num}_{len(pads)}"
                    pads[key] = (g_near(px), g_near(py), net)
                if p.GetDrillSizeX() > 0:
                    pbb = p.GetBoundingBox()
                    pad_margin = 0.25
                    tht_boxes.append((
                        g_lo(pcbnew.ToMM(pbb.GetLeft()) - pad_margin),
                        g_lo(-pcbnew.ToMM(pbb.GetBottom()) - pad_margin),
                        g_hi(pcbnew.ToMM(pbb.GetRight()) + pad_margin),
                        g_hi(-pcbnew.ToMM(pbb.GetTop()) + pad_margin),
                    ))
                elif num in ("EP", "EPAD", "9", "17", "21") and net == "GND" and len(pads_list) >= 6:
                    # Exposed thermal / ground centre pad: reserve opposite-layer via escape shadow.
                    pbb = p.GetBoundingBox()
                    pw = pcbnew.ToMM(pbb.GetWidth())
                    ph = pcbnew.ToMM(pbb.GetHeight())
                    if pw >= 1.2 and ph >= 1.2:
                        via_shadow.append((
                            g_lo(px - pw * 0.45),
                            g_lo(py - ph * 0.45),
                            g_hi(px + pw * 0.45),
                            g_hi(py + ph * 0.45),
                        ))
            oris[rot] = OriGeom(rot=rot, dx0=dx0, dy0=dy0, dx1=dx1, dy1=dy1,
                                pads=pads, tht_boxes=tht_boxes, via_shadow=via_shadow)

        specs[ref] = PartSpec(
            ref=ref, side=side, block=cfg.get("block", ""), locked=locked,
            init_x=float(cfg["x"]), init_y=float(cfg["y"]), init_rot=init_rot,
            fpid=fpid, pad_count=len(pads_list), fine_pitch=fine_pitch,
            raised=(ref in raised_refs), oris=oris,
        )
    return specs


def board_safe_polygon(board_name: str, plan: dict) -> Polygon:
    """Usable component courtyard region in board `(x, y)` mm coordinates."""
    frame = plan["frame"]
    w, h = frame["size"]["x"], frame["size"]["y"]
    poly = Polygon(frame["outline"]) if "outline" in frame else box(0.0, 0.0, w, h)
    # Footprints extend courtyards ~0.25..0.55 mm past copper pads; ultra-small round discs (Ø 13.6 mm dome_legacy)
    # allow courtyards to overhang Edge.Cuts by 0.2 mm while copper pads remain >= 0.35 mm inside Edge.Cuts.
    area = w * h
    margin = -0.20 if area < 200.0 else (0.10 if area < 350.0 else max(frame.get("edge_clearance", 0.3) - 0.10, 0.20))
    safe = poly.buffer(-margin, join_style="mitre")
    for slot in frame.get("slots", []):
        safe = safe.difference(box(slot["x0"] - 0.25, slot["y0"] - 0.25, slot["x1"] + 0.25, slot["y1"] + 0.25))

    # Cylinder inner wall limit (`OUTER_R - WALL - 0.5 = 45.5 mm`) mapped into horizontal board coordinates.
    bcfg = SPEC["boards"].get(board_name, {})
    if bcfg.get("plane") == "horizontal-xz":
        max_r = SPEC["envelope"]["outer_radius"] - SPEC["enclosure"]["wall"] - 0.65
        cx = w / 2.0 - bcfg.get("x_offset", 0.0)
        cy = h / 2.0 - bcfg.get("z_offset", 0.0)
        pts = [(cx + max_r * math.cos(2 * math.pi * k / 96),
                cy + max_r * math.sin(2 * math.pi * k / 96)) for k in range(96)]
        safe = safe.intersection(Polygon(pts))
    return safe


def decompose_union_to_grid_rects(polys: list[Polygon], w: float, h: float, strip_mm: float = 0.5) -> list[tuple[int, int, int, int]]:
    """Union arbitrary obstacle polygons inside `[0, w] x [0, h]` and slice into strictly disjoint integer grid boxes
    so fixed obstacles in `AddNoOverlap2D` never overlap one another."""
    valid = [p for p in polys if not p.is_empty and p.area > 1e-6]
    if not valid:
        return []
    merged = unary_union(valid).intersection(box(0.0, 0.0, w, h))
    if merged.is_empty:
        return []

    w_g, h_g = g_hi(w), g_hi(h)
    step_g = max(1, g_near(strip_mm))
    rects: list[tuple[int, int, int, int]] = []
    for gy0 in range(0, h_g, step_g):
        gy1 = min(h_g, gy0 + step_g)
        y0_mm, y1_mm = gy0 * GRID_MM, gy1 * GRID_MM
        piece = merged.intersection(box(0.0, y0_mm, w, y1_mm))
        if piece.is_empty:
            continue
        x_spans: list[tuple[int, int]] = []
        for g in getattr(piece, "geoms", [piece]):
            if g.area < 1e-5:
                continue
            bx0, _by0, bx1, _by1 = g.bounds
            gx0, gx1 = max(0, g_lo(bx0)), min(w_g, g_hi(bx1))
            if gx1 > gx0:
                x_spans.append((gx0, gx1))
        # Merge touching/overlapping x intervals within this horizontal strip so every rectangle is strictly disjoint.
        x_spans.sort()
        merged_x: list[list[int]] = []
        for x0, x1 in x_spans:
            if merged_x and x0 <= merged_x[-1][1]:
                merged_x[-1][1] = max(merged_x[-1][1], x1)
            else:
                merged_x.append([x0, x1])
        for x0, x1 in merged_x:
            rects.append((x0, gy0, x1, gy1))
    return rects


def mechanical_keepouts(board_name: str, plan: dict) -> dict[str, list[tuple[int, int, int, int]]]:
    """Enclosure posts, snap-clip hooks, sleeve rails and antenna keepouts per side (`front` / `back`)."""
    frame = plan["frame"]
    w, h = frame["size"]["x"], frame["size"]["y"]
    ko = {"front": [], "back": []}

    def add_mm(side: str, x0: float, y0: float, x1: float, y1: float) -> None:
        gx0, gy0 = max(0, g_lo(x0)), max(0, g_lo(y0))
        gx1, gy1 = min(g_hi(w), g_hi(x1)), min(g_hi(h), g_hi(y1))
        if gx1 > gx0 and gy1 > gy0:
            ko[side].append((gx0, gy0, gx1, gy1))

    if board_name == "crown":
        # 4 crown_base support posts at r = 16 mm, 22.5 deg off the axes (2.6 mm radius keepout on B.Cu)
        for px, py in ((44.8, 36.1), (23.9, 44.8), (15.2, 23.9), (36.1, 15.2)):
            add_mm("back", px - 2.6, py - 2.6, px + 2.6, py + 2.6)
        # AS3935 500 kHz ferrite antenna copper/metal keepout on B.Cu under L1
        add_mm("back", 6.3, 3.8, 19.7, 9.2)
    elif board_name == "main":
        # 4 corner standoffs at (2.2, 2.2) .. (w-2.2, h-2.2) with r = 1.65 mm (+margin -> 4.1 mm corner squares on B.Cu)
        for px0, px1 in ((0.0, 4.1), (w - 4.1, w)):
            for py0, py1 in ((0.0, 4.1), (h - 4.1, h)):
                add_mm("back", px0, py0, px1, py1)
        # Gravity-return reset button boss (`btn_boss` Ø 10.4 mm = r 5.2 mm at board (19.0, 6.0) on B.Cu; locked SW1 sits on top)
        add_mm("back", 13.6, 0.6, 24.4, 11.4)
        # 4 edge snap-clip hooks on F.Cu (`CLIPS["main"]`, overlap 1.1 mm, width 1.5 mm)
        add_mm("front", 20.8, 0.0, 23.2, 1.4)
        add_mm("front", 6.8, h - 1.4, 9.2, h)
        add_mm("front", w - 1.4, 2.8, w, 5.2)
        add_mm("front", 0.0, 46.8, 1.4, 49.2)
    elif board_name == "power":
        # 4 corner standoffs at (2.2, 2.2) .. (w-2.2, h-2.2) with r = 1.65 mm (+margin -> 4.1 mm corner squares on B.Cu)
        for px0, px1 in ((0.0, 4.1), (w - 4.1, w)):
            for py0, py1 in ((0.0, 4.1), (h - 4.1, h)):
                add_mm("back", px0, py0, px1, py1)
        # 4 edge snap-clip hooks on F.Cu (`CLIPS["power"]`, overlap 0.7 mm, width 1.5 mm)
        add_mm("front", 25.3, 0.0, 27.7, 1.0)
        add_mm("front", 1.8, h - 1.0, 4.2, h)
        add_mm("front", 0.0, 6.7, 1.0, 9.1)
        add_mm("front", 0.0, 16.8, 1.0, 19.2)
    elif board_name == "probe":
        # Probe sleeve side guide rails (1.05 mm along left and right edges, inside REAR_CABLE_CHANNEL +/-6.3 mm)
        for s in ("front", "back"):
            add_mm(s, 0.0, 0.0, 1.05, h)
            add_mm(s, w - 1.05, 0.0, w, h)
    elif board_name == "dome_legacy":
        # Top flat chord (x = 4.6..9.0, y = 13.6) needs >= 1.0 mm (pad+0.3) clearance for panelize mouse-bite tab
        add_mm("back", 4.6, 12.65, 9.0, 13.6)

    for kind, side, half in (("legs", "back", 2.0), ("pegs", "front", 1.5), ("standoffs", "front", 1.5)):
        for p in plan.get("case", {}).get(kind, []):
            add_mm(side, p["x"] - half, p["y"] - half, p["x"] + half, p["y"] + half)
    return ko


def net_weight(net_name: str, power_nets: set[str], pin_count: int) -> int:
    """Integer wirelength weight: strongly favor 2-3 pin local point-to-point & hot-loop nets over broadcast buses."""
    if not net_name or net_name in GLOBAL_GND or net_name.startswith("unconnected-"):
        return 0
    if net_name in power_nets or net_name.startswith("+"):
        return 3 if pin_count >= 5 else 10
    if pin_count <= 3:
        return 50 if any(pat in net_name for pat in HIGH_PRIORITY_PATTERNS) else 36
    if pin_count <= 5:
        return 20 if any(pat in net_name for pat in HIGH_PRIORITY_PATTERNS) else 12
    return 4


def build_net_index(specs: dict[str, PartSpec]) -> dict[str, list[tuple[str, str]]]:
    nets: dict[str, list[tuple[str, str]]] = {}
    for ref, sp in specs.items():
        g0 = sp.oris[sp.init_rot]
        for pnum, (_dx, _dy, net) in g0.pads.items():
            if net and not net.startswith("unconnected-"):
                nets.setdefault(net, []).append((ref, pnum))
    return nets


def parent_ic_for_passive(ref: str, sp: PartSpec, specs: dict[str, PartSpec],
                          nets: dict[str, list[tuple[str, str]]], power_nets: set[str]) -> str | None:
    """Identify the primary IC/active/connector this passive serves (signal net first, then same-block power net)."""
    g0 = sp.oris[sp.init_rot]
    pad_nets = [net for (_dx, _dy, net) in g0.pads.values() if net and net not in GLOBAL_GND]
    # 1. Non-power signal net touching an active/connector part.
    best_ref, best_deg = None, 999
    for net in pad_nets:
        if net in power_nets or net.startswith("+"):
            continue
        peers = [r for r, _ in nets.get(net, ()) if r != ref and not is_small_passive(specs[r])]
        if peers and len(nets[net]) < best_deg:
            same_blk = [r for r in peers if specs[r].block == sp.block]
            best_ref = same_blk[0] if same_blk else peers[0]
            best_deg = len(nets[net])
    if best_ref:
        return best_ref
    # 2. Decoupling cap on a power rail: match the IC in the same block connected to the same rail!
    for net in pad_nets:
        same_blk_ics = [r for r, _ in nets.get(net, ())
                        if r != ref and specs[r].block == sp.block and not is_small_passive(specs[r])]
        if same_blk_ics:
            same_blk_ics.sort(key=lambda r: (-specs[r].pad_count,
                                             math.hypot(specs[r].init_x - sp.init_x, specs[r].init_y - sp.init_y)))
            return same_blk_ics[0]
    # 3. Fallback: any Stage-1 part in the same block.
    blk_ics = [r for r, s in specs.items() if r != ref and s.block == sp.block and not is_small_passive(s)]
    if blk_ics:
        blk_ics.sort(key=lambda r: -specs[r].pad_count)
        return blk_ics[0]
    return None


def add_fixed_box(model: cp_model.CpModel, intervals_x: list, intervals_y: list,
                  x0: int, y0: int, x1: int, y1: int, tag: str) -> None:
    if x1 > x0 and y1 > y0:
        intervals_x.append(model.NewFixedSizeIntervalVar(x0, x1 - x0, f"fx_{tag}"))
        intervals_y.append(model.NewFixedSizeIntervalVar(y0, y1 - y0, f"fy_{tag}"))


def solve_board_cpsat(board_name: str, *, unlock_power_stage: bool = False,
                      seed: int = 42, stage1_time: float = 30.0, stage2_time: float = 15.0) -> dict[str, tuple[float, float, int]]:
    """Run deterministic 2-stage CP-SAT placement and return `{ref: (x_mm, y_mm, rot_deg)}`."""
    plan = layout.load_plan(board_name)
    frame = plan["frame"]
    w_mm, h_mm = frame["size"]["x"], frame["size"]["y"]
    w_g, h_g = g_hi(w_mm), g_hi(h_mm)
    power_nets = set(frame.get("power_nets", []))
    specs = extract_part_geoms(board_name, plan, unlock_power_stage=unlock_power_stage)
    nets = build_net_index(specs)
    outside_poly = box(0.0, 0.0, w_mm, h_mm).difference(board_safe_polygon(board_name, plan))
    mech_ko = mechanical_keepouts(board_name, plan)

    locked_refs = sorted(r for r, s in specs.items() if s.locked)
    stage1_refs = sorted(r for r, s in specs.items() if not s.locked and not is_small_passive(s))
    stage2_refs = sorted(r for r, s in specs.items() if not s.locked and is_small_passive(s))

    placed: dict[str, tuple[int, int, int]] = {
        r: (g_near(specs[r].init_x), g_near(specs[r].init_y), specs[r].init_rot)
        for r in locked_refs
    }

    compact_board = (w_mm * h_mm) < 350.0
    dense_power = unlock_power_stage and board_name == "power"

    def gbox_to_poly(gx0: int, gy0: int, gx1: int, gy1: int) -> Polygon:
        return box(gx0 * GRID_MM, gy0 * GRID_MM, gx1 * GRID_MM, gy1 * GRID_MM)

    def fixed_side_obstacles(side: str, frozen_map: dict[str, tuple[int, int, int]],
                             active_halo_mm: float, passive_halo_mm: float) -> list[tuple[int, int, int, int]]:
        polys = [outside_poly] + [gbox_to_poly(*b) for b in mech_ko[side]]
        for r, (px, py, prot) in frozen_map.items():
            sp = specs[r]
            og = sp.oris[prot]
            if sp.side == side:
                if sp.raised:
                    polys.append(gbox_to_poly(px + og.dx0, py + og.dy0, px + og.dx1, py + og.dy1))
                else:
                    if is_small_passive(sp):
                        hmm = passive_halo_mm
                    elif sp.pad_count >= 32 and not compact_board:
                        hmm = 2.20   # 64-pin 0.5 mm LQFP needs a multi-row via-escape moat around all 4 sides
                    elif sp.pad_count >= 14 and not compact_board and not dense_power:
                        hmm = 0.85   # 14..20-pin QFN / LGA / SOIC-16 escape ring
                    else:
                        hmm = active_halo_mm if sp.fine_pitch else active_halo_mm * 0.55
                    h = g_hi(hmm)
                    polys.append(gbox_to_poly(px + og.dx0 - h, py + og.dy0 - h, px + og.dx1 + h, py + og.dy1 + h))
                    if ref_prefix(r) == "J" and sp.pad_count >= 6 and og.pads:
                        xs = [px + pdx for pdx, _pdy, _n in og.pads.values()]
                        ys = [py + pdy for _pdx, pdy, _n in og.pads.values()]
                        esc = g_hi(0.40 if (compact_board or dense_power) else 1.20)
                        polys.append(gbox_to_poly(min(xs) - esc, min(ys) - esc, max(xs) + esc, max(ys) + esc))
            else:
                for tx0, ty0, tx1, ty1 in og.tht_boxes + og.via_shadow:
                    polys.append(gbox_to_poly(px + tx0, py + ty0, px + tx1, py + ty1))
        return decompose_union_to_grid_rects(polys, w_mm, h_mm, strip_mm=0.10 if compact_board else 0.5)

    def y_domain_for(sp: PartSpec) -> tuple[int, int]:
        if board_name == "probe":
            if sp.block == "thermo_hygro":
                return g_hi(11.4), g_lo(18.2)
            if sp.block in ("air_quality", "baro", "co2_back_heater"):
                return g_hi(20.6), g_lo(43.6)
        return 0, h_g

    dom_rects = [r for areas in frame.get("domain_regions", {}).values()
                 for r in (areas if isinstance(areas, list) else [areas])]
    pad_xlo = g_hi(min((r["x0"] for r in dom_rects), default=0.0) + 0.05)
    pad_xhi = g_lo(max((r["x1"] for r in dom_rects), default=w_mm) - 0.05)
    pad_ylo = g_hi(min((r["y0"] for r in dom_rects), default=0.0) + 0.05)
    pad_yhi = g_lo(max((r["y1"] for r in dom_rects), default=h_mm) - 0.05)

    s1_fixed_halo = 0.10 if (compact_board or dense_power) else 0.50
    s1_mov_fine_halo = 0.10 if compact_board else (0.20 if dense_power else 0.50)
    s1_mov_std_halo = 0.08 if compact_board else (0.12 if dense_power else 0.28)
    s2_active_halo = 0.05 if (w_mm * h_mm) < 200.0 else (0.12 if compact_board else (0.22 if dense_power else 0.40))
    # 0.26 mm courtyard halo -> >= 1.02 mm pad-to-pad gap between adjacent 0805 passives, fitting a 0.60 mm via + 2x0.20 mm clearance!
    s2_passive_halo = 0.05 if compact_board else (0.12 if board_name == "probe" else 0.26)

    # =========================================================================
    # STAGE 1: Solve all unlocked ICs, magnetics, inductors, transistors, diodes
    # =========================================================================
    if stage1_refs:
        m1 = cp_model.CpModel()
        ix = {"front": [], "back": []}
        iy = {"front": [], "back": []}
        mov_iv = {"front": [], "back": []}
        mov_shadow = {"front": [], "back": []}

        for s in ("front", "back"):
            for k_idx, (x0, y0, x1, y1) in enumerate(fixed_side_obstacles(s, placed, s1_fixed_halo, 0.10)):
                add_fixed_box(m1, ix[s], iy[s], x0, y0, x1, y1, f"obs1_{s}_{k_idx}")

        x_vars, y_vars, ori_bools = {}, {}, {}
        pad_coords: dict[tuple[str, str], tuple] = {}

        for r in locked_refs:
            sp = specs[r]
            px, py, prot = placed[r]
            for pnum, (pdx, pdy, _net) in sp.oris[prot].pads.items():
                pad_coords[(r, pnum)] = (px + pdx, py + pdy)

        for r in stage1_refs:
            sp = specs[r]
            ylo, yhi = y_domain_for(sp)
            halo = g_hi(s1_mov_fine_halo if sp.fine_pitch else s1_mov_std_halo)
            xv = m1.NewIntVar(0, w_g, f"x_{r}")
            yv = m1.NewIntVar(ylo, yhi, f"y_{r}")
            x_vars[r], y_vars[r] = xv, yv

            ob = {rot: m1.NewBoolVar(f"ori_{r}_{rot}") for rot in ANGLES}
            m1.AddExactlyOne(ob.values())
            ori_bools[r] = ob

            x0_v = m1.NewIntVar(0, w_g, f"x0_{r}")
            y0_v = m1.NewIntVar(ylo, yhi, f"y0_{r}")
            w_v = m1.NewIntVar(1, w_g, f"w_{r}")
            h_v = m1.NewIntVar(1, h_g, f"h_{r}")
            x1_v = m1.NewIntVar(0, w_g, f"x1_{r}")
            y1_v = m1.NewIntVar(ylo, yhi, f"y1_{r}")

            for rot, bv in ob.items():
                og = sp.oris[rot]
                bw = (og.dx1 - og.dx0) + 2 * halo
                bh = (og.dy1 - og.dy0) + 2 * halo
                m1.Add(x0_v == xv + og.dx0 - halo).OnlyEnforceIf(bv)
                m1.Add(y0_v == yv + og.dy0 - halo).OnlyEnforceIf(bv)
                m1.Add(w_v == bw).OnlyEnforceIf(bv)
                m1.Add(h_v == bh).OnlyEnforceIf(bv)
                m1.Add(x1_v == x0_v + bw).OnlyEnforceIf(bv)
                m1.Add(y1_v == y0_v + bh).OnlyEnforceIf(bv)

            ivx = m1.NewIntervalVar(x0_v, w_v, x1_v, f"ivx_{r}")
            ivy = m1.NewIntervalVar(y0_v, h_v, y1_v, f"ivy_{r}")
            ix[sp.side].append(ivx)
            iy[sp.side].append(ivy)
            mov_iv[sp.side].append((ivx, ivy))

            # Project movable Stage-1 IC THT/exposed-pad via shadow against movable opposite-side Stage-1 parts.
            if all(sp.oris[rot].via_shadow or sp.oris[rot].tht_boxes for rot in ANGLES):
                sx0 = m1.NewIntVar(0, w_g, f"sx0_{r}")
                sy0 = m1.NewIntVar(0, h_g, f"sy0_{r}")
                swv = m1.NewIntVar(1, w_g, f"sw_{r}")
                shv = m1.NewIntVar(1, h_g, f"sh_{r}")
                sx1 = m1.NewIntVar(0, w_g, f"sx1_{r}")
                sy1 = m1.NewIntVar(0, h_g, f"sy1_{r}")
                for rot, bv in ob.items():
                    boxes = sp.oris[rot].tht_boxes + sp.oris[rot].via_shadow
                    bx0, by0 = min(b[0] for b in boxes), min(b[1] for b in boxes)
                    bx1, by1 = max(b[2] for b in boxes), max(b[3] for b in boxes)
                    m1.Add(sx0 == xv + bx0).OnlyEnforceIf(bv)
                    m1.Add(sy0 == yv + by0).OnlyEnforceIf(bv)
                    m1.Add(swv == bx1 - bx0).OnlyEnforceIf(bv)
                    m1.Add(shv == by1 - by0).OnlyEnforceIf(bv)
                    m1.Add(sx1 == sx0 + (bx1 - bx0)).OnlyEnforceIf(bv)
                    m1.Add(sy1 == sy0 + (by1 - by0)).OnlyEnforceIf(bv)
                mov_shadow[sp.side].append((
                    m1.NewIntervalVar(sx0, swv, sx1, f"cfx_{r}"),
                    m1.NewIntervalVar(sy0, shv, sy1, f"cfy_{r}"),
                ))

            for pnum in sp.oris[ANGLES[0]].pads:
                px_v = m1.NewIntVar(pad_xlo, pad_xhi, f"px_{r}_{pnum}")
                py_v = m1.NewIntVar(pad_ylo, pad_yhi, f"py_{r}_{pnum}")
                for rot, bv in ob.items():
                    pdx, pdy, _ = sp.oris[rot].pads[pnum]
                    m1.Add(px_v == xv + pdx).OnlyEnforceIf(bv)
                    m1.Add(py_v == yv + pdy).OnlyEnforceIf(bv)
                pad_coords[(r, pnum)] = (px_v, py_v)

            m1.AddHint(xv, g_near(sp.init_x))
            m1.AddHint(yv, g_near(sp.init_y))
            init_r = sp.init_rot if sp.init_rot in ANGLES else 0
            for rot, bv in ob.items():
                m1.AddHint(bv, 1 if rot == init_r else 0)

        for s in ("front", "back"):
            if mov_iv[s]:
                m1.AddNoOverlap2D(ix[s], iy[s])
        if mov_shadow["front"] and mov_iv["back"]:
            pairs = mov_shadow["front"] + mov_iv["back"]
            m1.AddNoOverlap2D([p[0] for p in pairs], [p[1] for p in pairs])
        if mov_shadow["back"] and mov_iv["front"]:
            pairs = mov_shadow["back"] + mov_iv["front"]
            m1.AddNoOverlap2D([p[0] for p in pairs], [p[1] for p in pairs])

        # Build Stage 1 objective: direct Stage1<->(Locked|Stage1) nets + 2-hop virtual edges through 2-pin passives!
        obj_terms = []
        edge_id = 0

        def add_l1_term(p_a, p_b, weight: int) -> None:
            nonlocal edge_id
            if weight <= 0:
                return
            ax, ay = p_a
            bx, by = p_b
            if isinstance(ax, int) and isinstance(bx, int):
                return
            edge_id += 1
            dx = m1.NewIntVar(-2 * w_g, 2 * w_g, f"dx_{edge_id}")
            dy = m1.NewIntVar(-2 * h_g, 2 * h_g, f"dy_{edge_id}")
            adx = m1.NewIntVar(0, 2 * w_g, f"adx_{edge_id}")
            ady = m1.NewIntVar(0, 2 * h_g, f"ady_{edge_id}")
            m1.Add(dx == ax - bx)
            m1.Add(dy == ay - by)
            m1.AddAbsEquality(adx, dx)
            m1.AddAbsEquality(ady, dy)
            obj_terms.append(weight * (adx + ady))

        active_set = set(locked_refs) | set(stage1_refs)
        for net_name, pin_list in sorted(nets.items()):
            w = net_weight(net_name, power_nets, len(pin_list))
            if w <= 0:
                continue
            # Deduplicate tied pins on the same component (e.g. BAV99 pins 1+2 tied) so weights stay balanced.
            seen_refs = set()
            act_pins = []
            for r, p in pin_list:
                if r in active_set and (r, p) in pad_coords and r not in seen_refs:
                    seen_refs.add(r)
                    act_pins.append((r, p))
            if len(act_pins) >= 2:
                act_pins.sort(key=lambda rp: (0 if specs[rp[0]].locked else 1, -specs[rp[0]].pad_count, rp[0], rp[1]))
                hub = act_pins[0]
                for other in act_pins[1:]:
                    add_l1_term(pad_coords[hub], pad_coords[other], w)

        # 2-hop passive bridges (e.g., IC -> series R/C -> IC or connector, so Stage 1 pulls coupled actives together).
        for pref in stage2_refs:
            psp = specs[pref]
            p_nets = [net for (_dx, _dy, net) in psp.oris[psp.init_rot].pads.values()
                      if net and net not in GLOBAL_GND and net not in power_nets and not net.startswith("+")]
            if len(p_nets) == 2 and p_nets[0] != p_nets[1]:
                end0 = [(r, p) for r, p in nets.get(p_nets[0], ()) if r in active_set and (r, p) in pad_coords]
                end1 = [(r, p) for r, p in nets.get(p_nets[1], ()) if r in active_set and (r, p) in pad_coords]
                for a in end0[:2]:
                    for b in end1[:2]:
                        if a[0] != b[0]:
                            add_l1_term(pad_coords[a], pad_coords[b], 10)

        if obj_terms:
            m1.Minimize(sum(obj_terms))

        solver1 = cp_model.CpSolver()
        solver1.parameters.random_seed = seed
        solver1.parameters.num_search_workers = 1
        solver1.parameters.max_time_in_seconds = stage1_time
        status1 = solver1.Solve(m1)
        if status1 not in (cp_model.OPTIMAL, cp_model.FEASIBLE):
            raise RuntimeError(f"{board_name} Stage 1 CP-SAT infeasible ({solver1.StatusName(status1)})")
        for r in stage1_refs:
            chosen_rot = next(rot for rot, bv in ori_bools[r].items() if solver1.BooleanValue(bv))
            placed[r] = (solver1.Value(x_vars[r]), solver1.Value(y_vars[r]), chosen_rot)

    # =========================================================================
    # STAGE 2: Freeze Stage 1 with escape halos, solve passives in batches
    # =========================================================================
    if stage2_refs:
        # Pre-assign dedicated supply pins round-robin for identical decoupling caps in the same (side, block, rail)
        # so multi-VDD ICs (LQFP-64) and replicated channels (4x UCC27524) distribute one cap per pin/IC.
        decap_target_pin: dict[str, tuple[str, str]] = {}
        decap_buckets: dict[tuple[str, str, str], list[str]] = {}
        for r in stage2_refs:
            sp = specs[r]
            pnets = [net for (_dx, _dy, net) in sp.oris[0].pads.values() if net]
            if ref_prefix(r) == "C" and "GND" in pnets:
                other = [n for n in pnets if n != "GND"]
                if other and (other[0] in power_nets or other[0].startswith("+")):
                    decap_buckets.setdefault((sp.side, sp.block, other[0]), []).append(r)
        for (side_k, blk_k, rail_k), crefs in decap_buckets.items():
            cand_pins = sorted(
                [(pr, pp) for pr, pp in nets.get(rail_k, ())
                 if pr in placed and not is_small_passive(specs[pr])
                 and specs[pr].block == blk_k and specs[pr].side == side_k],
                key=lambda rp: (rp[0], int(rp[1]) if rp[1].isdigit() else 999, rp[1]),
            )
            if not cand_pins:
                cand_pins = sorted(
                    [(pr, pp) for pr, pp in nets.get(rail_k, ())
                     if pr in placed and not is_small_passive(specs[pr])
                     and specs[pr].block == blk_k],
                    key=lambda rp: (rp[0], rp[1]),
                )
            if cand_pins:
                for idx, cref in enumerate(sorted(crefs)):
                    decap_target_pin[cref] = cand_pins[idx % len(cand_pins)]

        parent_map = {
            r: (decap_target_pin[r][0] if r in decap_target_pin
                else parent_ic_for_passive(r, specs[r], specs, nets, power_nets))
            for r in stage2_refs
        }
        groups: dict[tuple[str, str], list[str]] = {}
        for r in stage2_refs:
            groups.setdefault((specs[r].side, specs[r].block), []).append(r)

        batches: list[list[str]] = []
        for (_side, _blk), members in sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0])):
            members.sort(key=lambda r: (parent_map[r] or "", r))
            for idx in range(0, len(members), 16):
                batches.append(members[idx:idx + 16])

        for batch in batches:
            m2 = cp_model.CpModel()
            side = specs[batch[0]].side
            ix_s, iy_s = [], []

            for k_idx, (x0, y0, x1, y1) in enumerate(fixed_side_obstacles(side, placed, s2_active_halo, s2_passive_halo)):
                add_fixed_box(m2, ix_s, iy_s, x0, y0, x1, y1, f"obs2_{k_idx}")

            # Frozen pad coordinate lookup.
            frozen_pads: dict[tuple[str, str], tuple[int, int]] = {}
            for r, (px, py, prot) in placed.items():
                for pnum, (pdx, pdy, _net) in specs[r].oris[prot].pads.items():
                    frozen_pads[(r, pnum)] = (px + pdx, py + pdy)

            bx_vars, by_vars, vert_vars, flip_vars = {}, {}, {}, {}
            batch_pads: dict[tuple[str, str], tuple] = {}
            p_halo = g_hi(s2_passive_halo)

            for r in batch:
                sp = specs[r]
                ylo, yhi = y_domain_for(sp)
                xv = m2.NewIntVar(0, w_g, f"x_{r}")
                yv = m2.NewIntVar(ylo, yhi, f"y_{r}")
                is_vert = m2.NewBoolVar(f"vert_{r}")
                is_flip = m2.NewBoolVar(f"flip_{r}")
                bx_vars[r], by_vars[r], vert_vars[r], flip_vars[r] = xv, yv, is_vert, is_flip

                # Width/height swap via `is_vert` (`0/180` = horizontal, `90/270` = vertical).
                og_h, og_v = sp.oris[0], sp.oris[90]
                wh_w = (og_h.dx1 - og_h.dx0) + 2 * p_halo
                wh_h = (og_h.dy1 - og_h.dy0) + 2 * p_halo
                wv_w = (og_v.dx1 - og_v.dx0) + 2 * p_halo
                wv_h = (og_v.dy1 - og_v.dy0) + 2 * p_halo

                x0_v = m2.NewIntVar(0, w_g, f"x0_{r}")
                y0_v = m2.NewIntVar(ylo, yhi, f"y0_{r}")
                w_v = m2.NewIntVar(1, w_g, f"w_{r}")
                h_v = m2.NewIntVar(1, h_g, f"h_{r}")
                x1_v = m2.NewIntVar(0, w_g, f"x1_{r}")
                y1_v = m2.NewIntVar(ylo, yhi, f"y1_{r}")

                m2.Add(x0_v == xv + og_h.dx0 - p_halo).OnlyEnforceIf(is_vert.Not())
                m2.Add(y0_v == yv + og_h.dy0 - p_halo).OnlyEnforceIf(is_vert.Not())
                m2.Add(w_v == wh_w).OnlyEnforceIf(is_vert.Not())
                m2.Add(h_v == wh_h).OnlyEnforceIf(is_vert.Not())
                m2.Add(x1_v == x0_v + wh_w).OnlyEnforceIf(is_vert.Not())
                m2.Add(y1_v == y0_v + wh_h).OnlyEnforceIf(is_vert.Not())

                m2.Add(x0_v == xv + og_v.dx0 - p_halo).OnlyEnforceIf(is_vert)
                m2.Add(y0_v == yv + og_v.dy0 - p_halo).OnlyEnforceIf(is_vert)
                m2.Add(w_v == wv_w).OnlyEnforceIf(is_vert)
                m2.Add(h_v == wv_h).OnlyEnforceIf(is_vert)
                m2.Add(x1_v == x0_v + wv_w).OnlyEnforceIf(is_vert)
                m2.Add(y1_v == y0_v + wv_h).OnlyEnforceIf(is_vert)

                ix_s.append(m2.NewIntervalVar(x0_v, w_v, x1_v, f"ivx_{r}"))
                iy_s.append(m2.NewIntervalVar(y0_v, h_v, y1_v, f"ivy_{r}"))

                # 4-state pad coordinates from `(is_vert, is_flip)` -> `0, 90, 180, 270`.
                b_0 = m2.NewBoolVar(f"b0_{r}")
                b_90 = m2.NewBoolVar(f"b90_{r}")
                b_180 = m2.NewBoolVar(f"b180_{r}")
                b_270 = m2.NewBoolVar(f"b270_{r}")
                m2.AddExactlyOne([b_0, b_90, b_180, b_270])
                m2.Add(is_vert == 0).OnlyEnforceIf([b_0, b_180])
                m2.Add(is_vert == 1).OnlyEnforceIf([b_90, b_270])
                m2.Add(is_flip == 0).OnlyEnforceIf([b_0, b_90])
                m2.Add(is_flip == 1).OnlyEnforceIf([b_180, b_270])

                for pnum in sp.oris[0].pads:
                    px_v = m2.NewIntVar(pad_xlo, pad_xhi, f"px_{r}_{pnum}")
                    py_v = m2.NewIntVar(pad_ylo, pad_yhi, f"py_{r}_{pnum}")
                    for rot, bv in ((0, b_0), (90, b_90), (180, b_180), (270, b_270)):
                        pdx, pdy, _ = sp.oris[rot].pads[pnum]
                        m2.Add(px_v == xv + pdx).OnlyEnforceIf(bv)
                        m2.Add(py_v == yv + pdy).OnlyEnforceIf(bv)
                    batch_pads[(r, pnum)] = (px_v, py_v)

                # Anchor initial hint near parent IC if available, else plan position.
                parent = parent_map[r]
                if parent and parent in placed:
                    m2.AddHint(xv, min(w_g, max(0, placed[parent][0])))
                    m2.AddHint(yv, min(yhi, max(ylo, placed[parent][1])))

            m2.AddNoOverlap2D(ix_s, iy_s)

            obj2 = []
            e2 = 0

            def add_p_dist(pa, pb, weight: int, _m2=m2, _obj2=obj2) -> None:
                nonlocal e2
                if weight <= 0:
                    return
                e2 += 1
                dx = _m2.NewIntVar(-2 * w_g, 2 * w_g, f"dx2_{e2}")
                dy = _m2.NewIntVar(-2 * h_g, 2 * h_g, f"dy2_{e2}")
                adx = _m2.NewIntVar(0, 2 * w_g, f"adx2_{e2}")
                ady = _m2.NewIntVar(0, 2 * h_g, f"ady2_{e2}")
                _m2.Add(dx == pa[0] - pb[0])
                _m2.Add(dy == pa[1] - pb[1])
                _m2.AddAbsEquality(adx, dx)
                _m2.AddAbsEquality(ady, dy)
                _obj2.append(weight * (adx + ady))

            batch_set = set(batch)
            for r in batch:
                sp = specs[r]
                parent = parent_map[r]
                for pnum, (_dx, _dy, net) in sp.oris[0].pads.items():
                    if not net or net.startswith("unconnected-"):
                        continue
                    my_pad = batch_pads[(r, pnum)]
                    peers = nets.get(net, [])
                    if net in GLOBAL_GND:
                        # Down-weighted GND tie to parent IC's GND pad (keeps return loop tight without pulling to J1).
                        if parent and parent in placed:
                            parent_gnd = [frozen_pads[(parent, pp)] for pr, pp in peers
                                          if pr == parent and (parent, pp) in frozen_pads]
                            if parent_gnd:
                                add_p_dist(my_pad, parent_gnd[0], 3)
                        continue
                    if net in power_nets or net.startswith("+"):
                        # Decoupling / pull-up rail: anchor strongly to assigned supply pin (or parent IC's supply pin)!
                        anchored = False
                        if r in decap_target_pin and decap_target_pin[r] in frozen_pads:
                            add_p_dist(my_pad, frozen_pads[decap_target_pin[r]], 40)
                            anchored = True
                        elif parent and parent in placed:
                            parent_pwr = [frozen_pads[(parent, pp)] for pr, pp in peers
                                          if pr == parent and (parent, pp) in frozen_pads]
                            for tp in parent_pwr[:2]:
                                add_p_dist(my_pad, tp, 32)
                                anchored = True
                        if not anchored:
                            same_blk = [frozen_pads[(pr, pp)] for pr, pp in peers
                                        if pr in placed and specs[pr].block == sp.block and (pr, pp) in frozen_pads]
                            for tp in same_blk[:2]:
                                add_p_dist(my_pad, tp, 18)
                                anchored = True
                        if not anchored:
                            any_frz = [frozen_pads[(pr, pp)] for pr, pp in peers if (pr, pp) in frozen_pads]
                            if any_frz:
                                add_p_dist(my_pad, any_frz[0], 8)
                        continue

                    # Signal / local analog / switching node: connect directly to frozen active pins & batch peers!
                    w_sig = net_weight(net, power_nets, len(peers))
                    frz_targets = [(pr, pp) for pr, pp in peers if pr != r and (pr, pp) in frozen_pads]
                    # Prefer same-block / parent IC pins if many pins on net.
                    frz_targets.sort(key=lambda rp: (0 if rp[0] == parent else (1 if specs[rp[0]].block == sp.block else 2),
                                                     -specs[rp[0]].pad_count))
                    for pr, pp in frz_targets[:3]:
                        add_p_dist(my_pad, frozen_pads[(pr, pp)], w_sig)
                    for pr, pp in peers:
                        if pr in batch_set and r < pr:
                            add_p_dist(my_pad, batch_pads[(pr, pp)], max(10, w_sig // 2))

                # Gentle tie-breaker toward parent IC centre if passive has only global nets with no parent pin on net.
                if parent and parent in placed:
                    px, py, _ = placed[parent]
                    add_p_dist((bx_vars[r], by_vars[r]), (px, py), 1)

            if obj2:
                m2.Minimize(sum(obj2))

            solver2 = cp_model.CpSolver()
            solver2.parameters.random_seed = seed
            solver2.parameters.num_search_workers = 1
            solver2.parameters.max_time_in_seconds = stage2_time
            status2 = solver2.Solve(m2)
            if status2 not in (cp_model.OPTIMAL, cp_model.FEASIBLE):
                raise RuntimeError(f"{board_name} Stage 2 batch {batch} CP-SAT infeasible ({solver2.StatusName(status2)})")
            for r in batch:
                v_flag = solver2.BooleanValue(vert_vars[r])
                f_flag = solver2.BooleanValue(flip_vars[r])
                rot = (270 if f_flag else 90) if v_flag else (180 if f_flag else 0)
                placed[r] = (solver2.Value(bx_vars[r]), solver2.Value(by_vars[r]), rot)

    return {r: (round(gx * GRID_MM, 2), round(gy * GRID_MM, 2), rot) for r, (gx, gy, rot) in sorted(placed.items())}


def rewrite_plan_positions(plan_text: str, positions: dict[str, tuple[float, float, int]]) -> str:
    """Update `x`, `y`, `rot` in `layout_<board>.yaml` preserving comments and formatting."""
    for ref, (x, y, rot) in positions.items():
        pat = re.compile(
            rf"^(  {re.escape(ref)}:\s*\{{\s*x:\s*)[-\d.]+(\s*,\s*y:\s*)[-\d.]+(\s*,\s*rot:\s*)[-\d.]+",
            re.MULTILINE,
        )
        plan_text, _ = pat.subn(rf"\g<1>{x}\g<2>{y}\g<3>{rot}", plan_text, count=1)
    return plan_text


def board_metrics(pcb_path: Path, plan: dict) -> dict:
    """Compute objective routing & placement quality metrics on a routed `.kicad_pcb`."""
    board = pcbnew.LoadBoard(str(pcb_path))
    layout.fill_zones(board)
    unconn = layout.unconnected(board)

    tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
    vias = [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T]
    track_len_mm = sum(pcbnew.ToMM(t.GetLength()) for t in tracks)

    # Decoupling cap proximity: mean distance from each 2-pin C on a supply rail + GND to nearest IC pad on same rail.
    ic_rail_pads: dict[str, list[tuple[float, float]]] = {}
    decap_dists: list[float] = []
    for fp in board.Footprints():
        ref = fp.GetReference()
        pads = list(fp.Pads())
        if ref_prefix(ref) not in PASSIVE_PREFIXES:
            for p in pads:
                net = p.GetNetname()
                if net and net not in GLOBAL_GND:
                    ic_rail_pads.setdefault(net, []).append(
                        (pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y))
                    )
    for fp in board.Footprints():
        ref = fp.GetReference()
        pads = list(fp.Pads())
        if ref.startswith("C") and len(pads) == 2:
            pnets = [p.GetNetname() for p in pads]
            if "GND" in pnets:
                other_idx = 1 if pnets[0] == "GND" else 0
                rail = pnets[other_idx]
                if rail and (rail.startswith("+") or rail in set(plan["frame"].get("power_nets", []))):
                    cx = pcbnew.ToMM(pads[other_idx].GetPosition().x)
                    cy = -pcbnew.ToMM(pads[other_idx].GetPosition().y)
                    targets = ic_rail_pads.get(rail, [])
                    if targets:
                        decap_dists.append(min(math.hypot(cx - tx, cy - ty) for tx, ty in targets))

    drc_rep = pcb_path.parent / f"{pcb_path.stem}_metrics_drc.json"
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--output", str(drc_rep), str(pcb_path)],
                   check=True, capture_output=True)
    drc = json.loads(drc_rep.read_text())
    drc_rep.unlink(missing_ok=True)
    errors = [v for v in drc["violations"]
              if v["severity"] == "error" and v["type"] not in layout.LIBRARY_ONLY_DRC]
    warnings = [v for v in drc["violations"]
                if v["severity"] != "error" and v["type"] not in layout.LIBRARY_ONLY_DRC]

    return {
        "unconnected": unconn,
        "drc_errors": len(errors),
        "drc_warnings": len(warnings),
        "track_segments": len(tracks),
        "track_length_mm": round(track_len_mm, 1),
        "vias": len(vias),
        "decap_mean_mm": round(sum(decap_dists) / len(decap_dists), 2) if decap_dists else 0.0,
        "decap_max_mm": round(max(decap_dists), 2) if decap_dists else 0.0,
    }


def evaluate_board(board_name: str, eval_root: Path, *, unlock_power_stage: bool = False,
                   route_time: int = 150) -> dict:
    """Place & route a board with CP-SAT in `eval_root/<board>/` without touching stored layouts, and compare."""
    out_dir = eval_root / (f"{board_name}_full" if unlock_power_stage else board_name)
    out_dir.mkdir(parents=True, exist_ok=True)

    curated_pcb = layout.stored_board_path(board_name)
    plan_path = REPO / "hardware/circuits/variants" / VARIANT / f"layout_{board_name}.yaml"
    orig_plan = yaml.safe_load(plan_path.read_text())
    curated_metrics = board_metrics(curated_pcb, orig_plan)

    t0 = time.time()
    positions = solve_board_cpsat(board_name, unlock_power_stage=unlock_power_stage)
    t_place = time.time() - t0

    cpsat_plan = copy.deepcopy(orig_plan)
    if board_name == "power" and unlock_power_stage:
        # Remove hand-drawn F.Cu hot-loop pours that were manually boxed around the curated coordinates.
        cpsat_plan["frame"]["pours"] = [p for p in cpsat_plan["frame"].get("pours", []) if p.get("priority", 0) == 0]
    if "ref_positions" in cpsat_plan["frame"]:
        # Let automatic inside reference placement place silkscreen next to moved parts.
        cpsat_plan["frame"] = {k: v for k, v in cpsat_plan["frame"].items() if k != "ref_positions"}
    for ref, (x, y, rot) in positions.items():
        if ref in cpsat_plan["parts"]:
            cpsat_plan["parts"][ref]["x"] = x
            cpsat_plan["parts"][ref]["y"] = y
            cpsat_plan["parts"][ref]["rot"] = rot

    (out_dir / f"layout_{board_name}.cpsat.yaml").write_text(
        rewrite_plan_positions(plan_path.read_text(), positions)
    )

    # Build fresh board with CP-SAT placement, export DSN, route with FastRoute (FANOUT=false), import & prune loops.
    board = layout.fresh_board(board_name)
    layout.place_parts(board, cpsat_plan, [])
    layout.apply_frame(board, cpsat_plan)
    layout.add_planned_tracks(board, cpsat_plan)

    pcb_path = out_dir / f"{board_name}.kicad_pcb"
    layout.save(board, cpsat_plan, board_name) if False else None  # do not touch stored_board_path!
    # Prepare routable copy (without pour zones, keeping rule areas & locked tracks, inheriting board .kicad_pro setup)
    routable = out_dir / f"{board_name}.routable.kicad_pcb"
    if curated_pcb.with_suffix(".kicad_pro").exists():
        shutil.copy(curated_pcb.with_suffix(".kicad_pro"), routable.with_suffix(".kicad_pro"))
    for z in layout.zones(board):
        if not z.GetIsRuleArea():
            board.RemoveNative(z)
    board.Save(str(routable))
    layout.write_rules(board, cpsat_plan, board_name, routable)

    dsn = out_dir / f"{board_name}.dsn"
    if not pcbnew.ExportSpecctraDSN(board, str(dsn)):
        raise RuntimeError(f"{board_name}: DSN export failed")
    nets = sorted(str(n) for n in board.GetNetsByName() if str(n))
    project = json.loads(routable.with_suffix(".kicad_pro").read_text())
    dsn_text = layout.with_net_classes(dsn.read_text(), nets, project["net_settings"])
    dsn_text = layout.with_inset_boundary(dsn_text, cpsat_plan["frame"])
    dsn.write_text(layout.with_via_size(dsn_text, cpsat_plan["frame"]))

    ses = out_dir / f"{board_name}.ses"
    rep = out_dir / "fastroute_report.json"
    t_r0 = time.time()
    for attempt in (1, 2):
        ses.unlink(missing_ok=True)
        det = ["--no-time-limits", "--router.autorouter.max_threads=1"] if attempt == 1 else []
        proc = subprocess.run(
            ["fastroute", "-de", str(dsn), "-do", str(ses), f"--max-time={route_time}",
             "--router.fanout.enabled=false", f"--router.min_trace_width_um={layout.MIN_TRACK_UM}",
             *det, f"--report={rep}", "--diagnose"],
            capture_output=True, text=True, check=False,
        )
        if proc.returncode == 0 and ses.exists():
            stats = json.loads(rep.read_text())["stats"]
            if stats["unrouted"] == 0:
                break
    t_route = time.time() - t_r0

    # Re-apply full frame (with copper pours), import SES, prune loops/stubs, tidy silk references, save & check DRC.
    final_board = layout.fresh_board(board_name)
    layout.place_parts(final_board, cpsat_plan, [])
    layout.apply_frame(final_board, cpsat_plan)
    layout.add_planned_tracks(final_board, cpsat_plan)
    if ses.exists():
        pcbnew.ImportSpecctraSES(final_board, str(ses))
    layout.fill_zones(final_board)
    layout.prune_track_loops(final_board)
    for fp in final_board.Footprints():
        rtxt = fp.Reference()
        rtxt.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(layout.SILK_TEXT_MM), pcbnew.FromMM(layout.SILK_TEXT_MM)))
        rtxt.SetTextThickness(pcbnew.FromMM(layout.SILK_STROKE_MM))
    if cpsat_plan["frame"].get("silk_inside"):
        layout.place_references_inside(final_board, cpsat_plan["frame"])
    layout.add_3d_models(final_board)
    layout.fill_zones(final_board)
    shutil.copy(routable.with_suffix(".kicad_pro"), pcb_path.with_suffix(".kicad_pro"))
    final_board.Save(str(pcb_path))
    layout.write_rules(final_board, cpsat_plan, board_name, pcb_path)

    # Hide any clashing silk references on the evaluated board (same as `layout.tidy_silk`) & widen narrow power tracks.
    silk_rep = out_dir / "silk_drc.json"
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--severity-all",
                    "--output", str(silk_rep), str(pcb_path)], capture_output=True, check=False)
    clashing = {item["description"].split("Reference field of ")[1].split()[0]
                for v in json.loads(silk_rep.read_text())["violations"]
                if v["type"] in ("silk_overlap", "silk_over_copper", "silk_edge_clearance")
                for item in v["items"] if item["description"].startswith("Reference field of ")}
    if clashing:
        for fp in final_board.Footprints():
            if fp.GetReference() in clashing:
                fp.Reference().SetVisible(False)
        final_board.Save(str(pcb_path))
    silk_rep.unlink(missing_ok=True)

    # Widen any necked power tracks where DRC allows (matching layout.widen_narrow_tracks).
    tw_rep = out_dir / "tw_drc.json"
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--output", str(tw_rep), str(pcb_path)],
                   capture_output=True, check=True)
    before = [v for v in json.loads(tw_rep.read_text())["violations"] if v["severity"] == "error"]
    narrow = {i["uuid"]: pcbnew.FromMM(float(re.search(r"min width ([\d.]+) mm", v["description"]).group(1)))
              for v in before if v["type"] == "track_width" for i in v["items"]}
    if narrow:
        b2 = pcbnew.LoadBoard(str(pcb_path))
        tmap = {t.m_Uuid.AsString(): t for t in b2.GetTracks() if t.m_Uuid.AsString() in narrow}
        old_w = {u: t.GetWidth() for u, t in tmap.items()}
        for u, t in tmap.items():
            t.SetWidth(narrow[u])
        b2.Save(str(pcb_path))
        subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--output", str(tw_rep), str(pcb_path)],
                       capture_output=True, check=True)
        after = [v for v in json.loads(tw_rep.read_text())["violations"] if v["severity"] == "error"]
        known = {layout.drc_key(v) for v in before}
        clash_u = {i["uuid"] for v in after if layout.drc_key(v) not in known for i in v["items"]} & tmap.keys()
        if clash_u:
            b3 = pcbnew.LoadBoard(str(pcb_path))
            for t in b3.GetTracks():
                if t.m_Uuid.AsString() in clash_u:
                    t.SetWidth(old_w[t.m_Uuid.AsString()])
            b3.Save(str(pcb_path))
    tw_rep.unlink(missing_ok=True)

    cpsat_metrics = board_metrics(pcb_path, cpsat_plan)
    moved_parts = sum(
        1 for r, (x, y, rot) in positions.items()
        if abs(x - orig_plan["parts"][r]["x"]) > 0.05
        or abs(y - orig_plan["parts"][r]["y"]) > 0.05
        or rot != (round(orig_plan["parts"][r].get("rot", 0)) % 360)
    )
    return {
        "board": f"{board_name} (full)" if unlock_power_stage else board_name,
        "unlocked_solved": moved_parts,
        "place_s": round(t_place, 2),
        "route_s": round(t_route, 2),
        "curated": curated_metrics,
        "cpsat": cpsat_metrics,
    }


def autoplace_cli(board_name: str, args: list[str]) -> None:
    parser = argparse.ArgumentParser(prog=f"layout.py {board_name} autoplace")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--stage1-time", type=float, default=30.0)
    parser.add_argument("--stage2-time", type=float, default=15.0)
    opts = parser.parse_args(args)
    positions = solve_board_cpsat(board_name, seed=opts.seed,
                                  stage1_time=opts.stage1_time, stage2_time=opts.stage2_time)
    plan_path = REPO / "hardware/circuits/variants" / VARIANT / f"layout_{board_name}.yaml"
    plan_path.write_text(rewrite_plan_positions(plan_path.read_text(), positions))
    print(f"{board_name}: wrote 2-stage CP-SAT placement for {len(positions)} parts to {plan_path.relative_to(REPO)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("boards", nargs="*", help="board names (default: all active boards)")
    parser.add_argument("--write", action="store_true", help="write solved coordinates into layout_<board>.yaml")
    parser.add_argument("--eval-dir", default="hardware/build/cpsat_eval",
                        help="directory for non-destructive CP-SAT placement + routing comparison")
    parser.add_argument("--unlock-power-stage", action="store_true",
                        help="on `power`, also unlock F.Cu buck power-stage parts for from-scratch comparison")
    parser.add_argument("--route-time", type=int, default=150)
    args = parser.parse_args()

    v = SPEC["variants"][VARIANT]
    targets = args.boards or list(v["boards"]) + list(v.get("legacy_boards", ()))
    if args.write:
        for b in targets:
            autoplace_cli(b, [])
        return

    eval_root = REPO / args.eval_dir
    results = []
    for b in targets:
        res = evaluate_board(b, eval_root, unlock_power_stage=False, route_time=args.route_time)
        results.append(res)
        c, s = res["curated"], res["cpsat"]
        print(f"{res['board']:14s} | place {res['place_s']:5.2f}s route {res['route_s']:6.2f}s | "
              f"curated: unconn={c['unconnected']} drc={c['drc_errors']} len={c['track_length_mm']:6.1f}mm vias={c['vias']:3d} decap={c['decap_mean_mm']:.2f}mm | "
              f"cpsat: unconn={s['unconnected']} drc={s['drc_errors']} len={s['track_length_mm']:6.1f}mm vias={s['vias']:3d} decap={s['decap_mean_mm']:.2f}mm",
              flush=True)
        if b == "power" and args.unlock_power_stage:
            res_full = evaluate_board(b, eval_root, unlock_power_stage=True, route_time=args.route_time)
            results.append(res_full)
            cf, sf = res_full["curated"], res_full["cpsat"]
            print(f"{res_full['board']:14s} | place {res_full['place_s']:5.2f}s route {res_full['route_s']:6.2f}s | "
                  f"curated: unconn={cf['unconnected']} drc={cf['drc_errors']} len={cf['track_length_mm']:6.1f}mm vias={cf['vias']:3d} decap={cf['decap_mean_mm']:.2f}mm | "
                  f"cpsat: unconn={sf['unconnected']} drc={sf['drc_errors']} len={sf['track_length_mm']:6.1f}mm vias={sf['vias']:3d} decap={sf['decap_mean_mm']:.2f}mm",
                  flush=True)
    (eval_root / "summary.json").write_text(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
