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
RAISE = ENV.get("above_base_raise", 0.0)   # parts above the base are placed this much higher
BASE_BOARDS = ("main", "power")

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
    into 3D enclosure (enc_x, enc_y, enc_z) coordinates (supporting optional `rot_deg` in XZ)."""
    cfg = SPEC["boards"][board]
    plane = cfg["plane"]
    w = cfg["max_outline"]["width"]
    h = cfg["max_outline"]["height"]
    if plane == "horizontal-xz":
        u = x - w / 2.0
        v = y - h / 2.0
        rot_rad = math.radians(cfg.get("rot_deg", 0.0))
        rx = u * math.cos(rot_rad) - v * math.sin(rot_rad)
        rz = u * math.sin(rot_rad) + v * math.cos(rot_rad)
        lift = 0.0 if board in BASE_BOARDS else RAISE
        return (rx + cfg.get("x_offset", 0.0),
                cfg["y"] + n + lift,
                rz + cfg.get("z_offset", 0.0))
    if plane == "vertical-xy":
        return x - w / 2.0, cfg["y_start"] + y + RAISE, cfg["z"] + n
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


# Plantower PMSA003I particulate module (38.0 x 35.0 x 11.8 mm) lying flat, inlet/fan face DOWN, on the left half of the
# base bay beside the 32 x 52 mm `main` board (Plantower: inlet/outlet face against the host plane). It rests 6 mm above
# the floor on two sealed duct chambers (1.5 mm closed-cell foam gasket compressed to 1.0 mm by two snap hooks): the
# fan chamber exhausts through floor slits at the rear-left, the inlet chamber draws through slits at the front-left.
# The clamp flange band (z = -8..+8 under the floor) separates the two exits, so exhaust never loops into the inlet.
# Seen from below with the openings edge outward (x = -37), the fan sits at +z and the 3x3 inlet grid at -z.
PM = {
    "x0": -37.0, "x1": -2.0, "z_half": 19.0,         # 35.0 (x) x 38.0 (z) footprint; openings along the x = -37 edge
    "face_y": 45.5, "top_y": 57.3,                   # 11.8 mm tall; face 11.0 mm above the y = 34.5 bay floor (tall ducts)
    "floor_y": 34.5, "wall": 1.2, "gasket": 1.0,     # chamber walls end 1.0 mm below the face (compressed gasket)
    "fan": {"x0": -35.8, "x1": -15.6, "z0": -3.0, "z1": 17.8},     # chamber around the Ø 19 mm fan (centre -26, +8.5)
    "inlet": {"x0": -34.6, "x1": -26.4, "z0": -21.0, "z1": -4.6},  # inlet grid (centre -30.5, -10.6) + roofed intake duct
    "inlet_roof": {"z0": -22.2, "z1": -17.8},                       # roof over the duct part outside the module face
    "fan_slits": {"xs": [-35.0, -32.6, -30.2, -27.8, -25.4, -23.0, -20.6], "z0": 9.8, "z1": 17.4},
    "inlet_slits": {"xs": [-33.6, -31.2, -28.8], "z0": -20.6, "z1": -9.8},
    "slit_w": 1.2,
    "hooks_x": (-24.0, -16.0), "hook_t": 1.6, "hook_lip": 1.0,    # snap hooks over both z ends of the module
    "pads": [(-5.0, -12.0), (-5.0, 12.0)],                          # hard stops under the inner edge (= face height)
}


# Snap clips holding the horizontal base-bay boards: (enc x, enc z, hook direction toward the board). Each sits just off
# a board edge, >= 1 mm clear of the bay wall so its 1.5 mm post can flex outward while the board is pressed in, and
# where the hook (reaching 1.1 mm over the board edge) lands on bare board. 4 per board + the 4 support posts.
CLIP_REACH = 1.1
CLIPS = {
    "main": [(21.5, -26.9, (0, 1)), (7.5, 26.9, (0, -1)), (32.4, -22.0, (-1, 0)), (-1.4, 22.0, (1, 0))],
    "power": [(36.0, -14.9, (0, 1)), (36.0, 16.9, (0, -1)), (8.6, -13.0, (1, 0)), (8.6, 15.0, (1, 0))],
}


def part_boxes():
    """3D bounding boxes of all PCB components plus off-board transducers, piezo disc,
    UV optical window, and the PMSA003I particulate module."""
    boxes = []
    for board in SPEC["variants"][VARIANT]["boards"]:
        thick = SPEC["boards"][board]["thickness"]
        for ref, c in components(board).items():
            h = c["height"]
            n0, n1 = (-thick - h, -thick) if c["side"] == "back" else (0.0, h)
            b = c["bbox"]
            corners = [board_point(board, bx, by, n)
                       for bx in (b["x0"], b["x1"])
                       for by in (b["y0"], b["y1"])
                       for n in (n0, n1)]
            boxes.append({
                "board": board, "ref": ref, "side": c["side"], "footprint": c["footprint"],
                "x0": min(p[0] for p in corners), "x1": max(p[0] for p in corners),
                "y0": min(p[1] for p in corners), "y1": max(p[1] for p in corners),
                "z0": min(p[2] for p in corners), "z1": max(p[2] for p in corners),
            })
    # Off-board 4x 40 kHz Ultrasonic Transducers (N, E, S, W at r=17.5 mm, flush from y=174.0 ceiling up to y=181.6 underside of `crown` PCB!)
    tr_r = ENV["wind_tunnel"]["transducer_radius"]
    crown_bot_y = SPEC["boards"]["crown"]["y"] - SPEC["boards"]["crown"]["thickness"] + RAISE
    for name, deg in ENV["wind_tunnel"]["transducer_angles_deg"].items():
        rad = math.radians(deg)
        tx, tz = tr_r * math.cos(rad), tr_r * math.sin(rad)
        boxes.append({
            "board": "crown", "ref": f"XD_{name}", "side": "back", "footprint": "ULTRASONIC_10MM",
            "x0": tx - 5.0, "x1": tx + 5.0, "y0": TUNNEL_Y1 + RAISE, "y1": crown_bot_y,
            "z0": tz - 5.0, "z1": tz + 5.0,
        })
    # Off-board Plantower PMSA003I particulate module lying face-down on its duct chambers (left half of the bay)
    boxes.append({
        "board": "pm", "ref": "PM_MODULE", "side": "front", "footprint": "PMSA003I_MODULE",
        "x0": PM["x0"], "x1": PM["x1"], "y0": PM["face_y"], "y1": PM["top_y"], "z0": -PM["z_half"], "z1": PM["z_half"],
    })
    # Off-board 4x 20 mm Murata Piezoelectric Rain Discs (7BB-20-6L0) glued DIRECTLY to the 1.0 mm-thin
    # acoustic rain-drum diaphragm pockets (r = 25.0 mm at 45/135/225/315 deg, y = 193.2..193.8 mm)
    for deg, quad in ((45, "NE"), (135, "NW"), (225, "SW"), (315, "SE")):
        rad = math.radians(deg)
        px, pz = 25.0 * math.cos(rad), 25.0 * math.sin(rad)
        boxes.append({
            "board": "crown", "ref": f"PIEZO_{quad}", "side": "front", "footprint": "PIEZO_20MM",
            "x0": px - 10.0, "x1": px + 10.0, "y0": 193.2 + RAISE, "y1": 193.8 + RAISE,
            "z0": pz - 10.0, "z1": pz + 10.0,
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
    cx, _, cz = board_point("dome", 6.8, 6.8)
    return {
        "x": round(cx, 3),
        "z": round(cz, 3),
        "diameter": 18.0,
    }


def standoffs(board):
    """4 mounting bosses per horizontal board."""
    cfg = SPEC["boards"][board]
    w, h = cfg["max_outline"]["width"], cfg["max_outline"]["height"]
    if board == "crown":
        local_pts = [(5.5, 2.0), (w - 5.5, 2.0), (5.5, h - 2.0), (w - 5.5, h - 2.0)]
        pts = []
        for bx, by in local_pts:
            ex, ey, ez = board_point(board, bx, by)
            pts.append({"x": round(ex, 3), "y": round(ey, 3), "z": round(ez, 3), "bx": bx, "by": by})
        return pts
    pts = []
    for bx in (2.2, w - 2.2):
        for by in (2.2, h - 2.2):
            ex, ey, ez = board_point(board, bx, by)
            pts.append({"x": round(ex, 3), "y": round(ey, 3), "z": round(ez, 3), "bx": bx, "by": by})
    return pts
