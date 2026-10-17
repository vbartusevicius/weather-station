"""Mechanical fit verification:
  - Every component on main, crown, and probe stays inside the cylindrical enclosure walls
  - Every component clears the floor/ceiling of its bay
  - The 4 cardinal ultrasonic transducers sit clear of the 4 diagonal airfoil struts
  - The board-to-board JST-SH cables reach between main<->crown and main<->probe with slack
"""

import math
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import enclosure_features as ef


def test_all_boards_and_parts_fit_inside_cylinder_radius():
    max_r = ef.OUTER_R - ef.WALL - 0.5
    offenders = []
    for b in ef.part_boxes():
        for x in (b["x0"], b["x1"]):
            for z in (b["z0"], b["z1"]):
                r = math.hypot(x, z)
                if r > max_r:
                    offenders.append((b["board"], b["ref"], round(r, 2)))
    assert not offenders, f"parts exceeding inner cylinder radius {max_r} mm: {offenders}"


def test_main_board_parts_fit_vertically_in_base_bay():
    floor_y = ef.SPEC["envelope"]["base_bay"]["floor_y"]
    ceil_y = ef.SPEC["envelope"]["base_bay"]["ceiling_y"]
    bad = [(b["ref"], b["y0"], b["y1"]) for b in ef.part_boxes()
           if b["board"] == "main" and (b["y0"] < floor_y or b["y1"] > ceil_y)]
    assert not bad, f"main bay vertical collisions: {bad}"


def test_probe_board_parts_fit_inside_pagoda_height():
    y0 = ef.PAGODA_Y0
    y1 = ef.PAGODA_Y1
    bad = [(b["ref"], b["y0"], b["y1"]) for b in ef.part_boxes()
           if b["board"] == "probe" and (b["y0"] < y0 or b["y1"] > y1)]
    assert not bad, f"probe vertical collisions: {bad}"


def test_reflector_board_parts_fit_under_reflector_deck():
    bad = [(b["ref"], b["y0"], b["y1"]) for b in ef.part_boxes()
           if b["board"] == "reflector" and (b["y0"] < 114.0 or b["y1"] > 125.0)]
    assert not bad, f"reflector vertical collisions: {bad}"


def test_ultrasonic_transducers_clear_diagonal_airfoil_struts():
    struts = ef.strut_centres()
    for tp in ef.transducer_pockets():
        for st in struts:
            dist = math.hypot(tp["x"] - st["x"], tp["z"] - st["z"])
            assert dist >= 20.0, f"transducer {tp['name']} too close to strut {st['deg']}: {dist:.1f} mm"


def test_cables_reach_between_boards_with_slack():
    for lk in ef.SPEC["boards"]["interconnects"]:
        b_from, r_from = lk["from"], lk["refs"][lk["from"]]
        b_to, r_to = lk["to"], lk["refs"][lk["to"]]
        c1 = ef.components(b_from)[r_from]
        c2 = ef.components(b_to)[r_to]
        p1 = ef.board_point(b_from, c1["x"], c1["y"])
        p2 = ef.board_point(b_to, c2["x"], c2["y"])
        dist = math.sqrt(sum((a - b) ** 2 for a, b in zip(p1, p2)))
        assert dist + 10.0 <= lk["cable_length_mm"]
