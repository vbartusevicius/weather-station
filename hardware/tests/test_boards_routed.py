"""Every stored board layout must have 0 DRC errors and 0 unrouted nets."""

import json
import subprocess

import pytest
from board_variant import BOARDS, build_path, layout_path


@pytest.mark.parametrize("board", BOARDS)
def test_board_is_routed_with_zero_drc_errors(board):
    pcb = layout_path(board)
    report = build_path(board, "test_drc.json")
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--output", str(report), str(pcb)],
                   check=True, capture_output=True)
    drc = json.loads(report.read_text())
    errors = [v for v in drc["violations"] if v["severity"] == "error"]
    assert not errors, f"{board} DRC errors: {errors}"
    assert not drc["unconnected_items"], f"{board} unrouted items: {drc['unconnected_items']}"
