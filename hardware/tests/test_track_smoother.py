"""Unit & integration tests for the DRC-safe octilinear (0/45/90 deg) track smoother (`layout.smooth_tracks`)."""

from __future__ import annotations

import math
import sys
from pathlib import Path

import pcbnew

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import layout


def _mm(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))


def _make_board(w_mm: float = 20.0, h_mm: float = 20.0) -> tuple[pcbnew.BOARD, dict]:
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(4)
    settings = board.GetDesignSettings()
    settings.m_MinClearance = pcbnew.FromMM(0.2)
    settings.m_CopperEdgeClearance = pcbnew.FromMM(0.3)
    settings.m_HoleClearance = pcbnew.FromMM(0.25)
    plan = {
        "frame": {
            "size": {"x": w_mm, "y": h_mm},
            "copper_layers": 4,
            "thickness": 1.6,
            "min_drill": 0.2,
            "min_track": 0.12,
            "edge_clearance": 0.3,
            "pours": [],
            "power_nets": [],
            "fine_pitch_power_pads": [],
        }
    }
    corners = [(0.0, 0.0), (w_mm, 0.0), (w_mm, h_mm), (0.0, h_mm)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        layout.edge_seg(board, *a, *b)
    return board, plan


def _add_net(board: pcbnew.BOARD, name: str, code: int) -> pcbnew.NETINFO_ITEM:
    net = pcbnew.NETINFO_ITEM(board, name, code)
    board.Add(net)
    return net


def _add_track(board: pcbnew.BOARD, net: pcbnew.NETINFO_ITEM, ax: float, ay: float, bx: float, by: float,
               w_mm: float = 0.2, layer: int = pcbnew.F_Cu) -> pcbnew.PCB_TRACK:
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(_mm(ax, -ay))
    t.SetEnd(_mm(bx, -by))
    t.SetWidth(pcbnew.FromMM(w_mm))
    t.SetLayer(layer)
    board.Add(t)
    t.SetNet(net)
    return t


def _add_circle_pad(board: pcbnew.BOARD, net: pcbnew.NETINFO_ITEM | None, ref: str, cx: float, cy: float,
                    dia_mm: float, drill_mm: float = 0.0, npth: bool = False) -> pcbnew.PAD:
    fp = pcbnew.FOOTPRINT(board)
    fp.SetReference(ref)
    fp.SetPosition(_mm(cx, -cy))
    pad = pcbnew.PAD(fp)
    pad.SetNumber("1")
    pad.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
    pad.SetSize(pcbnew.VECTOR2I(pcbnew.FromMM(dia_mm), pcbnew.FromMM(dia_mm)))
    if npth:
        pad.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
        pad.SetDrillSize(pcbnew.VECTOR2I(pcbnew.FromMM(drill_mm), pcbnew.FromMM(drill_mm)))
        pad.SetLayerSet(pad.UnplatedHoleMask())
    elif drill_mm > 0:
        pad.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        pad.SetDrillSize(pcbnew.VECTOR2I(pcbnew.FromMM(drill_mm), pcbnew.FromMM(drill_mm)))
        pad.SetLayerSet(pad.PTHMask())
    else:
        pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        pad.SetLayerSet(pad.SMDMask())
    pad.SetPosition(_mm(cx, -cy))
    fp.Add(pad)
    board.Add(fp)
    if net is not None:
        pad.SetNet(net)
    return pad


def test_octilinear_candidates_geometry():
    # Exact H, V, and 45-deg pairs return a single straight segment.
    assert layout._octilinear_candidates((0, 0), (10_000, 0)) == [[(0, 0), (10_000, 0)]]
    assert layout._octilinear_candidates((0, 0), (0, -10_000)) == [[(0, 0), (0, -10_000)]]
    assert layout._octilinear_candidates((0, 0), (5_000, -5_000)) == [[(0, 0), (5_000, -5_000)]]
    # Non-octilinear pairs return BOTH 45-deg + H/V doglegs, each strictly octilinear and minimal length.
    cands = layout._octilinear_candidates((0, 0), (10_000, 4_000))
    assert len(cands) == 2
    for c in cands:
        assert c[0] == (0, 0) and c[-1] == (10_000, 4_000) and len(c) == 3
        assert layout._is_octilinear(c[0], c[1]) and layout._is_octilinear(c[1], c[2])


def test_smoother_bridges_snap_tolerance_and_midchain_neckdown():
    """Endpoints separated by 0.010 mm (<= 0.015 mm) and mid-chain width neck-downs (0.20 -> 0.15 -> 0.20 mm)
    must stay in one degree-2 chain and smooth cleanly using `min(chain_widths)`."""
    board, plan = _make_board()
    sig = _add_net(board, "SIG_A", 1)
    _add_circle_pad(board, sig, "P1", 2.0, 10.0, 0.8)
    _add_circle_pad(board, sig, "P2", 14.0, 10.0, 0.8)

    # Micro-staircase with 0.010 mm endpoint gap and 0.15 mm neck-down mid-chain.
    _add_track(board, sig, 2.0, 10.0, 5.0, 10.0, 0.20)
    _add_track(board, sig, 5.008, 10.0, 6.0, 11.0, 0.15)
    _add_track(board, sig, 6.0, 11.0, 10.0, 11.0, 0.15)
    _add_track(board, sig, 10.0, 11.0, 11.0, 10.0, 0.20)
    _add_track(board, sig, 11.0, 10.0, 14.0, 10.0, 0.20)

    removed, saved_mm = layout.smooth_tracks(board, plan)
    tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
    assert removed == 4 and saved_mm > 0.7
    assert len(tracks) == 1
    assert tracks[0].GetWidth() == pcbnew.FromMM(0.15)
    assert layout.unconnected(board) == 0


def test_smoother_circular_pad_exact_distance_beats_inflated_bbox():
    """A 45-deg shortcut passing near a circular THT pad at distance `r + w/2 + clr + 0.05` must be ACCEPTED even when
    it cuts across the circular pad's inflated `sqrt(2)` axis-aligned bounding-box corner, and REJECTED when closer."""
    # Pad at (10.0, 10.0), dia = 2.0 mm (r = 1.0 mm). Track w = 0.20 (hw = 0.10), clr = 0.20 + 0.015 -> req = 1.315 mm.
    # A 45-deg line from (8.0, 10.0) to (10.0, 12.0) passes (10, 10) at distance sqrt(2) = 1.414 mm:
    #   - > 1.315 mm true circular clearance (VALID!),
    #   - but distance to the pad's square bbox [9..11, 9..11] corner (9, 11) is 0.0 mm (would falsely fail a bbox check!).
    board, plan = _make_board()
    sig = _add_net(board, "SIG_PASS", 1)
    other = _add_net(board, "OTHER", 2)
    _add_circle_pad(board, other, "J_THT", 10.0, 10.0, dia_mm=2.0, drill_mm=1.0)
    _add_circle_pad(board, sig, "P1", 8.0, 10.0, 0.6)
    _add_circle_pad(board, sig, "P2", 10.0, 12.0, 0.6)
    # 3-segment V-detour around the corner
    _add_track(board, sig, 8.0, 10.0, 8.0, 12.0, 0.20)
    _add_track(board, sig, 8.0, 12.0, 9.0, 12.0, 0.20)
    _add_track(board, sig, 9.0, 12.0, 10.0, 12.0, 0.20)

    removed, saved_mm = layout.smooth_tracks(board, plan)
    assert removed == 2 and saved_mm > 1.0

    # Now enlarge the circular pad to dia = 2.4 mm (r = 1.2 mm -> req = 1.515 mm > 1.414 mm): shortcut must be REJECTED!
    board2, plan2 = _make_board()
    sig2 = _add_net(board2, "SIG_BLOCK", 1)
    other2 = _add_net(board2, "OTHER", 2)
    _add_circle_pad(board2, other2, "J_THT", 10.0, 10.0, dia_mm=2.4, drill_mm=1.0)
    _add_circle_pad(board2, sig2, "P1", 8.0, 10.0, 0.6)
    _add_circle_pad(board2, sig2, "P2", 10.0, 12.0, 0.6)
    _add_track(board2, sig2, 8.0, 10.0, 8.0, 12.0, 0.20)
    _add_track(board2, sig2, 8.0, 12.0, 9.0, 12.0, 0.20)
    _add_track(board2, sig2, 9.0, 12.0, 10.0, 12.0, 0.20)
    removed2, _ = layout.smooth_tracks(board2, plan2)
    tracks2 = [t for t in board2.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
    # Collinear 2-seg top edge may merge (8,12)->(10,12), but the 45-deg shortcut (8,10)->(10,12) must NOT happen!
    assert len(tracks2) == 2 and removed2 == 1


def test_smoother_respects_rule_area_npth_diff_cap_and_self_crossing():
    # (a) Rule-area keepout & NPTH mounting hole block shortcuts.
    board, plan = _make_board()
    sig = _add_net(board, "SIG_KO", 1)
    _add_circle_pad(board, sig, "P1", 2.0, 10.0, 0.6)
    _add_circle_pad(board, sig, "P2", 12.0, 10.0, 0.6)
    layout.keepout_zone(board, 6.0, 9.0, 8.0, 11.0, [pcbnew.F_Cu])
    _add_track(board, sig, 2.0, 10.0, 5.0, 13.0, 0.20)
    _add_track(board, sig, 5.0, 13.0, 9.0, 13.0, 0.20)
    _add_track(board, sig, 9.0, 13.0, 12.0, 10.0, 0.20)
    removed, _ = layout.smooth_tracks(board, plan)
    assert removed == 0

    # (b) Unnetted NPTH mounting hole blocks straight shortcut.
    board_h, plan_h = _make_board()
    sig_h = _add_net(board_h, "SIG_H", 1)
    _add_circle_pad(board_h, sig_h, "P1", 2.0, 10.0, 0.6)
    _add_circle_pad(board_h, sig_h, "P2", 12.0, 10.0, 0.6)
    _add_circle_pad(board_h, None, "MH1", 7.0, 10.0, dia_mm=2.2, drill_mm=2.2, npth=True)
    _add_track(board_h, sig_h, 2.0, 10.0, 5.0, 13.0, 0.20)
    _add_track(board_h, sig_h, 5.0, 13.0, 9.0, 13.0, 0.20)
    _add_track(board_h, sig_h, 9.0, 13.0, 12.0, 10.0, 0.20)
    removed_h, _ = layout.smooth_tracks(board_h, plan_h)
    assert removed_h == 0

    # (c) Length-matched / differential net preserves large (> 1.0 mm) intentional meander while removing small jogs.
    board_d, plan_d = _make_board()
    diff = _add_net(board_d, "RS485_A", 1)
    _add_circle_pad(board_d, diff, "P1", 2.0, 10.0, 0.6)
    _add_circle_pad(board_d, diff, "P2", 16.0, 10.0, 0.6)
    # 3.0 mm tall trombone meander (shortcut would save 6.0 mm > 1.0 mm cap -> MUST be preserved!)
    _add_track(board_d, diff, 2.0, 10.0, 6.0, 10.0, 0.20)
    _add_track(board_d, diff, 6.0, 10.0, 6.0, 13.0, 0.20)
    _add_track(board_d, diff, 6.0, 13.0, 8.0, 13.0, 0.20)
    _add_track(board_d, diff, 8.0, 13.0, 8.0, 10.0, 0.20)
    # Plus a tiny 0.2 mm micro-staircase at the right (saving ~0.12 mm <= 1.0 mm -> MUST be smoothed!)
    _add_track(board_d, diff, 8.0, 10.0, 11.0, 10.0, 0.20)
    _add_track(board_d, diff, 11.0, 10.0, 11.2, 10.2, 0.20)
    _add_track(board_d, diff, 11.2, 10.2, 12.0, 10.2, 0.20)
    _add_track(board_d, diff, 12.0, 10.2, 12.2, 10.0, 0.20)
    _add_track(board_d, diff, 12.2, 10.0, 16.0, 10.0, 0.20)

    before_len = sum(pcbnew.ToMM(t.GetLength()) for t in board_d.GetTracks())
    removed_d, saved_d = layout.smooth_tracks(board_d, plan_d)
    after_len = sum(pcbnew.ToMM(t.GetLength()) for t in board_d.GetTracks())
    assert removed_d >= 3
    assert 0.05 < saved_d <= 1.05
    assert math.isclose(before_len - after_len, saved_d, abs_tol=0.05)

    # (d) Same-net self-crossing across another branch of the same net must be rejected.
    board_s, plan_s = _make_board()
    net_s = _add_net(board_s, "NET_S", 1)
    _add_circle_pad(board_s, net_s, "P1", 2.0, 10.0, 0.6)
    _add_circle_pad(board_s, net_s, "P2", 12.0, 10.0, 0.6)
    _add_circle_pad(board_s, net_s, "P3", 7.0, 5.0, 0.6)
    _add_circle_pad(board_s, net_s, "P4", 7.0, 11.5, 0.6)
    # Unrelated same-net vertical trunk right across y = 10 at x = 7
    _add_track(board_s, net_s, 7.0, 5.0, 7.0, 11.5, 0.20)
    # U-detour going around top of P4 at y = 13
    _add_track(board_s, net_s, 2.0, 10.0, 5.0, 13.0, 0.20)
    _add_track(board_s, net_s, 5.0, 13.0, 9.0, 13.0, 0.20)
    _add_track(board_s, net_s, 9.0, 13.0, 12.0, 10.0, 0.20)
    removed_s, _ = layout.smooth_tracks(board_s, plan_s)
    assert removed_s == 0
