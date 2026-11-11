"""Hand-assembly drawing per board (`<board>_assembly.pdf` in the release): one page per populated side (the bottom
mirrored, as seen from below) with every part's body outline, reference + value, a red dot on pin 1 and a marker on
hand-fitted parts; same value = same colour; large parts (modules, connectors, terminals) dashed under the small ones;
a last page lists every value with quantity, side and references. The silkscreen hides references on dense boards,
this drawing never does (hardware/tests/test_assembly_drawings.py checks that every BOM part is drawn).

    python3 hardware/scripts/assembly_drawing.py <board> <out.pdf>
"""

import colorsys
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import yaml
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import Polygon, Rectangle

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

LARGE_MM2 = 40.0   # parts with a bigger body are drawn dashed, underneath


def board_data(board):
    variant = load_spec()["active_variant"]
    build = REPO / "hardware/build" / variant / board
    comps = json.loads((build / f"components_{board}.json").read_text())
    parts = {p["ref"]: p for p in json.loads((build / "parts.json").read_text())}
    frame = yaml.safe_load((REPO / "hardware/circuits/variants" / variant / f"layout_{board}.yaml").read_text())["frame"]
    outline = frame.get("outline") or [[0, 0], [frame["size"]["x"], 0], [frame["size"]["x"], frame["size"]["y"]], [0, frame["size"]["y"]]]
    placed = [c for c in comps if parts.get(c["ref"], {}).get("assembly", "jlc") != "none"]
    return placed, parts, outline


def colours(values):
    vals = sorted(set(values))
    return {v: colorsys.hsv_to_rgb((i * 0.618034) % 1.0, 0.55, 0.85) for i, v in enumerate(vals)}


def draw_side(pdf, board, side, comps, parts, outline, cmap):
    mirror = side == "back"
    fx = (lambda x: -x) if mirror else (lambda x: x)
    xs, ys = [fx(p[0]) for p in outline], [p[1] for p in outline]
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    scale = min(180.0 / max(w, 1), 250.0 / max(h, 1), 8.0)     # mm on paper per board mm (A4 portrait area)
    fig = plt.figure(figsize=(8.27, 11.69))
    ax = fig.add_axes([0.05, 0.08, 0.9, 0.84])
    ax.add_patch(Polygon(list(zip(xs, ys)), closed=True, fill=False, lw=1.2, ec="black"))
    font = max(3.0, min(7.0, 1.1 * scale))
    drawn = []
    for c in sorted(comps, key=lambda c: -((c["bbox"]["x1"] - c["bbox"]["x0"]) * (c["bbox"]["y1"] - c["bbox"]["y0"]))):
        b = c["bbox"]
        x0, x1 = sorted((fx(b["x0"]), fx(b["x1"])))
        area = (x1 - x0) * (b["y1"] - b["y0"])
        p = parts.get(c["ref"], {})
        large = area > LARGE_MM2
        col = cmap[c["value"]]
        ax.add_patch(Rectangle((x0, b["y0"]), x1 - x0, b["y1"] - b["y0"], fill=not large, alpha=0.35 if not large else 1.0,
                               fc=col, ec=col if not large else "grey", lw=0.6, ls="--" if large else "-", zorder=1 if large else 2))
        pin1 = next((q for q in c["pads"] if str(q[2]) in ("1", "A1")), None)
        if pin1 and (len(c["pads"]) > 2 or c["ref"].startswith("D")):   # ICs, connectors and every (polarised) diode
            ax.plot(fx(pin1[0]), pin1[1], "o", color="red", ms=max(1.5, 0.45 * scale), zorder=4)
        label = f"{c['ref']}\n{c['value']}"
        if p.get("assembly") == "hand":
            label += " [H]"
        ax.text((x0 + x1) / 2, (b["y0"] + b["y1"]) / 2, label, ha="center", va="center", fontsize=font * (1.3 if large else 1.0),
                color="dimgray" if large else "black", zorder=5)
        drawn.append(c["ref"])
    ax.set_xlim(min(xs) - 1, min(xs) + w + 1)
    ax.set_ylim(min(ys) - 1, min(ys) + h + 1)
    ax.set_aspect("equal")
    ax.axis("off")
    fig.suptitle(f"{board} - {'bottom (seen from below, mirrored)' if mirror else 'top'}   "
                 "red dot = pin 1, [H] = hand-fitted, dashed = large part (fit last)", fontsize=9)
    pdf.savefig(fig)
    plt.close(fig)
    return drawn


def value_page(pdf, board, comps, cmap):
    rows = {}
    for c in comps:
        r = rows.setdefault(c["value"], {"top": [], "bottom": []})
        r["bottom" if c["side"] == "back" else "top"].append(c["ref"])
    fig = plt.figure(figsize=(8.27, 11.69))
    fig.text(0.05, 0.96, f"{board} - values (quantity, side, references)", fontsize=11, weight="bold")
    y = 0.93
    for val in sorted(rows, key=str):
        for side in ("top", "bottom"):
            refs = sorted(rows[val][side], key=lambda r: (r.rstrip("0123456789"), int("0" + "".join(ch for ch in r if ch.isdigit()))))
            if not refs:
                continue
            fig.patches.append(Rectangle((0.05, y - 0.004), 0.02, 0.012, transform=fig.transFigure, fc=cmap[val], ec="none"))
            fig.text(0.08, y, f"{val}   x{len(refs)}   {side}:  {', '.join(refs)}", fontsize=7, va="bottom")
            y -= 0.017
            if y < 0.04:
                pdf.savefig(fig)
                plt.close(fig)
                fig = plt.figure(figsize=(8.27, 11.69))
                y = 0.96
    pdf.savefig(fig)
    plt.close(fig)


def write(board, out):
    comps, parts, outline = board_data(board)
    cmap = colours(c["value"] for c in comps)
    drawn = []
    with PdfPages(out) as pdf:
        for side in ("front", "back"):
            on_side = [c for c in comps if c["side"] == side]
            if on_side:
                drawn += draw_side(pdf, board, "back" if side == "back" else "top", on_side, parts, outline, cmap)
        value_page(pdf, board, comps, cmap)
    return sorted(drawn)


if __name__ == "__main__":
    print(", ".join(write(sys.argv[1], Path(sys.argv[2]))))
