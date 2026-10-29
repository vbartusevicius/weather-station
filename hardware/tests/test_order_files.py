"""The self-assembly order files in the release match a fresh export (hardware/scripts/order_parts.py)."""

import csv
import io
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/scripts"))
import order_parts

RELEASE = REPO / "hardware/releases/v1"
FRESH = order_parts.orders(RELEASE)


@pytest.mark.parametrize("name", sorted(FRESH))
def test_order_file_matches_a_fresh_export(name):
    assert (RELEASE / name).read_text() == order_parts.render(FRESH[name]), \
        f"{name} is stale: run python3 hardware/scripts/order_parts.py"


def test_lcsc_order_has_one_line_per_number_and_every_board_part():
    rows = list(csv.DictReader(io.StringIO(order_parts.render(FRESH["order_lcsc.csv"]))))
    codes = [r["LCSC Part Number"] for r in rows]
    assert len(codes) == len(set(codes)), "duplicate LCSC numbers"
    elsewhere = csv.DictReader(io.StringIO(order_parts.render(FRESH["order_elsewhere.csv"])))
    ordered = {d for r in rows for d in r["Designators"].split()} | {d for r in elsewhere for d in r["Item"].split()}
    for board, r in order_parts.board_boms(RELEASE):
        if r["LCSC"]:
            assert {f"{board}:{x}" for x in r["Designators"].split(",")} <= ordered
