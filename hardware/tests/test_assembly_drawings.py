"""Every released board has a hand-assembly drawing that shows every BOM part (hardware/scripts/assembly_drawing.py)."""

import csv
import sys
from pathlib import Path

import pytest
from board_variant import BOARDS

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import assembly_drawing

RELEASE = REPO / "hardware/releases/v1"


@pytest.mark.parametrize("board", BOARDS)
def test_assembly_drawing_covers_every_part(board, tmp_path):
    assert (RELEASE / board / f"{board}_assembly.pdf").stat().st_size > 1000, f"{board}: no assembly drawing in the release"
    drawn = set(assembly_drawing.write(board, tmp_path / "a.pdf"))
    bom = {d for r in csv.DictReader((RELEASE / board / f"{board}_bom.csv").open()) for d in r["Designators"].split(",")}
    missing = sorted(bom - drawn)
    assert not missing, f"{board}: parts missing from the assembly drawing: {missing}"
