"""Offscreen VTK 3D renders and full assembly STEP of the AeroMet Roof Weather Station
(`base`, `pagoda`, `crown`, `main`, `crown` PCB, `probe` PCB, and realistic 3D components).

Writes mechanical/build/<variant>/assembly.step and renders/*.png:
  - iso.png, front.png, back.png, inside.png, section.png, exploded.png
  - main_top.png, main_bottom.png, crown_top.png, crown_bottom.png, probe_top.png, probe_bottom.png
"""

import math
import sys
from pathlib import Path

import cadquery as cq
import vtk

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be
import enclosure_features as ef
from build_enclosure import box, cyl_y

OUT = REPO / "mechanical/build" / ef.VARIANT
RENDERS = OUT / "renders"

CASE_BASE = (0.86, 0.88, 0.90)
CASE_PAGODA = (0.93, 0.94, 0.96)
CASE_CROWN = (0.90, 0.92, 0.95)
SILK = (0.10, 0.12, 0.15)
PCB_MAIN = (0.10, 0.48, 0.22)
PCB_CROWN = (0.12, 0.34, 0.68)
PCB_REFLECTOR = (0.58, 0.16, 0.32)
PCB_DOME = (0.48, 0.22, 0.68)
PCB_PROBE = (0.16, 0.42, 0.36)
PART = (0.18, 0.18, 0.20)
SILVER = (0.78, 0.80, 0.83)
ALU = (0.82, 0.84, 0.88)
GOLD = (0.86, 0.72, 0.24)
BRASS = (0.85, 0.68, 0.22)
CERAMIC = (0.72, 0.54, 0.32)
IVORY = (0.92, 0.90, 0.84)
GREEN_TERM = (0.12, 0.58, 0.28)
ORANGE_BTN = (0.95, 0.45, 0.10)
GLASS_UV = (0.35, 0.65, 0.92)


def board_plates():
    colours = {"main": PCB_MAIN, "crown": PCB_CROWN, "reflector": PCB_REFLECTOR, "dome": PCB_DOME, "probe": PCB_PROBE}
    plates = []
    for board in ef.SPEC["variants"][ef.VARIANT]["boards"]:
        cfg = ef.SPEC["boards"][board]
        s = ef.board_slab(board)
        if board == "dome":
            cx, cz = (s["x0"] + s["x1"]) / 2.0, (s["z0"] + s["z1"]) / 2.0
            plates.append((cyl_y(cx, cz, s["y0"], s["y1"], s["x1"] - s["x0"]), colours[board], board))
        elif cfg.get("shape") == "octagon":
            # Compact 34.0 x 34.0 mm Square-Chamfered `crown` PCB (`x = -17..+17, z = -17..+17` with 4.5 mm 45-deg corner chamfers!)
            pts = [(-12.5, -17.0), (12.5, -17.0), (17.0, -12.5), (17.0, 12.5),
                   (12.5, 17.0), (-12.5, 17.0), (-17.0, 12.5), (-17.0, -12.5)]
            oct_slab = cq.Workplane("XZ", origin=(0, s["y0"], 0)).polyline(pts).close().extrude(-(s["y1"] - s["y0"]))
            plates.append((oct_slab, colours[board], board))
        else:
            plates.append((box(s["x0"], s["x1"], s["y0"], s["y1"], s["z0"], s["z1"]), colours[board], board))
    return plates


def realistic_part(b):
    fp = b.get("footprint", "")
    x0, x1, y0, y1, z0, z1 = b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"]
    cx, cy, cz = (x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0

    if fp == "ULTRASONIC_10MM":
        tilt_deg = ef.SPEC["envelope"]["wind_tunnel"].get("transducer_tilt_deg", 29.0)
        # Rotate around `axis = (cz, 0, -cx)` by `+tilt_deg` (29.0 deg) so the transducer face tilts INWARD (`V-beam`)!
        # Seats against the 1.0+ mm bottom resting shoulder (`s = -1.80 mm`) with the Press-Fit Rear Retaining Ring behind it!
        axis = (cz, 0, -cx) if (cx != 0 or cz != 0) else (1, 0, 0)
        can = be.cone_y(-1.75, 3.4, 5.0, 5.0).rotate((0, 0, 0), axis, tilt_deg).translate((cx, 176.2, cz))
        face = be.cone_y(-1.80, -1.3, 4.3, 4.3).rotate((0, 0, 0), axis, tilt_deg).translate((cx, 176.2, cz))
        ret_ring = be.ring_cone_y(3.4, 4.8, 3.0, 5.2, 3.0, 5.2).rotate((0, 0, 0), axis, tilt_deg).translate((cx, 176.2, cz))
        pigtail = be.cone_y(3.4, 5.8, 1.2, 1.2).rotate((0, 0, 0), axis, tilt_deg).translate((cx, 176.2, cz))
        return [(can, ALU), (face, (0.25, 0.28, 0.32)), (ret_ring, (0.94, 0.45, 0.22)), (pigtail, GOLD)]
    if fp in ("PIEZO_27MM", "PIEZO_20MM"):
        d = 20.0 if fp == "PIEZO_20MM" else 27.0
        brass = cyl_y(cx, cz, y0, y0 + 0.4, d)
        ceramic = cyl_y(cx, cz, y0 - 0.3, y0, d * 0.74)
        return [(brass, BRASS), (ceramic, IVORY)]
    if fp == "SEN55_MODULE":
        housing = box(x0, x1, y0, y1, z0, z1)
        label_top = box(x0 + 3.0, x1 - 3.0, y1 - 0.2, y1 + 0.05, z0 + 2.0, z1 - 2.0)
        return [(housing, (0.92, 0.92, 0.94)), (label_top, (0.20, 0.55, 0.30))]
    if "KF142R" in fp:
        housing = box(x0, x1, y0, y1, z0, z1)
        btns = [box(cx - 2.54 - 0.9, cx - 2.54 + 0.9, y1 - 1.5, y1 + 0.2, z0 + 1.5, z0 + 4.5),
                box(cx + 2.54 - 0.9, cx + 2.54 + 0.9, y1 - 1.5, y1 + 0.2, z0 + 1.5, z0 + 4.5)]
        return [(housing, GREEN_TERM)] + [(bt, ORANGE_BTN) for bt in btns]
    if "CP_Elec" in fp:
        base_p = box(x0 + 0.5, x1 - 0.5, y0, y0 + 1.8, z0 + 0.5, z1 - 0.5)
        can = cyl_y(cx, cz, y0 + 1.8, y1, 9.6)
        return [(base_p, PART), (can, ALU)]
    if "XIAO_ESP32C6" in fp:
        sock1 = box(x0 + 0.5, x0 + 3.0, y0, y1 - 3.5, z0 + 1.5, z1 - 1.5)
        sock2 = box(x1 - 3.0, x1 - 0.5, y0, y1 - 3.5, z0 + 1.5, z1 - 1.5)
        pcb = box(x0 + 0.4, x1 - 0.4, y1 - 3.5, y1 - 2.1, z0 + 0.5, z1 - 0.5)
        shield = box(cx - 5.2, cx + 5.2, y1 - 2.1, y1 - 0.3, cz - 5.0, cz + 5.0)
        usbc = box(cx - 4.4, cx + 4.4, y1 - 2.1, y1 + 0.2, z0 - 0.5, z0 + 5.5)
        return [(sock1, PART), (sock2, PART), (pcb, (0.08, 0.32, 0.16)), (shield, SILVER), (usbc, ALU)]
    if "SCD41" in fp:
        body = box(x0 + 0.3, x1 - 0.3, y0 + 0.3, y1 - 0.3, z0, z1)
        membrane = box(cx - 2.5, cx + 2.5, cy - 2.5, cy + 2.5, z1 - 0.2, z1 + 0.05)
        return [(body, GOLD), (membrane, IVORY)]
    if "JST" in fp or "BM10B" in fp or "BM02B" in fp or "B6B" in fp:
        return [(box(x0 + 0.15, x1 - 0.15, y0, y1, z0 + 0.15, z1 - 0.15), IVORY)]
    if "HX_PM2.54-1x4P-RA-SMD" in fp:
        # Right-angle 1x4 2.54 mm female socket on `probe J2` facing straight UP (+Y) to y = 120.1 mm to mate flush with `reflector J1`!
        housing = box(-5.08, 5.08, 114.2, 120.1, 0.6, 3.1)
        return [(housing, PART)]
    if "HX_PZ2.54-1x4P-SMD" in fp and b.get("board") == "reflector":
        # Vertical 1x4 2.54 mm male header on underside of `reflector J1` (insulator at y = 120.1..122.6, 4 gold pins down to y = 115.2)
        insulator = box(-5.08, 5.08, 120.1, 122.6, 0.6, 3.1)
        pins = [box(px - 0.32, px + 0.32, 115.2, 122.6, 1.53, 2.17) for px in (-3.81, -1.27, 1.27, 3.81)]
        return [(insulator, PART)] + [(p, GOLD) for p in pins]
    if "LED_SK6805" in fp:
        return [(box(x0, x1, y0, y1, z0, z1), (0.10, 0.88, 0.32))]
    if fp.startswith("C_"):
        return [(box(x0, x1, y0, y1, z0, z1), CERAMIC)]
    if fp.startswith("L_"):
        return [(box(x0, x1, y0, y1, z0, z1), (0.26, 0.27, 0.30))]
    return [(box(x0, x1, y0, y1, z0, z1), PART)]


def part_solids():
    solids = []
    for b in ef.part_boxes():
        for shape, rgb in realistic_part(b):
            solids.append((shape, rgb, b["board"], b["ref"]))
    return solids


def actor(shape, rgb, opacity):
    vertices, faces = shape.val().tessellate(0.15, 0.25)
    points = vtk.vtkPoints()
    for v in vertices:
        points.InsertNextPoint(v.x, v.y, v.z)
    polys = vtk.vtkCellArray()
    for face in faces:
        polys.InsertNextCell(3, face)
    data = vtk.vtkPolyData()
    data.SetPoints(points)
    data.SetPolys(polys)
    normals = vtk.vtkPolyDataNormals()
    normals.SetInputData(data)
    normals.SetFeatureAngle(35)
    normals.AutoOrientNormalsOn()
    normals.ConsistencyOn()
    mapper = vtk.vtkPolyDataMapper()
    mapper.SetInputConnection(normals.GetOutputPort())
    a = vtk.vtkActor()
    a.SetMapper(mapper)
    a.GetProperty().SetColor(*rgb)
    a.GetProperty().SetOpacity(opacity)
    a.GetProperty().SetAmbient(0.28)
    a.GetProperty().SetDiffuse(0.78)
    a.GetProperty().SetSpecular(0.18)
    return a


def label(text, position):
    a = vtk.vtkBillboardTextActor3D()
    a.SetInput(text)
    a.SetPosition(*position)
    a.GetTextProperty().SetFontSize(15)
    a.GetTextProperty().SetColor(0.85, 0.1, 0.1)
    a.GetTextProperty().SetBold(True)
    return a


def render(name, items, position, focal=(0, 100, 0), up=(0, 1, 0), zoom=1.05, labels=()):
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(1, 1, 1)
    renderer.SetUseDepthPeeling(1)
    renderer.SetMaximumNumberOfPeels(30)
    for shape, rgb, opacity in items:
        renderer.AddActor(actor(shape, rgb, opacity))
    for text, pos in labels:
        renderer.AddActor(label(text, pos))
    window = vtk.vtkRenderWindow()
    window.SetOffScreenRendering(1)
    window.SetAlphaBitPlanes(1)
    window.SetMultiSamples(0)
    window.SetSize(1400, 1400)
    window.AddRenderer(renderer)
    camera = renderer.GetActiveCamera()
    camera.SetPosition(*position)
    camera.SetFocalPoint(*focal)
    camera.SetViewUp(*up)
    renderer.ResetCamera()
    camera.Zoom(zoom)
    window.Render()
    grab = vtk.vtkWindowToImageFilter()
    grab.SetInput(window)
    grab.Update()
    writer = vtk.vtkPNGWriter()
    writer.SetFileName(str(RENDERS / f"{name}.png"))
    writer.SetInputConnection(grab.GetOutputPort())
    writer.Write()
    print(f"{name}.png saved")


STL_COLOURS = {
    "base": (0.36, 0.46, 0.58),               # Slate Blue-Grey
    "mast_clamp": (0.84, 0.54, 0.22),         # Warm Amber-Bronze
    "orings": (0.88, 0.18, 0.24),             # Elastomer Seal Crimson-Red
    "probe_sleeve_back": (0.18, 0.58, 0.62),  # Deep Teal
    "probe_sleeve_front": (0.36, 0.80, 0.72), # Mint Cyan
    "pagoda": (0.70, 0.76, 0.82),             # Titanium Silver-Grey (high contrast against white background!)
    "wind_tunnel": (0.52, 0.60, 0.76),        # Cool Indigo-Silver
    "struts": (0.92, 0.42, 0.28),             # Coral Orange (Double-C Split Threaded Struts & Hex Nuts)
    "crown_base": (0.48, 0.68, 0.86),         # Sky Steel Blue (Lower Ultrasonic & Crown PCB Tray)
    "crown": (0.76, 0.86, 0.95),              # Arctic Ice Blue (Top Piezo Rain & JGS1 UV Dome Lid)
}


def stl_part_views(stl_map, crown_inside_items=(), pagoda_inside_items=()):
    """Render an individual 3/4 isometric view (`<part>.png`) for each modular STL part,
    and for `crown.png` and `pagoda.png` show the zoomed-in cutaway with internal boards, O-rings, and hardware."""
    for name, solid in stl_map.items():
        if name == "crown" and crown_inside_items:
            render("crown", crown_inside_items, (68, 228, 82), focal=(0, 190.5, 0), zoom=2.15)
            continue
        if name == "pagoda" and pagoda_inside_items:
            # Clean, 100% opaque cross-sectional slice zoomed out (zoom=1.32) so the full outer M90 threaded connections on both sides are visible!
            render("pagoda", pagoda_inside_items, (230, 108, 70), focal=(0, 96.5, 0), zoom=1.32)
            continue
        bb = solid.val().BoundingBox()
        cx = (bb.xmin + bb.xmax) / 2.0
        cy = (bb.ymin + bb.ymax) / 2.0
        cz = (bb.zmin + bb.zmax) / 2.0
        span = max(bb.xmax - bb.xmin, bb.ymax - bb.ymin, bb.zmax - bb.zmin, 25.0)
        dist = span * 2.1
        if name == "base":
            cam = (cx + dist * 0.72, cy + dist * 0.92, cz + dist * 0.82)
        elif name == "mast_clamp":
            cam = (cx + dist * 0.75, cy + dist * 0.55, cz - dist * 0.90)
        elif name == "wind_tunnel":
            cam = (cx + dist * 0.75, cy - dist * 0.80, cz + dist * 0.75)
        elif name == "struts":
            # Explode the front-right (45-deg NE) strut's two C-halves and two M12x1.75 hex nuts slightly in `struts.png`
            # and show the central M22x1.5 JGS1 Dome Nut + 4 Slotted Ultrasonic Press-Fit Retaining Rings in the center!
            st0 = ef.strut_centres()[0]
            sx, sz = st0["x"], st0["z"]
            uv_box = box(-24.0, 24.0, 185.0, 196.0, -24.0, 24.0)
            uv_n = solid.intersect(uv_box).translate((0, -40.0, 0))
            struts_only = solid.cut(uv_box)
            ne_box = box(10.0, 50.0, 120.0, 180.0, 10.0, 50.0)
            other_struts = struts_only.cut(ne_box)
            ne_sub = struts_only.intersect(ne_box)
            nut_ring_bot = be.ring_cone_y(121.6, 124.6, 5.10, 9.5, 5.10, 9.5).translate((sx, 0, sz))
            nut_ring_top = be.ring_cone_y(175.6, 178.6, 5.10, 9.5, 5.10, 9.5).translate((sx, 0, sz))
            bot_n = ne_sub.intersect(nut_ring_bot).translate((0, -4.5, 0))
            top_n = ne_sub.intersect(nut_ring_top).translate((0, 4.5, 0))
            c_only = ne_sub.cut(nut_ring_bot).cut(nut_ring_top)
            c_back = c_only.intersect(box(10, 50, 120, 180, 10, sz)).translate((0, 0, -2.0))
            c_front = c_only.intersect(box(10, 50, 120, 180, sz, 50)).translate((0, 0, 2.0))
            render(name, [
                (other_struts, STL_COLOURS[name], 1.0),
                (c_back, STL_COLOURS[name], 1.0),
                (c_front, STL_COLOURS[name], 1.0),
                (bot_n, BRASS, 1.0),
                (top_n, BRASS, 1.0),
                (uv_n, BRASS, 1.0),
            ], (cx + dist * 0.88, cy + dist * 0.48, cz + dist * 0.72), focal=(cx + 4, cy - 5, cz + 4), zoom=1.24)
            continue
        else:
            cam = (cx + dist * 0.85, cy + dist * 0.65, cz + dist * 0.95)
        render(name, [(solid, STL_COLOURS[name], 1.0)], cam, focal=(cx, cy, cz), zoom=1.18)


def shifted(items, dy):
    return [(s.translate((0, dy, 0)), rgb, op) for s, rgb, op in items]


def main():
    RENDERS.mkdir(parents=True, exist_ok=True)
    base = be.build_base()
    mast_clamp = be.build_mast_clamp()
    orings = be.build_orings()
    sleeve_back = be.build_probe_sleeve_back()
    sleeve_front = be.build_probe_sleeve_front()
    pagoda = be.build_pagoda()
    wind_tunnel = be.build_wind_tunnel()
    struts = be.build_struts()
    crown_base = be.build_crown_base()
    crown = be.build_crown()
    win = ef.uv_window()
    # Hollow JGS1 Fused Silica Hemispherical Dome (Ø 18.0 mm OD / Ø 15.6 mm ID, 1.2 mm wall) + O-Ring + Standard ISO M22x1.5 Hex-Flange Retainer Nut:
    uv_flange_shell = be.ring_cone_y(193.0, 194.2, 7.8, 10.35, 7.8, 10.35).translate((win["x"], 0, win["z"]))
    uv_neck_shell = be.ring_cone_y(194.2, 198.0, 7.8, 9.0, 7.8, 9.0).translate((win["x"], 0, win["z"]))
    hemi_outer = (cq.Workplane("XY", origin=(win["x"], 198.0, win["z"]))
                  .sphere(9.0)
                  .intersect(box(-25, 25, 198.0, 209.0, -25, 25)))
    hemi_inner = (cq.Workplane("XY", origin=(win["x"], 198.0, win["z"]))
                  .sphere(7.8)
                  .intersect(box(-25, 25, 197.8, 209.0, -25, 25)))
    uv_lens = uv_flange_shell.union(uv_neck_shell).union(hemi_outer.cut(hemi_inner))
    uv_nut_hex = be.hex_prism_y(189.8, 191.0, across_flats=22.0, cx=win["x"], cz=win["z"]).cut(
        be.cone_y(189.5, 193.5, 8.2, 8.2).translate((win["x"], 0, win["z"])))
    uv_nut_barrel = be.ring_cone_y(190.4, 193.0, 8.2, 10.20, 8.2, 10.20).translate((win["x"], 0, win["z"]))
    uv_retainer_nut = be.add_external_thread_y(uv_nut_hex.union(uv_nut_barrel), (191.2, 192.7),
                                               r_root=10.20, depth=0.70, half_base=0.45, half_crest=0.14,
                                               cx=win["x"], cz=win["z"])
    # Vertical 4-pin 2.54 mm board-to-board riser posts connecting `crown J10` (y=181.5) to round `dome J1` (y=196.0)
    b2b_riser = box(-5.1, 5.1, 181.5, 196.0, -1.25, 1.25)
    # 4x M6x55 mm Stainless Steel Through-Bolts + Bottom M6 Hex Nuts clamping the Pagoda stack through the Ø 6.2 mm bores (at r = 23.0 mm)
    # Top M6 bolt head (y = 117.8..120.2 mm) sits recessed inside the Ø 10.4 mm counterbore well, 12.5 mm radially clear of the r = 35.5 mm strut nuts!
    m4_bolts = None
    for deg in (45, 135, 225, 315):
        rad = math.radians(deg)
        px, pz = 20.5 * math.cos(rad), 20.5 * math.sin(rad)
        shaft = be.cone_y(67.8, 117.8, 2.95, 2.95).translate((px, 0, pz))
        head = be.cone_y(117.8, 120.2, 4.8, 4.8).translate((px, 0, pz))
        nut = be.hex_prism_y(67.8, 70.1, across_flats=9.8, cx=px, cz=pz)
        bolt = shaft.union(head).union(nut)
        m4_bolts = bolt if m4_bolts is None else m4_bolts.union(bolt)

    stl_map = {
        "base": base,
        "mast_clamp": mast_clamp,
        "orings": orings,
        "probe_sleeve_back": sleeve_back,
        "probe_sleeve_front": sleeve_front,
        "pagoda": pagoda,
        "wind_tunnel": wind_tunnel,
        "struts": struts,
        "crown_base": crown_base,
        "crown": crown,
    }

    plates = board_plates()
    assembly = (
        base
        .union(mast_clamp)
        .union(orings)
        .union(sleeve_back)
        .union(sleeve_front)
        .union(pagoda)
        .union(wind_tunnel)
        .union(struts)
        .union(crown_base)
        .union(crown)
    )
    for plate, _, _ in plates:
        assembly = assembly.union(plate)
    cq.exporters.export(assembly, str(OUT / "assembly.step"), cq.exporters.ExportTypes.STEP)
    print("assembly.step saved")

    full_parts = part_solids()
    parts = [(s, c, 1.0) for s, c, _, _ in full_parts]
    boards = [(s, c, 1.0) for s, c, _ in plates]
    inside = boards + parts + [(b2b_riser, GOLD, 1.0), (uv_retainer_nut, BRASS, 1.0), (uv_lens, GLASS_UV, 0.28)]

    exterior = [
        (base, STL_COLOURS["base"], 1.0),
        (mast_clamp, STL_COLOURS["mast_clamp"], 1.0),
        (orings, STL_COLOURS["orings"], 1.0),
        (sleeve_back, STL_COLOURS["probe_sleeve_back"], 1.0),
        (sleeve_front, STL_COLOURS["probe_sleeve_front"], 1.0),
        (pagoda, STL_COLOURS["pagoda"], 1.0),
        (wind_tunnel, STL_COLOURS["wind_tunnel"], 1.0),
        (struts, STL_COLOURS["struts"], 1.0),
        (crown_base, STL_COLOURS["crown_base"], 1.0),
        (crown, STL_COLOURS["crown"], 1.0),
    ] + inside

    render("iso", exterior, (260, 175, 260), focal=(0, 102, 0), zoom=1.12)
    render("inside", [
        (base, STL_COLOURS["base"], 0.22),
        (mast_clamp, STL_COLOURS["mast_clamp"], 0.28),
        (orings, STL_COLOURS["orings"], 0.90),
        (sleeve_back, STL_COLOURS["probe_sleeve_back"], 0.45),
        (sleeve_front, STL_COLOURS["probe_sleeve_front"], 0.45),
        (pagoda, STL_COLOURS["pagoda"], 0.18),
        (wind_tunnel, STL_COLOURS["wind_tunnel"], 0.25),
        (struts, STL_COLOURS["struts"], 0.55),
        (crown_base, STL_COLOURS["crown_base"], 0.22),
        (crown, STL_COLOURS["crown"], 0.22),
        (m4_bolts, ALU, 1.0),
    ] + inside, (250, 165, 250), focal=(0, 102, 0), zoom=1.12)

    # Half cutaway section (x <= 0) showing the internal boards, O-rings, meshing threads, pagoda conduit, and transducers
    keep = box(-60, 0, -5, 210, -60, 60)
    section = [
        (base.intersect(keep), STL_COLOURS["base"], 1.0),
        (mast_clamp.intersect(keep), STL_COLOURS["mast_clamp"], 1.0),
        (orings.intersect(keep), STL_COLOURS["orings"], 1.0),
        (sleeve_back.intersect(keep), STL_COLOURS["probe_sleeve_back"], 1.0),
        (sleeve_front.intersect(keep), STL_COLOURS["probe_sleeve_front"], 1.0),
        (pagoda.intersect(keep), STL_COLOURS["pagoda"], 1.0),
        (wind_tunnel.intersect(keep), STL_COLOURS["wind_tunnel"], 1.0),
        (struts.intersect(keep), STL_COLOURS["struts"], 1.0),
        (crown_base.intersect(keep), STL_COLOURS["crown_base"], 1.0),
        (crown.intersect(keep), STL_COLOURS["crown"], 1.0),
        (m4_bolts.intersect(keep), ALU, 1.0),
    ] + inside
    render("section", section, (320, 105, 90), focal=(0, 102, 0), zoom=1.12)

    # Exploded view separating all 10 modular parts + all 5 PCBs
    main_grp = [(s, c, 1.0) for s, c, b in plates if b == "main"] + [(s, c, 1.0) for s, c, b, _ in full_parts if b == "main"]
    probe_grp = [(s, c, 1.0) for s, c, b in plates if b == "probe"] + [(s, c, 1.0) for s, c, b, _ in full_parts if b == "probe"]
    refl_grp = [(s, c, 1.0) for s, c, b in plates if b == "reflector"] + [(s, c, 1.0) for s, c, b, _ in full_parts if b == "reflector"]
    crown_pcb_grp = [(s, c, 1.0) for s, c, b in plates if b == "crown"] + [(s, c, 1.0) for s, c, b, _ in full_parts if b == "crown"]
    dome_pcb_grp = [(s, c, 1.0) for s, c, b in plates if b == "dome"] + [(s, c, 1.0) for s, c, b, _ in full_parts if b == "dome"]

    exploded = (
        shifted([(base, STL_COLOURS["base"], 1.0), (mast_clamp.translate((0, 0, -24)), STL_COLOURS["mast_clamp"], 1.0)], -72)
        + shifted(main_grp, -38)
        + shifted([(orings, STL_COLOURS["orings"], 1.0)], -18)
        + shifted([(pagoda, STL_COLOURS["pagoda"], 0.42)], -6)
        + [(sleeve_back.translate((0, 0, -22)), STL_COLOURS["probe_sleeve_back"], 1.0),
           (sleeve_front.translate((0, 0, 22)), STL_COLOURS["probe_sleeve_front"], 1.0)]
        + probe_grp
        + shifted(refl_grp, 18)
        + shifted([(wind_tunnel, STL_COLOURS["wind_tunnel"], 0.95)], 32)
        + shifted([(struts, STL_COLOURS["struts"], 1.0)], 48)
        + shifted([(crown_base, STL_COLOURS["crown_base"], 0.92)], 66)
        + shifted(crown_pcb_grp + [(b2b_riser, GOLD, 1.0)], 84)
        + shifted(dome_pcb_grp + [(uv_retainer_nut, BRASS, 1.0)], 98)
        + shifted([(crown, STL_COLOURS["crown"], 0.95), (uv_lens, GLASS_UV, 0.35)], 118)
    )
    render("exploded", exploded, (360, 210, 360), focal=(0, 115, 0), zoom=0.98)

    # Crown close-up from above showing transparent hollow JGS1 hemisphere + elevated `dome` Micro-PCB + B2B riser + O-ring + nut + 4 Murata piezos + `crown` PCB + `crown_base` + `crown` meshing threads
    crown_inside_items = (
        [(crown_base.intersect(keep), STL_COLOURS["crown_base"], 0.55),
         (crown_base.cut(keep), STL_COLOURS["crown_base"], 0.14),
         (crown.intersect(keep), STL_COLOURS["crown"], 0.35),
         (crown.cut(keep), STL_COLOURS["crown"], 0.12),
         (uv_lens.intersect(keep), GLASS_UV, 0.38),
         (uv_lens.cut(keep), GLASS_UV, 0.16),
         (uv_retainer_nut.intersect(keep), BRASS, 1.0),
         (uv_retainer_nut.cut(keep), BRASS, 0.35),
         (b2b_riser, GOLD, 1.0),
         (orings.intersect(box(-50, 50, 184, 200, -50, 50)), STL_COLOURS["orings"], 0.92)]
        + crown_pcb_grp
        + dome_pcb_grp
    )
    # Pagoda clean 100% opaque cross-sectional slice (`x <= 0` only — zero translucent ghosting!)
    # Slice ALL items (including `probe_grp` and `refl_grp`!) on `x <= 0` so the `x > 0` half of `probe J1`
    # does not stick out in front of the cut plane and obscure the `x = 0` cross-section of `probe_sleeve_front/back`!
    col_slice = keep.cut(box(-30.0, 0.0, 64.0, 125.0, 14.50, 40.0))
    STEEL_BOLT = (0.26, 0.31, 0.38)
    pagoda_inside_items = (
        [(pagoda.intersect(col_slice), STL_COLOURS["pagoda"], 1.0),
         (wind_tunnel.intersect(keep), STL_COLOURS["wind_tunnel"], 1.0),
         (sleeve_back.intersect(keep), STL_COLOURS["probe_sleeve_back"], 1.0),
         (sleeve_front.intersect(keep), STL_COLOURS["probe_sleeve_front"], 1.0),
         (m4_bolts.intersect(keep), STEEL_BOLT, 1.0),
         (orings.intersect(box(-50, 0, 64, 122, -50, 50)), STL_COLOURS["orings"], 1.0)]
        + [(s.intersect(keep), c, op) for s, c, op in probe_grp]
        + [(s.intersect(keep), c, op) for s, c, op in refl_grp]
    )
    stl_part_views(stl_map, crown_inside_items=crown_inside_items, pagoda_inside_items=pagoda_inside_items)


if __name__ == "__main__":
    main()
