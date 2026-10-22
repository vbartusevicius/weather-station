"""Supplier drawings for purchased custom parts (SVG, mm), written next to the enclosure exports.

jgs1_dome.svg: the flanged JGS1 fused-silica UV dome over the `dome` board (checklist B3 / M13). The dimensions match
the crown lid seat in build_enclosure.py (Ø 20.8 mm flange bore, Ø 18.5 mm top hole, flange stack-up 193.0..194.2)
and the render model in render_assembly3d.py.
"""

from __future__ import annotations

import math
from pathlib import Path

# Dome geometry (mm): flange, cylindrical neck, hemisphere on top of the neck; constant 1.2 mm wall
FLANGE_OD, FLANGE_T = 20.6, 1.2
NECK_OD, NECK_H = 18.0, 3.8
WALL = 1.2
ID = NECK_OD - 2 * WALL
R_OUT = NECK_OD / 2
R_IN = R_OUT - WALL
TOTAL_H = FLANGE_T + NECK_H + R_OUT

S = 9.0             # drawing scale (px per mm)
OX, OY = 330.0, 360.0   # axis x, flange underside y (px)


def _p(x, y):
    return f"{OX + x * S:.1f},{OY - y * S:.1f}"


def _profile(sign):
    """Closed half-section polygon (sign +1 right half) of the dome wall, flange to apex."""
    pts = [(sign * ID / 2, 0.0), (sign * FLANGE_OD / 2, 0.0), (sign * FLANGE_OD / 2, FLANGE_T),
           (sign * R_OUT, FLANGE_T), (sign * R_OUT, FLANGE_T + NECK_H)]
    pts += [(sign * R_OUT * math.cos(math.radians(a)), FLANGE_T + NECK_H + R_OUT * math.sin(math.radians(a)))
            for a in range(0, 91, 5)]
    pts += [(sign * R_IN * math.cos(math.radians(a)), FLANGE_T + NECK_H + R_IN * math.sin(math.radians(a)))
            for a in range(90, -1, -5)]
    pts += [(sign * R_IN, 0.0)]
    return " ".join(_p(x, y) for x, y in pts)


def _dim_h(x0, x1, y, text, above=True):
    yy = OY - y * S + (-14 if above else 14)
    a, b = OX + x0 * S, OX + x1 * S
    return (f'<line x1="{a}" y1="{yy}" x2="{b}" y2="{yy}" class="dim" marker-start="url(#a)" marker-end="url(#a)"/>'
            f'<line x1="{a}" y1="{OY - y * S}" x2="{a}" y2="{yy}" class="ext"/>'
            f'<line x1="{b}" y1="{OY - y * S}" x2="{b}" y2="{yy}" class="ext"/>'
            f'<text x="{(a + b) / 2}" y="{yy - 4 if above else yy + 12}" class="t" text-anchor="middle">{text}</text>')


def _dim_v(x, y0, y1, text, dx=40):
    xx = OX + x * S + dx
    a, b = OY - y0 * S, OY - y1 * S
    return (f'<line x1="{xx}" y1="{a}" x2="{xx}" y2="{b}" class="dim" marker-start="url(#a)" marker-end="url(#a)"/>'
            f'<line x1="{OX + x * S}" y1="{a}" x2="{xx + 4}" y2="{a}" class="ext"/>'
            f'<line x1="{OX + x * S}" y1="{b}" x2="{xx + 4}" y2="{b}" class="ext"/>'
            f'<text x="{xx + 6}" y="{(a + b) / 2 + 4}" class="t">{text}</text>')


def dome_svg() -> str:
    notes = [
        "Material: JGS1 UV-grade fused silica (transmission >= 85 % 250-400 nm, >= 90 % 400-1100 nm), no coating.",
        "Wall 1.2 +/-0.1 mm, uniform; outer and inner hemisphere optically polished (60-40 S/D or better).",
        "Flange: OD 20.6 +0/-0.2 mm, thickness 1.0-1.2 mm (MAX 1.2); top face fine-ground flat (17 x 1.5 mm O-ring seal).",
        "Neck OD 18.0 +/-0.1 mm (passes a 18.5 mm hole); ID 15.6 +/-0.2 mm through flange and neck.",
        "Edges chamfered 0.2 mm x 45 deg; no chips > 0.3 mm on the flange or neck.",
        "Operating -40..+85 C outdoors; quantity per station: 1.",
    ]
    body = "".join(
        f'<polygon points="{_profile(s)}" class="{"cut" if s > 0 else "out"}"/>' for s in (1, -1))
    dims = (
        _dim_h(-FLANGE_OD / 2, FLANGE_OD / 2, 0.0, f"Ø {FLANGE_OD} +0/-0.2", above=False)
        + _dim_h(-R_OUT, R_OUT, FLANGE_T + NECK_H + 2.0, f"Ø {NECK_OD} ±0.1")
        + _dim_h(-ID / 2, ID / 2, FLANGE_T + 1.6, f"Ø {ID:.1f} ±0.2 (ID)")
        + _dim_v(FLANGE_OD / 2, 0.0, FLANGE_T, f"{FLANGE_T} max", dx=30)
        + _dim_v(R_OUT, FLANGE_T, FLANGE_T + NECK_H, f"{NECK_H}", dx=60)
        + _dim_v(R_OUT, 0.0, TOTAL_H, f"{TOTAL_H:.1f}", dx=110)
        + f'<text x="{OX + 4.5 * S}" y="{OY - (FLANGE_T + NECK_H + 6.6) * S}" class="t">SR {R_OUT} outside, SR {R_IN:.1f} inside</text>'
        + f'<line x1="{OX}" y1="{OY + 40}" x2="{OX}" y2="{OY - (TOTAL_H + 4) * S}" class="axis"/>'
    )
    notes_svg = "".join(f'<text x="40" y="{470 + 18 * i}" class="n">{i + 1}. {n}</text>' for i, n in enumerate(notes))
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="900" height="620" viewBox="0 0 900 620">
<defs><marker id="a" viewBox="0 0 10 10" refX="5" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
<path d="M0,2 L10,5 L0,8 z"/></marker>
<pattern id="h" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><line x1="0" y1="0" x2="0" y2="6" stroke="#333" stroke-width="1"/></pattern></defs>
<style>.cut{{fill:url(#h);stroke:#000;stroke-width:1.5}}.out{{fill:none;stroke:#000;stroke-width:1.5}}
.dim{{stroke:#06c;stroke-width:1}}.ext{{stroke:#06c;stroke-width:0.6}}.axis{{stroke:#999;stroke-dasharray:12 4 2 4}}
.t{{font:13px sans-serif;fill:#06c}}.n{{font:13px sans-serif}}.title{{font:bold 18px sans-serif}}</style>
<rect x="5" y="5" width="890" height="610" fill="#fff" stroke="#000"/>
<text x="40" y="40" class="title">AeroMet V1 — JGS1 fused-silica flanged UV dome (crown lid window)</text>
<text x="40" y="62" class="n">Half section (right), outline (left). All dimensions in mm. Scale ~9:1. Generated by mechanical/scripts/drawings.py.</text>
{body}{dims}{notes_svg}
</svg>
'''


def export_drawings(out_dir: Path) -> None:
    out = Path(out_dir) / "drawings"
    out.mkdir(parents=True, exist_ok=True)
    (out / "jgs1_dome.svg").write_text(dome_svg())
    print(f"Exported drawings/jgs1_dome.svg -> {out}")


if __name__ == "__main__":
    export_drawings(Path(__file__).resolve().parents[1] / "build" / "v1")
