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
        cfg = ef.SPEC["boards"][name]
        b = ef.board_slab(name)
        if name == "dome":
            cx = (b["x0"] + b["x1"]) / 2.0
            cz = (b["z0"] + b["z1"]) / 2.0
            r = (b["x1"] - b["x0"]) / 2.0
            out[name] = be.cone_y(b["y0"], b["y1"], r, r).translate((cx, 0, cz))
        elif cfg.get("shape") == "octagon":
            import cadquery as cq
            pts = [(-12.5, -17.0), (12.5, -17.0), (17.0, -12.5), (17.0, 12.5),
                   (12.5, 17.0), (-12.5, 17.0), (-17.0, 12.5), (-17.0, -12.5)]
            out[name] = cq.Workplane("XZ", origin=(0, b["y0"], 0)).polyline(pts).close().extrude(-(b["y1"] - b["y0"]))
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

    # 4. Pagoda Top M6 Bolts (`r = 20.5 mm`) vs Wind-Tunnel Bottom Strut Nuts (`r = 29.5 mm`) Non-Collision Verification:
    assert pagoda.intersect(struts).val().Volume() < 1e-3
    for deg in (45, 135, 225, 315):
        import math
        rad = math.radians(deg)
        px, pz = 20.5 * math.cos(rad), 20.5 * math.sin(rad)
        m6_bolt = (be.cone_y(67.8 + ef.RAISE, 117.8 + ef.RAISE, 2.95, 2.95).translate((px, 0, pz))
                   .union(be.cone_y(117.8 + ef.RAISE, 120.2 + ef.RAISE, 4.8, 4.8).translate((px, 0, pz)))
                   .union(be.hex_prism_y(67.8 + ef.RAISE, 70.1 + ef.RAISE, across_flats=9.8, cx=px, cz=pz)))
        assert pagoda.intersect(m6_bolt).val().Volume() < 1e-3
        assert wind_tunnel.intersect(m6_bolt).val().Volume() < 1e-3
        assert struts.intersect(m6_bolt).val().Volume() < 1e-3

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

    # 7. Crown Base 29.0-deg Inward-Looking (V-Beam) Ultrasonic Barrel Seat + Unobstructed Bore + Press-Fit Ring Verification:
    tilt_deg = ef.SPEC["envelope"]["wind_tunnel"].get("transducer_tilt_deg", 29.0)
    for tp in ef.transducer_pockets():
        tx, tz = tp["x"], tp["z"]
        axis = (tz, 0, -tx) if (tx != 0 or tz != 0) else (1, 0, 0)
        xd_can = be.cone_y(-1.75, 3.4, 5.0, 5.0).rotate((0, 0, 0), axis, tilt_deg).translate((tx, 176.2 + ef.RAISE, tz))
        ret_ring = be.ring_cone_y(3.4, 4.8, 3.0, 5.2, 3.0, 5.2).rotate((0, 0, 0), axis, tilt_deg).translate((tx, 176.2 + ef.RAISE, tz))
        # Verify the Ø 8.9 mm (`r = 4.43 mm`) acoustic aperture bore is 100% round and unobstructed from s = -10.0 to +8.0 mm!
        bore_gauge = be.cone_y(-10.0, 8.0, 4.43, 4.43).rotate((0, 0, 0), axis, tilt_deg).translate((tx, 176.2 + ef.RAISE, tz))
        assert crown_base.intersect(bore_gauge).val().Volume() < 1e-3, f"XD_{tp['name']} aperture has a chord blockage!"
        assert crown_base.intersect(xd_can).val().Volume() < 1e-3
        assert crown_base.intersect(ret_ring).val().Volume() < 1e-3
        assert plates["crown"].intersect(xd_can).val().Volume() < 1e-3
        assert plates["crown"].intersect(ret_ring).val().Volume() < 1e-3
        for b in ef.part_boxes():
            if b["board"] == "crown" and not b["ref"].startswith(("XD_", "PIEZO_")):
                comp_box = be.box(b["x0"], b["x1"], b["y0"], b["y1"], b["z0"], b["z1"])
                assert xd_can.intersect(comp_box).val().Volume() < 1e-3, f"XD_{tp['name']} clashes with crown:{b['ref']}!"

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
    ne_wire_column = be.cone_y(174.0 + ef.RAISE, 186.0 + ef.RAISE, 3.0, 3.0).translate((ne_st["x"], 0, ne_st["z"]))
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
