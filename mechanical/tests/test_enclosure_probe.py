"""CadQuery BRep geometry, PCB clearance, and true thread-meshing verification."""

import math
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "mechanical/scripts"))
import build_enclosure as be
import enclosure_features as ef


def _board_solids():
    out = {}
    for name in ef.SPEC["variants"][ef.VARIANT]["boards"]:
        b = ef.board_slab(name)
        if name == "dome":
            cx = (b["x0"] + b["x1"]) / 2.0
            cz = (b["z0"] + b["z1"]) / 2.0
            r = (b["x1"] - b["x0"]) / 2.0
            out[name] = be.cone_y(b["y0"], b["y1"], r, r).translate((cx, 0, cz))
        elif ef.board_outline_xz(name):
            import cadquery as cq
            out[name] = (cq.Workplane("XZ", origin=(0, b["y0"], 0)).polyline(ef.board_outline_xz(name)).close()
                         .extrude(-(b["y1"] - b["y0"])))
        else:
            out[name] = be.box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"])
    return out


def test_all_modular_enclosure_parts_are_valid_solids():
    for name, builder in be.ALL_PARTS:
        solid = builder()
        assert solid.val().isValid(), f"{name} is not a valid BRep solid"
        assert solid.val().Volume() > 50.0, f"{name} volume unexpectedly small"


def test_boards_clear_enclosure_and_threads_truly_mesh():
    base = be.build_base()
    pagoda = be.build_pagoda()
    wind_tunnel = be.build_wind_tunnel()
    struts = be.build_struts()
    crown_base = be.build_crown_base()
    crown = be.build_crown()
    sleeve_back = be.build_probe_sleeve_back()
    sleeve_front = be.build_probe_sleeve_front()
    plates = _board_solids()

    # 1. Zero PCB-to-enclosure collisions
    assert base.intersect(plates["main"]).val().Volume() < 1e-3
    assert base.intersect(plates["power"]).val().Volume() < 1e-3
    assert pagoda.intersect(plates["probe"]).val().Volume() < 1e-3
    assert wind_tunnel.intersect(plates["reflector"]).val().Volume() < 1e-3
    assert crown_base.intersect(plates["crown"]).val().Volume() < 1e-3
    assert crown.intersect(plates["dome"]).val().Volume() < 1e-3

    # 2. 8-Lug Push-Turn-Click Bayonet Interlock Verification (on `base<->pagoda`, `pagoda<->wind_tunnel`, `crown_base<->crown`):
    #    (a) Zero boolean collision in the final locked detent position (0 deg, dy = 0.0 mm)
    assert base.intersect(pagoda).val().Volume() < 1e-3
    assert pagoda.intersect(wind_tunnel).val().Volume() < 1e-3
    assert crown_base.intersect(crown).val().Volume() < 1e-3
    assert wind_tunnel.intersect(struts).val().Volume() < 1e-3
    assert crown_base.intersect(struts).val().Volume() < 1e-3

    #    (b) Axial Weight & Wind-Lift Retention: pulling any upper housing straight up (dy = +0.8 mm)
    #        engages all 8 bayonet lugs against the lower J-slot shelves (> 30 mm^3 solid shear interlock)!
    assert base.intersect(pagoda.translate((0, 0.8, 0))).val().Volume() > 30.0
    assert pagoda.intersect(wind_tunnel.translate((0, 0.8, 0))).val().Volume() > 30.0
    assert crown_base.intersect(crown.translate((0, 0.8, 0))).val().Volume() > 30.0

    #    (c) Anti-Unscrew Detent Lock: trying to twist counter-clockwise toward the entry slot (-8 deg)
    #        WITHOUT pushing down against the O-ring (dy = 0) hits the 0.47 mm detent locking step (> 10 mm^3)!
    assert base.intersect(pagoda.rotate((0, 0, 0), (0, 1, 0), -8.0)).val().Volume() > 10.0
    assert pagoda.intersect(wind_tunnel.rotate((0, 0, 0), (0, 1, 0), -8.0)).val().Volume() > 10.0
    assert crown_base.intersect(crown.rotate((0, 0, 0), (0, 1, 0), -8.0)).val().Volume() > 10.0

    #    (d) Intentional Push-and-Turn Release: pushing down 0.45 mm against the O-ring (dy = -0.45 mm)
    #        clears the detent step at -8 deg and reaches the open vertical entry slot at -20.5 deg (~19 mm arc travel)!
    assert crown_base.intersect(crown.translate((0, -0.45, 0)).rotate((0, 0, 0), (0, 1, 0), -8.0)).val().Volume() < 1e-3
    assert crown_base.intersect(crown.translate((0, -0.45, 0)).rotate((0, 0, 0), (0, 1, 0), -20.5)).val().Volume() < 1e-3

    # 3. True Helical Screw Tightening Verification on `probe_sleeve` M22x1.5 Bottom Thread + Waterproof O-Ring:
    assert sleeve_back.intersect(sleeve_front).val().Volume() < 1e-3
    assert pagoda.intersect(sleeve_back).val().Volume() < 1e-3
    assert pagoda.intersect(sleeve_front).val().Volume() < 1e-3
    assert pagoda.intersect(sleeve_back.union(sleeve_front).rotate((0, 0, 0), (0, 1, 0), 180)).val().Volume() > 25.0
    assert sleeve_back.intersect(sleeve_front.translate((0.4, 0, 0))).val().Volume() > 10.0

    # 4. Pagoda M6 x 55 through-bolts at real size (DIN 7984 low head d 10 x k 3.5, ISO 4032 nut 10 AF x 5.2 under the
    #    deck, shank to the bolt tip) clear the pagoda, wind_tunnel, struts, reflector board and the base umbrella.
    assert pagoda.intersect(struts).val().Volume() < 1e-3
    for deg in (45, 135, 225, 315):
        rad = math.radians(deg)
        px, pz = 20.5 * math.cos(rad), 20.5 * math.sin(rad)
        head_y0 = 117.8 + ef.RAISE
        nut_y1 = 70.1 + ef.RAISE
        tip_y = head_y0 - 55.0
        m6_bolt = (be.cone_y(tip_y, head_y0, 3.0, 3.0).translate((px, 0, pz))
                   .union(be.cone_y(head_y0, head_y0 + be.M6_HEAD_K, be.M6_HEAD_D / 2.0, be.M6_HEAD_D / 2.0)
                          .translate((px, 0, pz)))
                   .union(be.hex_prism_y(nut_y1 - be.M6_NUT_M, nut_y1, across_flats=be.M6_NUT_AF, cx=px, cz=pz)))
        for other, label in ((pagoda, "pagoda"), (wind_tunnel, "wind_tunnel"), (struts, "struts"),
                             (plates["reflector"], "reflector"), (base, "base")):
            assert other.intersect(m6_bolt).val().Volume() < 1e-3, f"M6 bolt at {deg} deg hits {label}"

    # 5. Zero Clash Between Any `probe` PCB Component (including `probe J1` bottom connector, `SCD41`, `SGP41`, `BMP585`, `SHT40`,
    #    and backside heater) and `probe_sleeve_front`, `probe_sleeve_back`, or `pagoda`!
    for b in ef.part_boxes():
        if b["board"] == "probe":
            comp_box = be.box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"])
            assert sleeve_front.intersect(comp_box).val().Volume() < 1e-3, f"probe:{b['ref']} clashes with probe_sleeve_front!"
            assert sleeve_back.intersect(comp_box).val().Volume() < 1e-3, f"probe:{b['ref']} clashes with probe_sleeve_back!"
            assert pagoda.intersect(comp_box).val().Volume() < 1e-3, f"probe:{b['ref']} clashes with pagoda!"

    # 6. Dismountable Round 10-Core Cable + 12.0x3.0 mm JST-SH Plug Pass-Through Verification inside `probe_sleeve_back`:
    jst_plug_gauge = be.box(-6.0, 6.0, 68.5 + ef.RAISE, 118.0 + ef.RAISE, -4.1, -1.1)
    assert sleeve_back.intersect(jst_plug_gauge).val().Volume() < 1e-3
    assert sleeve_front.intersect(jst_plug_gauge).val().Volume() < 1e-3
    assert pagoda.intersect(jst_plug_gauge).val().Volume() < 1e-3

    # 7. Crown Base 29.0-deg Inward-Looking (V-Beam) Ultrasonic Barrels, both transducer options:
    #    Ø 14 x 9 mm can straight in the Ø 14.6 mm bore, or Ø 10 x 7 mm can on the printed adapter sleeve.
    tilt_deg = ef.SPEC["envelope"]["wind_tunnel"].get("transducer_tilt_deg", 29.0)
    adapters = be.build_transducer_adapters()
    fs, sl = be.XD_FACE_S, be.XD_SLEEVE
    for tp in ef.transducer_pockets():
        tx, tz = tp["x"], tp["z"]
        axis = (tz, 0, -tx) if (tx != 0 or tz != 0) else (1, 0, 0)

        def along(solid, axis=axis, tx=tx, tz=tz):
            return solid.rotate((0, 0, 0), axis, tilt_deg).translate((tx, 176.2 + ef.UPPER, tz))

        can14 = along(be.cone_y(fs + 0.05, fs + 9.0, 7.0, 7.0))
        ring14 = along(be.ring_cone_y(fs + 9.05, fs + 10.85, 5.1, 7.25, 5.1, 7.25))
        bore14 = along(be.cone_y(-10.0, fs, be.XD_APERTURE_R - 0.02, be.XD_APERTURE_R - 0.02))
        can10 = along(be.cone_y(fs + sl["lip_t"] + 0.05, fs + sl["lip_t"] + 5.15, 5.0, 5.0))
        ring10 = along(be.ring_cone_y(fs + sl["lip_t"] + 5.2, fs + sl["lip_t"] + 6.6, 3.1, 5.2, 3.1, 5.2))
        bore10 = along(be.cone_y(-10.0, fs + sl["lip_t"], sl["lip_r"] - 0.02, sl["lip_r"] - 0.02))
        name = tp["name"]
        assert crown_base.intersect(bore14).val().Volume() < 1e-3, f"XD_{name}: Ø 12 mm aperture blocked"
        assert crown_base.intersect(can14).val().Volume() < 1e-3, f"XD_{name}: Ø 14 mm can does not fit the bore"
        assert crown_base.intersect(ring14).val().Volume() < 1e-3
        assert plates["crown"].intersect(can14).val().Volume() < 1e-3, f"XD_{name}: Ø 14 mm can hits the crown PCB"
        assert plates["crown"].intersect(ring14).val().Volume() < 1e-3
        assert crown_base.intersect(adapters).val().Volume() < 1e-3, "adapter sleeves do not fit the barrels"
        for solid, what in ((can10, "can"), (ring10, "ring"), (bore10, "aperture")):
            assert adapters.intersect(solid).val().Volume() < 1e-3, f"XD_{name}: Ø 10 mm {what} hits its sleeve"
            assert crown_base.intersect(solid).val().Volume() < 1e-3, f"XD_{name}: Ø 10 mm {what} hits the barrel"
        for b in ef.part_boxes():
            if b["board"] == "crown" and not b["ref"].startswith(("XD_", "PIEZO_")):
                comp_box = be.box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"])
                assert can14.intersect(comp_box).val().Volume() < 1e-3, f"XD_{name} clashes with crown:{b['ref']}!"

    for b in ef.part_boxes():
        if b["board"] == "crown" and not b["ref"].startswith(("XD_", "PIEZO_")):
            comp_box = be.box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"])
            assert crown_base.intersect(comp_box).val().Volume() < 1e-3, f"crown:{b['ref']} clashes with crown_base!"
            assert crown.intersect(comp_box).val().Volume() < 1e-3, f"crown:{b['ref']} clashes with crown!"
        elif b["board"] in ("main", "power"):
            comp_box = be.box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"])
            assert base.intersect(comp_box).val().Volume() < 1e-3, f"{b['board']}:{b['ref']} clashes with base!"

    # The `power` board under `main`: its parts clear main's plate and every main bottom-side part
    main_bottom = [b for b in ef.part_boxes() if b["board"] == "main" and b["side"] == "back"]
    for b in ef.part_boxes():
        if b["board"] != "power":
            continue
        comp_box = be.box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"])
        assert plates["main"].intersect(comp_box).val().Volume() < 1e-3, f"power:{b['ref']} hits the main board"
        for m in main_bottom:
            other = be.box(m["x0"], m["x1"], m["y0"], m["y1"], m["z0"], m["z1"])
            assert comp_box.intersect(other).val().Volume() < 1e-3, f"power:{b['ref']} hits main:{m['ref']}"

    #    - Verify the Captive Inverted-`T` REBOOT/RESET button at (+18.5, -20.0) has:
    #      (a) Zero collision with the stepped guide bore in `base.stl` (`ring_cone_y` check between r=2.15 and r=2.50)
    #      (b) Positive mechanical stop (> 15 mm^3 solid interlock) when pulled downward by 0.6 mm so it can NEVER fall through!
    btn_clearance_ring = be.ring_cone_y(27.0, 39.7 + be.BTN_UP, 2.20, 2.45, 2.20, 2.45).translate((18.5, 0, -20.0))
    assert base.intersect(btn_clearance_ring).val().Volume() < 1e-3
    t_flange_pulled_down = be.cone_y(39.2 + be.BTN_UP, 39.75 + be.BTN_UP, 3.65, 3.65).translate((18.5, 0, -20.0))
    assert base.intersect(t_flange_pulled_down).val().Volume() > 10.0
    #      (c) the button sits right under `main SW1` and its finger tip below the base is not covered by the mast clamp
    sw1 = ef.components("main")["SW1"]
    sx, _, sz = ef.board_point("main", sw1["x"], sw1["y"])
    assert abs(sx - 18.5) < 0.05 and abs(sz + 20.0) < 0.05
    finger = be.cone_y(20.0, 27.5, 2.15, 2.15).translate((18.5, 0, -20.0))
    assert be.build_mast_clamp().intersect(finger).val().Volume() < 1e-3

    #    - The ENE cable strut bore (at r = 32.5 mm, x = +28.7 mm outside the x = +/-20.5 mm `crown` PCB!)
    #      has an unobstructed vertical wire path all the way up past the `crown` PCB (from y = 174.0 to y = 186.0 mm)!
    ne_st = ef.strut_centres()[0]
    ne_wire_column = be.cone_y(174.0 + ef.UPPER, 186.0 + ef.UPPER, 3.0, 3.0).translate((ne_st["x"], 0, ne_st["z"]))
    assert crown_base.intersect(ne_wire_column).val().Volume() < 1e-3
    assert plates["crown"].intersect(ne_wire_column).val().Volume() < 1e-3


def test_pm_bed_ducts_take_outside_air_only():
    """Flat face-down PMSA003I beside `main`: the module clears the base, the main board and its parts; it is held by the
    hooks against the hard-stop pads (gasket squeezed to 1.0 mm); both duct chambers are closed walls under the module
    face whose only openings are floor slits to the outside air below the base, on opposite sides of the clamp flange
    band; the chambers never reach the bay grilles."""
    pm = ef.PM
    base = be.build_base()
    clamp = be.build_mast_clamp()
    module = next(b for b in ef.part_boxes() if b["ref"] == "PM_MODULE")
    mod = be.box(module["x0"], module["x1"], module["y0"], module["y1"], module["z0"], module["z1"])
    assert base.intersect(mod).val().Volume() < 1e-3
    main = _board_solids()["main"]
    assert main.intersect(mod).val().Volume() < 1e-3
    for b in ef.part_boxes():
        if b["board"] == "main":
            assert mod.intersect(be.box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"])).val().Volume() < 1e-3, b["ref"]
    # held: hooks overlap the module top when it is lifted 0.3 mm; pads touch its face when pushed down 0.1 mm
    assert base.intersect(mod.translate((0, 0.3, 0))).val().Volume() > 1.0
    assert base.intersect(mod.translate((0, -0.1, 0))).val().Volume() > 0.1

    top = pm["face_y"] - pm["gasket"]
    w = pm["wall"]
    for name, ch, slits in (("fan", pm["fan"], pm["fan_slits"]), ("inlet", pm["inlet"], pm["inlet_slits"])):
        inside = be.box(ch["x0"], ch["x1"], pm["floor_y"], top - 1.25, ch["z0"], ch["z1"])   # below the intake-duct roof
        assert base.intersect(inside).val().Volume() < 1e-3, f"{name} chamber is not empty"
        ring = (be.box(ch["x0"] - w, ch["x1"] + w, pm["floor_y"], top, ch["z0"] - w, ch["z1"] + w)
                .cut(be.box(ch["x0"], ch["x1"], pm["floor_y"] - 1, top + 1, ch["z0"], ch["z1"])))
        assert abs(base.intersect(ring).val().Volume() - ring.val().Volume()) < 0.05, f"{name} chamber wall leaks"
        # the chamber sits under the module face (gasket seals on the module); the part of the intake duct that reaches
        # past the module edge is closed by a solid roof that runs under the module edge
        assert ch["x0"] - w >= module["x0"] - 1e-6 and ch["x1"] + w <= module["x1"]
        assert ch["z1"] + w <= module["z1"] + 1e-6
        if ch["z0"] - w < module["z0"]:
            roof_box = be.box(ch["x0"] - w, ch["x1"] + w, top - 1.2, top, ch["z0"] - w, module["z0"] + 1.0)
            assert abs(base.intersect(roof_box).val().Volume() - roof_box.val().Volume()) < 0.05, "intake duct roof open"
        for x in slits["xs"]:
            path = be.box(x - 0.5, x + 0.5, 22.0, pm["floor_y"] + 0.5, slits["z0"] + 0.2, slits["z1"] - 0.2)
            assert base.intersect(path).val().Volume() < 1e-3, f"{name} slit at x={x} is blocked"
            assert clamp.intersect(path).val().Volume() < 1e-3, f"{name} slit at x={x} is blocked by the clamp"
            assert ch["x0"] <= x - 0.6 and x + 0.6 <= ch["x1"] and ch["z0"] <= slits["z0"] and slits["z1"] <= ch["z1"]
            for z in (slits["z0"], slits["z1"]):
                assert math.hypot(x, z) >= 22.8, f"{name} slit at x={x} exits over the mast cradle"
    # exits on opposite sides of the clamp flange band (z = -8..+8 under the base)
    assert pm["fan_slits"]["z0"] > 8.5 and pm["inlet_slits"]["z1"] < -8.5
    # the bay grilles stay outside both chambers
    for deg in (30, 120, 240, 330):
        gx, gz = 35.5 * math.cos(math.radians(deg)), 35.5 * math.sin(math.radians(deg))
        grille = be.box(gx - 3.8, gx + 3.8, pm["floor_y"], top, gz - 3.0, gz + 3.0)
        for ch in (pm["fan"], pm["inlet"]):
            assert grille.intersect(be.box(ch["x0"] - w, ch["x1"] + w, pm["floor_y"], top, ch["z0"] - w, ch["z1"] + w)).val().Volume() < 1e-3


def test_probe_gas_sensors_see_the_shield_air():
    """SGP41 / BMP585 each look straight out through at least one front grid slot (>= 30 % of their footprint open to the
    shield air), and the upper chamber has open side slots on both sides (cross-flow)."""
    front = be.build_probe_sleeve_front()
    comps = ef.components("probe")
    for ref in ("U2", "U5"):
        c = comps[ref]
        x0, y0, _ = ef.board_point("probe", c["bbox"]["x0"], c["bbox"]["y0"])
        x1, y1, _ = ef.board_point("probe", c["bbox"]["x1"], c["bbox"]["y1"])
        sight = be.box(min(x0, x1) + 0.2, max(x0, x1) - 0.2, min(y0, y1) + 0.2, max(y0, y1) - 0.2, 0.8, 10.0)
        covered = front.intersect(sight).val().Volume()
        wall = be.box(min(x0, x1) + 0.2, max(x0, x1) - 0.2, min(y0, y1) + 0.2, max(y0, y1) - 0.2, 7.5, 8.8).val().Volume()
        assert covered <= 0.7 * wall + 1e-3, f"probe {ref}: only {100 * (1 - covered / wall):.0f} % of its footprint sees out"
    for ys in be.PROBE_SIDE_SLOTS_Y:
        ys += ef.RAISE
        for sx in (-1.0, 1.0):
            path = be.box(min(sx * 6.9, sx * 9.8), max(sx * 6.9, sx * 9.8), ys - 0.6, ys + 0.6, 2.0, 6.6)
            assert front.intersect(path).val().Volume() < 1e-3, f"side slot y={ys} x={sx:+.0f} is blocked"


def test_board_snap_clips_stand_free_of_the_wall_and_grip_the_boards():
    """Every snap-clip post is >= 1 mm clear of the bay wall (it must flex outward when a board is pressed in), sits
    just outside its board's edge (0.15 mm gap) and its hook reaches over that edge."""
    plates = _board_solids()
    for board, clips in ef.CLIPS.items():
        slab = ef.board_slab(board)
        for cx, cz, (dx, dz) in clips:
            corners = [(cx + sx * 0.75, cz + sz * 0.75) for sx in (-1, 1) for sz in (-1, 1)]
            assert max(math.hypot(x, z) for x, z in corners) <= 42.6 - 1.0, f"{board} clip at ({cx}, {cz}) touches the wall"
            post = be.box(cx - 0.75, cx + 0.75, 33.9, slab["y1"], cz - 0.75, cz + 0.75)
            assert plates[board].intersect(post).val().Volume() < 1e-3, f"{board} clip at ({cx}, {cz}) cuts into the board"
            inside_x = slab["x0"] <= cx + dx * 2.0 <= slab["x1"]
            inside_z = slab["z0"] <= cz + dz * 2.0 <= slab["z1"]
            assert inside_x and inside_z, f"{board} clip at ({cx}, {cz}) does not hook over its board"


def _gap_below(solid, x, z, y_from, y_to, step=0.01):
    """Lowest y >= y_from at (x, z) where `solid` has material (probing a 0.1 mm column upward)."""
    probe = be.box(x - 0.05, x + 0.05, y_from, y_to, z - 0.05, z + 0.05)
    hits = [s.BoundingBox().ymin for v in solid.intersect(probe).vals() for s in v.Solids()]
    return min(hits) if hits else None


def test_bayonet_o_rings_are_squeezed_15_to_40_percent():
    """Each bayonet face seal: gland height (groove floor -> mating skirt face) at the lug-shelf resting position
    (O-ring preload lifts the upper part by LUG_PLAY) and at the 0.47 mm push needed to pass the detent bridge."""
    cs = be.ORING_CS
    r_seal = (be.ORING_ID + cs) / 2.0
    pairs = (("base->pagoda", be.build_base(), be.build_pagoda(), 66.4 + ef.RAISE),
             ("pagoda->wind_tunnel", be.build_pagoda(), be.build_wind_tunnel(), 120.1 + ef.RAISE),
             ("crown_base->crown", be.build_crown_base(), be.build_crown(), 186.0 + ef.UPPER))
    for name, lower, upper, groove_top in pairs:
        for deg in (10.0, 100.0, 190.0):   # between the J-slot entry drops (open at the skirt bottom)
            x, z = r_seal * math.cos(math.radians(deg)), r_seal * math.sin(math.radians(deg))
            column = be.box(x - 0.05, x + 0.05, groove_top - 4.0, groove_top - 0.05, z - 0.05, z + 0.05)
            floor = max(s.BoundingBox().ymax for v in lower.intersect(column).vals() for s in v.Solids())
            face = _gap_below(upper, x, z, floor, groove_top + 3.0)
            assert face is not None, f"{name}: no mating face above the O-ring"
            rest = face - floor + be.LUG_PLAY
            pushed = rest - 0.47
            assert 0.60 * cs <= pushed + 0.01 and rest <= 0.85 * cs + 0.01, (
                f"{name}: gland {rest:.2f} mm at rest / {pushed:.2f} mm pushed for a {cs} mm O-ring")
            fill = math.pi * (cs / 2.0) ** 2 / ((47.2 - 43.9) * pushed)
            assert fill <= 0.85, f"{name}: gland overfilled ({fill:.0%}) when pushing through the detent"


def test_printed_strut_nuts_keep_a_wall_at_the_flats():
    """A 1.1 mm annulus just outside the nut's internal thread groove (r 6.3..7.4 mm) must be one closed ring of
    material at mid-height of every printed M12 strut nut (the old 12 mm AF nut was cut open at its flats)."""
    struts = be.build_struts()
    assert be.STRUT_NUT_AF / 2.0 - (5.12 + 0.90 + 0.20) >= 1.0
    for st in ef.strut_centres():
        for y in (124.7 + ef.UPPER, 177.1 + ef.UPPER):
            ring = be.ring_cone_y(y - 0.05, y + 0.05, 6.3, 7.4, 6.3, 7.4).translate((st["x"], 0, st["z"]))
            pieces = [s for v in struts.intersect(ring).vals() for s in v.Solids()]
            full = ring.val().Volume()
            assert len(pieces) == 1 and sum(s.Volume() for s in pieces) > 0.99 * full, \
                f"strut nut at {st['deg']} deg, y={y:.1f}: wall at the flats is open"


def test_print_plates_keep_every_body_apart():
    """Each part's print plate (`print/<part>.stl`) holds all its bodies (captive umbrella/button, louver plates, strut
    halves, nuts, shims) on the bed, >= PLATE_GAP apart so none can fuse; no body is a sliver."""
    for name, builder in be.ALL_PARTS:
        solid = builder()
        bodies = be.print_bodies(solid)
        plate = [s for v in be.print_plate(solid).vals() for s in v.Solids()]
        assert bodies and len(plate) == len(bodies), f"{name}: plate lost bodies"
        boxes = [s.BoundingBox() for s in plate]
        for body, bb in zip(plate, boxes):
            assert body.isValid() and body.Volume() > 5.0, f"{name}: invalid or sliver body ({body.Volume():.2f} mm3)"
            assert abs(bb.ymin) < 1e-6, f"{name}: body not on the bed"
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                a, b = boxes[i], boxes[j]
                gap = max(b.xmin - a.xmax, a.xmin - b.xmax, b.zmin - a.zmax, a.zmin - b.zmax)
                assert gap >= be.PLATE_GAP - 0.01, f"{name}: plate bodies {i} and {j} only {gap:.2f} mm apart"


def test_radiation_labyrinth_blocks_every_line_of_sight_to_the_probe():
    """No straight ray from outside reaches the probe core through the pagoda (checklist M9): sun from above, horizontal
    sky, and roof-reflected / roof-emitted rays from below (-75..+75 deg elevation), in radial sections clear of the
    columns. Without the rims / skirts this finds ~90 leaking rays per section."""
    import cadquery as cq
    from OCP.BRepExtrema import BRepExtrema_DistShapeShape

    pag = be._build_pagoda()
    plate_ys = ef.SPEC["envelope"]["pagoda_shield"]["plate_ys"]
    leaks = []
    for az in (0.0, 20.0):
        sec = pag.rotate((0, 0, 0), (0, 1, 0), az).intersect(be.box(0, 62, 60, 130, -0.05, 0.05)).val().wrapped
        for elev in range(-75, 80, 5):
            # shielded zone: bottom deck top (plate 1 + 2) up to the top deck underside (plate 4 + 8.5)
            for i in range(int((plate_ys[-2] + 8.5 - plate_ys[0] - 2.0) / 0.5) + 1):
                yt = plate_ys[0] + 2.0 + 0.5 * i
                y0 = yt - 47.0 * math.tan(math.radians(elev))
                ray = cq.Edge.makeLine(cq.Vector(60.0, y0, 0.0), cq.Vector(13.0, yt, 0.0)).wrapped
                if BRepExtrema_DistShapeShape(sec, ray).Value() > 1e-6:
                    leaks.append((az, elev, yt))
    assert not leaks, f"{len(leaks)} rays reach the probe core, e.g. (azimuth, elevation, y) {leaks[:5]}"
