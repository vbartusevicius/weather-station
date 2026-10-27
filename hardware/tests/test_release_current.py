"""Verify that every board and the combined breakaway panel in hardware/releases/v1/
are up-to-date and DRC-clean."""

import csv
import json
import zipfile
from pathlib import Path

import pytest
from board_variant import BOARDS, VARIANT, layout_path

REPO = Path(__file__).resolve().parents[2]
RELEASE_DIR = REPO / "hardware/releases" / VARIANT


@pytest.mark.parametrize("board", (*BOARDS, "panel"))
def test_release_bundle_exists_and_is_drc_clean(board):
    bdir = RELEASE_DIR / board
    # JLCPCB set + the fab-neutral set (AISLER ODB++, PCBWay / Eurocircuits BOM + CPL)
    for suffix in ("_gerbers.zip", "_bom_jlc.csv", "_cpl_jlc.csv", "_odb.zip", "_bom.csv", "_cpl.csv"):
        assert (bdir / f"{board}{suffix}").exists(), f"{board}{suffix} missing"
    with zipfile.ZipFile(bdir / f"{board}_gerbers.zip") as z:
        names = z.namelist()
    assert any(n.endswith("-netlist.ipc") for n in names), "IPC-D-356 netlist missing from the Gerber zip"
    assert any(n.endswith("-PTH.drl") for n in names) and any(n.endswith("-Edge_Cuts.gbr") for n in names)
    with zipfile.ZipFile(bdir / f"{board}_odb.zip") as z:
        odb = z.namelist()
    assert any(n.endswith("eda/data") for n in odb) and any("netlists" in n for n in odb), "incomplete ODB++"
    with open(bdir / f"{board}_bom.csv") as f:
        rows = list(csv.DictReader(f))
    assert rows and all(r["MPN"] for r in rows), "BOM line without MPN (AISLER / PCBWay / Eurocircuits need it)"
    drc = json.loads((bdir / "drc.json").read_text())
    errors = [v for v in drc["violations"] if v["severity"] == "error"]
    assert not errors
    assert not drc["unconnected_items"]


@pytest.mark.parametrize("board", BOARDS)
def test_released_kicad_pcb_matches_layout(board):
    assert (RELEASE_DIR / board / f"{board}.kicad_pcb").read_bytes() == layout_path(board).read_bytes()
