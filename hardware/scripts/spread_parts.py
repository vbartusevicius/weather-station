"""Resolve courtyard overlaps in a layout plan after footprints grew (e.g. 0603 -> 0805): read the courtyard boxes of
the last placement (build/<variant>/<board>/components_<board>.json), push unlocked parts apart along the axis of
least overlap (keeping them inside the board frame), and write the new x / y into layout_<board>.yaml.

    python3 hardware/scripts/spread_parts.py <board> [--gap 0.1] [--margin 0.5]
"""

import json
import re
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec


def main(board, gap=0.1, margin=0.5, rounds=400):
    variant = load_spec()["active_variant"]
    plan_path = REPO / "hardware/circuits/variants" / variant / f"layout_{board}.yaml"
    text = plan_path.read_text()
    plan = yaml.safe_load(text)
    comps = {c["ref"]: c for c in json.loads((REPO / "hardware/build" / variant / board / f"components_{board}.json").read_text())}
    w, h = plan["frame"]["size"]["x"], plan["frame"]["size"]["y"]
    parts = plan["parts"]
    boxes = {}
    for ref, c in comps.items():
        if ref not in parts:
            continue
        b = c["bbox"]
        boxes[ref] = {"x0": b["x0"], "x1": b["x1"], "y0": b["y0"], "y1": b["y1"], "side": c["side"],
                      "fixed": bool(parts[ref].get("locked"))}
    moved = {}
    for _ in range(rounds):
        changed = False
        refs = sorted(boxes)
        for i, a in enumerate(refs):
            for b in refs[i + 1:]:
                A, B = boxes[a], boxes[b]
                if A["side"] != B["side"] or (A["fixed"] and B["fixed"]):
                    continue
                ox = min(A["x1"], B["x1"]) - max(A["x0"], B["x0"]) + gap
                oy = min(A["y1"], B["y1"]) - max(A["y0"], B["y0"]) + gap
                if ox <= 0 or oy <= 0:
                    continue
                # move the free one (both free: each half), along the smaller penetration
                movers = [r for r in (a, b) if not boxes[r]["fixed"]]
                share = 1.0 / len(movers)
                for r in movers:
                    other = B if r == a else A
                    me = boxes[r]
                    if ox <= oy:
                        d = ox * share * (1 if (me["x0"] + me["x1"]) >= (other["x0"] + other["x1"]) else -1)
                        me["x0"] += d
                        me["x1"] += d
                        moved.setdefault(r, [0.0, 0.0])[0] += d
                    else:
                        d = oy * share * (1 if (me["y0"] + me["y1"]) >= (other["y0"] + other["y1"]) else -1)
                        me["y0"] += d
                        me["y1"] += d
                        moved.setdefault(r, [0.0, 0.0])[1] += d
                    changed = True
        for r, me in boxes.items():   # keep inside the frame (margin to the edge)
            if me["fixed"]:
                continue
            for lo, hi, size, axis in (("x0", "x1", w, 0), ("y0", "y1", h, 1)):
                d = max(0.0, margin - me[lo]) - max(0.0, me[hi] - (size - margin))
                if d:
                    me[lo] += d
                    me[hi] += d
                    moved.setdefault(r, [0.0, 0.0])[axis] += d
                    changed = True
        if not changed:
            break
    for ref, (dx, dy) in sorted(moved.items()):
        if abs(dx) < 1e-3 and abs(dy) < 1e-3:
            continue
        x, y = round(parts[ref]["x"] + dx, 2), round(parts[ref]["y"] + dy, 2)
        pat = re.compile(rf"^(  {re.escape(ref)}:\s*\{{\s*x:\s*)[-\d.]+(\s*,\s*y:\s*)[-\d.]+", re.MULTILINE)
        text, n = pat.subn(rf"\g<1>{x}\g<2>{y}", text)
        if n != 1:
            sys.exit(f"{ref}: could not rewrite its plan line")
        print(f"{ref}: ({parts[ref]['x']}, {parts[ref]['y']}) -> ({x}, {y})")
    plan_path.write_text(text)


if __name__ == "__main__":
    args = sys.argv[1:]
    opts = {"gap": 0.1, "margin": 0.5}
    for k in list(opts):
        if f"--{k}" in args:
            i = args.index(f"--{k}")
            opts[k] = float(args[i + 1])
            del args[i:i + 2]
    main(args[0], **opts)
