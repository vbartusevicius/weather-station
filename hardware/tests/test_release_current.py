"""Verify that every board and the combined breakaway panel in hardware/releases/v1/
are up-to-date and DRC-clean."""

import json
from pathlib import Path

import pytest
from board_variant import BOARDS, VARIANT, layout_path

REPO = Path(__file__).resolve().parents[2]
RELEASE_DIR = REPO / "hardware/releases" / VARIANT


@pytest.mark.parametrize("board", (*BOARDS, "panel"))
def test_release_bundle_exists_and_is_drc_clean(board):
    bdir = RELEASE_DIR / board
    assert (bdir / f"{board}_gerbers.zip").exists()
    assert (bdir / f"{board}_bom_jlc.csv").exists()
    assert (bdir / f"{board}_cpl_jlc.csv").exists()
    drc = json.loads((bdir / "drc.json").read_text())
    errors = [v for v in drc["violations"] if v["severity"] == "error"]
    assert not errors
    assert not drc["unconnected_items"]


@pytest.mark.parametrize("board", BOARDS)
def test_released_kicad_pcb_matches_layout(board):
    assert (RELEASE_DIR / board / f"{board}.kicad_pcb").read_bytes() == layout_path(board).read_bytes()
