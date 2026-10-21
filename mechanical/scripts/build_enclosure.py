"""Build the 10-part modular ASA roof weather station enclosure with true meshing threads:
  1.  `base.stl/.step`:               Lower IP66 electronics bay + rear mast cradle + M90x1.8 top male thread & O-ring groove
                                      + sealed duct bed for the flat PMSA003I particulate module beside the `main` board
  2.  `mast_clamp.stl/.step`:         Detachable front half-moon pole clamp saddle with flat M6 bolt/washer faces
  3.  `orings.stl/.step`:             4-piece elastomeric gasket set (3x full-perimeter Ø 90 mm housing seals + 1x JGS1 dome seal)
  4.  `probe_sleeve_back.stl/.step`:  Rear clamshell half for `probe` PCB with 4 external snap-fit ramp catches
  5.  `probe_sleeve_front.stl/.step`: Front clamshell half for `probe` PCB with 4 external cantilevered snap-fit latch hooks
  6.  `pagoda.stl/.step`:             5-plate Stevenson shield with M90x1.8 bottom female thread, M90x1.8 top male thread,
                                      and an enclosed vertical cable conduit through all 5 plates at the 45-deg NE strut axis
  7.  `wind_tunnel.stl/.step`:        Compact heated conical reflector deck with M90x1.8 bottom female thread & 4 strut bores
  8.  `struts.stl/.step`:             4x separate Double-C split hollow threaded struts with flange stoppers, M11x1.5 threaded
                                      ends, and 8x meshing M11x1.5 hex locknuts clamping wind_tunnel to crown_base
  9.  `crown_base.stl/.step`:         Lower ultrasonic transducer & `crown` PCB tray with M90x1.8 top male thread & O-ring groove
  10. `crown.stl/.step`:              Removable top piezo rain & JGS1 UV dome lid with M90x1.8 bottom female thread & M22 JGS1 collar
"""

import math
import sys
from pathlib import Path

import cadquery as cq
import enclosure_features as ef
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakePolygon
from OCP.BRepOffsetAPI import BRepOffsetAPI_MakePipeShell
from OCP.gp import gp_Dir, gp_Pnt

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

SPEC = load_spec()
VARIANT = SPEC["active_variant"]
OUTER_R = ef.OUTER_R
WALL = ef.WALL
INNER_R = OUTER_R - WALL
RAISE = ef.RAISE          # base bay grown by this much; parts above it are built in the design frame and lifted
TOP_RAISE = ef.TOP_RAISE  # wind_tunnel deck, struts, crown_base, crown: lifted a further TOP_RAISE (crown-cable gap)
UPPER = ef.UPPER
BTN_UP = 10.0             # main board lift (reset button pin lengthened by the same)
UMB = 12.0                # umbrella lift inside the taller bay (clears the raised XIAO by 3.6 mm)

# Bayonet face seals use standard 88 x 2.0 mm O-rings (EPDM/NBR 70A). The gland floor sits 1.5 mm below the mating
# skirt face in the modelled (lugs centred) position: 25 % squeeze there; the O-ring lifts the upper part 0.20 mm
# onto the lug shelves (gland 1.70 mm = 15 % squeeze, which is the preload that keeps the lugs in the detent);
# pushing down 0.47 mm to pass the detent bridge compresses it to 1.23 mm (38 %, 77 % gland fill).
ORING_CS = 2.0
ORING_ID = 88.0
ORING_FLOOR_BELOW_SEAT = -1.5
LUG_PLAY = 0.20           # pocket floor (y0 + 1.45) to lug bottom (y0 + 1.65)

# Pagoda through-bolts: 4x DIN 7984 M6 x 55 low-head socket cap screws (head d 10.0 x k 3.5 mm) + ISO 4032 nuts
# (10 AF x 5.2 mm). A standard ISO 4762 head (k = 6 mm) would hit the reflector board corners.
M6_HEAD_D, M6_HEAD_K = 10.0, 3.5
M6_NUT_AF, M6_NUT_M = 10.0, 5.2
LOUVER_T = 1.6            # pagoda louver blade thickness normal to its surface (was 0.71 mm)
CLAMP_TRAVEL = 2.0        # mast clamp closing travel left between the clamp and base faces on a Ø 34 pole
CLAMP_EAR_T = 5.0         # clamp ear thickness (bolt seats at z = -8, clear of the PMSA003I floor slits at z <= -9.8)
MAST_Y0 = SPEC["envelope"]["mast_socket"]["y_start"]   # -31: 50 mm pole engagement (pole stop at y = 19)
CLAMP_BOLT_YS = (-22.0, -0.5, 20.5)   # 3 rows x 2 M6 bolts along the 59 mm tall clamp
CROWN_VENT_DEGS, CROWN_VENT_R = (45, 135, 225, 315), 14.0   # crown_base floor breather grilles (dome/cavity venting)
# crown PCB edge clips (design-frame x, z, hook direction): W, N, S, E straight edges of the 60 x 60 mm board
CROWN_CLIPS = ((-30.75, 15.0, (1, 0)), (-15.0, 30.75, (0, -1)), (-15.0, -30.75, (0, 1)), (30.75, -15.0, (-1, 0)))
# Printed M12x1.75 strut locknuts: the internal thread groove reaches r = 6.22 mm, so 15 mm across flats leaves a
# 1.28 mm wall at the flats (12 mm AF cut straight through them). Corners r = 8.66 mm -> 9.0 mm pockets.
STRUT_NUT_AF = 15.0
STRUT_NUT_POCKET_R = 9.0


def cone_y(y0, y1, r0, r1):
    """Solid frustum/cylinder along the vertical +Y axis from y0 to y1."""
    h = y1 - y0
    if abs(r0 - r1) < 1e-4:
        return cq.Workplane("XZ", origin=(0, y0, 0)).circle(r0).extrude(-h)
    return (cq.Workplane("XZ", origin=(0, y0, 0)).circle(r0)
            .workplane(offset=-h).circle(r1).loft(combine=True))


def ring_cone_y(y0, y1, r0_in, r0_out, r1_in, r1_out):
    """Hollow conical frustum along +Y."""
    outer = cone_y(y0, y1, r0_out, r1_out)
    inner = cone_y(y0 - 0.05, y1 + 0.05, r0_in, r1_in)
    return outer.cut(inner)


def box(x0, x1, y0, y1, z0, z1):
    return (cq.Workplane("XY")
            .transformed(offset=((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
            .box(x1 - x0, y1 - y0, z1 - z0))


def cyl_y(x, z, y0, y1, diameter):
    return cone_y(y0, y1, diameter / 2.0, diameter / 2.0).translate((x, 0, z))


def hex_prism_y(y0, y1, across_flats=15.4, cx=0.0, cz=0.0):
    """Regular hexagonal prism along Y (vertical axis) centered at (cx, cz)."""
    r_circ = across_flats / math.sqrt(3.0)
    pts = [(cx + r_circ * math.cos(math.radians(60 * k)),
            cz + r_circ * math.sin(math.radians(60 * k))) for k in range(6)]
    return cq.Workplane("XZ", origin=(0, y0, 0)).polyline(pts).close().extrude(-(y1 - y0))


def _helical_thread_solid_y(y_start, pitch, turns, r_base, depth, w_base, w_crest, cx=0.0, cz=0.0):
    """Build a true 3D right-hand helical spiral thread solid along +Y (`y(θ) = y_start + (P / 2π) * θ`)
    using OpenCascade `BRepOffsetAPI_MakePipeShell` with a constant +Y Frenet binormal so the trapezoidal
    thread tooth advances axially by `pitch` for every 360-deg clockwise turn!"""
    height = pitch * turns
    spine = cq.Wire.makeHelix(
        pitch=pitch, height=height, radius=r_base,
        center=cq.Vector(cx, y_start, cz), dir=cq.Vector(0, 1, 0), lefthand=False,
    )
    r_ov = r_base - 0.25
    r_tip = r_base + depth
    poly = BRepBuilderAPI_MakePolygon()
    poly.Add(gp_Pnt(cx, y_start - w_base / 2.0, cz + r_ov))
    poly.Add(gp_Pnt(cx, y_start - w_crest / 2.0, cz + r_tip))
    poly.Add(gp_Pnt(cx, y_start + w_crest / 2.0, cz + r_tip))
    poly.Add(gp_Pnt(cx, y_start + w_base / 2.0, cz + r_ov))
    poly.Close()
    ps = BRepOffsetAPI_MakePipeShell(spine.wrapped)
    ps.SetMode(gp_Dir(0, 1, 0))
    ps.Add(poly.Wire())
    ps.Build()
    ps.MakeSolid()
    return cq.Workplane(obj=cq.Shape.cast(ps.Shape()))


def add_external_thread_y(body, y_centres, r_root, depth=0.95, half_base=0.62, half_crest=0.18, cx=0.0, cz=0.0):
    """Add a true right-hand helical external (male) screw thread along +Y protruding from `r_root` to `r_root + depth`."""
    pitch = round(y_centres[1] - y_centres[0], 4) if len(y_centres) > 1 else 1.8
    y_min = y_centres[0] - half_base
    y_max = y_centres[-1] + half_base
    y_start = y_centres[0] - 0.25 * pitch
    turns = (y_max - y_start) / pitch + 0.35
    helix = _helical_thread_solid_y(
        y_start, pitch, turns, r_base=r_root, depth=depth,
        w_base=2.0 * half_base, w_crest=2.0 * half_crest, cx=cx, cz=cz,
    )
    clip = cone_y(y_min, y_max, r_root + depth + 1.0, r_root + depth + 1.0).translate((cx, 0, cz))
    return body.union(helix.intersect(clip))


def cut_internal_thread_y(body, y_centres, r_minor, depth=0.95, half_base=0.62, half_crest=0.18, clearance=0.18, cx=0.0, cz=0.0):
    """Cut a matching true right-hand helical internal (female) screw groove into a bore of radius `r_minor`.
    Starts exactly 1.000 full pitch below `y_start` of the male thread so the male and female helices are
    100% phase-locked around all 360 degrees and advance by +P/2 when rotated by 180 degrees!"""
    pitch = round(y_centres[1] - y_centres[0], 4) if len(y_centres) > 1 else 1.8
    hb = half_base + clearance
    hc = half_crest + clearance
    y_min = y_centres[0] - hb - 0.5 * pitch
    y_max = y_centres[-1] + hb + 0.5 * pitch
    y_start = y_centres[0] - 1.25 * pitch
    turns = (y_max - y_start) / pitch + 0.5
    groove = _helical_thread_solid_y(
        y_start, pitch, turns, r_base=r_minor, depth=depth,
        w_base=2.0 * hb, w_crest=2.0 * hc, cx=cx, cz=cz,
    )
    clip = cone_y(y_min, y_max, r_minor + depth + 1.0, r_minor + depth + 1.0).translate((cx, 0, cz))
    return body.cut(groove.intersect(clip))


def arc_sector_y(y0, y1, r_in, r_out, deg0, deg1, steps=6):
    """Annular sector prism along +Y from `y0` to `y1`, spanning radius `r_in..r_out` and angle `deg0..deg1`."""
    pts_out = [(r_out * math.cos(math.radians(deg0 + (deg1 - deg0) * k / steps)),
                r_out * math.sin(math.radians(deg0 + (deg1 - deg0) * k / steps))) for k in range(steps + 1)]
    pts_in = [(r_in * math.cos(math.radians(deg1 - (deg1 - deg0) * k / steps)),
               r_in * math.sin(math.radians(deg1 - (deg1 - deg0) * k / steps))) for k in range(steps + 1)]
    return cq.Workplane("XZ", origin=(0, y0, 0)).polyline(pts_out + pts_in).close().extrude(-(y1 - y0))


def add_external_bayonet_y(body, y0, r_root=44.0, r_lug=45.15, n_lugs=8):
    """Add 8 symmetric external bayonet lugs (every 45 deg around `r_root`) for a short-stroke (`20.5 deg` = ~18 mm arc travel)
    Push-Turn-Click Bayonet Interlock that compresses the full-perimeter O-ring without twisting internal wires!"""
    for k in range(n_lugs):
        deg_c = k * (360.0 / n_lugs)
        lug = arc_sector_y(y0 + 1.65, y0 + 3.25, r_root - 0.3, r_lug, deg_c - 4.8, deg_c + 4.8)
        body = body.union(lug)
    return body


def cut_internal_bayonet_y(body, y0, r_minor=44.18, r_slot=45.40, n_lugs=8):
    """Cut 8 matching internal J-slot bayonet channels into a female skirt (`r_minor = 44.18 mm`):
      1. Vertical Entry Drop-Slot (`deg_c - 26.5 .. deg_c - 14.5 deg`, open at bottom `y0 - 0.5 .. y0 + 4.00 mm`)
      2. Over-Center Detent Ridge / Cam Bridge (`deg_c - 15.0 .. deg_c - 5.8 deg`, floor at `y0 + 1.92 mm` — 0.27 mm higher
         than the bottom of the lug at `y0 + 1.65 mm`, so the lug must be pushed down 0.45 mm against the O-ring to pass!)
      3. Locked Detent Pocket (`deg_c - 6.0 .. deg_c + 6.0 deg`, floor drops to `y0 + 1.45 mm` so the O-ring snaps the lug
         into the pocket where the 0.47 mm detent step prevents accidental unscrewing, while the `y0..y0+1.45 mm` lower shelf
         holds the full weight and wind lift of the upper assembly!)
    """
    for k in range(n_lugs):
        deg_c = k * (360.0 / n_lugs)
        pocket = arc_sector_y(y0 + 1.45, y0 + 4.00, r_minor - 0.3, r_slot, deg_c - 6.0, deg_c + 6.0)
        bridge = arc_sector_y(y0 + 1.92, y0 + 4.00, r_minor - 0.3, r_slot, deg_c - 15.0, deg_c - 5.8)
        entry = arc_sector_y(y0 - 0.50, y0 + 4.00, r_minor - 0.3, r_slot, deg_c - 26.5, deg_c - 14.5)
        body = body.cut(pocket.union(bridge).union(entry))
    return body


def cut_lock_triangles_y(body, y_base, y_tip, half_w=1.45, depth=0.45):
    """Engrave 4 cardinal lock-alignment triangles (at 0, 90, 180, 270 deg around `r = OUTER_R`):
      - `y_tip > y_base`: Triangle-UP (▲) on the lower part's top edge pointing up at the seam
      - `y_tip < y_base`: Triangle-DOWN (▼) on the upper part's bottom edge pointing down at the seam
    When the two mating parts are twisted into their locked bayonet detent, ▲ and ▼ align tip-to-tip!"""
    # Always wind the 3 vertices counter-clockwise in XY so OpenCascade's face normal is strictly +Z!
    pts = [(-half_w, y_base), (half_w, y_base), (0.0, y_tip)] if y_tip > y_base else [(-half_w, y_base), (0.0, y_tip), (half_w, y_base)]
    tri_cutter = (
        cq.Workplane("XY", origin=(0, 0, -(OUTER_R - depth)))
        .polyline(pts)
        .close()
        .extrude(-2.5)
    )
    for deg in (0, 90, 180, 270):
        body = body.cut(tri_cutter.rotate((0, 0, 0), (0, 1, 0), deg))
    return body


def build_base(umbrella=True):
    """Part 1: Rear Mast Cradle (y=0..34, z>=-1) with Solid Flat Clamping Ears (for nut + washer)
    + Sealed Lower Electronics Bell Housing (y=34..70.2) with Single Center IP68 M12/PG7 Cable-Gland Bulkhead,
    Full-Perimeter O-Ring Groove (y=65.2..66.4), and 8-Lug Push-Turn-Click External Bayonet Rim (y=66.4..70.2)."""
    mast_ir = SPEC["envelope"]["mast_socket"]["inner_diameter"] / 2.0
    mast_or = SPEC["envelope"]["mast_socket"]["outer_diameter"] / 2.0

    # 1. Rear half-moon mast cradle (y = 0..34, z >= -1.0) + solid flat clamping ears (x = -33..+33, y = 0..28, z = -1..+8.0)
    #    The socket starts at MAST_Y0 (= -31): 50 mm of pole engagement up to the pole stop at y = 19.
    cradle_cyl = cone_y(MAST_Y0, 34.0, mast_or, mast_or).cut(box(-40.0, 40.0, MAST_Y0 - 0.5, 30.0, -40.0, -1.0))
    flange = box(-33.0, 33.0, MAST_Y0, 28.0, -1.0, 8.0)
    neck = cone_y(28.0, 35.0, mast_or, OUTER_R)
    rear_ear_flat = (
        box(-35.0, -19.2, MAST_Y0 - 0.5, 30.5, 8.0, 30.0)
        .union(box(19.2, 35.0, MAST_Y0 - 0.5, 30.5, 8.0, 30.0))
    )
    body = cradle_cyl.union(flange).union(neck).cut(rear_ear_flat)

    # 2. Lower electronics bay cylinder (y = 34..66.4) + 8-lug bayonet male spigot (y = 66.4..69.9, r_root = 44.0 mm) + O-ring groove
    #    (all raised by RAISE = 15 mm: the bay is 15 mm taller than the original 34..70 design)
    bay_outer = cone_y(34.0, 66.4 + RAISE, OUTER_R, OUTER_R).union(cone_y(66.4 + RAISE, 69.9 + RAISE, 44.0, 44.0))
    oring_groove = ring_cone_y(ORING_FLOOR_BELOW_SEAT + 66.9 + RAISE, 66.4 + RAISE, 43.9, OUTER_R - 0.8,
                               43.9, OUTER_R - 0.8)
    body = body.union(bay_outer).cut(oring_groove)
    body = add_external_bayonet_y(body, 66.4 + RAISE, r_root=44.0, r_lug=45.15, n_lugs=8)
    body = cut_lock_triangles_y(body, y_base=61.9 + RAISE, y_tip=64.3 + RAISE)

    # 3. Cut internal electronics cavity (y = 34.5..70.2, radius = 42.6 mm)
    cavity = cone_y(34.5, 70.2 + RAISE, 42.6, 42.6)
    body = body.cut(cavity)

    # 4. Stepped Center Bore for Mast Pole + IP68 M12x1.5 / PG7 Box-Wire Compression Cable Gland (Single Center Hole Only):
    mast_bore = cone_y(MAST_Y0 - 0.1, 19.0, mast_ir, mast_ir)
    gland_chamber = cone_y(18.8, 30.0, 13.0, 13.0)
    center_gland_hole = cone_y(29.5, 36.0, 6.1, 6.1)
    locknut_hex_seat = hex_prism_y(33.0, 35.0, across_flats=15.4)
    body = body.cut(mast_bore).cut(gland_chamber).cut(center_gland_hole).cut(locknut_hex_seat)

    # 5. Cut 6x M6 clearance bolt holes (Ø 6.4 mm at x = +/-25.5, 3 rows) on the solid flat clamping ears
    for xb in (-25.5, 25.5):
        for yb in CLAMP_BOLT_YS:
            bolt = (cq.Workplane("XY", origin=(0, 0, -10.0))
                    .center(xb, yb)
                    .circle(3.2)
                    .extrude(30.0))
            body = body.cut(bolt)

    # 6. Add 4 internal PCB standoffs supporting `main` (bottom of `main` is at y = 45.3) + 4 corner snap-fit retention clips
    #    Start at `y = 33.8 mm` (`0.70 mm` embedded into the `y = 34.5 mm` floor) so they are 100% fused into `base.stl`!
    #    The same posts + corner snap clips hold the `power` board flat 1 mm above the floor under `main`.
    for board in ("main", "power"):
        top_y = SPEC["boards"][board]["y"]
        bot_y = top_y - SPEC["boards"][board]["thickness"]
        for st in ef.standoffs(board):
            post = cone_y(33.8, bot_y, 1.65, 1.65).translate((st["x"], 0, st["z"]))
            body = body.union(post)
        for cx, cz, (dx, dz) in ef.CLIPS[board]:   # free-standing edge clips (see `ef.CLIPS`)
            guide = box(cx - 0.75, cx + 0.75, 33.8, top_y + 1.4, cz - 0.75, cz + 0.75)
            reach = 0.9 + ef.CLIP_REACH   # from the post centre: 0.9 mm to the board edge + 1.1 mm over the board
            hx = sorted((cx - 0.75 * dx, cx + reach * dx)) if dx else (cx - 0.75, cx + 0.75)
            hz = sorted((cz - 0.75 * dz, cz + reach * dz)) if dz else (cz - 0.75, cz + 0.75)
            hook = box(hx[0], hx[1], top_y + 0.12, top_y + 1.2, hz[0], hz[1])
            body = body.union(guide).union(hook)

    # 7. Cut 4x 4-slit 1.2 mm anti-spider micro-grilles through the bottom overhang (`r = 32..40 mm`)
    #    + 4x lowest-point perimeter condensation weep slots at the floor corner (`y = 34.4..35.3 mm`)!
    #    (30/330 deg under `main`, 120/240 deg beside the PMSA003I bed, clear of its duct chambers)
    for deg in (30, 120, 240, 330):
        rad = math.radians(deg)
        sx, sz = 35.5 * math.cos(rad), 35.5 * math.sin(rad)
        for offset in (-2.4, -0.8, 0.8, 2.4):
            slit = box(sx - 3.8, sx + 3.8, 30.5, 36.5, sz + offset - 0.6, sz + offset + 0.6)
            body = body.cut(slit)
        wx, wz = 41.8 * math.cos(rad), 41.8 * math.sin(rad)
        weep = box(wx - 2.0, wx + 2.0, 34.4, 35.4, wz - 2.0, wz + 2.0)
        body = body.cut(weep)

    # 8. Gravity-Return Captive Inverted-`T` REBOOT / FACTORY-RESET Button & Stepped Counterbore Boss (`at enc_x = +18.5, enc_z = -20.0 mm`,
    #    under `main SW1`, clear of the front mast saddle, the clamp flange band and the `main` standoffs):
    #    - No O-ring (so zero friction prevents `SW1`'s 1.5 N spring + gravity from returning the button!).
    #    - `btn_boss` (`y = 29.2..40.0 mm, Ø 10.4 mm OD`) has a stepped through-bore:
    #      * Lower guide bore (`y = 24.0..37.8 mm, r = 2.50 mm` = Ø 5.0 mm ID)
    #      * Upper T-flange counterbore (`y = 37.8..40.5 mm, r = 4.00 mm` = Ø 8.0 mm ID), forming a 1.50 mm-wide
    #        internal annular stop shelf at `y = 37.8 mm` so the T-button drops in from above and can NEVER fall through!
    #    - The Inverted-`T` Button Pin (`t_button`, with 0.35 mm radial and 0.30 mm vertical sliding clearance so it is
    #      a separate non-touching manifold shell in `base.stl`/`base.step`!) has:
    #      * Bottom protruding finger tip (`y = 27.0..38.10 mm, r = 2.15 mm` = Ø 4.3 mm)
    #      * Wide `T`-retaining flange (`y = 38.10..39.50 mm, r = 3.65 mm` = Ø 7.3 mm) resting above the `y = 37.80 mm` shelf
    #      * Top actuator tip (`y = 39.50..41.05 mm, r = 1.80 mm` = Ø 3.6 mm) sitting 0.25 mm below `SW1` on B.Cu (`y = 41.30 mm`)!
    #    (boss, bore, counterbore and button pin are BTN_UP = 10 mm taller since `main` sits 10 mm higher)
    bx, bz = 18.5, -20.0
    up = BTN_UP
    btn_boss = cone_y(29.2, 42.0 + up, 5.2, 5.2).translate((bx, 0, bz))
    btn_bottom_flat = cone_y(24.0, 29.2, 5.6, 5.6).translate((bx, 0, bz))
    btn_lower_bore = cone_y(24.0, 44.0 + up, 2.50, 2.50).translate((bx, 0, bz))
    btn_upper_counterbore = cone_y(39.80 + up, 44.0 + up, 4.00, 4.00).translate((bx, 0, bz))
    t_button = (cone_y(27.0, 40.10 + up, 2.15, 2.15)
                .union(cone_y(40.10 + up, 41.50 + up, 3.65, 3.65))
                .union(cone_y(41.50 + up, 43.55 + up, 1.80, 1.80))
                .translate((bx, 0, bz)))
    body = body.union(btn_boss).cut(btn_bottom_flat).cut(btn_lower_bore).cut(btn_upper_counterbore).union(t_button)

    # 9. Removable Conical "Umbrella" Drip Canopy (`y = 57.25..62.0 mm, r = 0..42.0 mm`) + 4 Click-Detent Wall Grooves:
    #    - 4 horizontal bayonet grooves on the inner wall of `base.stl` (`at 0, 90, 180, 270 deg`):
    #      * Lower support shelf (`y = 56.20..57.15 mm, r = 40.2..42.65 mm`) with a soft `0.30 mm` click-detent bump
    #        at `deg + 4.5..6.2` that clicks behind the tab edge when rotated into the locked position (`deg - 6..+4`),
    #        plus a solid rotation end-stop wall at `deg - 7.5`!
    #      * Upper keeper lip (`y = 58.65..59.55 mm, r = 40.2..42.65 mm, deg - 8.5..+4.5`) with a drop-in entry gap
    #        at `deg + 5..+15` so you drop the umbrella in, twist 10 deg until it clicks, or twist back 10 deg to lift out!
    #    - The Umbrella's 4 locking tabs are a smooth, continuous extension of the 1.25 mm-thick conical shell itself
    #      out to `r = 42.0 mm` (no boxy step!), and the cable pass-through window (`13.0 x 11.0 mm`) is >2x smaller!
    for deg in (0, 90, 180, 270):
        lower_shelf = arc_sector_y(55.90 + UMB, 56.90 + UMB, 40.2, 42.65, deg - 9.0, deg + 12.0)
        click_bump = arc_sector_y(56.90 + UMB, 57.18 + UMB, 40.5, 42.65, deg + 4.8, deg + 6.5)
        end_stop = arc_sector_y(56.90 + UMB, 59.05 + UMB, 40.2, 42.65, deg - 9.0, deg - 6.8)
        upper_keeper = arc_sector_y(59.05 + UMB, 59.95 + UMB, 40.2, 42.65, deg - 9.0, deg + 4.5)
        body = body.union(lower_shelf).union(click_bump).union(end_stop).union(upper_keeper)

    # Continuous conical shell sloping smoothly from (r=2.0, y=62.0) to (r=41.9, y=58.50) with 1.25 mm uniform thickness
    # and >= 0.25 mm vertical & angular clearance from the 4 wall grooves so the Removable Umbrella is a 100% separate
    # non-touching shell (`Split to Objects`) inside `base.stl`!
    umbrella_full = cone_y(58.50 + UMB, 62.0 + UMB, 41.9, 2.0).cut(cone_y(57.25 + UMB, 60.75 + UMB, 41.9, 0.5))
    outer_trim = ring_cone_y(56.5 + UMB, 60.0 + UMB, 39.0, 42.4, 39.0, 42.4)
    for deg in (0, 90, 180, 270):
        outer_trim = outer_trim.cut(arc_sector_y(56.0 + UMB, 60.5 + UMB, 38.8, 42.0, deg - 5.8, deg + 3.8))
    umbrella_cable_window = box(16.5, 31.5, 56.0 + UMB, 62.5 + UMB, 12.5, 26.5)   # over `main J2/J3` (crown + probe cables)
    umbrella_shell = umbrella_full.cut(outer_trim).cut(umbrella_cable_window)
    if umbrella:   # removable drip canopy (left out for the bay-floor render)
        body = body.union(umbrella_shell)

    # 10. Duct bed for the flat, face-down PMSA003I on the left half of the bay (see `ef.PM`).
    return add_pm_bed(body)


def add_pm_bed(body):
    """Two sealed duct chambers under the PMSA003I face + floor exit slits, hard-stop pads and two snap hooks:
      - Fan chamber (`x = -35.8..-15.6, z = -3.0..+17.8`) exhausts through 7 floor slits at the rear-left (`z = 9.8..17.4`).
      - Inlet chamber (`x = -34.6..-26.4, z = -15.0..-4.6`) draws through 3 floor slits at the front-left (`z = -14.6..-9.8`).
      - The chamber walls stop 1.0 mm under the module face: a 1.5 mm closed-cell foam gasket on the wall tops is squeezed
        to 1.0 mm by the hooks, so the chambers only connect the module openings to the outside air below the base.
      - The clamp flange band (`z = -8..+8`, `y < 28`) sits between the two exits under the base: no exhaust recirculation."""
    pm = ef.PM
    top = pm["face_y"] - pm["gasket"]
    w = pm["wall"]
    for ch in (pm["fan"], pm["inlet"]):
        frame = (box(ch["x0"] - w, ch["x1"] + w, pm["floor_y"] - 0.1, top, ch["z0"] - w, ch["z1"] + w)
                 .cut(box(ch["x0"], ch["x1"], pm["floor_y"] - 0.2, top + 0.1, ch["z0"], ch["z1"])))
        body = body.union(frame)
    roof = pm["inlet_roof"]
    body = body.union(box(pm["inlet"]["x0"] - w, pm["inlet"]["x1"] + w, top - 1.2, top, roof["z0"], roof["z1"]))
    for px, pz in pm["pads"]:
        body = body.union(cone_y(pm["floor_y"] - 0.1, pm["face_y"], 1.5, 1.5).translate((px, 0, pz)))
    hx0, hx1 = pm["hooks_x"]
    zh, t, lip = pm["z_half"], pm["hook_t"], pm["hook_lip"]
    for sz in (-1.0, 1.0):
        z_in, z_out = sz * (zh + 0.2), sz * (zh + 0.2 + t)
        post = box(hx0, hx1, pm["floor_y"] - 0.1, pm["top_y"] + 1.2, min(z_in, z_out), max(z_in, z_out))
        hook = box(hx0, hx1, pm["top_y"], pm["top_y"] + 1.2, min(sz * (zh - lip), z_out), max(sz * (zh - lip), z_out))
        body = body.union(post).union(hook)
    for slits, ch in ((pm["fan_slits"], pm["fan"]), (pm["inlet_slits"], pm["inlet"])):
        for x in slits["xs"]:
            body = body.cut(box(x - pm["slit_w"] / 2.0, x + pm["slit_w"] / 2.0, 26.0, pm["floor_y"] + 0.1,
                                slits["z0"], slits["z1"]))
    return body


def build_mast_clamp():
    """Part 2: Detachable Front Half-Moon Pole Clamp Saddle (y=0..28.0, z<=-1.5)
    with Solid Flat Bolt/Washer Seating Faces (x = -33..+33, z = -8.0)."""
    mast_ir = SPEC["envelope"]["mast_socket"]["inner_diameter"] / 2.0
    mast_or = SPEC["envelope"]["mast_socket"]["outer_diameter"] / 2.0

    # The clamp face sits CLAMP_TRAVEL behind the base face (z = -1.0) on a Ø 34 pole, so tightening the M6 bolts can
    # pull it CLAMP_TRAVEL closer: direct grip on Ø 32..34 mm poles, smaller poles with `mast_shims`.
    face = -1.0 - CLAMP_TRAVEL
    half_cyl = cone_y(MAST_Y0, 28.0, mast_or, mast_or).intersect(box(-40.0, 40.0, MAST_Y0 - 0.5, 29.0, -40.0, face))
    flange = box(-33.0, 33.0, MAST_Y0, 28.0, face - CLAMP_EAR_T, face)
    ear_flat_cut = (
        box(-35.0, -19.2, MAST_Y0 - 0.5, 28.5, -25.0, face - CLAMP_EAR_T)
        .union(box(19.2, 35.0, MAST_Y0 - 0.5, 28.5, -25.0, face - CLAMP_EAR_T))
    )
    body = half_cyl.union(flange).cut(cone_y(MAST_Y0 - 0.1, 29.0, mast_ir, mast_ir)).cut(ear_flat_cut)

    for xb in (-25.5, 25.5):
        for yb in CLAMP_BOLT_YS:
            bolt = (cq.Workplane("XY", origin=(0, 0, -15.0))
                    .center(xb, yb)
                    .circle(3.2)
                    .extrude(20.0))
            body = body.cut(bolt)

    return body


MAST_SHIMS = (1.0, 2.0, 3.0, 4.0)   # shim wall t: bore Ø (34 - 2t); with 2.0 mm clamp travel each covers 2 mm of pole Ø (24..32)


def build_mast_shims():
    """Part 11: Printable split reducer shims for poles smaller than Ø 32 mm (one pair per size, 170-deg half
    shells, 50 mm tall = the pole engagement of the mast bore). Laid out side by side next to the mast socket."""
    mast_ir = SPEC["envelope"]["mast_socket"]["inner_diameter"] / 2.0
    out = None
    for k, t in enumerate(MAST_SHIMS):
        for side in (0, 1):
            shell = ring_cone_y(MAST_Y0, 19.0, mast_ir - t, mast_ir - 0.15, mast_ir - t, mast_ir - 0.15)
            shell = shell.intersect(arc_sector_y(MAST_Y0 - 1.0, 20.0, 1.0, mast_ir + 1.0, 5.0, 175.0, steps=24))
            shell = shell.rotate((0, 0, 0), (0, 1, 0), 180.0 * side).translate((60.0 + 40.0 * k, 0, 0))
            out = shell if out is None else out.union(shell)
    return out


def _build_probe_sleeve_cyl_ends(z_min, z_max):
    """Build half of:
      - Bottom Split-Cylinder M22x1.5 Male Helical Threaded Spigot (y = 68.5..75.5, r_root = 10.4 mm) + wide Ø 26.8 mm
        upper sealing flange (y = 74.4..75.5) that screws directly into `pagoda`'s waterproof bottom bulkhead deck!
      - Internal Blind Pocket (`y = 68.0..76.25, x = -7.05..+7.05, z = +0.6..+5.1 mm`) that encloses `probe J1`
        100% INSIDE the solid front wall of `probe_sleeve_front` (zero external notch or hole — only open at the
        bottom `y = 68.5 mm` inside `base.stl` where the cable plugs in from below)!
      - Immediately above `J1` (`y = 76.25..80.2 mm`), both `probe_sleeve_front` (`z = +0.70 mm`) and `probe_sleeve_back`
        (`z = -0.70 mm`) pinch within `0.10 mm` of the `probe` PCB (`z = -0.60..+0.60 mm`) with a Grease / Glue Groove
        (`y = 77.2..79.2 mm`) so zero water can ever drip down onto the `main` board!
      - Wide Dismountable Rear Cable Pass-Through Duct (`x = -6.3..+6.3 mm, z = -4.3..-0.95 mm`) for the round 10-core cable!"""
    bot_cyl = cone_y(68.5, 74.4, 10.4, 10.4).union(cone_y(74.4, 75.5, 13.4, 13.4))
    bot_cyl = add_external_thread_y(bot_cyl, (69.8, 71.3, 72.8), r_root=10.4, depth=0.65, half_base=0.45, half_crest=0.14)
    pcb_close_slot = box(-7.15, 7.15, 68.0, 76.5, -0.70, 0.70)
    # Blind internal pocket (`z = +0.6..+5.1 mm`, leaving a solid >2.5 mm front wall at z = +5.1..+10.4 mm!)
    j1_blind_pocket = box(-7.05, 7.05, 68.0, 76.25, 0.6, 5.1)
    crown_cable_duct = box(-6.3, 6.3, 68.0, 76.5, -4.3, -0.95)
    bot_cyl = bot_cyl.cut(pcb_close_slot).cut(j1_blind_pocket).cut(crown_cable_duct)

    # Top smooth press-fit cylindrical spigot (y = 113.5..118.0, r = 10.4 mm smooth, 0.4 mm lead-in taper at top, stop shoulder at y = 113.5..114.5)
    top_cyl = (cone_y(113.5, 114.5, 12.4, 12.4)
               .union(cone_y(114.5, 117.4, 10.4, 10.4))
               .union(cone_y(117.4, 118.0, 10.4, 10.0)))
    top_pcb_close = box(-7.15, 7.15, 113.0, 119.0, -0.70, 0.70)
    top_j2_window = box(-5.5, 5.5, 111.0, 119.0, 0.6, 3.4)
    top_cable_duct = box(-6.3, 6.3, 113.0, 119.0, -4.3, -0.95)
    top_cyl = top_cyl.cut(top_pcb_close).cut(top_j2_window).cut(top_cable_duct)

    half_clip = box(-15.0, 15.0, 68.0, 119.5, z_min, z_max)
    return bot_cyl.union(top_cyl).intersect(half_clip)


def build_probe_sleeve_back():
    return _build_probe_sleeve_back().translate((0, RAISE, 0))


def _build_probe_sleeve_back():
    """Part 4: Rear Clamshell Sleeve for `probe` PCB (y=68.5..118.0, z=-5.0..-0.05):
      - Between `y = 68.5..80.2 mm`, the inner wall hugs the `probe` PCB back face at `z = -0.70 mm`
        (`0.10 mm` gap from `z = -0.60 mm`!) with a grease/glue reservoir groove at `y = 77.2..79.2 mm`!
      - Dedicated `12.6 x 3.35 mm` Vertical Rear Cable Conduit (`x = -6.3..+6.3 mm, z = -4.3..-0.95 mm`) running
        the full height so a round 10-core cable passes through with or without its 10-pin JST-SH connector!"""
    shell = box(-9.2, 9.2, 75.0, 114.0, -4.8, -0.05)
    back_dead_air = box(-7.4, 7.4, 80.2, 113.0, -3.4, -0.70)
    back_grease_groove = box(-7.0, 7.0, 77.2, 79.2, -1.8, -0.6)
    pcb_seat = box(-7.15, 7.15, 68.0, 119.0, -0.70, 0.1)
    rear_cable_channel = box(-6.3, 6.3, 68.0, 119.0, -4.3, -0.95)
    left_groove = box(-8.5, -7.6, 75.5, 113.5, -1.2, 0.1)
    right_groove = box(7.6, 8.5, 75.5, 113.5, -1.2, 0.1)
    body = shell.cut(back_dead_air).cut(back_grease_groove).cut(pcb_seat).cut(left_groove).cut(right_groove)
    return body.union(_build_probe_sleeve_cyl_ends(-15.0, -0.05)).cut(pcb_seat).cut(rear_cable_channel).cut(back_grease_groove)


PROBE_FRONT_SLOTS_Y = (93.0, 96.5, 100.0, 103.5, 107.0, 110.5)  # 3.5 mm grid: SCD41 (93..100), SGP41 + BMP585 (103.5..110.5)
PROBE_SIDE_SLOTS_Y = (92.0, 95.5, 99.0, 102.5, 106.0, 109.5)


def build_probe_sleeve_front():
    return _build_probe_sleeve_front().translate((0, RAISE, 0))


def _build_probe_sleeve_front():
    """Part 5: Front Clamshell Sleeve for `probe` PCB (y=68.5..118.0, z=+0.05..+8.8):
      - Solid Enclosed Lower Shroud (`y = 75.0..77.2, z = +0.05..+6.5 mm`) completely covering `probe J1` on the front
        so there is ZERO external notch or opening!
      - Immediately above `J1` (`y = 76.25..80.2 mm`, below `SHT40`), the inner wall hugs the `probe` PCB front face at `z = +0.70 mm`
        (`0.10 mm` gap from `z = +0.60 mm`!) with a grease/glue reservoir groove at `y = 77.2..79.2 mm`!
      - Upper gas chamber (`y = 89.2..113.0 mm`) around the `SCD41`, `SGP41` and `BMP585`: one uniform grid of 6 front
        slots (`12 x 1.2 mm`, 3.5 mm pitch; the slot at `y = 107.0` lies right over the SGP41 hot plate and the BMP585 gel
        port), plus 6 side slots in each side wall so the shield airflow crosses the chamber."""
    j1_cover_shroud = box(-9.2, 9.2, 75.0, 77.4, 0.05, 6.5)
    lower_hood = box(-9.2, 9.2, 75.0, 89.2, 0.05, 3.4)
    upper_hood = box(-9.2, 9.2, 87.8, 114.0, 0.05, 8.8)
    shell = j1_cover_shroud.union(lower_hood).union(upper_hood)

    pcb_seat = box(-7.15, 7.15, 68.0, 119.0, -0.1, 0.70)
    j1_blind_pocket = box(-7.05, 7.05, 68.0, 76.25, 0.6, 5.1)
    j2_top_clear = box(-5.5, 5.5, 111.0, 119.5, 0.6, 3.4)
    front_grease_groove = box(-7.0, 7.0, 77.2, 79.2, 0.6, 1.8)
    upper_gas_chamber = box(-7.4, 7.4, 89.2, 113.0, 0.65, 7.5)
    lower_sht_chamber = box(-7.4, 7.4, 80.2, 87.8, 0.65, 2.1)
    sht45_venturi = box(-3.8, 0.8, 80.2, 84.8, 0.65, 4.0).union(box(-9.5, 9.5, 80.5, 84.5, 0.8, 1.9))

    left_tongue = box(-8.32, -7.78, 75.8, 113.2, -0.95, 0.25)
    right_tongue = box(7.78, 8.32, 75.8, 113.2, -0.95, 0.25)

    body = (
        shell
        .cut(pcb_seat)
        .cut(upper_gas_chamber)
        .cut(lower_sht_chamber)
        .cut(sht45_venturi)
        .union(left_tongue)
        .union(right_tongue)
    )
    for ys in PROBE_FRONT_SLOTS_Y:
        body = body.cut(box(-6.0, 6.0, ys - 0.6, ys + 0.6, 7.0, 9.2))
    for ys in PROBE_SIDE_SLOTS_Y:  # cross-flow through both side walls (x = +/-7.4..9.2)
        for sx in (-1.0, 1.0):
            body = body.cut(box(min(sx * 7.0, sx * 9.6), max(sx * 7.0, sx * 9.6), ys - 0.8, ys + 0.8, 1.8, 6.8))

    return (body.union(_build_probe_sleeve_cyl_ends(0.05, 15.0))
            .cut(pcb_seat)
            .cut(j1_blind_pocket)
            .cut(j2_top_clear)
            .cut(front_grease_groove))


def build_pagoda():
    return _build_pagoda().translate((0, RAISE, 0))


def _build_pagoda():
    """Part 6: Heavy-Duty Bolted 5-Plate Stevenson Pagoda Assembly (`pagoda.stl/.step`, y=66.5..123.8):
      - Heavy-Duty 5.0 mm-Thick Waterproof Bottom Bulkhead Deck (`y = 68.5..73.5, r = 10.58..48.0 mm`):
        Completely seals off `base.stl` below from wind-driven rain in the pagoda louvers above, supports the full
        weight of the upper station, and carries a Central M22x1.5 Internal Thread (`y = 68.5..73.5`) + O-Ring #5 Seat
        (`y = 73.4..74.4, r = 10.2..13.2 mm`) where the bottom of `probe_sleeve` screws in watertight!
      - 4x M4 Stainless Steel Through-Bolt Columns (`r = 26.0 mm at 45/135/225/315 deg`, Ø 4.2 mm through-bores from
        `y = 68.5..120.0 mm` with bottom M4 hex nut pockets at `y = 68.5..70.3` and top M4 socket-cap seats at `y = 118.2..120.2`)
        clamping the entire pagoda stack into a rock-solid structure!
      - Seamless Top Cap & M90x1.8 Threaded Collar (`y = 114.5..123.8`): The 5th louver merges seamlessly with zero gap
        into the top M90x1.8 threaded collar and central spider hub (`y = 114.5..116.0`), where the top M22x1.5 hex nut
        (`y = 115.8..118.4`) clamps the top of `probe_sleeve`!
    """
    skirt = ring_cone_y(66.9, 73.5, 44.18, OUTER_R, 44.18, OUTER_R)
    skirt = cut_internal_bayonet_y(skirt, 66.4, r_minor=44.18, r_slot=45.40, n_lugs=8)

    # Heavy-Duty 3.5 mm-Thick Full-Circle Waterproof Bottom Bulkhead Deck (y = 70.1..73.5, r = 10.58..48.0 mm)
    # + Raised Central Boss (y = 68.5..74.2, r = 10.58..15.0 mm) with O-Ring #2 groove (y = 73.4..74.8, r = 10.5..13.6)
    # and True Meshing M22x1.5 Internal Threads (y = 69.8, 71.3, 72.8)!
    bot_deck = ring_cone_y(70.1, 73.5, 13.5, OUTER_R, 13.5, OUTER_R)
    bot_shed_cone = ring_cone_y(73.5, 74.6, 13.5, 44.5, 13.5, 22.0)
    bot_hub = ring_cone_y(67.8, 74.4, 10.58, 15.0, 10.58, 15.0)
    bot_hub = cut_internal_thread_y(bot_hub, (69.8, 71.3, 72.8),
                                    r_minor=10.58, depth=0.65, half_base=0.45, half_crest=0.14, clearance=0.18)
    bot_oring_seat = cone_y(73.4, 74.8, 13.6, 13.6)
    body = skirt.union(bot_deck).union(bot_shed_cone).union(bot_hub.cut(bot_oring_seat))

    # Bottom column base pads (y = 73.0..74.2, r = 5.0 mm) + Interlocking Male Spigot Endings (y = 73.8..75.4, r = 3.1..3.95 mm)
    # placed at r = 20.5 mm so pagoda M6 bolts and the r = 29.5 mm square wind_tunnel struts never collide!
    for deg in (45, 135, 225, 315):
        rad = math.radians(deg)
        px, pz = 20.5 * math.cos(rad), 20.5 * math.sin(rad)
        base_pad = cone_y(72.5, 74.2, 5.0, 5.0).translate((px, 0, pz))
        male_tip = cone_y(73.8, 75.4, 3.95, 3.95).translate((px, 0, pz))
        body = body.union(base_pad).union(male_tip)

    # 4 Lower Stackable Thin (1.6 mm wall) Conical Louver Rings (`plate_ys = 74, 85, 96, 107 mm`)
    # Each spacer column (`exact Ø 10.0 mm OD / Ø 6.2 mm ID` at r = 20.5 mm) has a Female Socket Bottom (`r = 4.1..5.0 mm`)
    # and a Male Spigot Top (`r = 3.1..3.95 mm`) so every column segment locks snugly into the next before bolting!
    for idx, py in enumerate(SPEC["envelope"]["pagoda_shield"]["plate_ys"][:4]):
        # 12.3-deg conical blade, LOUVER_T thick normal to its surface (radial offset = t / sin(slope)); the upper
        # surface is unchanged, the extra material hangs below it.
        dr = LOUVER_T / math.sin(math.atan(5.2 / 23.8))
        louver_blade = ring_cone_y(py - 2.2, py + 3.0, OUTER_R - dr, OUTER_R, 24.2 - dr, 24.2)
        drip_lip = ring_cone_y(py - 4.5, py - 2.0, OUTER_R - 1.8, OUTER_R, OUTER_R - 1.8, OUTER_R)
        plate_ring = louver_blade.union(drip_lip)
        y_bot = 74.35 if idx == 0 else (py - 0.65)
        y_top_shoulder = (py + 10.2) if idx < 3 else 115.35
        for deg in (45, 135, 225, 315):
            rad = math.radians(deg)
            px, pz = 20.5 * math.cos(rad), 20.5 * math.sin(rad)
            col_main = cone_y(y_bot, y_top_shoulder, 5.0, 5.0).translate((px, 0, pz))
            female_sock = cone_y(y_bot - 0.1, y_bot + 1.25, 4.1, 4.1).translate((px, 0, pz))
            male_spigot = cone_y(y_top_shoulder - 0.3, y_top_shoulder + 1.25, 3.95, 3.95).translate((px, 0, pz))
            boss = col_main.union(male_spigot).cut(female_sock)
            ux, uz = math.cos(rad), math.sin(rad)
            web = (cq.Workplane("XZ", origin=(0, py + 3.0, 0))
                   .polyline([(19.0 * ux - 2.2 * uz, 19.0 * uz + 2.2 * ux),
                              (25.0 * ux - 2.2 * uz, 25.0 * uz + 2.2 * ux),
                              (25.0 * ux + 2.2 * uz, 25.0 * uz - 2.2 * ux),
                              (19.0 * ux + 2.2 * uz, 19.0 * uz - 2.2 * ux)])
                   .close().extrude(2.2))
            plate_ring = plate_ring.union(boss).union(web)
        body = body.union(plate_ring)

    # 5th (Topmost) Louver + Continuous 360-deg Solid Top Structural Bulkhead (`y = 115.5..120.1 mm`)
    top_drip_lip = ring_cone_y(113.5, 116.0, OUTER_R - 1.8, OUTER_R, OUTER_R - 1.8, OUTER_R)
    top_structural_deck = (
        ring_cone_y(115.5, 118.8, 44.8, OUTER_R, 10.55, OUTER_R)
        .union(ring_cone_y(117.0, 120.1, 10.55, OUTER_R, 10.55, OUTER_R))
    )
    top_oring_groove = ring_cone_y(ORING_FLOOR_BELOW_SEAT + 120.6, 120.1, 43.9, OUTER_R - 0.8, 43.9, OUTER_R - 0.8)
    top_spigot = ring_cone_y(119.8, 123.9, 42.6, 44.0, 42.6, 44.0)
    center_refl_well = cone_y(118.2, 124.2, 14.5, 14.5)
    top_cap = (top_drip_lip.union(top_structural_deck)
               .cut(center_refl_well)
               .union(ring_cone_y(114.6, 118.4, 10.55, 13.4, 10.55, 13.4))
               .cut(top_oring_groove)
               .union(top_spigot))
    top_cap = add_external_bayonet_y(top_cap, 120.1, r_root=44.0, r_lug=45.15, n_lugs=8)
    top_cap = cut_lock_triangles_y(top_cap, y_base=115.7, y_tip=118.1)
    body = body.union(top_cap)

    for deg in (45, 135, 225, 315):
        rad = math.radians(deg)
        px, pz = 20.5 * math.cos(rad), 20.5 * math.sin(rad)
        top_well_boss = cone_y(115.5, 120.6, M6_HEAD_D / 2.0 + 1.1, M6_HEAD_D / 2.0 + 1.1).translate((px, 0, pz))
        top_female_sock = cone_y(115.4, 116.8, 4.1, 4.1).translate((px, 0, pz))
        bolt_head_well = cone_y(117.8, 121.0, M6_HEAD_D / 2.0 + 0.3, M6_HEAD_D / 2.0 + 0.3).translate((px, 0, pz))
        m6_bore = cone_y(68.0, 121.6, 3.1, 3.1).translate((px, 0, pz))
        body = body.union(top_well_boss).cut(top_female_sock).cut(bolt_head_well).cut(m6_bore)

    # 4x Sheltered Ventilation & Moisture-Weep Micro-Grilles in `pagoda`'s Top Piece (`at 0, 90, 180, 270 deg`, `r = 25..36 mm`, `y = 114.8..120.2 mm`)
    # so any moisture or condensation entering through the C-shaped struts or joints dissipates downward into the 5th louver space!
    for deg in (0, 90, 180, 270):
        rad = math.radians(deg)
        gx, gz = 30.5 * math.cos(rad), 30.5 * math.sin(rad)
        for off in (-2.4, 0.0, 2.4):
            if deg in (90, 270):
                vent_slit = box(gx - 3.5, gx + 3.5, 114.8, 120.2, gz + off - 0.6, gz + off + 0.6)
            else:
                vent_slit = box(gx + off - 0.6, gx + off + 0.6, 114.8, 120.2, gz - 3.5, gz + 3.5)
            body = body.cut(vent_slit)

    body = cut_lock_triangles_y(body, y_base=70.1, y_tip=67.7)
    return body


def build_wind_tunnel():
    return _build_wind_tunnel().translate((0, RAISE, 0))


def _build_wind_tunnel():
    """Part 7: Compact Heated Conical Reflector Deck (`wind_tunnel.stl/.step`, y=120.1..130.0):
      - Outer Bottom Skirt (y = 120.1..125.0) with 8-Lug Internal Push-Turn-Click Bayonet J-Slots
        locking onto `pagoda.stl` and compressing O-Ring #3 at the widest radius!
      - Central Underside Cavity (y = 118.5..124.48) housing the 24x24 mm horizontal `reflector` heater PCB
        plus a Radial Cable Pass-Through Channel from the 45-deg NE strut pocket to the center `probe_sleeve_back` duct!
    """
    # The bayonet skirt stays on the pagoda joint; the skirt is TOP_RAISE taller so the deck (and everything that
    # bolts onto it) sits TOP_RAISE higher, leaving a 10.4 mm crown-cable / service gap above the pagoda top deck.
    outer_skirt = ring_cone_y(120.6, 125.0 + TOP_RAISE, 44.18, OUTER_R, 44.18, OUTER_R)
    outer_skirt = cut_internal_bayonet_y(outer_skirt, 120.1, r_minor=44.18, r_slot=45.40, n_lugs=8)

    deck_plate = cone_y(124.48, 126.5, OUTER_R, OUTER_R)
    reflector_cone = cone_y(126.5, 130.0, OUTER_R, 6.0)
    upper = deck_plate.union(reflector_cone)

    for st in ef.strut_centres():
        sx, sz = st["x"], st["z"]
        # Recessed `nut_pocket` pushed 1.5 mm deeper into the thick `wind_tunnel` deck (`up to y = 126.0 mm`)
        # with `spot_face` at `y = 127.15 mm` so there is 3.4..5.9 mm of open vertical wire space above `pagoda` top (`y = 120.1 mm`)!
        spot_face = cone_y(127.15, 130.5, 7.2, 7.2).translate((sx, 0, sz))
        strut_hole = cone_y(123.0, 130.5, 6.1, 6.1).translate((sx, 0, sz))
        nut_pocket = cone_y(118.5, 126.0, STRUT_NUT_POCKET_R, STRUT_NUT_POCKET_R).translate((sx, 0, sz))
        upper = upper.cut(spot_face).cut(strut_hole).cut(nut_pocket)

    body = outer_skirt.union(upper.translate((0, TOP_RAISE, 0)))
    return cut_lock_triangles_y(body, y_base=123.9, y_tip=121.5)


def build_struts():
    return _build_struts().translate((0, UPPER, 0))


def _build_struts():
    """Part 8: 4x Separate Double-C Split Hollow Threaded Struts (Standard ISO M12x1.75) + 8x M12x1.75 Hex Locknuts
    + 1x Standard ISO M22x1.5 JGS1 Dome Hex Flange Retainer Nut (`struts.stl/.step`):
      - Each strut (at 45, 135, 225, 315 deg, r = 35.5 mm) is split lengthwise into TWO distinct C-shaped half-shells
        (`0.48 mm` longitudinal seam gap) with a hollow Ø 6.4 mm inner cable channel!
      - Uses standard ISO M12x1.75 threads (`r_root = 4.92 mm, depth = 0.90 mm -> d_major = 11.64 mm, pitch = 1.75 mm`)
        and standard ISO M22x1.5 threads on the central JGS1 dome hex retainer nut so standard off-the-shelf metal
        M12x1.75 and M22x1.5 nuts also fit directly if desired!
    """
    all_struts = None
    for st in ef.strut_centres():
        sx, sz = st["x"], st["z"]
        # Use a 2.12 mm thick wall (`r = 2.90..5.02 mm`) along the central unthreaded span (`y = 128.5..172.5 mm`)
        # so the C-shaped side walls have a deep, unmistakable 3-wall Male Tongue + Female U-Groove joint!
        tube_ends = ring_cone_y(123.3, 178.9, 3.00, 4.92, 3.00, 4.92).translate((sx, 0, sz))
        tube_mid = ring_cone_y(128.5, 172.5, 2.90, 5.05, 2.90, 5.05).translate((sx, 0, sz))
        bot_stopper = ring_cone_y(127.2, 128.5, 3.00, 7.0, 3.00, 7.0).translate((sx, 0, sz))
        top_stopper = ring_cone_y(172.5, 174.0, 3.00, 7.0, 3.00, 7.0).translate((sx, 0, sz))
        strut = tube_ends.union(tube_mid).union(bot_stopper).union(top_stopper)

        # Add standard ISO M12x1.75 external male helical threads (pitch = 1.75 mm) on bottom (`recessed higher into wind_tunnel!`) and top ends
        strut = add_external_thread_y(strut, (123.9, 125.65),
                                      r_root=4.92, depth=0.90, half_base=0.55, half_crest=0.16, cx=sx, cz=sz)
        strut = add_external_thread_y(strut, (176.1, 177.85),
                                      r_root=4.92, depth=0.90, half_base=0.55, half_crest=0.16, cx=sx, cz=sz)

        # Split the hollow threaded tube into two hermaphroditic C-shaped half-shells (`z < sz - 0.18` and `z > sz + 0.18`):
        #   - On the `-X` C-wall (`dx < 0`): Front C-Half has a protruding Male Tongue (`z = sz - 1.15..sz + 0.35`)
        #     mating into a deep Female U-Groove (`z = sz - 1.45..sz - 0.10`) in the Back C-Half!
        #   - On the `+X` C-wall (`dx > 0`): Back C-Half has a protruding Male Tongue (`z = sz - 0.35..sz + 1.15`)
        #     mating into a deep Female U-Groove (`z = sz + 0.10..sz + 1.45`) in the Front C-Half!
        split_slot = box(sx - 8.5, sx + 8.5, 122.8, 179.2, sz - 0.18, sz + 0.18)
        strut = strut.cut(split_slot)
        # -X wall: Female U-Groove in Back C-Half (`z < sz`) + Male Tongue on Front C-Half (`z > sz`)
        fem_groove_negx = box(sx - 4.50, sx - 3.44, 128.55, 172.45, sz - 1.45, sz - 0.10)
        male_tongue_negx = box(sx - 4.30, sx - 3.64, 128.80, 172.20, sz - 1.15, sz + 0.35)
        # +X wall: Female U-Groove in Front C-Half (`z > sz`) + Male Tongue on Back C-Half (`z < sz`)
        fem_groove_posx = box(sx + 3.44, sx + 4.50, 128.55, 172.45, sz + 0.10, sz + 1.45)
        male_tongue_posx = box(sx + 3.64, sx + 4.30, 128.80, 172.20, sz - 0.35, sz + 1.15)
        strut = (strut.cut(fem_groove_negx).cut(fem_groove_posx)
                 .union(male_tongue_negx).union(male_tongue_posx))

        # Bottom & Top ISO M12x1.75 Hex Locknuts (`bot_nut` recessed up at y = 123.5..125.9 mm inside `wind_tunnel`!)
        bot_nut = hex_prism_y(123.5, 125.9, across_flats=STRUT_NUT_AF, cx=sx, cz=sz).cut(
            cone_y(123.3, 126.1, 5.12, 5.12).translate((sx, 0, sz)))
        bot_nut = cut_internal_thread_y(bot_nut, (123.9, 125.65),
                                        r_minor=5.12, depth=0.90, half_base=0.55, half_crest=0.16, clearance=0.20, cx=sx, cz=sz)

        top_nut = hex_prism_y(175.8, 178.4, across_flats=STRUT_NUT_AF, cx=sx, cz=sz).cut(
            cone_y(175.6, 178.6, 5.12, 5.12).translate((sx, 0, sz)))
        top_nut = cut_internal_thread_y(top_nut, (176.1, 177.85),
                                        r_minor=5.12, depth=0.90, half_base=0.55, half_crest=0.16, clearance=0.20, cx=sx, cz=sz)

        assy = strut.union(bot_nut).union(top_nut)
        all_struts = assy if all_struts is None else all_struts.union(assy)

    # Include the printable Standard ISO M22x1.5 JGS1 Quartz Dome Hex-Flange Retainer Nut (`at y = 189.8..193.0 mm`)
    # inside `struts.stl` so all 8 C-strut halves, all 8 M12x1.75 strut locknuts, and the M22x1.5 JGS1 dome nut print together!
    win = ef.uv_window()
    uv_nut_hex = hex_prism_y(189.8, 191.0, across_flats=22.0, cx=win["x"], cz=win["z"]).cut(
        cone_y(189.5, 193.5, 8.2, 8.2).translate((win["x"], 0, win["z"])))
    uv_nut_barrel = ring_cone_y(190.4, 193.0, 8.2, 10.20, 8.2, 10.20).translate((win["x"], 0, win["z"]))
    uv_nut = add_external_thread_y(uv_nut_hex.union(uv_nut_barrel), (191.2, 192.7),
                                   r_root=10.20, depth=0.70, half_base=0.45, half_crest=0.14,
                                   cx=win["x"], cz=win["z"])
    # Also include 4x Printable Slotted Press-Fit Rear Retaining Rings (`for locking the 4 ultrasonic transducers
    # into their angled barrel seats from the back!`) arranged around the central M22 dome nut at r = 18.0 mm:
    for deg in (0, 90, 180, 270):
        rad = math.radians(deg)
        rx, rz = 18.0 * math.cos(rad), 18.0 * math.sin(rad)
        ret_ring = (ring_cone_y(188.0, 189.8, 3.1, 5.2, 3.1, 5.2)
                    .cut(box(-1.3, 1.3, 187.5, 190.5, 0.0, 6.0))
                    .translate((rx, 0, rz)))
        all_struts = all_struts.union(ret_ring)
    return all_struts.union(uv_nut)


def build_crown_base():
    return _build_crown_base().translate((0, UPPER, 0))


def _build_crown_base():
    """Part 9: Lower Ultrasonic Transducer & Diagonal `crown` PCB Tray (`crown_base.stl/.step`, y=172.5..189.8):
      - 4x Strut Holes (`r = 32.5 mm` at 45/135/225/315 deg — a full 10.0 mm inside the r = 42.5 mm wall so the
        ISO M12x1.75 hex locknuts have >3.0 mm of open wrench clearance all around!)
      - 4x Ultrasonic Transducer Collars (`r = 26.5 mm` at 0/90/180/270 deg — 100% aligned with XD_N, XD_E, XD_S, XD_W!)
      - Diagonal (45-deg rotated) `crown` PCB Standoffs (`y = 175.8..182.5 mm` at r = 17.5..18.5 mm, raising `crown` PCB
        2.0 mm above the top of the transducer cans and leaving the 45-deg NE cable strut and all 4 M12 nuts unobstructed!)
    """
    shroud = cone_y(174.0, 186.0, OUTER_R, OUTER_R).union(cone_y(186.0, 189.8, 44.0, 44.0))
    crown_drip = ring_cone_y(172.5, 174.5, OUTER_R - 1.2, OUTER_R + 0.6, OUTER_R - 1.2, OUTER_R)
    oring_groove = ring_cone_y(ORING_FLOOR_BELOW_SEAT + 186.5, 186.0, 43.9, OUTER_R - 0.8, 43.9, OUTER_R - 0.8)
    body = shroud.union(crown_drip).cut(oring_groove)
    body = add_external_bayonet_y(body, 186.0, r_root=44.0, r_lug=45.15, n_lugs=8)
    body = cut_lock_triangles_y(body, y_base=181.6, y_tip=184.0)

    cavity = cone_y(175.8, 190.2, 42.5, 42.5)
    body = body.cut(cavity)

    crown_bot_y = SPEC["boards"]["crown"]["y"] - SPEC["boards"]["crown"]["thickness"]
    tilt_deg = SPEC["envelope"]["wind_tunnel"].get("transducer_tilt_deg", 29.0)
    for tp in ef.transducer_pockets():
        tx, tz = tp["x"], tp["z"]
        # Full-Length Inward-Looking (`29.0 deg` V-Beam) Ultrasonic Barrel Seat matching the exact two-segment bottom profile:
        #   1. Left/inward angled face (`barrel_angled`, starting at `s = -2.80 mm` — parallel to and 1.00 mm below the
        #      sensor's bottom face at `s = -1.80 mm`)
        #   2. Right/outward horizontal face (`barrel_above_floor`, clipped flat at the `y = 174.00 mm` underside floor
        #      of `crown_base.stl` so nothing hangs below `y = 174.00 mm` on the right side while filling the barrel above `y = 174.00 mm`!)
        axis = (tz, 0, -tx) if (tx != 0 or tz != 0) else (1, 0, 0)
        barrel_angled = (cone_y(-2.80, 4.2, 6.80, 6.80)
                         .rotate((0, 0, 0), (0, 1, 0), 23.0)
                         .rotate((0, 0, 0), axis, tilt_deg)
                         .translate((tx, 176.2, tz)))
        barrel_above_floor = (cone_y(-7.5, 4.2, 6.80, 6.80)
                              .rotate((0, 0, 0), (0, 1, 0), 23.0)
                              .rotate((0, 0, 0), axis, tilt_deg)
                              .translate((tx, 176.2, tz))
                              .intersect(box(-42.0, 42.0, 174.0, 185.0, -42.0, 42.0)))
        barrel_shroud = barrel_angled.union(barrel_above_floor)
        upper_seat_cup = (cone_y(-1.80, 8.0, 5.25, 5.25)
                          .rotate((0, 0, 0), (0, 1, 0), 23.0)
                          .rotate((0, 0, 0), axis, tilt_deg)
                          .translate((tx, 176.2, tz)))
        lower_aperture = (cone_y(-9.0, 8.0, 4.45, 4.45)
                          .rotate((0, 0, 0), (0, 1, 0), 23.0)
                          .rotate((0, 0, 0), axis, tilt_deg)
                          .translate((tx, 176.2, tz)))
        body = body.union(barrel_shroud).cut(upper_seat_cup).cut(lower_aperture)
    # Trim the sharp lowest barrel corner horizontally at `y = 171.05 mm` (leaving `1.03 mm` of solid plastic
    # directly below the sensor's deepest corner at `y = 172.08 mm`!):
    body = body.cut(box(-45.0, 45.0, 160.0, 171.05, -45.0, 45.0))

    for st in ef.strut_centres():
        sx, sz = st["x"], st["z"]
        strut_hole = cone_y(173.8, 176.2, 6.1, 6.1).translate((sx, 0, sz))
        nut_pocket = cone_y(175.8, 179.0, STRUT_NUT_POCKET_R, STRUT_NUT_POCKET_R).translate((sx, 0, sz))
        body = body.cut(strut_hole).cut(nut_pocket)

    for st in ef.standoffs("crown"):
        post = cone_y(175.8, crown_bot_y, 1.7, 1.7).translate((st["x"], 0, st["z"]))
        body = body.union(post)

    # 4x 3-slit 1.2 mm downward-facing breather grilles through the floor (r = 14 mm, between the 4 transducer barrels).
    # The JGS1 dome's inside is open to this cavity through the retainer bore, so the dome breathes with it and dries out
    # after a dew night instead of trapping moisture (same principle as the base bay grilles; rain cannot fall upward).
    for deg in CROWN_VENT_DEGS:
        for off in (-1.6, 0.0, 1.6):
            slit = box(-3.0, 3.0, 173.8, 176.0, CROWN_VENT_R + off - 0.6, CROWN_VENT_R + off + 0.6)
            body = body.cut(slit.rotate((0, 0, 0), (0, 1, 0), 90.0 - deg))

    # 4x Snap-Fit Retention Clips over straight edges of the 60 x 60 mm `crown` PCB (rotationally symmetric, >= 11 mm
    # from the strut nut pockets and the transducer barrels): (clip x, clip z, hook direction toward the board)
    for cx, cz, (dx, dz) in CROWN_CLIPS:
        if dx:
            guide = box(cx - 0.75, cx + 0.75, 175.8, 184.0, cz - 1.2, cz + 1.2)
            hook = box(min(cx - dx * 0.75, cx + dx * 1.95), max(cx - dx * 0.75, cx + dx * 1.95), 182.92, 183.9,
                       cz - 1.2, cz + 1.2)
        else:
            guide = box(cx - 1.2, cx + 1.2, 175.8, 184.0, cz - 0.75, cz + 0.75)
            hook = box(cx - 1.2, cx + 1.2, 182.92, 183.9,
                       min(cz - dz * 0.75, cz + dz * 1.95), max(cz - dz * 0.75, cz + dz * 1.95))
        body = body.union(guide).union(hook)

    return body


def build_crown():
    return _build_crown().translate((0, UPPER, 0))


def _build_crown():
    """Part 10: Removable Top Piezo Rain & JGS1 UV Dome Cover (`crown.stl/.step`, y=185.8..198.0):
      - Open on the bottom with an Internal M90x1.8 Female Thread (y = 186.0..190.2) that screws onto `crown_base.stl`
        and compresses the full-perimeter O-Ring #3!
      - Center (r = 9..13.5 mm): Beefy 2.8 mm-thick Top Retaining Shoulder (y = 195.2..198.0) with M22x1.0 internal threads
        holding O-Ring #4, the JGS1 Quartz Flange, and the M22 Threaded Retainer Nut
      - Smooth Taper into a 1.0 mm-Thin Acoustic Rain-Drum Diaphragm (r = 15..36.5 mm) above the 4x 20 mm Murata Piezos!
    """
    skirt = ring_cone_y(186.5, 190.6, 44.18, OUTER_R, 44.18, OUTER_R)
    skirt = cut_internal_bayonet_y(skirt, 186.0, r_minor=44.18, r_slot=45.40, n_lugs=8)

    dome_rim = cone_y(190.5, 194.8, OUTER_R, 37.0)
    dome_diaphragm = cone_y(194.8, 196.4, 37.0, 14.0)
    dome_apex = cone_y(196.4, 198.0, 14.0, 9.0)
    dome = dome_rim.union(dome_diaphragm).union(dome_apex)
    body = skirt.union(dome)
    body = cut_lock_triangles_y(body, y_base=189.4, y_tip=187.0)

    cavity = (
        cone_y(190.2, 190.6, 44.18, 42.5)
        .union(cone_y(190.5, 193.8, 42.5, 36.5))
        .union(cone_y(193.8, 195.3, 36.5, 14.2))
    )
    body = body.cut(cavity)

    # 4x 1.0 mm-Thin Coplanar Piezo Bonding Seats (r = 25.0 mm at 45/135/225/315 deg, bottom face at y = 193.8 mm)
    for deg in (45, 135, 225, 315):
        rad = math.radians(deg)
        px, pz = 25.0 * math.cos(rad), 25.0 * math.sin(rad)
        pad_boss = cone_y(193.8, 195.2, 10.3, 10.3).translate((px, 0, pz)).intersect(dome)
        pad_rim = ring_cone_y(193.4, 193.8, 10.0, 10.5, 10.0, 10.5).translate((px, 0, pz))
        body = body.union(pad_boss).union(pad_rim)

    # Dead-center (0, 0) Heavy-Duty 2.8 mm Top Retaining Lip (y = 195.2..198.0) + Standard ISO M22x1.5 Internal Helical Thread
    win = ef.uv_window()
    r_win = 9.0
    uv_collar = ring_cone_y(190.4, 198.0, r_win, r_win + 3.5, r_win, r_win + 5.5).translate((win["x"], 0, win["z"])).intersect(dome)
    uv_top_hole = cone_y(190.0, 198.5, r_win, r_win).translate((win["x"], 0, win["z"]))
    uv_inner_bore = cone_y(190.0, 195.2, 10.40, 10.40).translate((win["x"], 0, win["z"]))
    body = body.union(uv_collar).cut(uv_top_hole).cut(uv_inner_bore)
    body = cut_internal_thread_y(body, (191.2, 192.7),
                                 r_minor=10.40, depth=0.70, half_base=0.45, half_crest=0.14, clearance=0.20,
                                 cx=win["x"], cz=win["z"])
    return body


def build_orings():
    """Part 3: Elastomeric O-Ring & Gasket Set (`orings.stl/.step` — standard EPDM/NBR 70A, or TPU 95A prints):
      1.-3. `base <-> pagoda`, `pagoda <-> wind_tunnel`, `crown_base <-> crown`: 88 x 2.0 mm O-rings, shown
            uncompressed on their gland floors (they overlap the mating skirt by the 0.5 mm design squeeze)
      4. Bottom Center `probe_sleeve <-> pagoda` waterproof bulkhead O-ring (y = 73.5..74.35, r = 10.6..13.1 mm)
      5. Top Center (0, 0) JGS1 Quartz Dome flange O-ring (y = 194.2..195.1, r = 8.6..10.3 mm)
    """
    def bayonet_ring(seat_y):   # uncompressed 88 x 2.0 mm O-ring resting on its gland floor
        y = seat_y + ORING_FLOOR_BELOW_SEAT + ORING_CS / 2.0
        return cq.Workplane(obj=cq.Solid.makeTorus((ORING_ID + ORING_CS) / 2.0, ORING_CS / 2.0,
                                                   cq.Vector(0, y, 0), cq.Vector(0, 1, 0)))

    ring_base = bayonet_ring(66.9)
    ring_probe_bot = ring_cone_y(73.5, 74.35, 10.6, 13.1, 10.6, 13.1)
    ring_refl = bayonet_ring(120.6)
    ring_crown = bayonet_ring(186.5).translate((0, TOP_RAISE, 0))
    win = ef.uv_window()
    ring_uv = ring_cone_y(194.2, 195.1, 8.6, 10.3, 8.6, 10.3).translate((win["x"], TOP_RAISE, win["z"]))
    return ring_base.union(ring_probe_bot).union(ring_refl).union(ring_crown).union(ring_uv).translate((0, RAISE, 0))


ALL_PARTS = (
    ("base", build_base),
    ("mast_clamp", build_mast_clamp),
    ("orings", build_orings),
    ("probe_sleeve_back", build_probe_sleeve_back),
    ("probe_sleeve_front", build_probe_sleeve_front),
    ("pagoda", build_pagoda),
    ("wind_tunnel", build_wind_tunnel),
    ("struts", build_struts),
    ("crown_base", build_crown_base),
    ("crown", build_crown),
    ("mast_shims", build_mast_shims),
)


def print_bodies(solid):
    """Separate printable bodies of a part, largest first."""
    return sorted((s for v in solid.vals() for s in v.Solids()), key=lambda s: -s.Volume())


PLATE_GAP = 6.0       # mm between bodies on a print plate (MJF/SLS fuse gaps < ~0.5 mm; FDM needs room for brims)
PLATE_WIDTH = 220.0   # row length before starting a new row


def print_plate(solid):
    """All bodies of a part laid out on the print bed (y = 0), in rows, >= PLATE_GAP apart: captive or stacked bodies
    (umbrella, reset button, louver plates, strut halves, nuts, shims) can never print fused, yet each part stays
    a single file to upload."""
    placed, x, z, row_depth = [], 0.0, 0.0, 0.0
    for body in print_bodies(solid):
        bb = body.BoundingBox()
        if x > 0.0 and x + bb.xlen > PLATE_WIDTH:
            x, z, row_depth = 0.0, z + row_depth + PLATE_GAP, 0.0
        placed.append(body.translate(cq.Vector(x - bb.xmin, -bb.ymin, z - bb.zmin)))
        x += bb.xlen + PLATE_GAP
        row_depth = max(row_depth, bb.zlen)
    return cq.Workplane(obj=cq.Compound.makeCompound(placed))


def export_parts(out_dir):
    """Assembly-position STL/STEP per part, plus `print/<part>.stl`: the same bodies laid out flat for printing."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    print_dir = out_dir / "print"
    print_dir.mkdir(exist_ok=True)
    for old in print_dir.glob("*.stl"):
        old.unlink()
    for name, builder in ALL_PARTS:
        solid = builder()
        cq.exporters.export(solid, str(out_dir / f"{name}.stl"))
        cq.exporters.export(solid, str(out_dir / f"{name}.step"))
        cq.exporters.export(print_plate(solid), str(print_dir / f"{name}.stl"))
        print(f"Exported {name}.stl/.step + print plate ({len(print_bodies(solid))} bodies) -> {out_dir.relative_to(REPO)}")


if __name__ == "__main__":
    export_parts(REPO / "mechanical/build" / VARIANT)
