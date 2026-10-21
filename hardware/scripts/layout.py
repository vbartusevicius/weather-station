"""Persistent board layout (KiCad 10 pcbnew, `eda` container).

    python3 hardware/scripts/layout.py <board> sync
    python3 hardware/scripts/layout.py <board> place <ref-or-block> ...
    python3 hardware/scripts/layout.py <board> route [attempts]
    python3 hardware/scripts/layout.py <board> tidy-silk
    python3 hardware/scripts/layout.py <board> components
"""

import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path

import kinet2pcb
import pcbnew
import yaml
from shapely.geometry import Polygon

REPO = Path("/work")
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

SPEC = load_spec()
VARIANT = SPEC["active_variant"]
LAYERS = {"F.Cu": pcbnew.F_Cu, "In1.Cu": pcbnew.In1_Cu,
          "In2.Cu": pcbnew.In2_Cu, "B.Cu": pcbnew.B_Cu}

MODELS_3D = {
    "XIAO_ESP32C6_Header": ("XIAO_ESP32C6.step", 7.5),
    "TerminalBlock_Kefa_KF142R-5.08-2P": ("TerminalBlock_Kefa_KF142R-5.08-2P.step", 0),
    "Fuse_1812_4532Metric": ("Fuse_1812_4532Metric.step", 0),
    "LED_SK6805-EC15_1.5x1.5mm": ("LED_SK6805-EC15_1.5x1.5mm.step", 0),
    "Sensirion_SHT45_DFN-4": ("Sensirion_SHT45_DFN-4.step", 0),
    "Bosch_BMP581_LGA-10": ("Bosch_BMP581_LGA-10.step", 0),
    "Bosch_BMP585_LGA-9": ("Bosch_BMP585_LGA-9.step", 0),
    "TI_ADS1115_VSSOP-10": ("TI_ADS1115_VSSOP-10.step", 0),
    "Sensirion_SGP41_DFN-6": ("Sensirion_SGP41_DFN-6.step", 0),
    "Sensirion_SCD41_LGA-20": ("Sensirion_SCD41_LGA-20.step", 0),
    "Vishay_VEML7700-TT": ("Vishay_VEML7700-TT.step", 0),
    "LiteOn_LTR-390UV-01": ("LiteOn_LTR-390UV-01.step", 0),
    "AMS_TSL25911FN": ("AMS_TSL25911FN.step", 0),
    "GenUV_GUVB-C31SM": ("GenUV_GUVB-C31SM.step", 0),
    "ST_LIS2MDLTR_LGA-12": ("ST_LIS2MDLTR_LGA-12.step", 0),
    "ST_LSM6DS3TR-C_LGA-14": ("ST_LSM6DS3TR-C_LGA-14.step", 0),
    "ScioSense_AS3935_QFN-16": ("ScioSense_AS3935_QFN-16.step", 0),
    "TI_CD4052BM96_SOIC-16": ("TI_CD4052BM96_SOIC-16.step", 0),
    "TI_SN74LVC2G04_SOT-23-6": ("TI_SN74LVC2G04_SOT-23-6.step", 0),
    "TI_TLV9062IDR_SOIC-8": ("TI_TLV9062IDR_SOIC-8.step", 0),
    "ON_LM393DR2G_SOIC-8": ("ON_LM393DR2G_SOIC-8.step", 0),
    "AOS_AO3400A_SOT-23": ("AOS_AO3400A_SOT-23.step", 0),
    "JST_B6B-ZR-SM4-TF": ("JST_B6B-ZR-SM4-TF.step", 0),
    "JST_BM02B-SRSS-TB": ("JST_BM02B-SRSS-TB.step", 0),
    "HX_PZ2.54-1x4P-SMD": ("HX_PZ2.54-1x4P-SMD.step", 0),
    "HX_PM2.54-1x4P-RA-SMD": ("HX_PM2.54-1x4P-RA-SMD.step", 0),
    "TerminalBlock_Kefa_KF250-3.5-4P": ("TerminalBlock_Kefa_KF250-3.5-4P.step", 0),
    "MEMSIC_MMC5603NJ_WLCSP-4": ("MEMSIC_MMC5603NJ_WLCSP-4.step", 0),
}


SILK_TEXT_MM = 0.7     # reference + connector-label height (JLC prints >= 0.6 mm legibly with a 0.12 mm stroke)
SILK_STROKE_MM = 0.12


def pos(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(-y))


def edge_seg(board, x0, y0, x1, y1):
    seg = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    seg.SetLayer(pcbnew.Edge_Cuts)
    seg.SetWidth(pcbnew.FromMM(0.05))
    seg.SetStart(pos(x0, y0))
    seg.SetEnd(pos(x1, y1))
    board.Add(seg)


def keepout_zone(board, x0, y0, x1, y1, layers):
    zone = pcbnew.ZONE(board)
    zone.SetIsRuleArea(True)
    zone.SetDoNotAllowTracks(True)
    zone.SetDoNotAllowVias(True)
    zone.SetDoNotAllowPads(False)
    zone.SetDoNotAllowFootprints(False)
    zone.SetDoNotAllowZoneFills(True)
    layer_set = pcbnew.LSET()
    for layer in layers:
        layer_set.AddLayer(layer)
    zone.SetLayerSet(layer_set)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in rect_points({"x0": x0, "y0": y0, "x1": x1, "y1": y1}):
        p = pos(x, y)
        outline.Append(p.x, p.y)
    board.Add(zone)


POWER_CLASS = {
    "SELV-MAIN": "MAIN-Power",
    "SELV-CROWN": "CROWN-Power",
    "SELV-REFLECTOR": "REFLECTOR-Power",
    "SELV-DOME": "DOME-Power",
    "SELV-PROBE": "PROBE-Power",
    "SELV-POWER": "POWER-Power",
}


def dru_rules(domains, isolators, isolation, base):
    minimum = {frozenset(i["between"]): i["creepage_min"] for i in isolation or []}
    exempt = "".join(f" && !(A.Reference == '{r}' && B.Reference == '{r}')" for r in isolators)
    rules = [base]
    for i, a in enumerate(domains):
        for b in domains[i + 1:]:
            cond_a = f"(A.hasNetclass('{a}') || A.hasNetclass('{POWER_CLASS[a]}'))"
            cond_b = f"(B.hasNetclass('{b}') || B.hasNetclass('{POWER_CLASS[b]}'))"
            rules.append(f"(rule creepage_{a.lower()}_{b.lower()}\n"
                         f"  (constraint creepage (min {minimum.get(frozenset((a, b)), 6.0)}mm))\n"
                         f"  (condition \"{cond_a} && {cond_b}{exempt}\"))\n")
    return "".join(rules)


def model_offset(fp):
    fp.BuildCourtyardCaches()
    box = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd).BBox()
    wx = pcbnew.ToMM(box.GetCenter().x - fp.GetPosition().x)
    wy = pcbnew.ToMM(box.GetCenter().y - fp.GetPosition().y)
    theta = math.radians(fp.GetOrientation().AsDegrees())
    lx = wx * math.cos(theta) - wy * math.sin(theta)
    ly = wx * math.sin(theta) + wy * math.cos(theta)
    return (lx, ly) if fp.IsFlipped() else (lx, -ly)


def add_3d_models(board):
    for fp in board.Footprints():
        models = fp.Models()
        for i in reversed(range(len(models))):
            if str(models[i].m_Filename).startswith("${"):
                del models[i]
        model = MODELS_3D.get(str(fp.GetFPID().GetLibItemName()))
        if model:
            ox, oy = model_offset(fp)
            m = pcbnew.FP_3DMODEL()
            m.m_Filename = f"hardware/circuits/lib/gateway_3d/{model[0]}"
            m.m_Offset = pcbnew.VECTOR3D(ox, oy, model[1])
            fp.Add3DModel(m)


def layout_dir(board):
    return REPO / "hardware/layout" / VARIANT / board


def build_dir(board):
    path = REPO / "hardware/build" / VARIANT / board
    path.mkdir(parents=True, exist_ok=True)
    return path


def load_plan(board):
    path = REPO / "hardware/circuits/variants" / VARIANT / f"layout_{board}.yaml"
    return yaml.safe_load(path.read_text())


def stored_board_path(board):
    return layout_dir(board) / f"{board}.kicad_pcb"


def fresh_board(board):
    out = build_dir(board) / f"{board}.fresh.kicad_pcb"
    kinet2pcb.kinet2pcb(str(build_dir(board) / f"{board}.net"), str(out),
                        [str(REPO / "hardware/circuits/lib")])
    return pcbnew.LoadBoard(str(out))


def set_side(fp, back):
    if fp.IsFlipped() != back:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_TOP_BOTTOM)


def carry_over(target, stored):
    moved = 0
    for fp in target.Footprints():
        old = stored.FindFootprintByReference(fp.GetReference())
        if old is None:
            continue
        set_side(fp, old.IsFlipped())
        fp.SetOrientation(old.GetOrientation())
        fp.SetPosition(old.GetPosition())
        fp.SetLocked(old.IsLocked())
        fp.Reference().SetVisible(old.Reference().IsVisible())
        moved += 1
    copied = 0
    for item in stored.GetTracks():
        net = target.FindNet(item.GetNetname())
        if net is None or item.GetNetCode() == 0:
            continue
        if item.GetClass() == "PCB_VIA":
            new = pcbnew.PCB_VIA(target)
            new.SetPosition(item.GetPosition())
            new.SetViaType(pcbnew.VIATYPE_THROUGH)
            new.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            new.SetDrill(item.GetDrillValue())
            new.SetWidth(item.GetWidth(pcbnew.F_Cu))
        elif item.GetClass() == "PCB_ARC":
            new = pcbnew.PCB_ARC(target)
            new.SetStart(item.GetStart())
            new.SetMid(item.GetMid())
            new.SetEnd(item.GetEnd())
            new.SetWidth(item.GetWidth())
            new.SetLayer(item.GetLayer())
        else:
            new = pcbnew.PCB_TRACK(target)
            new.SetStart(item.GetStart())
            new.SetEnd(item.GetEnd())
            new.SetWidth(item.GetWidth())
            new.SetLayer(item.GetLayer())
        new.SetLocked(item.IsLocked())
        target.Add(new)
        new.SetNet(net)
        copied += 1
    return moved, copied


def place_parts(board, plan, names):
    parts = plan["parts"]
    if not names:
        refs = list(parts)
    else:
        refs = [r for r, p in parts.items() if r in names or p.get("block") in names]
        unknown = set(names) - set(refs) - {p.get("block") for p in parts.values()}
        if unknown:
            sys.exit(f"unknown refs/blocks: {sorted(unknown)}")
    for ref in refs:
        p = parts[ref]
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            sys.exit(f"{ref} is in the layout plan but not in the netlist")
        set_side(fp, p.get("side") == "back")
        fp.SetOrientationDegrees(p.get("rot", 0))
        fp.SetPosition(pos(p["x"], p["y"]))
        fp.SetLocked(bool(p.get("locked", False)))
    return refs


def rect_points(r):
    return [(r["x0"], r["y0"]), (r["x1"], r["y0"]), (r["x1"], r["y1"]), (r["x0"], r["y1"])]


def add_pour(board, net, layer, region, priority=0):
    zone = pcbnew.ZONE(board)
    zone.SetLayer(LAYERS[layer])
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in rect_points(region):
        p = pos(x, y)
        outline.Append(p.x, p.y)
    zone.SetNet(board.FindNet(net))
    zone.SetAssignedPriority(priority)
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    zone.SetLocalClearance(pcbnew.FromMM(0.2))
    zone.SetMinThickness(pcbnew.FromMM(0.2))
    zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    board.Add(zone)


def clear_frame(board):
    for item in list(board.GetDrawings()):
        if item.GetLayer() in (pcbnew.Edge_Cuts, pcbnew.F_SilkS, pcbnew.B_SilkS):
            board.Remove(item)
    for zone in zones(board):
        board.RemoveNative(zone)


def zones(board):
    return [board.GetArea(i) for i in range(board.GetAreaCount())]


def apply_frame(board, plan):
    clear_frame(board)
    frame = plan["frame"]
    w, h = frame["size"]["x"], frame["size"]["y"]
    board.SetCopperLayerCount(frame["copper_layers"])
    settings = board.GetDesignSettings()
    settings.SetBoardThickness(pcbnew.FromMM(frame["thickness"]))
    settings.m_MinThroughDrill = pcbnew.FromMM(frame["min_drill"])
    settings.m_TrackMinWidth = pcbnew.FromMM(frame["min_track"])
    settings.m_CopperEdgeClearance = pcbnew.FromMM(frame["edge_clearance"])
    settings.m_MinSilkTextHeight = pcbnew.FromMM(0.6)
    corners = [tuple(p) for p in frame["outline"]] if "outline" in frame else \
        rect_points({"x0": 0, "y0": 0, "x1": w, "y1": h})
    for a, b in zip(corners, corners[1:] + corners[:1]):
        edge_seg(board, *a, *b)
    for slot in frame.get("slots", []):
        corners = rect_points(slot)
        for a, b in zip(corners, corners[1:] + corners[:1]):
            edge_seg(board, *a, *b)
    for kc in frame.get("no_coat_keepouts", []):
        lyr = pcbnew.B_SilkS if kc.get("layer") == "B.SilkS" else pcbnew.F_SilkS
        corners = rect_points(kc)
        for a, b in zip(corners, corners[1:] + corners[:1]):
            seg = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
            seg.SetLayer(lyr)
            seg.SetWidth(pcbnew.FromMM(0.15))
            seg.SetStart(pos(*a))
            seg.SetEnd(pos(*b))
            board.Add(seg)
    for lbl in frame.get("silk_labels", []):
        txt = pcbnew.PCB_TEXT(board)
        lyr = pcbnew.B_SilkS if lbl.get("layer") == "B.SilkS" else pcbnew.F_SilkS
        txt.SetLayer(lyr)
        txt.SetText(str(lbl["text"]))
        txt.SetPosition(pos(lbl["x"], lbl["y"]))
        txt.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(SILK_TEXT_MM), pcbnew.FromMM(SILK_TEXT_MM)))
        txt.SetTextThickness(pcbnew.FromMM(SILK_STROKE_MM))
        if lyr == pcbnew.B_SilkS:
            txt.SetMirrored(True)
        board.Add(txt)
    for layer in frame.get("plane_layers", []):
        board.SetLayerType(LAYERS[layer], pcbnew.LT_POWER)
    all_copper = [LAYERS[n] for n in LAYERS if board.IsLayerEnabled(LAYERS[n])]
    for k in frame.get("copper_keepout", []):
        layers = [LAYERS[n] for n in k["layers"]] if "layers" in k else all_copper
        keepout_zone(board, k["x0"], k["y0"], k["x1"], k["y1"], layers)
    for z in frame.get("pours", []):
        for layer in z["layers"]:
            add_pour(board, z["net"], layer, z["region"], z.get("priority", 0))
    case = plan.get("case", {})
    for kind, layer, half in (("legs", pcbnew.B_Cu, 2.0), ("pegs", pcbnew.F_Cu, 1.5),
                              ("standoffs", pcbnew.F_Cu, 1.5)):
        for p in case.get(kind, []):
            case_contact_keepout(board, p["x"], p["y"], half, layer)


def case_contact_keepout(board, x, y, half, layer):
    zone = pcbnew.ZONE(board)
    zone.SetIsRuleArea(True)
    zone.SetDoNotAllowFootprints(True)
    zone.SetDoNotAllowTracks(False)
    zone.SetDoNotAllowVias(False)
    zone.SetDoNotAllowPads(True)
    zone.SetDoNotAllowZoneFills(False)
    zone.SetLayer(layer)
    outline = zone.Outline()
    outline.NewOutline()
    for px, py in rect_points({"x0": x - half, "y0": y - half, "x1": x + half, "y1": y + half}):
        p = pos(px, py)
        outline.Append(p.x, p.y)
    board.Add(zone)


def fill_zones(board):
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


DRU_4LAYER = """\
(version 1)
(rule jlcpcb_minimum
  (constraint track_width (min 0.12mm))
  (constraint clearance (min 0.2mm))
  (constraint hole_size (min 0.2mm)))
(rule power_track_width
  (constraint track_width (min 0.22mm))
  (condition "A.hasNetclass('MAIN-Power') || A.hasNetclass('CROWN-Power') || A.hasNetclass('REFLECTOR-Power') || A.hasNetclass('DOME-Power') || A.hasNetclass('PROBE-Power')"))
"""


def neck_down_rule(refs):
    if not refs:
        return ""
    courtyards = " || ".join(f"A.intersectsCourtyard('{r}')" for r in refs)
    return ("(rule power_neck_down_at_fine_pitch_pins\n"
            "  (constraint track_width (min 0.15mm))\n"
            f"  (condition \"(A.hasNetclass('MAIN-Power') || A.hasNetclass('CROWN-Power') || A.hasNetclass('REFLECTOR-Power') || A.hasNetclass('DOME-Power') || A.hasNetclass('PROBE-Power')) && ({courtyards})\"))\n"
            "(rule neck_down_at_fine_pitch_pins\n"
            "  (constraint track_width (min 0.1mm))\n"
            f"  (condition \"{courtyards}\"))\n")


def raised_module_rule(refs):
    if not refs:
        return ""
    either = " || ".join(f"A.Reference == '{r}' || B.Reference == '{r}'" for r in refs)
    return ("(rule parts_under_raised_modules\n"
            "  (constraint courtyard_clearance (min -100mm))\n"
            f"  (condition \"{either}\"))\n")


def domain_at(regions, x, y):
    return next((d for d, areas in regions.items()
                 for r in (areas if isinstance(areas, list) else [areas])
                 if r["x0"] <= x <= r["x1"] and r["y0"] <= y <= r["y1"]), None)


def net_domains(board, regions):
    found = {}
    for fp in board.Footprints():
        for pad in fp.Pads():
            if not pad.GetNetname():
                continue
            p = pad.GetPosition()
            domain = domain_at(regions, pcbnew.ToMM(p.x), -pcbnew.ToMM(p.y))
            found.setdefault(pad.GetNetname(), set()).add(domain)
    return found


def net_classes(board, frame):
    classes = {}
    for net, domains in net_domains(board, frame["domain_regions"]).items():
        domains.discard(None)
        if len(domains) != 1:
            sys.exit(f"net {net} touches domains {sorted(domains) or 'none'}")
        domain = domains.pop()
        classes[net] = POWER_CLASS[domain] if net in frame["power_nets"] else domain
    return classes


def project_net_settings(classes, power_width=0.3):
    def netclass(name, priority, width):
        return {"name": name, "priority": priority, "clearance": 0.2, "track_width": width,
                "via_diameter": 0.6, "via_drill": 0.3, "microvia_diameter": 0.3,
                "microvia_drill": 0.1, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
                "diff_pair_width": 0.2, "wire_width": 6, "bus_width": 12, "line_style": 0,
                "pcb_color": "rgba(0,0,0,0)", "schematic_color": "rgba(0,0,0,0)"}
    defs = [netclass("Default", 2147483647, 0.2)]
    defs += [netclass(d, 2, 0.2) for d in POWER_CLASS]
    defs += [netclass(p, 1, power_width) for p in POWER_CLASS.values()]
    patterns = [{"netclass": c, "pattern": n} for n, c in sorted(classes.items())]
    return defs, patterns


def write_rules(board, plan, board_name, pcb_path):
    frame = plan["frame"]
    dru = dru_rules(frame["domains"], sorted(frame.get("isolators", [])),
                    SPEC["boards"].get(board_name, {}).get("isolation"),
                    DRU_4LAYER + neck_down_rule(frame.get("fine_pitch_power_pads", []))
                    + raised_module_rule(frame.get("raised_modules", [])))
    pcb_path.with_suffix(".kicad_dru").write_text(dru)
    pro_path = pcb_path.with_suffix(".kicad_pro")
    project = json.loads(pro_path.read_text()) if pro_path.exists() else {}
    defs, patterns = project_net_settings(net_classes(board, frame), frame.get("power_track_width", 0.3))
    project.setdefault("net_settings", {}).update({"classes": defs, "netclass_patterns": patterns})
    pro_path.write_text(json.dumps(project, indent=2))


RAISED_BODIES = {"XIAO_ESP32C6_Header"}


def body_box(fp):
    fp.BuildCourtyardCaches()
    bb = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd).BBox()
    if str(fp.GetFPID().GetLibItemName()) in RAISED_BODIES:
        fab = pcbnew.B_Fab if fp.IsFlipped() else pcbnew.F_Fab
        for item in fp.GraphicalItems():
            if item.GetLayer() == fab and item.Type() == pcbnew.PCB_SHAPE_T:
                bb.Merge(item.GetBoundingBox())
    return bb


def export_components(board, plan, out):
    heights = plan.get("heights", {})
    comps = []
    for fp in board.Footprints():
        bb = body_box(fp)
        p = fp.GetPosition()
        fpid = str(fp.GetFPID().GetLibItemName())
        comps.append({
            "ref": fp.GetReference(), "value": fp.GetValue(), "footprint": fpid,
            "x": round(pcbnew.ToMM(p.x), 3), "y": round(-pcbnew.ToMM(p.y), 3),
            "rot": fp.GetOrientation().AsDegrees(),
            "side": "back" if fp.IsFlipped() else "front",
            "height": heights.get(fp.GetReference(), heights.get("default", 1.2)),
            "pads": [[round(pcbnew.ToMM(pad.GetPosition().x), 3),
                      round(-pcbnew.ToMM(pad.GetPosition().y), 3), pad.GetNumber()]
                     for pad in fp.Pads() if pad.GetNumber() and pad.GetNumber() != "MP"],
            "tht_pads": [[round(pcbnew.ToMM(pad.GetPosition().x), 3),
                          round(-pcbnew.ToMM(pad.GetPosition().y), 3)]
                         for pad in fp.Pads() if pad.GetDrillSizeX() > 0],
            "bbox": {"x0": round(pcbnew.ToMM(bb.GetLeft()), 3),
                     "x1": round(pcbnew.ToMM(bb.GetRight()), 3),
                     "y0": round(-pcbnew.ToMM(bb.GetBottom()), 3),
                     "y1": round(-pcbnew.ToMM(bb.GetTop()), 3)},
        })
    out.write_text(json.dumps(comps, indent=1))


def save(board, plan, board_name, hidden_refs=()):
    path = stored_board_path(board_name)
    path.parent.mkdir(parents=True, exist_ok=True)
    widen_neckdown_stubs(board)
    for fp in board.Footprints():
        ref = fp.Reference()
        ref.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(SILK_TEXT_MM), pcbnew.FromMM(SILK_TEXT_MM)))
        ref.SetTextThickness(pcbnew.FromMM(SILK_STROKE_MM))
        if fp.GetReference() in hidden_refs:
            ref.SetVisible(False)
    if plan["frame"].get("silk_inside"):
        place_references_inside(board, plan["frame"])
    add_3d_models(board)
    fill_zones(board)
    board.Save(str(path))
    write_rules(board, plan, board_name, path)
    export_components(board, plan, build_dir(board_name) / f"components_{board_name}.json")


def outline_polygon(frame):
    if "outline" in frame:
        return Polygon([(x, -y) for x, y in frame["outline"]])
    w, h = frame["size"]["x"], frame["size"]["y"]
    return Polygon([(0, 0), (w, 0), (w, -h), (0, -h)])


def mm_box(box, grow=0.0):
    return (pcbnew.ToMM(box.GetLeft()) - grow, pcbnew.ToMM(box.GetTop()) - grow,
            pcbnew.ToMM(box.GetRight()) + grow, pcbnew.ToMM(box.GetBottom()) + grow)


def boxes_overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def silk_edges(item, grow=0.1):
    x0, y0, x1, y1 = mm_box(item.GetBoundingBox(), grow)
    if not isinstance(item, pcbnew.PCB_SHAPE) or item.GetShape() not in (pcbnew.SHAPE_T_RECT, pcbnew.SHAPE_T_POLY):
        return [(x0, y0, x1, y1)]
    w = 2 * grow + pcbnew.ToMM(item.GetWidth())
    return [(x0, y0, x1, y0 + w), (x0, y1 - w, x1, y1), (x0, y0, x0 + w, y1), (x1 - w, y0, x1, y1)]


def place_references_inside(board, frame):
    inside = outline_polygon(frame).buffer(-0.2)
    pads = {False: [], True: []}
    for item in board.GetDrawings():
        if item.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            pads[item.GetLayer() == pcbnew.B_SilkS].extend(silk_edges(item))
    for fp in board.Footprints():
        for pad in fp.Pads():
            for back in (False, True):
                if pad.IsOnLayer(pcbnew.B_Cu if back else pcbnew.F_Cu):
                    pads[back].append(mm_box(pad.GetBoundingBox(), 0.15))
        for item in fp.GraphicalItems():
            if item.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                pads[item.GetLayer() == pcbnew.B_SilkS].extend(silk_edges(item))
    taken = {False: [], True: []}

    def area(fp):
        x0, y0, x1, y1 = mm_box(fp.GetBoundingBox(False, False))
        return (x1 - x0) * (y1 - y0)

    def place_next_to(fp, text, back, rotate=False):
        """Try spots around `fp` (above, below, left, right, centre, corners) until `text` fits inside the board
        without touching pads, silk or already placed labels on that side."""
        x0, y0, x1, y1 = mm_box(fp.GetBoundingBox(False, False))
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        for angle in ((0, 90) if rotate else (0,)):
            text.SetTextAngle(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T))
            tx0, ty0, tx1, ty1 = mm_box(text.GetBoundingBox())
            dx, dy = (tx1 - tx0) / 2 + 0.2, (ty1 - ty0) / 2 + 0.2
            spots = [(cx, y0 - dy), (cx, y1 + dy), (x0 - dx, cy), (x1 + dx, cy), (cx, cy)]
            spots += [(x, y) for x in (x0 - dx, x1 + dx) for y in (y0 - dy, y1 + dy)]
            if rotate:   # connector labels may sit a little farther out
                spots += [(cx, y0 - dy - 1.0), (cx, y1 + dy + 1.0), (x0 - dx - 1.0, cy), (x1 + dx + 1.0, cy)]
                spots += [(cx + sx, y) for y in (y0 - dy, y1 + dy) for sx in (-3.0, 3.0, -6.0, 6.0)]
            for x, y in spots:
                text.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
                box = mm_box(text.GetBoundingBox(), 0.05)
                fits = inside.contains(Polygon([(box[0], box[1]), (box[2], box[1]), (box[2], box[3]), (box[0], box[3])]))
                if fits and not any(boxes_overlap(box, other) for other in pads[back] + taken[back]):
                    taken[back].append(box)
                    return True
        return False

    # Connector destination labels first ("CROWN", "MAIN", "N", "PZ NE", ...), on the connector's own side
    for ref_name, label in sorted(frame.get("connector_labels", {}).items()):
        fp = board.FindFootprintByReference(ref_name)
        if fp is None:
            continue
        back = fp.IsFlipped()
        for size in (SILK_TEXT_MM, 0.55):
            txt = pcbnew.PCB_TEXT(board)
            txt.SetLayer(pcbnew.B_SilkS if back else pcbnew.F_SilkS)
            txt.SetText(str(label))
            txt.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(size), pcbnew.FromMM(size)))
            txt.SetTextThickness(pcbnew.FromMM(SILK_STROKE_MM))
            txt.SetMirrored(back)
            if place_next_to(fp, txt, back, rotate=True):
                board.Add(txt)
                break
        else:
            print(f"  WARNING: no room for the silk label {label!r} next to {ref_name}")

    for fp in sorted(board.Footprints(), key=area, reverse=True):
        ref = fp.Reference()
        ref.SetVisible(True)
        ref.SetTextAngle(pcbnew.EDA_ANGLE(0, pcbnew.DEGREES_T))
        if not place_next_to(fp, ref, fp.IsFlipped()):
            ref.SetVisible(False)


def current_board(board_name, plan):
    board = fresh_board(board_name)
    stored = stored_board_path(board_name)
    if stored.exists():
        old = pcbnew.LoadBoard(str(stored))
        moved, copied = carry_over(board, old)
        new_refs = [fp.GetReference() for fp in board.Footprints()
                    if old.FindFootprintByReference(fp.GetReference()) is None]
        if new_refs:
            place_parts(board, plan, new_refs)
        print(f"{board_name}: kept {moved} footprint positions, {copied} tracks/vias, "
              f"placed new {new_refs or 'none'}")
    else:
        placed = place_parts(board, plan, list(plan["parts"]))
        print(f"{board_name}: new layout, placed {len(placed)} planned parts")
    apply_frame(board, plan)
    return board


def sync(board_name):
    plan = load_plan(board_name)
    save(current_board(board_name, plan), plan, board_name)
    print(f"{board_name}: saved {stored_board_path(board_name).relative_to(REPO)}")


def tidy_silk(board_name):
    plan = load_plan(board_name)
    path = stored_board_path(board_name)
    report = build_dir(board_name) / "silk_drc.json"
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--severity-all",
                    "--output", str(report), str(path)], capture_output=True, check=False)
    clashing = {item["description"].split("Reference field of ")[1].split()[0]
                for v in json.loads(report.read_text())["violations"]
                if v["type"] in ("silk_overlap", "silk_over_copper", "silk_edge_clearance")
                for item in v["items"] if item["description"].startswith("Reference field of ")}
    save(current_board(board_name, plan), plan, board_name, clashing)
    print(f"{board_name}: hid {len(clashing)} clashing references: {', '.join(sorted(clashing))}")


def export_dsn(board_name):
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    for zone in zones(board):
        if not zone.GetIsRuleArea():
            board.RemoveNative(zone)
    for track in list(board.GetTracks()):
        if not track.IsLocked():
            board.RemoveNative(track)
    routable = build_dir(board_name) / f"{board_name}.routable.kicad_pcb"
    board.Save(str(routable))
    write_rules(board, plan, board_name, routable)
    dsn = build_dir(board_name) / f"{board_name}.dsn"
    if not pcbnew.ExportSpecctraDSN(board, str(dsn)):
        sys.exit("DSN export failed")
    nets = sorted(str(n) for n in board.GetNetsByName() if str(n))
    project = json.loads(routable.with_suffix(".kicad_pro").read_text())
    text = with_net_classes(dsn.read_text(), nets, project["net_settings"])
    text = with_inset_boundary(without_plane_layers(text, plan["frame"]), plan["frame"])
    dsn.write_text(text)
    print(f"{board_name}: exported {dsn.relative_to(REPO)}")


def with_inset_boundary(dsn_text, frame):
    inset = (frame["edge_clearance"] + 0.05) * 1000   # router keeps tracks inside the DRC copper-to-edge clearance

    def rewrite(match):
        nums = [float(n) for n in match.group(2).split()]
        shrunk = Polygon(list(zip(nums[::2], nums[1::2]))).buffer(-inset, join_style="mitre")
        points = list(shrunk.exterior.coords)
        return match.group(1) + "  ".join(f"{x:.0f} {y:.0f}" for x, y in points) + ")"

    return re.sub(r"(\(boundary\s+\(path pcb 0\s+)([-\d.\s]+)\)", rewrite, dsn_text, count=1)


def without_plane_layers(dsn_text, frame):
    for layer in map(re.escape, frame.get("plane_layers", [])):
        dsn_text = re.sub(rf"    \(layer {layer}\n.*?\n    \)\n", "", dsn_text,
                          count=1, flags=re.DOTALL)
        dsn_text = re.sub(rf"[ \t]*\(shape \(\w+ {layer} [^()]*\)\)\n", "", dsn_text)
        dsn_text = re.sub(rf'[ \t]*\(keepout "" \(polygon {layer} [^()]*\)\)\n', "", dsn_text)
    if any(layer in dsn_text for layer in frame.get("plane_layers", [])):
        sys.exit("plane layer still referenced in DSN")
    index = iter(range(100))
    return re.sub(r"\(index \d+\)", lambda _: f"(index {next(index)})", dsn_text)


def with_net_classes(dsn_text, nets, net_settings):
    start = dsn_text.index("    (class ")
    end = dsn_text.index("  (wiring")
    via = dsn_text[start:end].split("(use_via ")[1].split(")")[0]
    assigned = {p["pattern"]: p["netclass"] for p in net_settings["netclass_patterns"]}
    classes = {c["name"]: c for c in net_settings["classes"]}
    groups = {}
    for net in nets:
        groups.setdefault(assigned.get(net, "Default"), []).append(net)
    blocks = []
    for cls, members in sorted(groups.items()):
        blocks.append(f'    (class {cls} {" ".join(members)}\n      (circuit\n        (use_via {via})\n      )\n'
                      f'      (rule\n        (width {classes[cls]["track_width"] * 1000:.0f})\n'
                      # +10 um over the DRC minimum: Freerouting rounds 45-degree geometry right onto the limit
                      f'        (clearance {(classes[cls]["clearance"] + 0.01) * 1000:.0f})\n      )\n    )\n')
    return dsn_text[:start] + "".join(blocks) + "  )\n" + dsn_text[end:]


GROUND_NETS = {"GND"}
_DETACHED = []


def unconnected(board):
    fill_zones(board)
    board.BuildConnectivity()
    return board.GetConnectivity().GetUnconnectedCount(False)


def prune_ground_tracks(board, nets=GROUND_NETS):
    """Remove router tracks of `nets` (GND + every net that has a copper pour) that the pours already connect."""
    candidates = [t for t in board.GetTracks() if t.GetNetname() in nets
                  and (t.GetClass() != "PCB_VIA" or t.GetNetname() not in GROUND_NETS)]   # keep GND stitching vias
    _DETACHED.extend(candidates)
    baseline = unconnected(board)
    removed, changed = 0, True
    while changed:
        changed = False
        for track in list(candidates):
            board.Remove(track)
            if unconnected(board) > baseline:
                board.Add(track)
            else:
                candidates.remove(track)
                removed += 1
                changed = True
    return removed


def widen_neckdown_stubs(board):
    traces = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
    min_w = pcbnew.FromMM(0.15)
    widened = 0
    for track in traces:
        if track.GetWidth() < min_w:
            track.SetWidth(min_w)
            widened += 1
    return widened


def import_ses(board_name):
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    ses = build_dir(board_name) / f"{board_name}.ses"
    if not pcbnew.ImportSpecctraSES(board, str(ses)):
        sys.exit("SES import failed")
    widened = widen_neckdown_stubs(board)
    pour_nets = GROUND_NETS | {z["net"] for z in plan["frame"].get("pours", [])}
    removed = prune_ground_tracks(board, pour_nets) if plan["frame"].get("prune_ground_tracks", True) else 0
    print(f"{board_name}: removed {removed} ground tracks the pours already cover, "
          f"widened {widened} neck-down stubs")
    save(board, plan, board_name)
    print(f"{board_name}: imported {ses.relative_to(REPO)}")


FREEROUTING = ["java", "-jar", os.environ.get("FREEROUTING_JAR", "/opt/freerouting.jar"),
               "-mp", "100", "-mt", "4", "--gui.enabled=false"]


def route(board_name, attempts=4):
    export_dsn(board_name)
    bdir = build_dir(board_name)
    dsn, ses = bdir / f"{board_name}.dsn", bdir / f"{board_name}.ses"
    for attempt in range(1, attempts + 1):
        run = subprocess.run(FREEROUTING + ["-de", str(dsn), "-do", str(ses)], capture_output=True, text=True,
                             check=True)
        summary = next((line for line in (run.stdout + run.stderr).splitlines()
                        if "Auto-routing stage completed" in line), "no summary")
        print(f"{board_name}: route attempt {attempt}: {summary.split('completed')[-1].strip()[:120]}")
        if "(0 unrouted" in summary:
            break
    import_ses(board_name)


def components(board_name):
    out = build_dir(board_name) / f"components_{board_name}.json"
    export_components(pcbnew.LoadBoard(str(stored_board_path(board_name))), load_plan(board_name), out)
    print(f"{board_name}: wrote {out.relative_to(REPO)}")


def unroute(board_name):
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    tracks = list(board.GetTracks())
    for track in tracks:
        board.Remove(track)
    save(board, plan, board_name)
    print(f"{board_name}: removed {len(tracks)} tracks/vias")


def place(board_name, names):
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    refs = place_parts(board, plan, names)
    save(board, plan, board_name)
    print(f"{board_name}: placed {', '.join(refs)}")


if __name__ == "__main__":
    name, command, *args = sys.argv[1:]
    if command == "sync":
        sync(name)
    elif command == "place":
        place(name, args)
    elif command == "tidy-silk":
        tidy_silk(name)
    elif command == "components":
        components(name)
    elif command == "unroute":
        unroute(name)
    elif command == "route":
        route(name, *(int(a) for a in args))
    elif command == "export-dsn":
        export_dsn(name)
    elif command == "import-ses":
        import_ses(name)
    else:
        sys.exit(f"unknown command: {command}")
