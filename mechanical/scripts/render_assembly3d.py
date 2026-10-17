"""Offscreen VTK 3D renders and full assembly STEP of the AeroMet Roof Weather Station
(`base`, `pagoda`, `crown`, `main`, `crown` PCB, `probe` PCB, and realistic 3D components).

Writes mechanical/build/<variant>/assembly.step and renders/*.png:
  - iso.png, front.png, back.png, inside.png, section.png, exploded.png
  - main_top.png, main_bottom.png, crown_top.png, crown_bottom.png, probe_top.png, probe_bottom.png
"""

import sys
from pathlib import Path

import cadquery as cq
import vtk

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be
import enclosure_features as ef
import legend_color_model as lcm
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
        s = ef.board_slab(board)
        if board == "dome":
            cx, cz = (s["x0"] + s["x1"]) / 2.0, (s["z0"] + s["z1"]) / 2.0
            plates.append((cyl_y(cx, cz, s["y0"], s["y1"], s["x1"] - s["x0"]), colours[board], board))
        else:
            plates.append((box(s["x0"], s["x1"], s["y0"], s["y1"], s["z0"], s["z1"]), colours[board], board))
    return plates


def realistic_part(b):
    fp = b.get("footprint", "")
    x0, x1, y0, y1, z0, z1 = b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"]
    cx, cy, cz = (x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0

    if fp == "ULTRASONIC_10MM":
        can = cyl_y(cx, cz, y0, y1, 10.0)
        face = cyl_y(cx, cz, y0 - 0.1, y0 + 0.4, 8.2)
        return [(can, ALU), (face, (0.25, 0.28, 0.32))]
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
    "pagoda": (0.93, 0.95, 0.97),             # Pearl White
    "wind_tunnel": (0.58, 0.64, 0.78),        # Cool Indigo-Silver
    "strut_cover": (0.92, 0.42, 0.28),        # Coral Orange
    "crown": (0.76, 0.86, 0.95),              # Arctic Ice Blue
}


def stl_part_views(stl_map, crown_inside_items=()):
    """Render an individual 3/4 isometric view (`<part>.png`) for each modular STL part,
    and for `crown.png` show the cutaway Crown enclosure from above with its PCB and parts inside."""
    for name, solid in stl_map.items():
        if name == "crown" and crown_inside_items:
            # Close-up zoomed-in view looking right inside the transparent hollow JGS1 hemisphere & elevated `dome` Micro-PCB!
            render("crown", crown_inside_items, (68, 228, 82), focal=(0, 190.5, 0), zoom=2.15)
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
    strut_cover = be.build_strut_cover()
    crown = be.build_crown()
    legend = lcm.legend_bodies()
    crown_silk = crown.cut(legend)
    win = ef.uv_window()
    # Hollow JGS1 Fused Silica Hemispherical Dome (Ø 18.0 mm OD / Ø 15.6 mm ID, 1.2 mm wall) + O-Ring + M22 Threaded Retainer Nut:
    #   1. Solid 2.8 mm-thick Top Retaining Lip on `crown.stl` (y = 195.2..198.0 mm)
    #   2. Crimson O-Ring FIRST (y = 194.2..195.2 mm, r = 8.6..10.5 mm) against the 2.8 mm-thick inner roof shoulder
    #   3. Hollow JGS1 Quartz Flange (y = 193.0..194.2 mm, r = 7.8..10.4 mm) + Neck (y = 194.2..198.0 mm, r = 7.8..9.0 mm)
    #   4. Hollow JGS1 Quartz Hemisphere (y = 198.0..207.0 mm, R_out = 9.0 mm, R_in = 7.8 mm) so U1 & U2 on the elevated
    #      round Ø 13.6 mm `dome` Micro-PCB (y = 197.2..200.2 mm) sit visibly INSIDE the transparent glass dome!
    #   5. Threaded M22 Retainer Nut (brass/gold at y = 191.2..193.0 mm, r = 8.4..10.5 mm) clamping the JGS1 flange from below!
    uv_flange_shell = be.ring_cone_y(193.0, 194.2, 7.8, 10.4, 7.8, 10.4).translate((win["x"], 0, win["z"]))
    uv_neck_shell = be.ring_cone_y(194.2, 198.0, 7.8, 9.0, 7.8, 9.0).translate((win["x"], 0, win["z"]))
    hemi_outer = (cq.Workplane("XY", origin=(win["x"], 198.0, win["z"]))
                  .sphere(9.0)
                  .intersect(box(-25, 25, 198.0, 209.0, -25, 25)))
    hemi_inner = (cq.Workplane("XY", origin=(win["x"], 198.0, win["z"]))
                  .sphere(7.8)
                  .intersect(box(-25, 25, 197.8, 209.0, -25, 25)))
    uv_lens = uv_flange_shell.union(uv_neck_shell).union(hemi_outer.cut(hemi_inner))
    uv_retainer_nut = be.ring_cone_y(191.2, 193.0, 8.4, 10.5, 8.4, 10.5).translate((win["x"], 0, win["z"]))
    # Vertical 4-pin 2.54 mm board-to-board riser posts connecting `crown J10` (y=181.5) to round `dome J1` (y=196.0)
    b2b_riser = box(-5.1, 5.1, 181.5, 196.0, -1.25, 1.25)

    stl_map = {
        "base": base,
        "mast_clamp": mast_clamp,
        "orings": orings,
        "probe_sleeve_back": sleeve_back,
        "probe_sleeve_front": sleeve_front,
        "pagoda": pagoda,
        "wind_tunnel": wind_tunnel,
        "strut_cover": strut_cover,
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
        .union(strut_cover)
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
        (strut_cover, STL_COLOURS["strut_cover"], 1.0),
        (crown_silk, STL_COLOURS["crown"], 1.0),
        (legend, SILK, 1.0),
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
        (strut_cover, STL_COLOURS["strut_cover"], 0.65),
        (crown, STL_COLOURS["crown"], 0.22),
    ] + inside, (250, 165, 250), focal=(0, 102, 0), zoom=1.12)

    # Half cutaway section (x <= 0) showing the internal boards, O-rings, pagoda probe sleeve, reflector cone, and transducers
    keep = box(-60, 0, -5, 210, -60, 60)
    section = [
        (base.intersect(keep), STL_COLOURS["base"], 1.0),
        (mast_clamp.intersect(keep), STL_COLOURS["mast_clamp"], 1.0),
        (orings.intersect(keep), STL_COLOURS["orings"], 1.0),
        (sleeve_back.intersect(keep), STL_COLOURS["probe_sleeve_back"], 1.0),
        (sleeve_front.intersect(keep), STL_COLOURS["probe_sleeve_front"], 1.0),
        (pagoda.intersect(keep), STL_COLOURS["pagoda"], 1.0),
        (wind_tunnel.intersect(keep), STL_COLOURS["wind_tunnel"], 1.0),
        (crown.intersect(keep), STL_COLOURS["crown"], 1.0),
    ] + inside
    render("section", section, (320, 105, 90), focal=(0, 102, 0), zoom=1.12)

    # Exploded view separating all 9 modular parts + all 5 PCBs
    main_grp = [(s, c, 1.0) for s, c, b in plates if b == "main"] + [(s, c, 1.0) for s, c, b, _ in full_parts if b == "main"]
    probe_grp = [(s, c, 1.0) for s, c, b in plates if b == "probe"] + [(s, c, 1.0) for s, c, b, _ in full_parts if b == "probe"]
    refl_grp = [(s, c, 1.0) for s, c, b in plates if b == "reflector"] + [(s, c, 1.0) for s, c, b, _ in full_parts if b == "reflector"]
    crown_pcb_grp = [(s, c, 1.0) for s, c, b in plates if b == "crown"] + [(s, c, 1.0) for s, c, b, _ in full_parts if b == "crown"]
    dome_pcb_grp = [(s, c, 1.0) for s, c, b in plates if b == "dome"] + [(s, c, 1.0) for s, c, b, _ in full_parts if b == "dome"]

    exploded = (
        shifted([(base, STL_COLOURS["base"], 1.0), (mast_clamp.translate((0, 0, -24)), STL_COLOURS["mast_clamp"], 1.0)], -68)
        + shifted(main_grp, -34)
        + shifted([(orings, STL_COLOURS["orings"], 1.0)], -16)
        + shifted([(pagoda, STL_COLOURS["pagoda"], 0.42)], -6)
        + [(sleeve_back.translate((0, 0, -22)), STL_COLOURS["probe_sleeve_back"], 1.0),
           (sleeve_front.translate((0, 0, 22)), STL_COLOURS["probe_sleeve_front"], 1.0)]
        + probe_grp
        + shifted(refl_grp, 18)
        + shifted([(wind_tunnel, STL_COLOURS["wind_tunnel"], 0.92),
                   (strut_cover.translate((16, 0, 16)), STL_COLOURS["strut_cover"], 1.0)], 36)
        + shifted(crown_pcb_grp + [(b2b_riser, GOLD, 1.0)], 62)
        + shifted(dome_pcb_grp + [(uv_retainer_nut, BRASS, 1.0)], 78)
        + shifted([(crown_silk, STL_COLOURS["crown"], 0.95), (legend, SILK, 1.0), (uv_lens, GLASS_UV, 0.35)], 104)
    )
    render("exploded", exploded, (360, 210, 360), focal=(0, 115, 0), zoom=1.02)

    # Crown close-up from above showing transparent hollow JGS1 hemisphere + elevated `dome` Micro-PCB + B2B riser + O-ring + nut + 4 Murata piezos + lowered `crown` PCB
    crown_inside_items = (
        [(crown.intersect(keep), STL_COLOURS["crown"], 0.30),
         (crown.cut(keep), STL_COLOURS["crown"], 0.12),
         (uv_lens.intersect(keep), GLASS_UV, 0.38),
         (uv_lens.cut(keep), GLASS_UV, 0.16),
         (uv_retainer_nut.intersect(keep), BRASS, 1.0),
         (uv_retainer_nut.cut(keep), BRASS, 0.35),
         (b2b_riser, GOLD, 1.0),
         (orings.intersect(box(-15, 15, 190, 200, -15, 15)), STL_COLOURS["orings"], 0.92)]
        + crown_pcb_grp
        + dome_pcb_grp
    )
    stl_part_views(stl_map, crown_inside_items=crown_inside_items)


if __name__ == "__main__":
    main()
