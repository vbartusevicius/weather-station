"""Hand-assembly drawings (hardware/scripts/assembly_drawing.py): every sourced part drawn on its own side, the release
ships the PDF, polarity marked only where orientation matters and every polarised part has its mark pad."""

import csv
import json
import sys
from pathlib import Path

import pytest
from board_variant import BOARDS

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import assembly_drawing as ad

RELEASE = REPO / "hardware/releases/v1"


@pytest.fixture(scope="module", params=BOARDS)
def drawing(request, tmp_path_factory):
    board = request.param
    return board, ad.write(board, tmp_path_factory.mktemp(board) / "a.pdf")


def test_release_ships_the_drawing(drawing):
    board, _ = drawing
    assert (RELEASE / board / f"{board}_assembly.pdf").stat().st_size > 1000, f"{board}: no assembly drawing in the release"


def test_every_sourced_part_is_drawn_on_its_own_side(drawing):
    board, drawn = drawing
    comps = {c["ref"]: c for c in json.loads((REPO / "hardware/build/v1" / board / f"components_{board}.json").read_text())}
    bom = {d for r in csv.DictReader((RELEASE / board / f"{board}_bom.csv").open()) for d in r["Designators"].split(",")}
    assert not sorted(bom - set(drawn)), f"{board}: parts missing from the drawing: {sorted(bom - set(drawn))}"
    for ref in bom:
        want = "bottom" if comps[ref]["side"] == "back" else "top"
        assert drawn[ref]["side"] == want, f"{board} {ref}: drawn on the {drawn[ref]['side']} page, sits on the {want}"


def test_polarity_marks_only_where_orientation_matters(drawing):
    board, drawn = drawing
    comps = {c["ref"]: c for c in json.loads((REPO / "hardware/build/v1" / board / f"components_{board}.json").read_text())}
    for ref, d in drawn.items():
        c = comps[ref]
        assert d["mark"] != "missing", f"{board} {ref} ({c['footprint']}): polarised but has no pin-1 / cathode pad"
        if c["footprint"].startswith(ad.NONPOLAR_PREFIXES):
            assert d["mark"] is None, f"{board} {ref} ({c['footprint']}): non-polar part carries a mark"


def test_mark_rules():
    assert ad.mark_kind("D_SMA", "SMAJ36A") == "cathode"
    assert ad.mark_kind("D_SMA", "SMAJ40CA") is None            # bidirectional TVS: non-polar
    assert ad.mark_kind("CP_Elec_6.3x5.4", "100u") == "plus"
    for fp in ("R_0805_2012Metric", "C_0805_2012Metric", "L_Sunlord_SWPA6045S", "Fuse_2920_7451Metric", "Crystal_SMD_HC49-SD"):
        assert ad.mark_kind(fp, "x") is None, fp
    for fp in ("SOT-23", "SOIC-8_3.9x4.9mm_P1.27mm", "JST_SH_BM10B-SRSS-TB_1x10-1MP_P1.00mm_Vertical", "XIAO_ESP32C6_Header"):
        assert ad.mark_kind(fp, "x") == "pin1", fp
