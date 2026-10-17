"""Build the 8-part modular ASA roof weather station enclosure:
  1. `base.stl/.step`:              Lower IP66 electronics bay (y=30..68) + rear half-moon mast cradle (y=0..34)
  2. `mast_clamp.stl/.step`:        Detachable front half-moon pole clamp saddle (y=0..29.5) with M4 bolt flanges
  3. `probe_sleeve_back.stl/.step`: Rear clamshell half for `probe` PCB (sealed dead-air chamber over 0.5W backside heater)
  4. `probe_sleeve_front.stl/.step`:Front clamshell half for `probe` PCB (thermal bulkhead rib, SCD41/SGP41 hood, SHT45 Venturi)
  5. `pagoda.stl/.step`:            5-plate Stevenson louver stack (y=65..121) with open top collar & bottom J1 potting cup
  6. `wind_tunnel.stl/.step`:       Heated conical reflector deck (y=119.5..130) + 4 airfoil struts (45-deg NE open U-channel)
  7. `strut_cover.stl/.step`:       Click-on aerodynamic cover strip for the 45-deg NE cable strut (y=127..173.5)
  8. `crown.stl/.step`:             Top ultrasonic shroud, 12-deg sloped haptic rain dome & 120-deg flared UV window
"""

import math
import sys
from pathlib import Path

import cadquery as cq
import enclosure_features as ef

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

SPEC = load_spec()
VARIANT = SPEC["active_variant"]
OUTER_R = ef.OUTER_R
WALL = ef.WALL
INNER_R = OUTER_R - WALL


def cone_y(y0, y1, r0, r1):
    """Solid frustum/cylinder along the vertical +Y axis from y0 to y1."""
    h = y1 - y0
    if abs(r0 - r1) < 1e-4:
        return cq.Workplane("XZ", origin=(0, y0, 0)).circle(r0).extrude(-h)
    return (cq.Workplane("XZ", origin=(0, y0, 0)).circle(r0)
            .workplane(offset=-h).circle(r1).loft(combine=True))


def ring_cone_y(y0, y1, r0_in, r0_out, r1_in, r1_out):
    """Hollow conical frustum (Pagoda louver plate or shroud ring) along +Y."""
    outer = cone_y(y0, y1, r0_out, r1_out)
    inner = cone_y(y0 - 0.05, y1 + 0.05, r0_in, r1_in)
    return outer.cut(inner)


def box(x0, x1, y0, y1, z0, z1):
    return (cq.Workplane("XY")
            .transformed(offset=((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
            .box(x1 - x0, y1 - y0, z1 - z0))


def cyl_y(x, z, y0, y1, diameter):
    return cone_y(y0, y1, diameter / 2.0, diameter / 2.0).translate((x, 0, z))


def hex_prism_z(cx, cy, z0, z1, across_flats=10.25):
    """Regular hexagonal prism along Z (flat sides horizontal) for captured M6 hex nuts (ISO 4032 s=10.0 mm)."""
    r_circ = across_flats / math.sqrt(3.0)
    pts = [(cx + r_circ * math.cos(math.radians(60 * k)),
            cy + r_circ * math.sin(math.radians(60 * k))) for k in range(6)]
    return cq.Workplane("XY", origin=(0, 0, z0)).polyline(pts).close().extrude(z1 - z0)


def build_base():
    """Part 1: Rear Mast Cradle (y=0..34, z>=-1) with Flat M6 Clamping Ears & Captured M6 Hex Nut Traps
    + Sealed Lower Electronics Bell Housing (y=34..68) with M12 Power Cable Entry Hole & Center Mast Conduit."""
    mast_ir = SPEC["envelope"]["mast_socket"]["inner_diameter"] / 2.0
    mast_or = SPEC["envelope"]["mast_socket"]["outer_diameter"] / 2.0

    # 1. Rear half-moon mast cradle (y = 0..34, z >= -1.0) + wide flat M6 clamping ears (x = -36..+36, y = 0..28, z = -1..+11)
    cradle_cyl = cone_y(0.0, 34.0, mast_or, mast_or).cut(box(-40.0, 40.0, -0.5, 30.0, -40.0, -1.0))
    flange = box(-36.0, 36.0, 0.0, 28.0, -1.0, 11.0)
    neck = cone_y(28.0, 35.0, mast_or, OUTER_R)
    rear_ear_flat = (
        box(-37.0, -19.2, -0.5, 30.5, 11.0, 30.0)
        .union(box(19.2, 37.0, -0.5, 30.5, 11.0, 30.0))
    )
    body = cradle_cyl.union(flange).union(neck).cut(rear_ear_flat)

    # 2. Lower electronics bay cylinder (y = 34..68) + stepped top mating lip (y = 66.5..68) + O-ring groove
    bay_outer = cone_y(34.0, 66.5, OUTER_R, OUTER_R).union(cone_y(66.5, 68.0, INNER_R - 0.2, INNER_R - 0.2))
    oring_groove = ring_cone_y(65.2, 66.6, INNER_R - 1.3, OUTER_R - 0.8, INNER_R - 1.3, OUTER_R - 0.8)
    body = body.union(bay_outer).cut(oring_groove)

    # 3. Cut internal electronics cavity (y = 35.5..68.2, radius = INNER_R - 1.5)
    cavity = cone_y(35.5, 68.2, INNER_R - 1.5, INNER_R - 1.5)
    body = body.cut(cavity)

    # 4. Cut mast bore (y = -0.1..29.5), central through-mast power cable hole (Ø 14 mm through floor y = 28..37),
    #    AND dedicated M12 waterproof power cable gland hole (Ø 12.2 mm at x = 0.0, z = -30.0, clear of all 4 standoffs!)
    mast_bore = cone_y(-0.1, 29.5, mast_ir, mast_ir)
    center_power_hole = cone_y(28.0, 37.0, 7.0, 7.0)
    m12_power_gland_hole = cone_y(28.0, 37.0, 6.1, 6.1).translate((0.0, 0, -30.0))
    body = body.cut(mast_bore).cut(center_power_hole).cut(m12_power_gland_hole)

    # 5. Cut 4x M6 clearance bolt holes (Ø 6.4 mm at x = +/-27.0, y = 7.5 & 20.5)
    #    + 4x captured M6 hex nut pockets (10.25 mm across-flats, 5.2 mm deep on the flat rear face z = 5.8..11.2)
    for xb in (-27.0, 27.0):
        for yb in (7.5, 20.5):
            bolt = (cq.Workplane("XY", origin=(0, 0, -10.0))
                    .center(xb, yb)
                    .circle(3.2)
                    .extrude(30.0))
            nut_trap = hex_prism_z(xb, yb, 5.8, 11.5, across_flats=10.25)
            body = body.cut(bolt).cut(nut_trap)

    # 6. Add 4 internal PCB standoffs supporting `main` (bottom of `main` is at y = 42.8)
    main_bot_y = SPEC["boards"]["main"]["y"] - SPEC["boards"]["main"]["thickness"]
    for st in ef.standoffs("main"):
        post = cone_y(35.0, main_bot_y, 2.5, 2.5).translate((st["x"], 0, st["z"]))
        body = body.union(post)

    # 7. Cut 4x 1.2 mm anti-spider micro-slit intake grilles + condensation weep drains on the bottom overhang
    for deg in (30, 150, 210, 330):
        rad = math.radians(deg)
        sx, sz = 34.0 * math.cos(rad), 34.0 * math.sin(rad)
        for offset in (-2.0, 0.0, 2.0):
            slit = box(sx - 3.5, sx + 3.5, 31.0, 36.5, sz + offset - 0.6, sz + offset + 0.6)
            body = body.cut(slit)

    return body


def build_mast_clamp():
    """Part 2: Detachable Front Half-Moon Pole Clamp Saddle (y=0..28.0, z<=-1.5)
    with Wide Flat M6 Bolt Seating Faces (x = -36..+36, z = -11.0)."""
    mast_ir = SPEC["envelope"]["mast_socket"]["inner_diameter"] / 2.0
    mast_or = SPEC["envelope"]["mast_socket"]["outer_diameter"] / 2.0

    half_cyl = cone_y(0.0, 28.0, mast_or, mast_or).intersect(box(-40.0, 40.0, -0.5, 29.0, -40.0, -1.5))
    flange = box(-36.0, 36.0, 0.0, 28.0, -11.0, -1.5)
    ear_flat_cut = (
        box(-37.0, -19.2, -0.5, 28.5, -25.0, -11.0)
        .union(box(19.2, 37.0, -0.5, 28.5, -25.0, -11.0))
    )
    body = half_cyl.union(flange).cut(cone_y(-0.1, 29.0, mast_ir, mast_ir)).cut(ear_flat_cut)

    for xb in (-27.0, 27.0):
        for yb in (7.5, 20.5):
            bolt = (cq.Workplane("XY", origin=(0, 0, -15.0))
                    .center(xb, yb)
                    .circle(3.2)
                    .extrude(20.0))
            body = body.cut(bolt)

    return body


def build_probe_sleeve_back():
    """Part 3: Rear Clamshell Sleeve for `probe` PCB (y=75.5..116.0, z=-4.6..0.0).
    Seals the backside 0.5W parallel heater (R1||R2), TH1, U4, Q1 inside an insulated dead-air chamber."""
    shell = box(-9.2, 9.2, 75.5, 116.0, -4.6, 0.0)
    back_dead_air = box(-7.4, 7.4, 76.5, 114.5, -3.4, 0.1)
    pcb_seat = box(-7.15, 7.15, 75.0, 116.5, -0.65, 0.1)
    top_neck = box(-6.0, 6.0, 112.0, 116.5, -3.4, 0.1)
    return shell.cut(back_dead_air).cut(pcb_seat).cut(top_neck)


def build_probe_sleeve_front():
    """Part 4: Front Clamshell Sleeve for `probe` PCB (y=75.5..116.0, z=0.0..+8.8).
    Features the internal horizontal thermal bulkhead rib (y=87.8..89.2), the draft-shielded 5-slit
    diffusion hood over SCD41 & SGP41, and the 4.6x4.6 mm Venturi window over SHT45."""
    lower_hood = box(-9.2, 9.2, 75.5, 89.2, 0.0, 3.4)
    upper_hood = box(-9.2, 9.2, 87.8, 116.0, 0.0, 8.8)
    shell = lower_hood.union(upper_hood)

    pcb_seat = box(-7.15, 7.15, 75.0, 116.5, -0.1, 0.65)
    upper_gas_chamber = box(-7.4, 7.4, 89.2, 114.5, 0.6, 7.5)
    lower_sht_chamber = box(-7.4, 7.4, 76.5, 87.8, 0.6, 2.1)
    top_t_neck = box(-6.0, 6.0, 112.0, 116.5, -0.1, 3.4)
    sht45_venturi = box(-3.8, 0.8, 80.2, 84.8, 0.6, 4.0).union(box(-9.5, 9.5, 80.5, 84.5, 0.8, 1.9))

    body = (
        shell
        .cut(pcb_seat)
        .cut(upper_gas_chamber)
        .cut(lower_sht_chamber)
        .cut(top_t_neck)
        .cut(sht45_venturi)
    )
    for ys in (93.0, 96.5, 100.0, 104.0, 107.5):
        body = body.cut(box(-6.0, 6.0, ys - 0.6, ys + 0.6, 7.0, 9.2))
    return body


def build_pagoda():
    """Part 5: 5-Plate Stevenson Pagoda Stack (y=65.0..121.0) with Bottom J1 Potting Cup
    and Open Top Collar (so `probe J2` is completely accessible to plug `reflector` onto it)."""
    skirt = ring_cone_y(66.5, 72.0, INNER_R - 1.5, OUTER_R, INNER_R - 1.5, OUTER_R)
    drip_skirt = ring_cone_y(65.0, 66.8, OUTER_R - 0.8, OUTER_R + 0.8, OUTER_R - 0.8, OUTER_R)
    bridge = box(-INNER_R + 1.0, INNER_R - 1.0, 68.0, 71.0, -8.0, 8.0)
    cable_hole = box(-18.0, 18.0, 67.5, 71.5, -5.5, 5.5)
    potting_cup_outer = box(-8.6, 8.6, 68.5, 75.5, -4.4, 4.4)
    potting_cup_inner = box(-7.2, 7.2, 68.4, 75.6, -3.2, 3.2)
    body = skirt.union(drip_skirt).union(bridge).cut(cable_hole).union(potting_cup_outer).cut(potting_cup_inner)

    for deg in (45, 135, 225, 315):
        rad = math.radians(deg)
        px, pz = 26.0 * math.cos(rad), 26.0 * math.sin(rad)
        pillar = cone_y(68.0, 121.0, 3.2, 3.2).translate((px, 0, pz))
        body = body.union(pillar)

    for py in SPEC["envelope"]["pagoda_shield"]["plate_ys"]:
        louver = ring_cone_y(py - 3.5, py + 2.5, 22.0, OUTER_R, 18.0, OUTER_R - 6.0)
        lip = ring_cone_y(py - 5.0, py - 3.5, OUTER_R - 2.2, OUTER_R, OUTER_R - 2.2, OUTER_R)
        body = body.union(louver).union(lip)

    # Top mating ring (y = 118.5..121.0) that seats into the underside of `wind_tunnel.stl`
    top_ring = ring_cone_y(118.5, 121.0, 22.0, 29.5, 22.0, 29.5)
    body = body.union(top_ring)
    return body


def build_wind_tunnel():
    """Part 6: Heated Conical Reflector Deck (y=119.0..130.0) + 4 Airfoil Struts (y=125.5..175.0)
    with Open U-Channel Trough & Pre-Crimped JST-SH Plug Slot on the 45-deg NE Strut."""
    reflector_skirt = cone_y(120.0, 126.5, OUTER_R - 2.0, OUTER_R)
    reflector_drip = ring_cone_y(119.0, 121.0, OUTER_R - 1.5, OUTER_R + 0.6, OUTER_R - 1.5, OUTER_R)
    reflector_cone = cone_y(126.5, 130.0, OUTER_R, 6.0)
    # Underside pocket (y = 119.0..125.0, r = 18.5 mm) that drops directly over the 24x24 mm `reflector` PCB
    reflector_cavity = cone_y(118.8, 125.0, 18.5, 18.5)
    pagoda_seat = ring_cone_y(118.8, 121.2, 21.8, 29.7, 21.8, 29.7)
    body = reflector_skirt.union(reflector_drip).union(reflector_cone).cut(reflector_cavity).cut(pagoda_seat)

    for st in ef.strut_centres():
        sx, sz = st["x"], st["z"]
        strut = cone_y(125.5, 175.0, 4.2, 4.2).translate((sx, 0, sz))
        body = body.union(strut)
        if st["deg"] == SPEC["envelope"]["wind_tunnel"]["cable_conduit_strut_deg"]:
            u_trough = box(sx - 2.1, sx + 5.0, 125.0, 175.5, sz - 2.1, sz + 5.0)
            plug_slot = box(sx - 5.5, sx + 4.5, 118.5, 130.5, sz - 2.5, sz + 2.5)
            body = body.cut(u_trough).cut(plug_slot)

    return body


def build_strut_cover():
    """Part 7: Clickable Snap-On Aerodynamic Cover for the 45-deg NE Cable Strut (y = 127.0..173.5)."""
    st = next(s for s in ef.strut_centres() if s["deg"] == SPEC["envelope"]["wind_tunnel"]["cable_conduit_strut_deg"])
    sx, sz = st["x"], st["z"]
    outer = cone_y(127.0, 173.5, 4.2, 4.2).translate((sx, 0, sz))
    cap_half = box(sx - 0.5, sx + 5.0, 126.8, 173.7, sz - 0.5, sz + 5.0)
    wire_channel = cone_y(126.5, 174.0, 2.0, 2.0).translate((sx, 0, sz))
    return outer.intersect(cap_half).cut(wire_channel)


def build_crown():
    """Part 8: Top Ultrasonic Shroud & Sloped Haptic Rain / Solar-UV Crown (y=172.5..198)."""
    shroud = cone_y(174.0, 190.0, OUTER_R, OUTER_R)
    crown_drip = ring_cone_y(172.5, 174.5, OUTER_R - 1.2, OUTER_R + 0.6, OUTER_R - 1.2, OUTER_R)
    dome = cone_y(190.0, 198.0, OUTER_R, 6.0)
    body = shroud.union(crown_drip).union(dome)

    cavity = cone_y(176.2, 189.5, INNER_R - 1.5, INNER_R - 1.5).union(
        cone_y(189.5, 194.5, INNER_R - 1.5, 12.0))
    body = body.cut(cavity)

    for tp in ef.transducer_pockets():
        hole = cone_y(173.8, 176.8, 5.2, 5.2).translate((tp["x"], 0, tp["z"]))
        collar = ring_cone_y(176.0, 179.5, 5.2, 6.6, 5.2, 6.6).translate((tp["x"], 0, tp["z"]))
        body = body.cut(hole).union(collar)

    for st in ef.strut_centres():
        sx, sz = st["x"], st["z"]
        sock = cone_y(173.8, 175.3, 4.35, 4.35).translate((sx, 0, sz))
        body = body.cut(sock)
        if st["deg"] == SPEC["envelope"]["wind_tunnel"]["cable_conduit_strut_deg"]:
            plug_slot = box(sx - 5.5, sx + 3.5, 173.5, 177.5, sz - 2.5, sz + 2.5)
            body = body.cut(plug_slot)

    win = ef.uv_window()
    r_win = win["diameter"] / 2.0
    uv_hole = cone_y(190.0, 198.5, r_win, r_win).translate((win["x"], 0, win["z"]))
    uv_chamfer = cone_y(195.2, 198.2, r_win, r_win + 4.5).translate((win["x"], 0, win["z"]))
    body = body.cut(uv_hole).cut(uv_chamfer)

    crown_bot_y = SPEC["boards"]["crown"]["y"] - SPEC["boards"]["crown"]["thickness"]
    for st in ef.standoffs("crown"):
        post = cone_y(176.0, crown_bot_y, 2.4, 2.4).translate((st["x"], 0, st["z"]))
        body = body.union(post)

    return body


def build_orings():
    """Part 9: Elastomeric O-Ring & Gasket Set (`orings.stl/.step` — TPU 95A or Nitrile/EPDM):
      1. Lower Base-to-Pagoda radial/axial O-ring gasket (y = 65.3..66.5, r = 43.8..47.1 mm)
      2. Middle Pagoda-to-Wind-Tunnel reflector cavity gasket (y = 119.0..120.2, r = 22.0..25.0 mm)
      3. Upper Crown UV optical window seal ring (y = 193.8..195.0, r = 3.8..5.4 mm)
    """
    ring_base = ring_cone_y(65.3, 66.5, INNER_R - 1.2, OUTER_R - 0.9, INNER_R - 1.2, OUTER_R - 0.9)
    ring_refl = ring_cone_y(119.0, 120.2, 22.0, 24.5, 22.0, 24.5)
    win = ef.uv_window()
    r_win = win["diameter"] / 2.0
    ring_uv = ring_cone_y(193.8, 195.0, r_win - 0.1, r_win + 1.4, r_win - 0.1, r_win + 1.4).translate((win["x"], 0, win["z"]))
    return ring_base.union(ring_refl).union(ring_uv)


ALL_PARTS = (
    ("base", build_base),
    ("mast_clamp", build_mast_clamp),
    ("orings", build_orings),
    ("probe_sleeve_back", build_probe_sleeve_back),
    ("probe_sleeve_front", build_probe_sleeve_front),
    ("pagoda", build_pagoda),
    ("wind_tunnel", build_wind_tunnel),
    ("strut_cover", build_strut_cover),
    ("crown", build_crown),
)


def export_parts(out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, builder in ALL_PARTS:
        solid = builder()
        cq.exporters.export(solid, str(out_dir / f"{name}.stl"))
        cq.exporters.export(solid, str(out_dir / f"{name}.step"))
        print(f"Exported {name}.stl and {name}.step -> {out_dir.relative_to(REPO)}")


if __name__ == "__main__":
    export_parts(REPO / "mechanical/build" / VARIANT)
