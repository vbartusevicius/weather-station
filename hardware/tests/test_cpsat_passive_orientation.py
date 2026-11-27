"""Unit tests for CP-SAT 4-orientation 2-pin passive modeling, shared-parent pin attraction,
and zero-courtyard post-placement 180-deg uncrossing + adjacent pin-pair midpoint alignment."""

from __future__ import annotations

import math
import sys
from pathlib import Path

import pytest
from shapely.geometry import box

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import place_cpsat as pc


def _rot_pt(x_mm: float, y_mm: float, rot_deg: int) -> tuple[int, int]:
    rad = math.radians(rot_deg)
    rx = x_mm * math.cos(rad) - y_mm * math.sin(rad)
    ry = x_mm * math.sin(rad) + y_mm * math.cos(rad)
    return pc.g_near(rx), pc.g_near(ry)


def _make_2pin_spec(
    ref: str,
    net1: str,
    net2: str,
    *,
    fpid: str = "C_0805_2012Metric",
    half_w: float = 1.75,
    half_h: float = 1.05,
    pad_x: float = 0.95,
    init_x: float = 10.0,
    init_y: float = 10.0,
    init_rot: int = 0,
    side: str = "front",
    block: str = "mcu",
    locked: bool = False,
) -> pc.PartSpec:
    oris = {}
    for rot in pc.ANGLES:
        p1 = _rot_pt(-pad_x, 0.0, rot)
        p2 = _rot_pt(pad_x, 0.0, rot)
        if rot in (0, 180):
            dx0, dy0, dx1, dy1 = -pc.g_hi(half_w), -pc.g_hi(half_h), pc.g_hi(half_w), pc.g_hi(half_h)
        else:
            dx0, dy0, dx1, dy1 = -pc.g_hi(half_h), -pc.g_hi(half_w), pc.g_hi(half_h), pc.g_hi(half_w)
        oris[rot] = pc.OriGeom(
            rot=rot, dx0=dx0, dy0=dy0, dx1=dx1, dy1=dy1,
            pads={"1": (p1[0], p1[1], net1), "2": (p2[0], p2[1], net2)},
            tht_boxes=[], via_shadow=[],
        )
    return pc.PartSpec(
        ref=ref, side=side, block=block, locked=locked,
        init_x=init_x, init_y=init_y, init_rot=init_rot,
        fpid=fpid, pad_count=2, fine_pitch=False, raised=False, oris=oris,
    )


def _make_ic_spec(
    ref: str,
    pads_mm: dict[str, tuple[float, float, str]],
    *,
    half_w: float = 2.5,
    half_h: float = 2.5,
    init_x: float = 15.0,
    init_y: float = 12.0,
    init_rot: int = 0,
    block: str = "mcu",
    locked: bool = True,
) -> pc.PartSpec:
    oris = {}
    for rot in pc.ANGLES:
        rpads = {}
        for pnum, (px, py, net) in pads_mm.items():
            rx, ry = _rot_pt(px, py, rot)
            rpads[pnum] = (rx, ry, net)
        if rot in (0, 180):
            dx0, dy0, dx1, dy1 = -pc.g_hi(half_w), -pc.g_hi(half_h), pc.g_hi(half_w), pc.g_hi(half_h)
        else:
            dx0, dy0, dx1, dy1 = -pc.g_hi(half_h), -pc.g_hi(half_w), pc.g_hi(half_h), pc.g_hi(half_w)
        oris[rot] = pc.OriGeom(
            rot=rot, dx0=dx0, dy0=dy0, dx1=dx1, dy1=dy1,
            pads=rpads, tht_boxes=[], via_shadow=[],
        )
    return pc.PartSpec(
        ref=ref, side="front", block=block, locked=locked,
        init_x=init_x, init_y=init_y, init_rot=init_rot,
        fpid="SOIC-8", pad_count=len(pads_mm), fine_pitch=True, raised=False, oris=oris,
    )


@pytest.mark.parametrize("swap_cap_pins", (False, True))
def test_bypass_cap_hugs_ic_vcc_gnd_pair_and_never_flips_toward_distant_gnd(swap_cap_pins: bool):
    """A 2-pin bypass cap next to an IC's adjacent VCC/GND pins must orient its VCC pad toward the IC's VCC pin
    and its GND pad toward the IC's local GND pin—never flipping 180 deg toward a distant board GND connector."""
    # IC U1 at (16.0, 12.0) has adjacent VCC at (15.0, 14.2) and GND at (17.0, 14.2) on its top edge,
    # while a distant high-pin-count board connector J1 sits far to the LEFT at (2.0, 14.2) with 4 GND pins!
    u1 = _make_ic_spec(
        "U1",
        {
            "1": (-1.0, 2.2, "+3V3"),
            "2": (1.0, 2.2, "GND"),
            "3": (-1.0, -2.2, "SIG_A"),
            "4": (1.0, -2.2, "SIG_B"),
        },
        half_w=2.2, half_h=2.2, init_x=16.0, init_y=12.0, block="mcu", locked=True,
    )
    j1 = _make_ic_spec(
        "J1",
        {
            "1": (0.0, 1.0, "GND"),
            "2": (0.0, 0.3, "GND"),
            "3": (0.0, -0.3, "GND"),
            "4": (0.0, -1.0, "GND"),
        },
        half_w=1.0, half_h=2.0, init_x=2.0, init_y=14.2, block="power_in", locked=True,
    )
    net1, net2 = ("GND", "+3V3") if swap_cap_pins else ("+3V3", "GND")
    c1 = _make_2pin_spec("C1", net1, net2, init_x=8.0, init_y=17.0, init_rot=180, block="mcu", locked=False)

    plan = {
        "frame": {
            "size": {"x": 24.0, "y": 24.0},
            "edge_clearance": 0.3,
            "power_nets": ["+3V3"],
            "pours": [],
        }
    }
    specs = {"U1": u1, "J1": j1, "C1": c1}
    safe_poly = box(0.3, 0.3, 23.7, 23.7)
    mech_ko = {"front": [], "back": []}

    pos = pc.solve_specs_cpsat(
        "unit_test", plan, specs,
        seed=42, stage1_time=5.0, stage2_time=5.0,
        safe_poly=safe_poly, mech_ko=mech_ko, force_two_stage=True,
    )
    cx, cy, crot = pos["C1"]
    og = c1.oris[crot]
    pads_abs = {
        og.pads[p][2]: (cx + og.pads[p][0] * pc.GRID_MM, cy + og.pads[p][1] * pc.GRID_MM)
        for p in ("1", "2")
    }
    vcc_pad = pads_abs["+3V3"]
    gnd_pad = pads_abs["GND"]
    ic_vcc = (15.0, 14.2)
    ic_gnd = (17.0, 14.2)

    # C1 must sit right above U1's VCC/GND pair, centered on midpoint x = 16.0, with VCC pad LEFT of GND pad!
    assert abs(cx - 16.0) <= 0.15, f"C1 not centered on VCC/GND midpoint (16.0): cx={cx:.2f}"
    assert vcc_pad[0] < gnd_pad[0], f"C1 flipped upside-down toward distant left GND: VCC={vcc_pad}, GND={gnd_pad}, rot={crot}"
    assert not pc._grid_segs_intersect(
        (pc.g_near(vcc_pad[0]), pc.g_near(vcc_pad[1])),
        (pc.g_near(ic_vcc[0]), pc.g_near(ic_vcc[1])),
        (pc.g_near(gnd_pad[0]), pc.g_near(gnd_pad[1])),
        (pc.g_near(ic_gnd[0]), pc.g_near(ic_gnd[1])),
    ), f"C1->U1 leads cross: {vcc_pad}->{ic_vcc} vs {gnd_pad}->{ic_gnd}"


def test_post_placement_180_uncrossing_alignment_and_polarized_guard():
    """Verify post-placement uncrossing flips upside-down symmetric 2-pin passives (C, R, L), centers beside
    adjacent shared-parent pin pairs when clash-free, and never flips polarized caps or diodes."""
    u1 = _make_ic_spec(
        "U1",
        {"1": (-1.0, 2.0, "NET_A"), "2": (1.0, 2.0, "NET_B")},
        half_w=2.0, half_h=2.0, init_x=10.0, init_y=10.0, locked=True,
    )
    # Intentionally upside-down (rot=180 puts NET_A at +x and NET_B at -x!) and off-center at x=10.8!
    r1 = _make_2pin_spec("R1", "NET_A", "NET_B", fpid="R_0805_2012Metric", init_x=10.8, init_y=13.5, init_rot=180)
    # Polarized electrolytic cap intentionally upside-down -> MUST NOT be flipped!
    c_pol = _make_2pin_spec("C9", "NET_A", "NET_B", fpid="CP_Elec_6.3x5.4", init_x=10.0, init_y=5.0, init_rot=180)

    specs = {"U1": u1, "R1": r1, "C9": c_pol}
    nets = pc.build_net_index(specs)
    placed = {
        "U1": (pc.g_near(10.0), pc.g_near(10.0), 0),
        "R1": (pc.g_near(10.8), pc.g_near(13.5), 180),
        "C9": (pc.g_near(10.0), pc.g_near(5.0), 180),
    }
    flipped, aligned = pc.uncross_and_align_passives(
        specs, nets, placed,
        w_g=pc.g_hi(24.0), h_g=pc.g_hi(24.0),
        safe_poly=box(0.3, 0.3, 23.7, 23.7),
        mech_ko={"front": [], "back": []},
        y_domain_fn=lambda _sp: (0, pc.g_hi(24.0)),
    )
    assert flipped >= 1 and aligned >= 1
    assert placed["R1"][2] == 0, f"R1 should have flipped 180 -> 0 deg, got {placed['R1'][2]}"
    assert abs(placed["R1"][0] * pc.GRID_MM - 10.0) <= 0.06, "R1 should align to pin-pair midpoint x = 10.0"
    assert placed["C9"][2] == 180, "Polarized CP_Elec cap must never be 180-deg flipped!"


@pytest.mark.parametrize("board", ("main", "power", "probe", "dome", "dome_legacy", "reflector"))
def test_cpsat_boards_have_zero_upside_down_symmetric_passives(board: str):
    """After CP-SAT placement, no unlocked symmetric 2-pin passive on the 6 CP-SAT boards may have a strictly
    cheaper 180-deg flip (including crossing penalty) against its shared parent / nearest same-net pins."""
    plan = pc.layout.load_plan(board)
    specs = pc.extract_part_geoms(board, plan)
    nets = pc.build_net_index(specs)
    placed = {
        r: (pc.g_near(sp.init_x), pc.g_near(sp.init_y), sp.init_rot)
        for r, sp in specs.items()
    }
    frame = plan["frame"]
    w_mm, h_mm = frame["size"]["x"], frame["size"]["y"]
    flipped, _aligned = pc.uncross_and_align_passives(
        specs, nets, placed,
        w_g=pc.g_hi(w_mm), h_g=pc.g_hi(h_mm),
        safe_poly=pc.board_safe_polygon(board, plan),
        mech_ko=pc.mechanical_keepouts(board, plan),
        y_domain_fn=lambda _sp: (0, pc.g_hi(h_mm)),
    )
    assert flipped == 0, f"{board}: {flipped} unlocked symmetric 2-pin passives were placed upside-down!"
