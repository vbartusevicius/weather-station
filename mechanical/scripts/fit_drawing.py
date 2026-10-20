"""Fit drawings (`fit_side.svg` and `fit_top.svg`) verifying all 3 boards (`main`, `crown`, `probe`),
ultrasonic transducers, piezo rain disc, SEN55 PM module, and 4 airfoil struts inside the cylindrical
AeroMet roof weather station enclosure."""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import enclosure_features as ef
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Polygon, Rectangle

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "mechanical/build" / ef.VARIANT
COLOURS = {"main": "#2e7d32", "crown": "#1565c0", "reflector": "#ad1457", "dome": "#6a1b9a", "probe": "#ef6c00"}
LABELLED = {
    "main": {"J1", "J2", "J3", "J4", "C1", "U3", "U7", "L2", "SW1", "SEN55_MOD"},
    "crown": {"J1", "J2", "J3", "J4", "J5", "J6", "J7", "J8", "J9", "J10", "U2", "U5", "U6", "XD_N", "XD_S", "PIEZO_NE", "PIEZO_SW"},
    "reflector": {"J1", "R1", "R2", "R3", "R4", "TH1"},
    "dome": {"J1", "U1", "U2", "U3"},
    "probe": {"J1", "J2", "U1", "U2", "U3", "R1", "R2"},
}


def rect(ax, x, y, w, h, **kw):
    ax.add_patch(Rectangle((x, y), w, h, **kw))


def side_view():
    """Vertical cross-section (x vs y) through the full 198 mm height of the station."""
    fig, ax = plt.subplots(figsize=(8, 12))
    r = ef.OUTER_R
    mr = ef.MAST_OD / 2.0
    mir = ef.MAST_ID / 2.0

    # Mast socket + lower electronics bay outline
    rect(ax, -mr, 0, 2 * mr, 32, fill=True, fc="#eceff1", ec="black", lw=1.5)
    rect(ax, -mir, 0, 2 * mir, 30, fill=True, fc="white", ec="grey", ls="--", lw=1.0)
    rect(ax, -r, 34, 2 * r, 34, fill=True, fc="#f5f5f5", ec="black", lw=1.8)

    # 5-plate passive Stevenson pagoda outline
    for py in ef.SPEC["envelope"]["pagoda_shield"]["plate_ys"]:
        pts = [(-r, py - 3.5), (-18, py + 2.5), (18, py + 2.5), (r, py - 3.5)]
        ax.add_patch(Polygon(pts, closed=True, fc="#eeeeee", ec="black", lw=1.2, alpha=0.7))

    # Heated reflector cone + 4 airfoil struts
    refl = [(-r, 126.0), (0, 130.0), (r, 126.0)]
    ax.add_patch(Polygon(refl, closed=True, fc="#cfd8dc", ec="black", lw=1.5))
    for st in ef.strut_centres():
        rect(ax, st["x"] - 3.5, 126.0, 7.0, 48.0, fc="#e0e0e0", ec="black", lw=1.0, alpha=0.5)

    # Top ultrasonic shroud & sloped rain/UV crown dome
    crown_pts = [(-r, 174.0), (-r, 190.0), (-4, 198.0), (4, 198.0), (r, 190.0), (r, 174.0)]
    ax.add_patch(Polygon(crown_pts, closed=True, fc="#f5f5f5", ec="black", lw=1.8))

    # Board slabs and part bounding boxes
    boxes = ef.part_boxes()
    for board in ("main", "probe", "reflector", "crown", "dome"):
        c = COLOURS[board]
        for slab in ef.board_slabs(board):
            rect(ax, slab["x0"], slab["y0"], slab["x1"] - slab["x0"], slab["y1"] - slab["y0"],
                 fc=c, ec=c, alpha=0.85)
        for b in [p for p in boxes if p["board"] == board]:
            ref = b["ref"]
            x, y, bw, bh = b["x0"], b["y0"], b["x1"] - b["x0"], b["y1"] - b["y0"]
            rect(ax, x, y, bw, bh, fc=c, ec=c, alpha=0.22, lw=0.7)
            if ref in LABELLED[board]:
                ax.text(x + bw / 2, y + bh / 2, f"{board}:{ref}", fontsize=5.5,
                        ha="center", va="center", color=c, weight="bold")

    ax.set_xlim(-r - 8, r + 8)
    ax.set_ylim(-5, ef.TOTAL_H + 8)
    ax.set_aspect("equal")
    ax.set_title("AeroMet V1 Vertical Cross-Section (x vs y): Mast Clamp, Main Bay,\n"
                 "5-Plate Stevenson Pagoda (`probe`), Heated Wind Tunnel & Top Crown (`crown`)", fontsize=9)
    ax.set_xlabel("x [mm] (West -> East)")
    ax.set_ylabel("y [mm] (Height above mast clamp base)")
    ax.grid(alpha=0.25)
    return fig


def top_view():
    """Plan view (x vs z) showing concentric cylinders, 4 airfoil struts at 45/135/225/315 deg,
    4 cardinal ultrasonic transducers (N/E/S/W), UV window, and PCB footprints."""
    fig, ax = plt.subplots(figsize=(9, 9))
    ax.add_patch(Circle((0, 0), ef.OUTER_R, fill=True, fc="#fafafa", ec="black", lw=2.0))
    ax.add_patch(Circle((0, 0), ef.OUTER_R - ef.WALL, fill=False, ec="grey", ls="--", lw=1.0))

    # 4 airfoil struts (intercardinal diagonals)
    for st in ef.strut_centres():
        ax.add_patch(Circle((st["x"], st["z"]), 4.2, fc="#b0bec5", ec="black", lw=1.2))
        ax.text(st["x"], st["z"], f"Strut\n{st['deg']}°", fontsize=6, ha="center", va="center")

    # 4 cardinal 40 kHz Ultrasonic Transducers (N, E, S, W at r=21 mm)
    for tp in ef.transducer_pockets():
        ax.add_patch(Circle((tp["x"], tp["z"]), 5.2, fc="#ff7043", alpha=0.55, ec="#d84315", lw=1.5))
        ax.text(tp["x"], tp["z"], f"40kHz\n{tp['name']}", fontsize=6.5, ha="center", va="center", weight="bold")

    # Board outlines and parts in XZ plane
    boxes = ef.part_boxes()
    for board in ("main", "crown", "reflector", "dome", "probe"):
        c = COLOURS[board]
        for slab in ef.board_slabs(board):
            rect(ax, slab["x0"], slab["z0"], slab["x1"] - slab["x0"], slab["z1"] - slab["z0"],
                 fill=False, ec=c, ls="--", lw=1.4)
        for b in [p for p in boxes if p["board"] == board and p["ref"] in LABELLED[board]]:
            x, z, bw, bz = b["x0"], b["z0"], b["x1"] - b["x0"], b["z1"] - b["z0"]
            rect(ax, x, z, bw, bz, fill=False, ec=c, lw=0.8)

    win = ef.uv_window()
    ax.add_patch(Circle((win["x"], win["z"]), win["diameter"] / 2, fc="#ffee58", alpha=0.6, ec="#f57f17", lw=1.5))
    ax.text(win["x"], win["z"], "UV/ALS", fontsize=6, ha="center", va="center")

    ax.set_xlim(-ef.OUTER_R - 6, ef.OUTER_R + 6)
    ax.set_ylim(-ef.OUTER_R - 6, ef.OUTER_R + 6)
    ax.set_aspect("equal")
    ax.set_title("AeroMet V1 Plan View (x vs z): 4x Diagonal Airfoil Struts,\n"
                 "4x Cardinal N/E/S/W Ultrasonic Transducers, UV Window & PCB Envelopes", fontsize=9)
    ax.set_xlabel("x [mm] (West -> East)")
    ax.set_ylabel("z [mm] (South -> North)")
    ax.grid(alpha=0.25)
    return fig


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, fig in (("fit_side", side_view()), ("fit_front", top_view())):
        fig.savefig(OUT / f"{name}.svg", bbox_inches="tight")
        plt.close(fig)
        print(f"{name}.svg written")


if __name__ == "__main__":
    main()
