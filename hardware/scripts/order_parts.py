"""Upload-ready parts orders for self-assembling one complete station, from a release folder.

    order_lcsc.csv       LCSC BOM Tool: every assembled part of every board, one line per LCSC number (quantities
                         summed, designators `board:ref`), + off-board parts whose Part names an LCSC number
    order_mouser.csv     Mouser BOM tool: off-board parts with a Mouser number and no LCSC number
    order_elsewhere.csv  what neither sells (Digi-Key / TME numbers where the parts list gives them)

Inputs: `<board>/<board>_bom.csv` (release BOMs with MPN + LCSC) and `assembly_parts.csv` (off-board parts).
`hardware/product/lcsc_store.yaml` swaps JLCPCB numbers the LCSC store does not stock for verified store numbers.
Printed parts and items with quantity 0 are skipped. Quantities are for one device (LCSC rounds up to its minimum).

    python3 hardware/scripts/order_parts.py [release_dir]          # write the three files
    python3 hardware/scripts/order_parts.py [release_dir] --check  # + live LCSC check of every number
"""

from __future__ import annotations

import csv
import io
import json
import re
import sys
import time
import urllib.request
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

_SPEC = load_spec()
LEGACY = set(_SPEC["variants"][_SPEC["active_variant"]].get("legacy_boards", ()))   # alternatives, e.g. dome_legacy
STORE = REPO / "hardware/product/lcsc_store.yaml"
LCSC_API = "https://wmsc.lcsc.com/ftps/wm/product/detail?productCode={}"
LCSC_IN_PART = re.compile(r"\bLCSC (C\d+)\b")
MOUSER_IN_TEXT = re.compile(r"\bMouser (\d{2,3}-[A-Za-z0-9.\-/]+)")
DIGIKEY_IN_TEXT = re.compile(r"\bDigi-Key (\d{2,4}-[A-Za-z0-9.\-/]+)")
TME_IN_TEXT = re.compile(r"\bTME ([A-Za-z0-9.\-/]+)")


def board_boms(release: Path) -> list[tuple[str, dict]]:
    rows = []
    for bom in sorted(release.glob("*/*_bom.csv")):
        board = bom.parent.name
        if board == "panel" or board in LEGACY or bom.name != f"{board}_bom.csv":
            continue
        rows += [(board, r) for r in csv.DictReader(bom.open())]
    return rows


def first(pattern: re.Pattern, *texts: str) -> str:
    for text in texts:
        m = pattern.search(text.split("(or")[0]) or pattern.search(text)
        if m:
            return m.group(1)
    return ""


def orders(release: Path) -> dict[str, list[list]]:
    store = yaml.safe_load(STORE.read_text()) or {}
    subs, away = store.get("substitutes", {}), store.get("elsewhere", {})
    lines: dict[str, dict] = {}
    mouser, elsewhere, moved = [], [], {}

    def add(lcsc, qty, mpn, value, footprint, designators):
        if lcsc in away:   # not sold by the LCSC store: one order_elsewhere line per number
            moved.setdefault(lcsc, [0, []])
            moved[lcsc][0] += qty
            moved[lcsc][1] += designators
            return
        if lcsc in subs:
            sub = subs[lcsc]
            lcsc, mpn, value = sub["lcsc"], sub["mpn"], sub.get("value", value)
        line = lines.setdefault(lcsc, {"qty": 0, "mpn": mpn, "value": value, "footprint": footprint, "refs": []})
        line["qty"] += qty
        line["refs"] += designators

    for board, r in board_boms(release):
        if r["LCSC"]:
            add(r["LCSC"], int(r["Qty"]), r["MPN"], r["Value"], r["Package"], [f"{board}:{x}" for x in r["Designators"].split(",")])
    for p in csv.DictReader((release / "assembly_parts.csv").open()):
        qty, source = int(p["Qty"]), p["Source"].strip().lower()
        if qty == 0 or source == "printed":
            continue
        texts = (p["Part"], p["Source"])
        lcsc = first(LCSC_IN_PART, p["Part"])
        if lcsc:
            add(lcsc, qty, p["Part"].split(" (")[0], "", "", [f"off-board:{p['Item']}"])
        elif first(MOUSER_IN_TEXT, *texts):
            mouser.append([first(MOUSER_IN_TEXT, *texts), qty, p["Part"], p["Item"]])
        else:
            elsewhere.append([p["Item"], qty, p["Part"], first(DIGIKEY_IN_TEXT, *texts), first(TME_IN_TEXT, *texts), p["Source"]])
    # legacy (backup) boards: every part of one board, so it can be built too; listed apart, not in the LCSC order
    for bom in sorted(release.glob("*/*_bom.csv")):
        board = bom.parent.name
        if board not in LEGACY or bom.name != f"{board}_bom.csv":
            continue
        for r in csv.DictReader(bom.open()):
            src = f"LCSC {r['LCSC']}" if r["LCSC"] else "Digi-Key / brokers (not stocked at LCSC / JLCPCB)"
            elsewhere.append([f"{board} (legacy board only): {r['Designators'].replace(',', ' ')}", int(r["Qty"]),
                              f"{r['Manufacturer']} {r['MPN']} ({r['Value']}, {r['Package']})", "", "", src])
    for lcsc, (qty, refs) in sorted(moved.items()):
        a = away[lcsc]
        elsewhere.append([" ".join(sorted(refs)), qty, a["part"], a.get("digikey", ""), a.get("tme", ""), a["source"]])
    lcsc_rows = [[c, v["qty"], v["mpn"], v["value"], v["footprint"], " ".join(sorted(v["refs"]))] for c, v in sorted(lines.items())]
    return {
        "order_lcsc.csv": [["LCSC Part Number", "Quantity", "Manufacturer Part Number", "Value", "Footprint", "Designators"], *lcsc_rows],
        "order_mouser.csv": [["Mouser Part Number", "Quantity", "Part", "Item"], *mouser],
        "order_elsewhere.csv": [["Item", "Quantity", "Part", "Digi-Key", "TME", "Source"], *elsewhere],
    }


def render(rows: list[list]) -> str:
    buf = io.StringIO()
    csv.writer(buf, lineterminator="\n").writerows(rows)
    return buf.getvalue()


def write(release: Path) -> None:
    for name, rows in orders(release).items():
        (release / name).write_text(render(rows))


def check(release: Path) -> list[str]:
    """Live LCSC store check: number exists, not discontinued, stock covers the order quantity."""
    problems = []
    for code, qty, mpn, *_ in orders(release)["order_lcsc.csv"][1:]:
        req = urllib.request.Request(LCSC_API.format(code), headers={"User-Agent": "Mozilla/5.0"})
        r = json.loads(urllib.request.urlopen(req, timeout=30).read()).get("result")
        time.sleep(0.3)
        if not r:
            problems.append(f"{code} ({mpn}): not sold in the LCSC store")
        elif r.get("productCycle") not in (None, "normal"):
            problems.append(f"{code} ({mpn}): {r.get('productCycle')}, stock {r.get('stockNumber')}")
        elif (r.get("stockNumber") or 0) < qty:
            problems.append(f"{code} ({mpn}): stock {r.get('stockNumber') or 0} < {qty}")
    return problems


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    release = Path(args[0]) if args else REPO / "hardware/releases/v1"
    write(release)
    if "--check" in sys.argv:
        found = check(release)
        print("\n".join(found) or "every LCSC number exists, is active and in stock")
        sys.exit(1 if found else 0)
