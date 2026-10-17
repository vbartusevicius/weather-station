"""Enclosure feature positions taken from the real board layouts (`main`, `crown`, `probe`).

Coordinate system (installed vertical roof cylinder):
  x = West (-) -> East (+), centred at 0.0 (-48.0 .. +48.0 mm)
  y = Vertical height from bottom of mast socket (0.0) -> top of rain/UV dome (198.0 mm)
  z = South (-) -> North (+), centred at 0.0 (-48.0 .. +48.0 mm)
"""

import json
import math
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

SPEC = load_spec()
VARIANT = SPEC["active_variant"]
ENV = SPEC["envelope"]
WALL = SPEC["enclosure"]["wall"]

OUTER_D = ENV["outer_diameter"]
OUTER_R = ENV["outer_radius"]
TOTAL_H = ENV["total_height"]

MAST_Y0 = ENV["mast_socket"]["y_start"]
MAST_Y1 = ENV["mast_socket"]["y_end"]
MAST_OD = ENV["mast_socket"]["outer_diameter"]
MAST_ID = ENV["mast_socket"]["inner_diameter"]

BASE_Y0 = ENV["base_bay"]["y_start"]
BASE_Y1 = ENV["base_bay"]["y_end"]
PAGODA_Y0 = ENV["pagoda_shield"]["y_start"]
PAGODA_Y1 = ENV["pagoda_shield"]["y_end"]
TUNNEL_Y0 = ENV["wind_tunnel"]["y_start"]
TUNNEL_Y1 = ENV["wind_tunnel"]["y_end"]
CROWN_Y0 = ENV["top_crown"]["y_start"]
CROWN_Y1 = ENV["top_crown"]["y_end"]

MAIN_W = SPEC["boards"]["main"]["max_outline"]["width"]
MAIN_H = SPEC["boards"]["main"]["max_outline"]["height"]
CROWN_W = SPEC["boards"]["crown"]["max_outline"]["width"]
CROWN_H = SPEC["boards"]["crown"]["max_outline"]["height"]
PROBE_W = SPEC["boards"]["probe"]["max_outline"]["width"]
PROBE_H = SPEC["boards"]["probe"]["max_outline"]["height"]


def components(board):
    path = REPO / "hardware/build" / VARIANT / board / f"components_{board}.json"
    return {c["ref"]: c for c in json.loads(path.read_text())}


def layout_plan(board):
    return yaml.safe_load((REPO / f"hardware/circuits/variants/{VARIANT}/layout_{board}.yaml").read_text())


def board_point(board, x, y, n=0.0):
    """Map board coordinates (x, y) + normal offset n (+n = front/top, -n = back/bottom)
    into 3D enclosure (enc_x, enc_y, enc_z) coordinates."""
    cfg = SPEC["boards"][board]
    plane = cfg["plane"]
    w = cfg["max_outline"]["width"]
    h = cfg["max_outline"]["height"]
    if plane == "horizontal-xz":
        return x - w / 2.0, cfg["y"] + n, y - h / 2.0
    if plane == "vertical-xy":
        return x - w / 2.0, cfg["y_start"] + y, cfg["z"] + n
    raise ValueError(f"Unsupported board plane: {plane}")


def board_to_enc(board, x, y):
    ex, ey, ez = board_point(board, x, y)
    return (ex, ez) if SPEC["boards"][board]["plane"] == "horizontal-xz" else (ex, ey)


def span_box(a, b, **extra):
    return {**extra, "x0": min(a[0], b[0]), "x1": max(a[0], b[0]),
            "y0": min(a[1], b[1]), "y1": max(a[1], b[1]),
            "z0": min(a[2], b[2]), "z1": max(a[2], b[2])}


def board_slab(board):
    cfg = SPEC["boards"][board]
    w, h = cfg["max_outline"]["width"], cfg["max_outline"]["height"]
    return span_box(board_point(board, 0, 0, -cfg["thickness"]),
                    board_point(board, w, h, 0.0), board=board)


def board_slabs(board):
    return [board_slab(board)]


def bbox_centre(comp):
    b = comp["bbox"]
    return (b["x0"] + b["x1"]) / 2.0, (b["y0"] + b["y1"]) / 2.0


def part_boxes():
    """3D bounding boxes of all PCB components plus off-board transducers, piezo disc,
    UV optical window, and SEN55 particulate module."""
    boxes = []
    for board in SPEC["variants"][VARIANT]["boards"]:
        thick = SPEC["boards"][board]["thickness"]
        for ref, c in components(board).items():
            h = c["height"]
            n0, n1 = (-thick - h, -thick) if c["side"] == "back" else (0.0, h)
            b = c["bbox"]
            boxes.append(span_box(
                board_point(board, b["x0"], b["y0"], n0),
                board_point(board, b["x1"], b["y1"], n1),
                board=board, ref=ref, side=c["side"], footprint=c["footprint"],
            ))
    # Off-board 4x 40 kHz Ultrasonic Transducers (N, E, S, W at r=21 mm, flush with y=174.0 ceiling)
    tr_r = ENV["wind_tunnel"]["transducer_radius"]
    for name, deg in ENV["wind_tunnel"]["transducer_angles_deg"].items():
        rad = math.radians(deg)
        tx, tz = tr_r * math.cos(rad), tr_r * math.sin(rad)
        boxes.append({
            "board": "crown", "ref": f"XD_{name}", "side": "back", "footprint": "ULTRASONIC_10MM",
            "x0": tx - 5.0, "x1": tx + 5.0, "y0": TUNNEL_Y1, "y1": TUNNEL_Y1 + 6.6,
            "z0": tz - 5.0, "z1": tz + 5.0,
        })
    # Off-board 27 mm Piezoelectric Rain Disc bonded to underside of top rain dome (y=191.2..192.0)
    boxes.append({
        "board": "crown", "ref": "PIEZO_DISC", "side": "front", "footprint": "PIEZO_27MM",
        "x0": -13.5, "x1": 13.5, "y0": 191.2, "y1": 192.0, "z0": -13.5, "z1": 13.5,
    })
    # Off-board Sensirion SEN55-SDN-T PM sensor in lower base bay (below/beside main board)
    boxes.append({
        "board": "main", "ref": "SEN55_MOD", "side": "back", "footprint": "SEN55_MODULE",
        "x0": -20.0, "x1": 20.0, "y0": 34.2, "y1": 41.2, "z0": -11.0, "z1": 11.0,
    })
    return boxes


def strut_centres():
    r = ENV["wind_tunnel"]["strut_radius"]
    pts = []
    for deg in ENV["wind_tunnel"]["strut_angles_deg"]:
        rad = math.radians(deg)
        pts.append({"deg": deg, "x": round(r * math.cos(rad), 3), "z": round(r * math.sin(rad), 3)})
    return pts


def transducer_pockets():
    r = ENV["wind_tunnel"]["transducer_radius"]
    pts = []
    for name, deg in ENV["wind_tunnel"]["transducer_angles_deg"].items():
        rad = math.radians(deg)
        pts.append({"name": name, "deg": deg, "x": round(r * math.cos(rad), 3), "z": round(r * math.sin(rad), 3)})
    return pts


def uv_window():
    u6 = components("crown")["U6"]
    u7 = components("crown")["U7"]
    x6, _, z6 = board_point("crown", u6["x"], u6["y"])
    x7, _, z7 = board_point("crown", u7["x"], u7["y"])
    return {
        "x": round((x6 + x7) / 2.0, 3),
        "z": round((z6 + z7) / 2.0, 3),
        "diameter": ENV["top_crown"]["uv_window_diameter"],
    }


def led_windows():
    main = components("main")
    out = []
    for ref in ("D4", "D5"):
        x, y, z = board_point("main", main[ref]["x"], main[ref]["y"])
        out.append({"ref": ref, "x": round(x, 3), "y": round(y, 3), "z": round(z, 3)})
    return out


def standoffs(board):
    """4 corner mounting bosses per horizontal board (inset 2.5 mm from corners)."""
    cfg = SPEC["boards"][board]
    w, h = cfg["max_outline"]["width"], cfg["max_outline"]["height"]
    pts = []
    for bx in (2.2, w - 2.2):
        for by in (2.2, h - 2.2):
            ex, ey, ez = board_point(board, bx, by)
            pts.append({"x": round(ex, 3), "y": round(ey, 3), "z": round(ez, 3), "bx": bx, "by": by})
    return pts
