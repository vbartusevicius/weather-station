"""Hand-assembly drawing per board (`<board>_assembly.pdf` in the release).

Pages: one per populated side (the bottom mirrored, as seen from below), a values page (value, quantity, side,
references) and a key page (the marks, finding pin 1 on real parts, tools). Same value = same colour; large parts
(modules, terminals, converters) dashed and drawn under the small ones; hand-fitted parts carry a corner flag.

Polarity is marked only where orientation matters, the way the real part is marked (`mark_kind`):
  cathode bar : diodes (D_SMA / D_SOD footprints: KiCad puts K on pad 1); bidirectional TVS ("...CA") get none
  plus        : electrolytic / tantalum capacitors (CP_ footprints: + on pad 1)
  none        : resistors, ceramic capacitors, inductors, fuses, crystals, pin strips, switches, the antenna coil
  pin1        : everything else (ICs, transistors, LEDs, optocouplers, connectors, modules): red dot + "1"
hardware/tests/test_assembly_drawings.py checks the drawing against the BOM.

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

LARGE_MM2 = 40.0           # bodies bigger than this are drawn dashed, underneath
A4 = (8.27, 11.69)
NONPOLAR_PREFIXES = ("R_", "C_", "L_", "Fuse_", "Crystal_", "PinHeader", "PinSocket", "SW_")
NONPOLAR_NAMES = ("Antenna",)                       # AS3935 ferrite-rod coil: a plain inductor
CATHODE_PREFIXES = ("D_SMA", "D_SMB", "D_SMC", "D_SOD")
CATHODE_PAD = {"Genicom_COB2418": "2"}              # Genicom UV photodiodes: pad 1 = anode, pad 2 = cathode


def mark_kind(footprint, value):
    """'cathode', 'plus', 'pin1' or None (orientation does not matter)."""
    if footprint.startswith(CATHODE_PREFIXES) or footprint in CATHODE_PAD:
        return None if value.strip().upper().endswith("CA") else "cathode"
    if footprint.startswith("CP_"):
        return "plus"
    if footprint.startswith(NONPOLAR_PREFIXES) or any(n in footprint for n in NONPOLAR_NAMES):
        return None
    return "pin1"


def mark_pad(comp):
    """The pad the mark sits on (cathode / + / pin 1), or None if the footprint has no such pad."""
    want = CATHODE_PAD.get(comp["footprint"], "1")
    return next((p for p in comp["pads"] if str(p[2]) in (want, "A1" if want == "1" else want)), None)


def board_data(board):
    variant = load_spec()["active_variant"]
    build = REPO / "hardware/build" / variant / board
    comps = json.loads((build / f"components_{board}.json").read_text())
    parts = {p["ref"]: p for p in json.loads((build / "parts.json").read_text())}
    frame = yaml.safe_load((REPO / "hardware/circuits/variants" / variant / f"layout_{board}.yaml").read_text())["frame"]
    outline = frame.get("outline") or [[0, 0], [frame["size"]["x"], 0], [frame["size"]["x"], frame["size"]["y"]],
                                       [0, frame["size"]["y"]]]
    placed = [c for c in comps if parts.get(c["ref"], {}).get("assembly", "jlc") != "none"]
    return placed, parts, outline


def colours(values):
    return {v: colorsys.hsv_to_rgb((i * 0.618034) % 1.0, 0.45, 0.92) for i, v in enumerate(sorted(set(values)))}


def _new_page():
    fig = plt.figure(figsize=A4)
    return fig, fig.add_axes([0.04, 0.05, 0.92, 0.88])


def draw_side(pdf, board, side, comps, parts, outline, cmap):
    mirror = side == "back"
    fx = (lambda x: -x) if mirror else (lambda x: x)
    xs, ys = [fx(p[0]) for p in outline], [p[1] for p in outline]
    fig, ax = _new_page()
    ax.add_patch(Polygon(list(zip(xs, ys)), closed=True, fill=False, lw=1.4, ec="black"))
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    mm_per_pt = max(w / (A4[0] * 0.92 * 72), h / (A4[1] * 0.88 * 72))   # board mm per printed point
    drawn, marks = [], {}
    for c in sorted(comps, key=lambda c: -((c["bbox"]["x1"] - c["bbox"]["x0"]) * (c["bbox"]["y1"] - c["bbox"]["y0"]))):
        b = c["bbox"]
        x0, x1 = sorted((fx(b["x0"]), fx(b["x1"])))
        y0, y1 = b["y0"], b["y1"]
        bw, bh = x1 - x0, y1 - y0
        large = bw * bh > LARGE_MM2
        col = cmap[c["value"]]
        ax.add_patch(Rectangle((x0, y0), bw, bh, fill=not large, fc=col, alpha=1.0 if large else 0.55,
                               ec="dimgray" if large else "black", lw=0.8 if large else 0.4, ls="--" if large else "-",
                               zorder=1 if large else 2))
        kind = mark_kind(c["footprint"], c["value"])
        pad = mark_pad(c) if kind else None
        if kind and pad:
            px, py = fx(pad[0]), pad[1]
            if kind == "cathode":   # black bar across the body at the cathode pad, like the band on the part
                cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
                if abs(px - cx) >= abs(py - cy):
                    bx = x0 if px < cx else x1 - bw * 0.18
                    ax.add_patch(Rectangle((bx, y0), bw * 0.18, bh, fc="black", ec="none", zorder=4))
                else:
                    by = y0 if py < cy else y1 - bh * 0.18
                    ax.add_patch(Rectangle((x0, by), bw, bh * 0.18, fc="black", ec="none", zorder=4))
            elif kind == "plus":
                ax.text(px, py, "+", color="red", fontsize=11, weight="bold", ha="center", va="center", zorder=6)
            else:
                ax.plot(px, py, "o", color="red", ms=4.5, zorder=6)
                ax.text(px + 1.2 * mm_per_pt * 4, py, "1", color="red", fontsize=6, weight="bold", ha="left", va="center", zorder=6)
        marks[c["ref"]] = kind if pad or not kind else "missing"
        if parts.get(c["ref"], {}).get("assembly") == "hand":   # corner flag: triangle in the top-right corner
            f = min(min(bw, bh) * 0.35, 2.5)
            ax.add_patch(Polygon([(x1, y1), (x1 - f, y1), (x1, y1 - f)], closed=True, fc="darkorange", ec="black", lw=0.3, zorder=5))
        font = 7.5 if not large else 9
        ax.text((x0 + x1) / 2, (y0 + y1) / 2, f"{c['ref']}\n{c['value']}", ha="center", va="center", fontsize=font,
                color="dimgray" if large else "black", zorder=7, linespacing=0.95)
        drawn.append(c["ref"])
    ax.set_xlim(min(xs) - 1, max(xs) + 1)
    ax.set_ylim(min(ys) - 1, max(ys) + 1)
    ax.set_aspect("equal")
    ax.axis("off")
    fig.text(0.04, 0.965, f"{board} - {'BOTTOM (seen from below, mirrored)' if mirror else 'TOP'}", fontsize=13, weight="bold")
    fig.text(0.04, 0.945, "red dot 1 = pin 1   black bar = cathode   red + = cap +   orange corner = hand-fitted   "
             "dashed = fit last   (key: last page)", fontsize=7.5)
    pdf.savefig(fig)
    plt.close(fig)
    return drawn, marks


def value_page(pdf, board, comps, cmap):
    rows = {}
    for c in comps:
        rows.setdefault(c["value"], {"top": [], "bottom": []})["bottom" if c["side"] == "back" else "top"].append(c["ref"])
    order = lambda r: (r.rstrip("0123456789"), int("0" + "".join(ch for ch in r if ch.isdigit())))
    fig = plt.figure(figsize=A4)
    fig.text(0.05, 0.96, f"{board} - values", fontsize=13, weight="bold")
    y = 0.93
    for val in sorted(rows, key=str):
        for side in ("top", "bottom"):
            refs = sorted(rows[val][side], key=order)
            if not refs:
                continue
            if y < 0.05:
                pdf.savefig(fig)
                plt.close(fig)
                fig, y = plt.figure(figsize=A4), 0.95
            fig.patches.append(Rectangle((0.05, y - 0.003), 0.025, 0.013, transform=fig.transFigure, fc=cmap[val], ec="black", lw=0.3))
            fig.text(0.085, y, f"{val}    x{len(refs)}    {side}:  {', '.join(refs)}", fontsize=8.5, va="bottom")
            y -= 0.021
    pdf.savefig(fig)
    plt.close(fig)


KEY = [
    ("Marks on the board pages", True),
    ("  red dot + 1 : pin 1 of ICs, transistors, LEDs, optocouplers, connectors and modules", False),
    ("  black bar   : diode cathode, matching the band printed on the diode body", False),
    ("  red +       : + pad of electrolytic / tantalum capacitors", False),
    ("  no mark     : resistors, ceramic capacitors, inductors, fuses, crystals, pin strips, bidirectional TVS (...CA)", False),
    ("  orange corner: part fitted by hand (not placed by JLCPCB); dashed outline: large part, fit it last", False),
    ("  the BOTTOM page is mirrored: it shows the board as you see it from below", False),
    ("", False),
    ("Finding pin 1 on the real part", True),
    ("  SOT-23 (3 legs): the single leg on its own side is pin 3; pin 1 is left of it on the two-leg side", False),
    ("  SOT-23-5 / SC-70 / SOIC / VSSOP / QFN / LGA: a dot, dimple or bevelled edge marks pin 1", False),
    ("  Addressable LEDs: a marked (cut / green) corner - check the datasheet, it is easy to misread", False),
    ("  Diodes: the band is the cathode (K)", False),
    ("  Electrolytic capacitors: the stripe marks the - side (the other pad is +)", False),
    ("", False),
    ("Tools", True),
    ("  0805 / SOT-23: a 3-5x magnifier or a 10x loupe", False),
    ("  QFN / LGA / DFN / SC-70: a USB microscope (check the pads under the part after reflow)", False),
]


def key_page(pdf, board):
    fig = plt.figure(figsize=A4)
    fig.text(0.05, 0.96, f"{board} - key", fontsize=13, weight="bold")
    y = 0.92
    for text, head in KEY:
        fig.text(0.05, y, text, fontsize=11 if head else 9, weight="bold" if head else "normal", family="monospace" if not head else None)
        y -= 0.03
    pdf.savefig(fig)
    plt.close(fig)


def write(board, out):
    """Write the PDF; returns {ref: {"side": "top"/"bottom", "mark": kind | None | "missing"}} of every part drawn."""
    comps, parts, outline = board_data(board)
    cmap = colours(c["value"] for c in comps)
    result = {}
    with PdfPages(out) as pdf:
        for side, name in (("front", "top"), ("back", "back")):
            on_side = [c for c in comps if c["side"] == side]
            if on_side:
                drawn, marks = draw_side(pdf, board, name, on_side, parts, outline, cmap)
                for ref in drawn:
                    result[ref] = {"side": "bottom" if side == "back" else "top", "mark": marks[ref]}
        value_page(pdf, board, comps, cmap)
        key_page(pdf, board)
    return result


if __name__ == "__main__":
    print(", ".join(sorted(write(sys.argv[1], Path(sys.argv[2])))))
