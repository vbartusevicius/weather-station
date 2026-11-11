"""Powder-print (MJF / SLS PA12) rules learnt from the first printed knx-dali-gateway case: snap strain, hook and
mating clearances, body-to-body gaps on a print plate, engraved mark size / depth, and board support."""

import inspect
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be
import enclosure_features as ef
from OCP.BRepExtrema import BRepExtrema_DistShapeShape

SEPARATE_BODY_GAP = 0.3   # mating clearance between separate bodies printed together (they fuse closer than this)


def test_snap_beams_stay_within_the_pa12_strain_limit():
    for b in ef.snap_beams():
        assert b["strain"] <= ef.PA12_MAX_STRAIN, (
            f"{b['name']}: strain {100 * b['strain']:.1f} % > {100 * ef.PA12_MAX_STRAIN:.1f} % "
            f"(t {b['t']} mm, deflection {b['deflection']} mm, free length {b['length']:.2f} mm)")
        assert b["hook_gap"] >= 0.3 - 1e-9, f"{b['name']}: only {b['hook_gap']:.2f} mm of fit tolerance"


def test_the_old_clip_geometry_fails_the_strain_rule():
    """The clips as first designed (t 1.5 mm, hooks 0.12 mm above the board): power 1.1 mm and crown 1.2 mm hook
    overlap on ~7 mm beams. The rule must reject them."""
    old_power = ef.cantilever_strain(1.5, 1.1, ef.SPEC["boards"]["power"]["y"] + 0.12 - ef.BAY_FLOOR_Y)
    old_crown = ef.cantilever_strain(1.5, 1.2, ef.SPEC["boards"]["crown"]["y"] + 0.12 - ef.CROWN_FLOOR_Y)
    assert old_power > ef.PA12_MAX_STRAIN and old_crown > ef.PA12_MAX_STRAIN, (old_power, old_crown)


def _clips():
    """(name, body, beam centre x / z, hook direction, SNAP entry, board top y, root y) of every board clip."""
    base, crown_base = be.build_base(umbrella=False), be.build_crown_base()
    for board in ("main", "power"):
        for cx, cz, d in ef.CLIPS[board]:
            yield f"{board} ({cx}, {cz})", base, cx, cz, d, ef.SNAP[board], ef.SPEC["boards"][board]["y"], ef.BAY_FLOOR_Y
    top = ef.SPEC["boards"]["crown"]["y"] + ef.UPPER
    for cx, cz, d in be.CROWN_CLIPS:
        yield f"crown ({cx}, {cz})", crown_base, cx, cz, d, ef.SNAP["crown"], top, ef.CROWN_FLOOR_Y + ef.UPPER


def _local_box(cx, cz, d, u0, u1, y0, y1, w0, w1):
    """Box in clip coordinates (u toward the board, w across) -> enclosure frame."""
    dx, dz = d
    if dx:
        return be.box(cx + min(u0 * dx, u1 * dx), cx + max(u0 * dx, u1 * dx), y0, y1, cz + w0, cz + w1)
    return be.box(cx + w0, cx + w1, y0, y1, cz + min(u0 * dz, u1 * dz), cz + max(u0 * dz, u1 * dz))


def test_snap_beams_flex_free_over_their_whole_length():
    """No other part of the print stands within 0.6 mm (it would fuse) of a clip beam between its root and its hook,
    so the free length the strain estimate uses is real; and the hook underside is >= HOOK_GAP above the board."""
    for name, body, cx, cz, d, s, top, root in _clips():
        t, w, g = s["t"], s["w"], 0.6
        y0, y1 = root + 0.3, top + ef.HOOK_GAP - 0.05
        for u0, u1, w0, w1 in ((-t / 2 - g, -t / 2 - 0.02, -w / 2 - g, w / 2 + g),     # behind (outward)
                               (-t / 2 - g, t / 2 + g, w / 2 + 0.02, w / 2 + g),       # both sides
                               (-t / 2 - g, t / 2 + g, -w / 2 - g, -w / 2 - 0.02)):
            vol = body.intersect(_local_box(cx, cz, d, u0, u1, y0, y1, w0, w1)).val().Volume()
            assert vol < 1e-3, f"{name} clip: material within 0.6 mm of the beam ({vol:.3f} mm^3)"
        under_hook = _local_box(cx, cz, d, t / 2 + 0.05, t / 2 + s["overlap"], top, top + ef.HOOK_GAP - 0.02, -w / 2, w / 2)
        assert body.intersect(under_hook).val().Volume() < 1e-3, f"{name} clip: hook less than {ef.HOOK_GAP} mm above the board"


def test_printed_threads_have_powder_print_flank_clearance():
    src = inspect.getsource(be)
    assert be.THREAD_CLEARANCE >= 0.3
    small = [m for m in re.findall(r"clearance=(0\.\d+)", src) if float(m) < 0.3]
    assert not small, f"thread calls with clearance {small} mm per side"


def test_base_plate_bodies_keep_a_mating_gap():
    """Base, umbrella and reset button print as one plate: every pair of separate bodies >= SEPARATE_BODY_GAP apart."""
    assert be.BTN_BORE_R - be.BTN_PIN_R >= 0.4 - 1e-9 and be.BTN_CBORE_R - be.BTN_FLANGE_R >= 0.4 - 1e-9
    solids = be.build_base().val().Solids()
    assert len(solids) == 3
    for i in range(len(solids)):
        for j in range(i + 1, len(solids)):
            gap = BRepExtrema_DistShapeShape(solids[i].wrapped, solids[j].wrapped).Value()
            assert gap >= SEPARATE_BODY_GAP - 1e-6, f"bodies {i} / {j} only {gap:.2f} mm apart (fuse in powder)"


def test_engraved_lock_marks_are_deep_and_large_enough():
    """Engraved marks read on powder prints at >= 0.6 mm depth and >= 3 mm size (0.45 mm / 2.9 mm did not)."""
    p = inspect.signature(be.cut_lock_triangles_y).parameters
    assert p["depth"].default >= 0.6 and 2 * p["half_w"].default >= 3.0


def test_every_horizontal_board_is_supported_in_all_four_quadrants():
    """Each base / crown board rests on case posts in all four quadrants (no corner left to sag onto a component)."""
    for board in ("main", "power", "crown"):
        cfg = ef.SPEC["boards"][board]["max_outline"]
        quadrants = {(st["bx"] > cfg["width"] / 2, st["by"] > cfg["height"] / 2) for st in ef.standoffs(board)}
        assert len(quadrants) == 4, f"{board}: posts only in quadrants {sorted(quadrants)}"


def _ink(path, px_per_mm=40):
    """Rasterise a matplotlib path (mm) to a boolean ink image."""
    import numpy as np
    from matplotlib.path import Path as MPath
    ext = path.get_extents()
    xs = np.arange(ext.x0 - 1, ext.x1 + 1, 1 / px_per_mm)
    ys = np.arange(ext.y0 - 1, ext.y1 + 1, 1 / px_per_mm)
    gx, gy = np.meshgrid(xs, ys)
    pts = np.column_stack([gx.ravel(), gy.ravel()])
    ink = np.zeros(len(pts), bool)
    for poly in path.to_polygons():   # even-odd fill (letter counters stay open)
        ink ^= MPath(poly).contains_points(pts)
    return ink.reshape(gx.shape)


def test_legend_glyphs_are_printable():
    """Every legend label (if engraved / embossed): >= 3 mm tall, <= 20 % of its ink thinner than a 0.4 mm stroke
    (morphological opening with a 0.4 mm disc), >= 1 mm between labels."""
    import front_legend
    import numpy as np
    from scipy.ndimage import binary_opening
    px = 40
    r = round(0.2 * px)
    yy, xx = np.mgrid[-r:r + 1, -r:r + 1]
    disc = xx ** 2 + yy ** 2 <= r * r
    labels = front_legend.labels()
    for lbl in labels:
        _, y0, _, y1 = lbl.box()
        assert y1 - y0 >= 3.0 - 1e-6, f"{lbl.text!r}: {y1 - y0:.2f} mm tall"
        ink = _ink(lbl.path(), px)
        thin = 1.0 - binary_opening(ink, structure=disc).sum() / ink.sum()
        assert thin <= 0.20, f"{lbl.text!r}: {100 * thin:.0f} % of the ink is thinner than 0.4 mm"
    for i, a in enumerate(labels):
        for b in labels[i + 1:]:
            ax0, ay0, ax1, ay1 = a.box()
            bx0, by0, bx1, by1 = b.box()
            gap = max(bx0 - ax1, ax0 - bx1, by0 - ay1, ay0 - by1)
            assert gap >= 1.0, f"{a.text!r} / {b.text!r} only {gap:.2f} mm apart"


def test_push_in_terminal_j1_is_reachable():
    """main J1 (Kefa KF250-3.5-4P, datasheet: 13.3 mm tall incl. the release levers, 45-deg wire entry, 300 V / 5 A
    for the 12-32 V + RS-485 circuit): its body clears the base and umbrella, and >= 10 mm stay free on every side
    (wire entry at 45 deg + a finger / screwdriver on the levers once the umbrella is lifted out)."""
    j = next(p for p in ef.part_boxes() if p["board"] == "main" and p["ref"] == "J1")
    assert j["y1"] - j["y0"] >= 13.3 - 1e-6, "J1 modelled lower than its datasheet height"
    base = be.build_base()
    body = be.box(j["x0"], j["x1"], j["y0"], j["y1"], j["z0"], j["z1"])
    assert base.intersect(body).val().Volume() < 1e-3
    y0, y1 = j["y0"] + 2.0, j["y1"] - 1.0
    for side in (be.box(j["x1"], j["x1"] + 10.0, y0, y1, j["z0"], j["z1"]), be.box(j["x0"] - 10.0, j["x0"], y0, y1, j["z0"], j["z1"]),
                 be.box(j["x0"], j["x1"], y0, y1, j["z1"], j["z1"] + 10.0), be.box(j["x0"], j["x1"], y0, y1, j["z0"] - 10.0, j["z0"])):
        assert base.intersect(side).val().Volume() < 1e-3, "less than 10 mm free beside J1"


def test_pagoda_weep_grilles_stay_clear_of_the_louver_rings():
    """The top-piece weep slits must not cut through the louver skirt / rim rings (they sliced the skirt into slivers
    at r 30.5): every slit edge >= 0.6 mm from each ring's radial span."""
    half = be.PAGODA_VENT_W / 2.0
    rings = (be.LOUVER_SKIRT[:2], be.LOUVER_RIM[:2])
    for off in be.PAGODA_VENT_OFFSETS:
        r0, r1 = be.PAGODA_VENT_R + off - half, be.PAGODA_VENT_R + off + half
        for a, b in rings:
            gap = max(a - r1, r0 - b)
            assert gap >= 0.6, f"slit r {r0:.1f}..{r1:.1f} only {gap:.2f} mm from the ring r {a}..{b}"
