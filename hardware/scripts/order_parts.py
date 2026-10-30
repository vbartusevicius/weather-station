"""Upload-ready parts orders for self-assembling one complete station, from a release folder.

    order_lcsc.csv       LCSC BOM Tool: every assembled part of every board (legacy boards too, designators
                         `dome_legacy:ref`), one line per LCSC number (quantities summed, designators `board:ref`),
                         + off-board electronics whose Part names an LCSC number
    order_elsewhere.csv  electronics the LCSC store does not sell: Mouser / Digi-Key / TME numbers (several per shop
                         allowed, the first is the preferred one) + other sources

Inputs: `<board>/<board>_bom.csv` (release BOMs with MPN + LCSC) and `assembly_parts.csv` (off-board parts with a
Category: electronics / utility / printed). Utility items (fasteners, O-rings, gland, tapes, coatings, the UV cover)
and printed parts stay in assembly_parts.csv only. `hardware/product/lcsc_store.yaml` swaps JLCPCB numbers the LCSC
store does not stock for verified store numbers. Quantities are for one device (LCSC rounds up to its minimum).

    python3 hardware/scripts/order_parts.py [release_dir]          # write both files
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
TME_IN_TEXT = re.compile(r"\bTME ([A-Za-z0-9][A-Za-z0-9.\-/]+)")
ELSEWHERE_HEADER = ["Item", "Quantity", "Part", "Mouser", "Digi-Key", "TME", "Other sources"]


def board_boms(release: Path) -> list[tuple[str, dict]]:
    rows = []
    for bom in sorted(release.glob("*/*_bom.csv")):
        board = bom.parent.name
        if board == "panel" or bom.name != f"{board}_bom.csv":
            continue
        rows += [(board, r) for r in csv.DictReader(bom.open())]
    return rows


def first(pattern: re.Pattern, *texts: str) -> str:
    for text in texts:
        m = pattern.search(text.split("(or")[0]) or pattern.search(text)
        if m:
            return m.group(1)
    return ""


def every(pattern: re.Pattern, *texts: str) -> str:
    """All numbers of one shop in the texts, in order, ' / '-separated (the first is the preferred one)."""
    found = []
    for text in texts:
        found += [n.rstrip(".,;") for n in pattern.findall(text) if n.rstrip(".,;") not in found]
    return " / ".join(found)


def orders(release: Path) -> dict[str, list[list]]:
    store = yaml.safe_load(STORE.read_text()) or {}
    subs, away = store.get("substitutes", {}), store.get("elsewhere", {})
    lines: dict[str, dict] = {}
    elsewhere, moved = [], {}

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
        qty = int(p["Qty"])
        if qty == 0 or p["Category"] != "electronics":
            continue
        texts = (p["Part"], p["Source"])
        lcsc = first(LCSC_IN_PART, p["Part"])
        if lcsc:
            add(lcsc, qty, p["Part"].split(" (")[0], "", "", [f"off-board:{p['Item']}"])
        else:
            elsewhere.append([p["Item"], qty, p["Part"], every(MOUSER_IN_TEXT, *texts), every(DIGIKEY_IN_TEXT, *texts),
                              every(TME_IN_TEXT, *texts), p["Source"]])
    # legacy-board parts without an LCSC number (e.g. the obsolete GUVB-C31SM): hand-fitted, bought elsewhere
    legacy = store.get("legacy", {})
    for board, r in board_boms(release):
        if board in LEGACY and not r["LCSC"]:
            src = legacy.get(r["MPN"], {})
            elsewhere.append([f"{board}:{r['Designators'].replace(',', ' ')} {src.get('item', r['Value'])} (legacy board only)",
                              int(r["Qty"]), f"{r['Manufacturer']} {r['MPN']}", src.get("mouser", ""), src.get("digikey", ""),
                              src.get("tme", ""), src.get("source", "brokers only (not at LCSC / JLCPCB)")])
    for lcsc, (qty, refs) in sorted(moved.items()):
        a = away[lcsc]
        elsewhere.append([" ".join(sorted(refs)), qty, a["part"], a.get("mouser", ""), a.get("digikey", ""), a.get("tme", ""),
                          a["source"]])
    lcsc_rows = [[c, v["qty"], v["mpn"], v["value"], v["footprint"], " ".join(sorted(v["refs"]))] for c, v in sorted(lines.items())]
    return {
        "order_lcsc.csv": [["LCSC Part Number", "Quantity", "Manufacturer Part Number", "Value", "Footprint", "Designators"], *lcsc_rows],
        "order_elsewhere.csv": [ELSEWHERE_HEADER, *elsewhere],
    }


def render(rows: list[list]) -> str:
    buf = io.StringIO()
    csv.writer(buf, lineterminator="\n").writerows(rows)
    return buf.getvalue()


def write(release: Path) -> None:
    for name, rows in orders(release).items():
        (release / name).write_text(render(rows))


def check(release: Path) -> list[str]:
    """Live LCSC store check: number exists, not discontinued, stock covers the order quantity. Lines used only by
    legacy boards (populated even if out of stock; skipped at assembly) are reported as notes, not problems."""
    problems = []
    for code, qty, mpn, _value, _fp, refs in orders(release)["order_lcsc.csv"][1:]:
        if all(r.split(":")[0] in LEGACY for r in refs.split()):
            continue
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
