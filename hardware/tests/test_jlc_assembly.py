"""JLCPCB assembly: every part lands pin-for-pin on JLCPCB's EasyEDA footprint."""

import json
import sys
from pathlib import Path

import pcbnew
import pytest
from board_variant import BOARDS, build_path, layout_path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import jlc_parts as jp


@pytest.fixture(scope="module", params=BOARDS)
def board_checks(request):
    board = request.param
    pcb = pcbnew.LoadBoard(str(layout_path(board)))
    parts = json.loads(build_path(board, "parts.json").read_text())
    return parts, jp.check_board(pcb, parts)


def test_every_part_is_checked_and_lands_pin_for_pin(board_checks):
    parts, checks = board_checks
    assert {c.ref for c in checks} == {p["ref"] for p in parts if p["assembly"] != "none"}   # none = copper-only pads
    assert not [f"{c.ref} {c.lcsc}: {p}" for c in checks for p in c.problems]
