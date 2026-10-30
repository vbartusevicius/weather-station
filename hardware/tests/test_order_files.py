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


def test_only_the_two_order_files_exist():
    assert sorted(p.name for p in RELEASE.glob("order_*.csv")) == ["order_elsewhere.csv", "order_lcsc.csv"]


def test_off_board_electronics_in_exactly_one_order_and_utility_in_none():
    lcsc = order_parts.render(FRESH["order_lcsc.csv"])
    elsewhere = {name for r in csv.DictReader(io.StringIO(order_parts.render(FRESH["order_elsewhere.csv"])))
                 for name in (r["Item"], *(d.split(":", 1)[1] for d in r["Item"].split(" off-board:") if ":" in d))}
    for p in csv.DictReader((RELEASE / "assembly_parts.csv").open()):
        in_lcsc, in_elsewhere = f"off-board:{p['Item']}" in lcsc, p["Item"] in elsewhere
        if p["Category"] == "electronics" and int(p["Qty"]):
            assert in_lcsc != in_elsewhere, f"{p['Item']}: must be in exactly one order file"
        else:
            assert not (in_lcsc or in_elsewhere), f"{p['Item']} ({p['Category']}) belongs in assembly_parts.csv only"
