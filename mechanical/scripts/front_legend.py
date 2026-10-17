"""Top-Crown compass rose & cardinal N/E/S/W legend artwork (`front_legend.svg/.pdf`)."""

from dataclasses import dataclass
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import enclosure_features as ef
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, PathPatch
from matplotlib.textpath import TextPath
from matplotlib.transforms import Affine2D

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "mechanical/build" / ef.VARIANT
INK = "#000000"
GUIDE = "#00acc1"
FONT = {"family": "DejaVu Sans", "weight": "bold"}


@dataclass
class Label:
    text: str
    x: float
    z: float
    height: float

    def path(self):
        raw = TextPath((0, 0), self.text, size=self.height, prop=FONT)
        ext = raw.get_extents()
        return raw.transformed(Affine2D().translate(self.x - ext.x0 - ext.width / 2.0,
                                                    self.z - ext.y0 - ext.height / 2.0))

    def box(self):
        ext = self.path().get_extents()
        return ext.x0, ext.y0, ext.x1, ext.y1


def labels():
    return [
        Label("N", 0.0, 31.0, 4.2),
        Label("S", 0.0, -31.0, 3.6),
        Label("E", 31.0, 0.0, 3.6),
        Label("W", -31.0, 0.0, 3.6),
        Label("AEROMET V1", 0.0, -17.0, 2.6),
        Label("ULTRASONIC", 0.0, 17.0, 2.2),
    ]


def render_legend():
    fig, ax = plt.subplots(figsize=(6, 6))
    ax.add_patch(Circle((0, 0), ef.OUTER_R, fill=False, ec=GUIDE, lw=1.2))
    win = ef.uv_window()
    ax.add_patch(Circle((win["x"], win["z"]), win["diameter"] / 2.0, fill=False, ec=GUIDE, lw=1.0))
    for lbl in labels():
        ax.add_patch(PathPatch(lbl.path(), facecolor=INK, edgecolor="none"))
    ax.set_xlim(-ef.OUTER_R - 2, ef.OUTER_R + 2)
    ax.set_ylim(-ef.OUTER_R - 2, ef.OUTER_R + 2)
    ax.set_aspect("equal")
    ax.axis("off")
    return fig


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    fig = render_legend()
    fig.savefig(OUT / "front_legend.svg", bbox_inches="tight")
    fig.savefig(OUT / "front_legend.pdf", bbox_inches="tight")
    plt.close(fig)
    print("front_legend.svg and front_legend.pdf written")


if __name__ == "__main__":
    main()
