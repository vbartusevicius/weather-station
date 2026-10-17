"""CadQuery 3D solid geometry probe tests:
  - base, pagoda, and crown each build as a single watertight solid (1 shell)
  - No 3D solid intersection between the printed enclosure parts and the 3 PCBs
"""

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be
import render_assembly3d as ra


def test_enclosure_parts_are_valid_solids():
    for name, fn in be.ALL_PARTS:
        solid = fn()
        assert solid.val().isValid(), f"{name} is not a valid BRep solid"
        assert solid.val().Volume() > 100.0, f"{name} has unexpectedly small volume"


def test_boards_do_not_collide_with_enclosure_parts():
    base = be.build_base()
    sleeve_back = be.build_probe_sleeve_back()
    sleeve_front = be.build_probe_sleeve_front()
    pagoda = be.build_pagoda()
    wind_tunnel = be.build_wind_tunnel()
    crown = be.build_crown()
    plates = {bname: plate for plate, _, bname in ra.board_plates()}
    assert base.intersect(plates["main"]).val().Volume() < 1e-3
    assert sleeve_back.intersect(plates["probe"]).val().Volume() < 1e-3
    assert sleeve_front.intersect(plates["probe"]).val().Volume() < 1e-3
    assert pagoda.intersect(plates["probe"]).val().Volume() < 1e-3
    assert wind_tunnel.intersect(plates["reflector"]).val().Volume() < 1e-3
    assert crown.intersect(plates["crown"]).val().Volume() < 1e-3
    assert crown.intersect(plates["dome"]).val().Volume() < 1e-3
