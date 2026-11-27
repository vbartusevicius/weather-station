"""Persistent board layout (KiCad 10 pcbnew, `eda` container).

    python3 hardware/scripts/layout.py <board> sync
    python3 hardware/scripts/layout.py <board> place <ref-or-block> ...
    python3 hardware/scripts/layout.py <board> route [time-limit-seconds]
    python3 hardware/scripts/layout.py <board> tidy-silk
    python3 hardware/scripts/layout.py <board> components
"""

import itertools
import json
import math
import re
import subprocess
import sys
from pathlib import Path

import kinet2pcb
import pcbnew
import yaml
from shapely.geometry import LineString, Point, Polygon

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
    if "via" in frame:
        settings.m_ViasMinSize = pcbnew.FromMM(frame["via"]["diameter"])
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
        size = lbl.get("size", SILK_TEXT_MM)   # optional larger marking text (mm)
        txt.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(size), pcbnew.FromMM(size)))
        if lbl.get("rot"):
            txt.SetTextAngle(pcbnew.EDA_ANGLE(lbl["rot"], pcbnew.DEGREES_T))
        txt.SetTextThickness(pcbnew.FromMM(max(SILK_STROKE_MM, 0.15 * size)))
        if lyr == pcbnew.B_SilkS:
            txt.SetMirrored(True)
        board.Add(txt)
    for line in frame.get("silk_lines", []):   # polylines, e.g. orientation arrows: {points: [[x, y], ...], width}
        lyr = pcbnew.B_SilkS if line.get("layer") == "B.SilkS" else pcbnew.F_SilkS
        pts = line["points"]
        for a, b in itertools.pairwise(pts):
            seg = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
            seg.SetLayer(lyr)
            seg.SetWidth(pcbnew.FromMM(line.get("width", 0.25)))
            seg.SetStart(pos(*a))
            seg.SetEnd(pos(*b))
            board.Add(seg)
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


def project_net_settings(classes, power_width=0.3, signal_width=0.2, via=(0.6, 0.3)):
    def netclass(name, priority, width):
        return {"name": name, "priority": priority, "clearance": 0.2, "track_width": width,
                "via_diameter": via[0], "via_drill": via[1], "microvia_diameter": 0.3,
                "microvia_drill": 0.1, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
                "diff_pair_width": 0.2, "wire_width": 6, "bus_width": 12, "line_style": 0,
                "pcb_color": "rgba(0,0,0,0)", "schematic_color": "rgba(0,0,0,0)"}
    defs = [netclass("Default", 2147483647, signal_width)]
    defs += [netclass(d, 2, signal_width) for d in POWER_CLASS]
    defs += [netclass(p, 1, power_width) for p in POWER_CLASS.values()]
    patterns = [{"netclass": c, "pattern": n} for n, c in sorted(classes.items())]
    return defs, patterns


def write_rules(board, plan, board_name, pcb_path):
    frame = plan["frame"]
    dru = dru_rules(frame["domains"], sorted(frame.get("isolators", [])),
                    SPEC["boards"].get(board_name, {}).get("isolation"),
                    DRU_4LAYER + neck_down_rule(frame.get("fine_pitch_power_pads", []))
                    + raised_module_rule(frame.get("raised_modules", [])))
    if "power_track_min" in frame:   # low-current boards (dome: < 2 mA) may neck power tracks down below 0.22 mm
        dru = dru.replace("(constraint track_width (min 0.22mm))", f"(constraint track_width (min {frame['power_track_min']}mm))")
    pcb_path.with_suffix(".kicad_dru").write_text(dru)
    pro_path = pcb_path.with_suffix(".kicad_pro")
    project = json.loads(pro_path.read_text()) if pro_path.exists() else {}
    # small boards with fine-pitch parts (dome: WSON-8 / X2QFN-10) may set narrower signal tracks and smaller vias
    via = frame.get("via", {})
    defs, patterns = project_net_settings(net_classes(board, frame), frame.get("power_track_width", 0.3),
                                          frame.get("signal_track_width", 0.2),
                                          (via.get("diameter", 0.6), via.get("drill", 0.3)))
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

    # hand-placed references from the plan (`ref_positions: {R1: {x, y, rot}}`) first, then the automatic search
    fixed = frame.get("ref_positions", {})
    for name, at in fixed.items():
        fp = board.FindFootprintByReference(name)
        if fp is None:
            continue
        ref = fp.Reference()
        ref.SetVisible(True)
        ref.SetTextAngle(pcbnew.EDA_ANGLE(at.get("rot", 0), pcbnew.DEGREES_T))
        ref.SetPosition(pos(at["x"], at["y"]))
        taken[fp.IsFlipped()].append(mm_box(ref.GetBoundingBox(), 0.05))
    for fp in sorted(board.Footprints(), key=area, reverse=True):
        if fp.GetReference() in fixed:
            continue
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
    add_planned_tracks(board, plan)
    return board


def add_planned_tracks(board, plan):
    """Hand-routed, locked tracks / vias from the plan's `tracks:` ({net, layer, width, points: [[x, y], ...]}) and
    `vias:` ({net, at: [x, y]}, frame via size) lists: they
    survive re-routing (export_dsn keeps locked tracks, so Freerouting treats them as fixed) and are rebuilt from the
    plan on every sync, so editing the plan moves them."""
    tracks, vias = plan.get("tracks", []), plan.get("vias", [])
    if not tracks and not vias:
        return
    for item in list(board.GetTracks()):
        if item.IsLocked():
            board.RemoveNative(item)
    for t in tracks:
        net = board.FindNet(t["net"])
        if net is None:
            sys.exit(f"planned track on unknown net {t['net']}")
        for a, b in zip(t["points"], t["points"][1:]):
            seg = pcbnew.PCB_TRACK(board)
            seg.SetStart(pos(*a))
            seg.SetEnd(pos(*b))
            seg.SetWidth(pcbnew.FromMM(t.get("width", 0.15)))
            seg.SetLayer(LAYERS[t.get("layer", "F.Cu")])
            seg.SetLocked(True)
            board.Add(seg)
            seg.SetNet(net)
    size = plan["frame"].get("via", {"diameter": 0.6, "drill": 0.3})
    for v in vias:
        net = board.FindNet(v["net"])
        if net is None:
            sys.exit(f"planned via on unknown net {v['net']}")
        via = pcbnew.PCB_VIA(board)
        via.SetPosition(pos(*v["at"]))
        via.SetWidth(pcbnew.FromMM(size["diameter"]))
        via.SetDrill(pcbnew.FromMM(size["drill"]))
        via.SetLocked(True)
        board.Add(via)
        via.SetNet(net)


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
    text = with_inset_boundary(text, plan["frame"])
    dsn.write_text(with_via_size(text, plan["frame"]))
    print(f"{board_name}: exported {dsn.relative_to(REPO)}")


def with_via_size(dsn_text, frame):
    """The loaded board exports the default 0.6 / 0.3 mm via; a frame `via` override renames the padstack (the SES
    import takes the via size from that name) and shrinks its copper rings."""
    via = frame.get("via")
    if not via:
        return dsn_text
    dia, drill = round(via["diameter"] * 1000), round(via["drill"] * 1000)
    m = re.search(r'\(padstack "(Via\[0-3\]_(\d+):(\d+)_um)"', dsn_text)
    if not m:
        sys.exit("no via padstack in DSN")
    name, old_dia = m.group(1), m.group(2)
    new = f"Via[0-3]_{dia}:{drill}_um"
    dsn_text = dsn_text.replace(f'"{name}"', f'"{new}"')
    start = dsn_text.index(f'(padstack "{new}"')
    end = dsn_text.index("(attach", start)
    block = re.sub(rf"\(circle (\S+) {old_dia}\)", rf"(circle \1 {dia})", dsn_text[start:end])
    return dsn_text[:start] + block + dsn_text[end:]


def with_inset_boundary(dsn_text, frame):
    inset = (frame["edge_clearance"] + 0.05) * 1000   # router keeps tracks inside the DRC copper-to-edge clearance

    def rewrite(match):
        nums = [float(n) for n in match.group(2).split()]
        shrunk = Polygon(list(zip(nums[::2], nums[1::2]))).buffer(-inset, join_style="mitre")
        points = list(shrunk.exterior.coords)
        return match.group(1) + "  ".join(f"{x:.0f} {y:.0f}" for x, y in points) + ")"

    match = re.search(r"\(boundary\s+\(path pcb 0\s+([-\d.\s]+)\)", dsn_text)
    dsn_text = re.sub(r"(\(boundary\s+\(path pcb 0\s+)([-\d.\s]+)\)", rewrite, dsn_text, count=1)
    if "outline" not in frame or not match:
        return dsn_text
    # Concave outlines (crown notches): Freerouting does not reliably honour the boundary in inner corners, so the
    # area outside the outline, grown by the edge clearance, is also a keepout on every routing layer.
    nums = [float(n) for n in match.group(1).split()]
    board = Polygon(list(zip(nums[::2], nums[1::2])))
    pockets = board.envelope.difference(board)          # notches / chamfers: hole-free pieces touching the bbox
    parts = [p.buffer(inset, join_style="mitre").intersection(board.envelope)
             for p in getattr(pockets, "geoms", [pockets])]
    layers = re.findall(r"\(layer (\S+)\n\s+\(type signal\)", dsn_text)
    keepouts = "".join(
        f'    (keepout "" (polygon {layer} 0  ' + "  ".join(f"{x:.0f} {y:.0f}" for x, y in part.exterior.coords) + "))\n"
        for part in parts if part.area > 0 for layer in layers)
    return re.sub(r"(\(boundary\s+\(path pcb 0[^)]*\)\s*\)\n)", lambda m: m.group(1) + keepouts, dsn_text, count=1)




def with_net_classes(dsn_text, nets, net_settings):
    start = dsn_text.index("    (class ")
    end = dsn_text.index("  (wiring")
    via = dsn_text[start:end].split("(use_via ")[1].split(")")[0]
    assigned = {p["pattern"]: p["netclass"] for p in net_settings["netclass_patterns"]}
    classes = {c["name"]: c for c in net_settings["classes"]}
    base_clearance = min(c["clearance"] for c in classes.values())
    base_um = round((base_clearance + 0.003) * 1000)
    smd_pad_um = round((base_clearance - 0.002) * 1000)   # KiCad LQFP-64 0.5 mm pads sit 199.5 um apart
    groups = {}
    for net in nets:
        groups.setdefault(assigned.get(net, "Default"), []).append(net)
    blocks = []
    for cls, members in sorted(groups.items()):
        clr_um = round((classes[cls]["clearance"] + 0.003) * 1000)
        wire_smd = f"\n        (clearance {base_um} (type wire_smd))" if classes[cls]["clearance"] > base_clearance else ""
        blocks.append(f'    (class {cls} {" ".join(members)}\n      (circuit\n        (use_via {via})\n      )\n'
                      f'      (rule\n        (width {classes[cls]["track_width"] * 1000:.0f})\n'
                      # +3 um over DRC minimum avoids 45-degree rounding errors while letting traces escape 0.5 mm LQFP pins
                      f'        (clearance {clr_um}){wire_smd}\n'
                      f'        (clearance {smd_pad_um} (type smd_smd))\n      )\n    )\n')
    return dsn_text[:start] + "".join(blocks) + "  )\n" + dsn_text[end:]


def unconnected(board):
    fill_zones(board)
    board.BuildConnectivity()
    return board.GetConnectivity().GetUnconnectedCount(False)


def _split_planar_segments(segs, snap_nm=500):
    """Split collinear overlaps, T-junctions and X-crossings among unlocked segments `[(ax, ay, bx, by, width), ...]`
    on one `(net, layer)`, keeping exact original endpoint coordinates where points coincide within `snap_nm`."""
    canon = []

    def snap_pt(px, py):
        for cx, cy in canon:
            if math.hypot(px - cx, py - cy) <= snap_nm:
                return (cx, cy)
        pt = (round(px), round(py))
        canon.append(pt)
        return pt

    raw = []
    for ax, ay, bx, by, w in segs:
        p, q = snap_pt(ax, ay), snap_pt(bx, by)
        if p != q:
            raw.append((p, q, w))
    if len(raw) <= 1:
        return raw

    # Collect interior split parameters t in (0, 1) along each segment from other endpoints and proper crossings.
    cuts = [[0.0, 1.0] for _ in raw]
    for i, (a, b, _) in enumerate(raw):
        ax, ay = a
        dx, dy = b[0] - ax, b[1] - ay
        len2 = dx * dx + dy * dy
        length = math.sqrt(len2)
        for j, (c, d, _) in enumerate(raw):
            if i == j:
                continue
            for px, py in (c, d):
                t = ((px - ax) * dx + (py - ay) * dy) / len2
                if 0.0 < t < 1.0 and math.hypot(px - (ax + t * dx), py - (ay + t * dy)) <= snap_nm:
                    snap_pt(px, py)
                    cuts[i].append(t)
            if j > i:
                ex, ey = d[0] - c[0], d[1] - c[1]
                denom = dx * ey - dy * ex
                if abs(denom) > 1e-6 * length * math.hypot(ex, ey):
                    rx, ry = c[0] - ax, c[1] - ay
                    ti = (rx * ey - ry * ex) / denom
                    tj = (rx * dy - ry * dx) / denom
                    if 0.0 < ti < 1.0 and 0.0 < tj < 1.0:
                        ix, iy = ax + ti * dx, ay + ti * dy
                        snap_pt(ix, iy)
                        cuts[i].append(ti)
                        cuts[j].append(tj)

    dedup = {}
    for (a, b, w), ts in zip(raw, cuts):
        ax, ay = a
        dx, dy = b[0] - ax, b[1] - ay
        pts = [snap_pt(ax + t * dx, ay + t * dy) for t in sorted(ts)]
        for u, v in itertools.pairwise(pts):
            if u != v:
                key = (u, v) if u < v else (v, u)
                dedup[key] = max(dedup.get(key, 0), w)
    return [(u, v, w) for (u, v), w in dedup.items()]


def prune_track_loops(board):
    """Planar split of collinear/crossing same-net segments per layer, followed by pruning open-air stubs and same-net
    track/via/pour cycles down to a minimal forest while preserving full pad connectivity."""
    baseline = unconnected(board)
    snap_nm = 500
    unlocked = [t for t in board.GetTracks()
                if t.Type() == pcbnew.PCB_TRACE_T and not t.IsLocked() and t.GetNetCode() > 0]
    if not unlocked:
        return 0, 0

    locked_tracks = [t for t in board.GetTracks()
                     if t.Type() == pcbnew.PCB_TRACE_T and t.IsLocked() and t.GetNetCode() > 0]
    vias = [v for v in board.GetTracks() if v.Type() == pcbnew.PCB_VIA_T and v.GetNetCode() > 0]
    pads_by_net = {}
    for p in board.GetPads():
        if p.GetNetCode() > 0:
            pads_by_net.setdefault(p.GetNetCode(), []).append(p)
    vias_by_net = {}
    for v in vias:
        vias_by_net.setdefault(v.GetNetCode(), []).append(v)
    locked_by_nl = {}
    for t in locked_tracks:
        locked_by_nl.setdefault((t.GetNetCode(), t.GetLayer()), []).append(t)

    # Pre-extract per-(net, layer) filled copper islands so pour-shorted loops and redundant pour tracks are seen.
    islands_by_nl = {}
    for z_idx, z in enumerate(zones(board)):
        if z.GetIsRuleArea() or z.GetNetCode() <= 0:
            continue
        for lyr in LAYERS.values():
            if not z.IsOnLayer(lyr):
                continue
            ps = z.GetFilledPolysList(lyr)
            for idx in range(ps.OutlineCount()):
                sub = pcbnew.SHAPE_POLY_SET()
                sub.AddOutline(ps.Outline(idx))
                for h in range(ps.HoleCount(idx)):
                    sub.AddHole(ps.Hole(idx, h), 0)
                islands_by_nl.setdefault((z.GetNetCode(), lyr), []).append((f"zone_{z_idx}_{lyr}_{idx}", z, sub))

    by_net = {}
    for t in unlocked:
        by_net.setdefault(t.GetNetCode(), []).append(t)

    split_delta = 0
    pruned_count = 0
    replacements = []
    to_remove = []

    for net_code, net_tracks in by_net.items():
        net_obj = net_tracks[0].GetNet()
        by_lyr = {}
        for t in net_tracks:
            s, e = t.GetStart(), t.GetEnd()
            by_lyr.setdefault(t.GetLayer(), []).append((s.x, s.y, e.x, e.y, t.GetWidth()))

        # 1. Split collinear/crossing segments on each layer.
        layer_segs = {}
        orig_cnt = len(net_tracks)
        after_split_cnt = 0
        for lyr, segs in by_lyr.items():
            sp = _split_planar_segments(segs, snap_nm)
            layer_segs[lyr] = sp
            after_split_cnt += len(sp)

        # 2. Build 3D net connectivity: anchors (pads, vias, locked tracks, filled zone islands) have cost 0;
        #    unlocked split track segments have positive cost = length / width (keeps shorter, wider tracks).
        pads = pads_by_net.get(net_code, [])
        net_vias = vias_by_net.get(net_code, [])

        def point_anchors(pt, lyr, _pads=pads, _vias=net_vias, _nc=net_code):
            v2 = pcbnew.VECTOR2I(pt[0], pt[1])
            anc = []
            for p_idx, p in enumerate(_pads):
                if p.IsOnLayer(lyr) and p.HitTest(v2):
                    anc.append(("pad", p_idx))
            for v_idx, v in enumerate(_vias):
                vp = v.GetPosition()
                if math.hypot(pt[0] - vp.x, pt[1] - vp.y) <= v.GetWidth(lyr) / 2:
                    anc.append(("via", v_idx))
            for lt_idx, lt in enumerate(locked_by_nl.get((_nc, lyr), ())):
                ls, le = lt.GetStart(), lt.GetEnd()
                dx, dy = le.x - ls.x, le.y - ls.y
                l2 = dx * dx + dy * dy
                u = 0.0 if l2 == 0 else max(0.0, min(1.0, ((pt[0] - ls.x) * dx + (pt[1] - ls.y) * dy) / l2))
                if math.hypot(pt[0] - (ls.x + u * dx), pt[1] - (ls.y + u * dy)) <= snap_nm:
                    anc.append(("locked", lyr, lt_idx))
            for z_tag, zone_obj, poly in islands_by_nl.get((_nc, lyr), ()):
                if zone_obj.HitTestFilledArea(lyr, v2) and poly.Collide(v2, 0):
                    anc.append((z_tag,))
            return anc

        # Union-Find over zero-cost physical contacts (pads <-> vias <-> zones <-> track vertices).
        parent = {}

        def find(x, _par=parent):
            while _par.setdefault(x, x) != x:
                _par[x] = _par[_par[x]]
                x = _par[x]
            return x

        def union(a, b, _find=find, _par=parent):
            ra, rb = _find(a), _find(b)
            if ra != rb:
                _par[rb] = ra
                return True
            return False

        # Direct pad <-> via and pad/via <-> zone contacts even without tracks.
        for lyr in LAYERS.values():
            for p_idx, p in enumerate(pads):
                if not p.IsOnLayer(lyr):
                    continue
                pp = p.GetPosition()
                for z_tag, zone_obj, poly in islands_by_nl.get((net_code, lyr), ()):
                    if zone_obj.HitTestFilledArea(lyr, pp) and poly.Collide(pp, 0):
                        union(("pad", p_idx), (z_tag,))
            for v_idx, v in enumerate(net_vias):
                vp = v.GetPosition()
                for p_idx, p in enumerate(pads):
                    if p.IsOnLayer(lyr) and p.HitTest(vp):
                        union(("via", v_idx), ("pad", p_idx))
                for z_tag, zone_obj, poly in islands_by_nl.get((net_code, lyr), ()):
                    if zone_obj.HitTestFilledArea(lyr, vp) and poly.Collide(vp, 0):
                        union(("via", v_idx), (z_tag,))
            for lt_idx, lt in enumerate(locked_by_nl.get((net_code, lyr), ())):
                for end in (lt.GetStart(), lt.GetEnd()):
                    for anc in point_anchors((end.x, end.y), lyr):
                        union(("locked", lyr, lt_idx), anc)

        weighted_edges = []
        vertex_anchors = {}
        for lyr, sp in layer_segs.items():
            for u, v, w in sp:
                nu, nv = ("pt", lyr, u), ("pt", lyr, v)
                for pt, node in ((u, nu), (v, nv)):
                    if node not in vertex_anchors:
                        ancs = point_anchors(pt, lyr)
                        vertex_anchors[node] = bool(ancs)
                        for anc in ancs:
                            union(node, anc)
                length = math.hypot(v[0] - u[0], v[1] - u[1])
                weighted_edges.append((length / max(w, 1), lyr, u, v, w, nu, nv))

        # Kruskal's spanning forest over unlocked track segments breaks all same-net cycles & redundant pour tracks.
        kept = []
        for _cost, lyr, u, v, w, nu, nv in sorted(weighted_edges, key=lambda item: item[0]):
            if union(nu, nv):
                kept.append((lyr, u, v, w, nu, nv))

        # Iteratively peel open-air degree-1 stubs (vertices that are not anchors and have degree <= 1).
        changed = True
        while changed:
            changed = False
            deg = {}
            for _lyr, _u, _v, _w, nu, nv in kept:
                deg[nu] = deg.get(nu, 0) + 1
                deg[nv] = deg.get(nv, 0) + 1
            nxt = [e for e in kept
                   if (deg[e[4]] > 1 or vertex_anchors.get(e[4], False))
                   and (deg[e[5]] > 1 or vertex_anchors.get(e[5], False))]
            if len(nxt) < len(kept):
                kept = nxt
                changed = True

        split_delta += after_split_cnt - orig_cnt
        pruned_count += after_split_cnt - len(kept)
        to_remove.extend(net_tracks)
        for lyr, u, v, w, _nu, _nv in kept:
            replacements.append((net_obj, lyr, u, v, w))

    if not split_delta and not pruned_count:
        return 0, 0

    snapshot = [(t.GetNet(), t.GetLayer(), t.GetStart(), t.GetEnd(), t.GetWidth()) for t in to_remove]
    for t in to_remove:
        board.RemoveNative(t)
    added = []
    for net_obj, lyr, u, v, w in replacements:
        seg = pcbnew.PCB_TRACK(board)
        seg.SetStart(pcbnew.VECTOR2I(u[0], u[1]))
        seg.SetEnd(pcbnew.VECTOR2I(v[0], v[1]))
        seg.SetWidth(w)
        seg.SetLayer(lyr)
        board.Add(seg)
        seg.SetNet(net_obj)
        added.append(seg)

    if unconnected(board) > baseline:
        for seg in added:
            board.RemoveNative(seg)
        for net_obj, lyr, s, e, w in snapshot:
            seg = pcbnew.PCB_TRACK(board)
            seg.SetStart(s)
            seg.SetEnd(e)
            seg.SetWidth(w)
            seg.SetLayer(lyr)
            board.Add(seg)
            seg.SetNet(net_obj)
        fill_zones(board)
        return 0, 0
    return split_delta, pruned_count


SMOOTH_SNAP_NM = 15_000        # 0.015 mm endpoint snap tolerance for linking router segments into chains
SMOOTH_SAFETY_MM = 0.015       # 15 um clearance safety margin above KiCad DRC minimums
SMOOTH_DIFF_MAX_DELTA_NM = 1_000_000   # <= 1.0 mm per-shortcut reduction cap on length-matched / diff nets
SMOOTH_MAX_WINDOW = 8


def _is_octilinear(a, b):
    dx = abs(b[0] - a[0])
    dy = abs(b[1] - a[1])
    return dx == 0 or dy == 0 or abs(dx - dy) <= 1


def _octilinear_candidates(a, b):
    """Return octilinear (0/45/90 deg) paths from `a` to `b`: either the single straight segment `[a, b]` or both
    minimal-length 2-segment 45-deg + H/V doglegs `[a, m1, b]` and `[a, m2, b]`."""
    if a == b:
        return []
    if _is_octilinear(a, b):
        return [[a, b]]
    dx, dy = b[0] - a[0], b[1] - a[1]
    d = min(abs(dx), abs(dy))
    sx = 1 if dx > 0 else -1
    sy = 1 if dy > 0 else -1
    m1 = (a[0] + sx * d, a[1] + sy * d)   # 45 deg first, then H/V
    m2 = (b[0] - sx * d, b[1] - sy * d)   # H/V first, then 45 deg
    cands = []
    if m1 != a and m1 != b:
        cands.append([a, m1, b])
    if m2 != a and m2 != b and m2 != m1:
        cands.append([a, m2, b])
    return cands


def _pt_seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    if l2 <= 1e-18:
        return math.hypot(px - ax, py - ay)
    u = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    return math.hypot(px - (ax + u * dx), py - (ay + u * dy))


def _seg_intersect(ax, ay, bx, by, cx, cy, dx, dy):
    def orient(px, py, qx, qy, rx, ry):
        return (qx - px) * (ry - py) - (qy - py) * (rx - px)

    def on_seg(px, py, qx, qy, rx, ry):
        return (min(px, rx) - 1e-9 <= qx <= max(px, rx) + 1e-9
                and min(py, ry) - 1e-9 <= qy <= max(py, ry) + 1e-9)

    o1 = orient(ax, ay, bx, by, cx, cy)
    o2 = orient(ax, ay, bx, by, dx, dy)
    o3 = orient(cx, cy, dx, dy, ax, ay)
    o4 = orient(cx, cy, dx, dy, bx, by)
    if (o1 > 1e-12 and o2 < -1e-12 or o1 < -1e-12 and o2 > 1e-12) and (
        o3 > 1e-12 and o4 < -1e-12 or o3 < -1e-12 and o4 > 1e-12
    ):
        return True
    return (
        (abs(o1) <= 1e-12 and on_seg(ax, ay, cx, cy, bx, by))
        or (abs(o2) <= 1e-12 and on_seg(ax, ay, dx, dy, bx, by))
        or (abs(o3) <= 1e-12 and on_seg(cx, cy, ax, ay, dx, dy))
        or (abs(o4) <= 1e-12 and on_seg(cx, cy, bx, by, dx, dy))
    )


def _seg_seg_dist(ax, ay, bx, by, cx, cy, dx, dy):
    if _seg_intersect(ax, ay, bx, by, cx, cy, dx, dy):
        return 0.0
    return min(
        _pt_seg_dist(ax, ay, cx, cy, dx, dy),
        _pt_seg_dist(bx, by, cx, cy, dx, dy),
        _pt_seg_dist(cx, cy, ax, ay, bx, by),
        _pt_seg_dist(dx, dy, ax, ay, bx, by),
    )


def _is_length_matched_net(net_name, frame):
    if net_name in set(frame.get("length_matched_nets", ())):
        return True
    upper = net_name.upper()
    return (
        upper.endswith(("_P", "_N", "+", "-"))
        or upper.startswith(("RS485_", "USB_D", "TX_P", "TX_N", "XD_"))
    )


def _board_polygon_kicad_mm(frame):
    """Board outline minus milled slots in KiCad mm coordinates `(x, -y)`."""
    w, h = frame["size"]["x"], frame["size"]["y"]
    pts = [(p[0], -p[1]) for p in frame["outline"]] if "outline" in frame else [
        (0.0, 0.0), (w, 0.0), (w, -h), (0.0, -h)]
    poly = Polygon(pts)
    for s in frame.get("slots", []):
        slot = Polygon([(s["x0"], -s["y0"]), (s["x1"], -s["y0"]), (s["x1"], -s["y1"]), (s["x0"], -s["y1"])])
        poly = poly.difference(slot)
    return poly


def _pad_polygon_mm(pad, layer):
    polyset = pcbnew.SHAPE_POLY_SET()
    pad.TransformShapeToPolygon(polyset, layer, 0, pcbnew.FromMM(0.01), pcbnew.ERROR_INSIDE)
    if polyset.OutlineCount() == 0:
        bb = pad.GetBoundingBox()
        return Polygon([
            (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop())),
            (pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetTop())),
            (pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom())),
            (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetBottom())),
        ])
    ring = polyset.Outline(0)
    coords = [(pcbnew.ToMM(ring.CPoint(i).x), pcbnew.ToMM(ring.CPoint(i).y)) for i in range(ring.PointCount())]
    return Polygon(coords).buffer(0)


def _zone_outline_poly_mm(zone):
    outline = zone.Outline()
    if outline.OutlineCount() == 0:
        return None
    ring = outline.Outline(0)
    coords = [(pcbnew.ToMM(ring.CPoint(i).x), pcbnew.ToMM(ring.CPoint(i).y)) for i in range(ring.PointCount())]
    return Polygon(coords).buffer(0) if len(coords) >= 3 else None


def smooth_tracks(board, plan):
    """DRC-safe octilinear (0/45/90 deg) track smoother eliminating autorouter V-detours and micro-staircase jogs.

    Returns `(removed_segments, saved_mm)`. Modifies `board` in-place (with automatic rollback if connectivity changes).
    """
    frame = plan["frame"]
    baseline_unconn = unconnected(board)
    settings = board.GetDesignSettings()
    base_clr = max(0.20, pcbnew.ToMM(settings.m_MinClearance))
    edge_clr = frame["edge_clearance"] + 0.05   # match with_inset_boundary so tracks clear panel mouse-bite holes
    hole_clr = max(0.25, pcbnew.ToMM(settings.m_HoleClearance), edge_clr)
    power_nets = set(frame.get("power_nets", ()))
    power_min_nm = pcbnew.FromMM(frame.get("power_track_min", 0.22))

    # Build per-netclass clearance map (0.20 mm base; respects any larger netclass clearance in `.kicad_pro`).
    pro_path = Path(board.GetFileName()).with_suffix(".kicad_pro") if board.GetFileName() else None
    cls_clr_by_name = {}
    pat_cls_by_net = {}
    if pro_path is not None and pro_path.exists():
        ns = json.loads(pro_path.read_text()).get("net_settings", {})
        cls_clr_by_name = {c["name"]: float(c.get("clearance", base_clr)) for c in ns.get("classes", [])}
        pat_cls_by_net = {p["pattern"]: p["netclass"] for p in ns.get("netclass_patterns", [])}
    net_clr_by_code = {0: base_clr}
    for net in board.GetNetsByNetcode().values():
        nc = net.GetNetCode()
        cname = pat_cls_by_net.get(net.GetNetname(), net.GetNetClassName())
        net_clr_by_code[nc] = max(base_clr, cls_clr_by_name.get(cname, base_clr))

    # Skip plane/pour nets (zones handle their connectivity; smoothing them is unnecessary and risks thermal relief issues).
    pour_nets = {p["net"] for p in frame.get("pours", ())}
    for z in zones(board):
        if not z.GetIsRuleArea() and z.GetNetname():
            pour_nets.add(z.GetNetname())

    # Fine-pitch power-pad courtyards (where DRU allows power traces to neck below power_min_nm).
    fine_pitch_polys = []
    for ref in frame.get("fine_pitch_power_pads", ()):
        fp = board.FindFootprintByReference(ref)
        if fp is not None:
            fp.BuildCourtyardCaches()
            bb = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd).BBox()
            fine_pitch_polys.append(Polygon([
                (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop())),
                (pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetTop())),
                (pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom())),
                (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetBottom())),
            ]))

    # 1. Pre-index static board obstacles: Edge.Cuts segments, board polygon, rule areas, vias, pads, holes, pours.
    board_poly = _board_polygon_kicad_mm(frame)
    edge_segs = []
    for item in board.GetDrawings():
        if item.GetLayer() != pcbnew.Edge_Cuts or not isinstance(item, pcbnew.PCB_SHAPE):
            continue
        if item.GetShape() == pcbnew.SHAPE_T_SEGMENT:
            s, e = item.GetStart(), item.GetEnd()
            ax, ay, bx, by = pcbnew.ToMM(s.x), pcbnew.ToMM(s.y), pcbnew.ToMM(e.x), pcbnew.ToMM(e.y)
            edge_segs.append((ax, ay, bx, by, min(ax, bx), min(ay, by), max(ax, bx), max(ay, by)))
        else:
            polyset = pcbnew.SHAPE_POLY_SET()
            item.TransformShapeToPolygon(polyset, pcbnew.Edge_Cuts, 0, pcbnew.FromMM(0.05), pcbnew.ERROR_INSIDE)
            for oi in range(polyset.OutlineCount()):
                ring = polyset.Outline(oi)
                n = ring.PointCount()
                for k in range(n):
                    p1, p2 = ring.CPoint(k), ring.CPoint((k + 1) % n)
                    ax, ay, bx, by = pcbnew.ToMM(p1.x), pcbnew.ToMM(p1.y), pcbnew.ToMM(p2.x), pcbnew.ToMM(p2.y)
                    edge_segs.append((ax, ay, bx, by, min(ax, bx), min(ay, by), max(ax, bx), max(ay, by)))

    keepout_polys = {lyr: [] for lyr in LAYERS.values()}
    other_pour_polys = {lyr: [] for lyr in LAYERS.values()}
    for z in zones(board):
        poly = _zone_outline_poly_mm(z)
        if poly is None or poly.is_empty:
            continue
        bounds = poly.bounds
        if z.GetIsRuleArea():
            if z.GetDoNotAllowTracks():
                for lyr in LAYERS.values():
                    if z.IsOnLayer(lyr):
                        keepout_polys[lyr].append((poly, bounds))
        elif z.GetNetname() and z.GetNetname() != "GND":
            for lyr in LAYERS.values():
                if z.IsOnLayer(lyr):
                    other_pour_polys[lyr].append((z.GetNetCode(), poly, bounds, net_clr_by_code.get(z.GetNetCode(), base_clr)))

    vias_all = []
    vias_by_net = {}
    for item in board.GetTracks():
        if item.Type() == pcbnew.PCB_VIA_T:
            vp = item.GetPosition()
            nc = item.GetNetCode()
            vr = pcbnew.ToMM(item.GetWidth(pcbnew.F_Cu)) / 2.0
            vias_all.append((nc, pcbnew.ToMM(vp.x), pcbnew.ToMM(vp.y), vr, net_clr_by_code.get(nc, base_clr)))
            vias_by_net.setdefault(nc, []).append((vp.x, vp.y, item.GetWidth(pcbnew.F_Cu) // 2))

    pads_by_layer = {lyr: [] for lyr in LAYERS.values()}
    pads_by_nl = {}
    holes_all = []
    for p in board.GetPads():
        nc = p.GetNetCode()
        pp = p.GetPosition()
        cx, cy = pcbnew.ToMM(pp.x), pcbnew.ToMM(pp.y)
        clr = net_clr_by_code.get(nc, base_clr)
        ds = p.GetDrillSize()
        drill_r = max(pcbnew.ToMM(ds.x), pcbnew.ToMM(ds.y)) / 2.0
        if drill_r > 0.0:
            holes_all.append((nc, cx, cy, drill_r, clr))
        is_circle = p.GetShape() == pcbnew.PAD_SHAPE_CIRCLE
        for lyr in LAYERS.values():
            if not p.IsOnLayer(lyr):
                continue
            pads_by_nl.setdefault((nc, lyr), []).append(p)
            if is_circle:
                pr = pcbnew.ToMM(p.GetSize().x) / 2.0
                pads_by_layer[lyr].append(("circle", nc, cx, cy, pr, clr, (cx - pr, cy - pr, cx + pr, cy + pr)))
            else:
                poly = _pad_polygon_mm(p, lyr)
                pads_by_layer[lyr].append(("poly", nc, poly, cx, cy, clr, poly.bounds))

    # 2. Index all tracks per copper layer (live mutable table so accepted shortcuts update obstacle geometry).
    orig_unlocked_by_nl = {}
    locked_by_nl = {}
    live_tracks = {lyr: {} for lyr in LAYERS.values()}
    next_tid = 0
    for item in board.GetTracks():
        if item.Type() == pcbnew.PCB_VIA_T:
            continue
        lyr = item.GetLayer()
        if lyr not in live_tracks:
            continue
        nc = item.GetNetCode()
        s, e = item.GetStart(), item.GetEnd()
        w_nm = item.GetWidth()
        w_mm = pcbnew.ToMM(w_nm)
        clr = net_clr_by_code.get(nc, base_clr)
        if item.Type() == pcbnew.PCB_TRACE_T and not item.IsLocked():
            next_tid += 1
            live_tracks[lyr][next_tid] = (nc, pcbnew.ToMM(s.x), pcbnew.ToMM(s.y),
                                          pcbnew.ToMM(e.x), pcbnew.ToMM(e.y), w_mm, clr)
            orig_unlocked_by_nl.setdefault((nc, lyr), []).append((next_tid, item))
        else:
            locked_by_nl.setdefault((nc, lyr), []).append(item)
            if item.Type() == pcbnew.PCB_ARC_T:
                polyset = pcbnew.SHAPE_POLY_SET()
                item.TransformShapeToPolygon(polyset, lyr, 0, pcbnew.FromMM(0.02), pcbnew.ERROR_INSIDE)
                for oi in range(polyset.OutlineCount()):
                    ring = polyset.Outline(oi)
                    n = ring.PointCount()
                    for k in range(n):
                        p1, p2 = ring.CPoint(k), ring.CPoint((k + 1) % n)
                        next_tid += 1
                        live_tracks[lyr][next_tid] = (nc, pcbnew.ToMM(p1.x), pcbnew.ToMM(p1.y),
                                                      pcbnew.ToMM(p2.x), pcbnew.ToMM(p2.y), 0.0, clr)
            else:
                next_tid += 1
                live_tracks[lyr][next_tid] = (nc, pcbnew.ToMM(s.x), pcbnew.ToMM(s.y),
                                              pcbnew.ToMM(e.x), pcbnew.ToMM(e.y), w_mm, clr)

    def segment_clear(lyr, nc, ax_nm, ay_nm, bx_nm, by_nm, w_nm, ignore_tids, shared_end_pts_mm):
        """Verify candidate segment `(ax, ay) -> (bx, by)` against outline, cutouts, holes, keepouts, other-net
        tracks/vias/pads/pours, and same-net self-crossings / non-local copper overlaps."""
        ax, ay = pcbnew.ToMM(ax_nm), pcbnew.ToMM(ay_nm)
        bx, by = pcbnew.ToMM(bx_nm), pcbnew.ToMM(by_nm)
        if math.hypot(bx - ax, by - ay) <= 1e-6:
            return False
        hw = pcbnew.ToMM(w_nm) / 2.0
        my_clr = net_clr_by_code.get(nc, base_clr)
        sx0, sy0, sx1, sy1 = min(ax, bx), min(ay, by), max(ax, bx), max(ay, by)

        # (a) Board outline & milled slots (must lie strictly inside with edge_clr + safety margin).
        if not board_poly.contains(Point((ax + bx) * 0.5, (ay + by) * 0.5)):
            return False
        req_edge = edge_clr + SMOOTH_SAFETY_MM + hw
        for eax, eay, ebx, eby, ex0, ey0, ex1, ey1 in edge_segs:
            if sx1 + req_edge < ex0 or ex1 + req_edge < sx0 or sy1 + req_edge < ey0 or ey1 + req_edge < sy0:
                continue
            if _seg_seg_dist(ax, ay, bx, by, eax, eay, ebx, eby) < req_edge:
                return False

        # (b) Rule-area keepout zones on this layer.
        req_ko = hw + SMOOTH_SAFETY_MM
        seg_ls = None
        for poly, (kx0, ky0, kx1, ky1) in keepout_polys[lyr]:
            if sx1 + req_ko < kx0 or kx1 + req_ko < sx0 or sy1 + req_ko < ky0 or ky1 + req_ko < sy0:
                continue
            if seg_ls is None:
                seg_ls = LineString([(ax, ay), (bx, by)])
            if seg_ls.distance(poly) < req_ko:
                return False

        # (c) Unnetted & other-net drill holes (NPTH & PTH across all layers).
        for h_nc, hx, hy, hr, h_clr in holes_all:
            if h_nc == nc:
                continue
            req = hr + hw + (max(hole_clr, my_clr) if h_nc == 0 else max(hole_clr, my_clr, h_clr)) + SMOOTH_SAFETY_MM
            if sx1 + req < hx or hx + req < sx0 or sy1 + req < hy or hy + req < sy0:
                continue
            if _pt_seg_dist(hx, hy, ax, ay, bx, by) < req:
                return False

        # (d) Other-net vias (using GetWidth(F_Cu)).
        for v_nc, vx, vy, vr, v_clr in vias_all:
            if v_nc == nc:
                continue
            req = vr + hw + max(my_clr, v_clr) + SMOOTH_SAFETY_MM
            if sx1 + req < vx or vx + req < sx0 or sy1 + req < vy or vy + req < sy0:
                continue
            if _pt_seg_dist(vx, vy, ax, ay, bx, by) < req:
                return False

        # (e) Pads on this layer: exact circular distance for PAD_SHAPE_CIRCLE, exact polygon for others.
        for pad_rec in pads_by_layer[lyr]:
            kind = pad_rec[0]
            p_nc = pad_rec[1]
            if p_nc == nc:
                # Reject grazing an unrelated same-net pad that is not one of the segment's endpoints.
                if kind == "circle":
                    _, _, px, py, pr, _, _ = pad_rec
                    if any(math.hypot(px - ex, py - ey) <= pr + 0.02 for ex, ey in ((ax, ay), (bx, by))):
                        continue
                    if _pt_seg_dist(px, py, ax, ay, bx, by) < pr + hw + 0.02:
                        return False
                else:
                    _, _, poly, _, _, _, (px0, py0, px1, py1) = pad_rec
                    if sx1 + hw + 0.02 < px0 or px1 + hw + 0.02 < sx0 or sy1 + hw + 0.02 < py0 or py1 + hw + 0.02 < sy0:
                        continue
                    if any(poly.distance(Point(ex, ey)) <= 0.02 for ex, ey in ((ax, ay), (bx, by))):
                        continue
                    if seg_ls is None:
                        seg_ls = LineString([(ax, ay), (bx, by)])
                    if seg_ls.distance(poly) < hw + 0.02:
                        return False
                continue

            if kind == "circle":
                _, _, px, py, pr, p_clr, _ = pad_rec
                req = pr + hw + max(my_clr, p_clr) + SMOOTH_SAFETY_MM
                if sx1 + req < px or px + req < sx0 or sy1 + req < py or py + req < sy0:
                    continue
                if _pt_seg_dist(px, py, ax, ay, bx, by) < req:
                    return False
            else:
                _, _, poly, _, _, p_clr, (px0, py0, px1, py1) = pad_rec
                req = hw + max(my_clr, p_clr) + SMOOTH_SAFETY_MM
                if sx1 + req < px0 or px1 + req < sx0 or sy1 + req < py0 or py1 + req < sy0:
                    continue
                if seg_ls is None:
                    seg_ls = LineString([(ax, ay), (bx, by)])
                if seg_ls.distance(poly) < req:
                    return False

        # (f) Other-net non-GND copper pours on this layer (e.g. priority-1 F.Cu power/SW pours).
        for z_nc, z_poly, (zx0, zy0, zx1, zy1), z_clr in other_pour_polys[lyr]:
            if z_nc == nc:
                continue
            req = hw + max(my_clr, z_clr) + SMOOTH_SAFETY_MM
            if sx1 + req < zx0 or zx1 + req < sx0 or sy1 + req < zy0 or zy1 + req < sy0:
                continue
            if seg_ls is None:
                seg_ls = LineString([(ax, ay), (bx, by)])
            if seg_ls.distance(z_poly) < req:
                return False

        # (g) All other tracks on this layer (other-net clearance + same-net self-crossing / non-local self-overlap).
        cdx, cdy = bx - ax, by - ay
        clen = math.hypot(cdx, cdy)
        for tid, (t_nc, tax, tay, tbx, tby, tw, t_clr) in live_tracks[lyr].items():
            if tid in ignore_tids:
                continue
            tx0, ty0, tx1, ty1 = min(tax, tbx), min(tay, tby), max(tax, tbx), max(tay, tby)
            if t_nc != nc:
                req = hw + tw / 2.0 + max(my_clr, t_clr) + SMOOTH_SAFETY_MM
                if sx1 + req < tx0 or tx1 + req < sx0 or sy1 + req < ty0 or ty1 + req < sy0:
                    continue
                if _seg_seg_dist(ax, ay, bx, by, tax, tay, tbx, tby) < req:
                    return False
            else:
                # Same-net track: reject self-crossings, acute fold-backs (< 45 deg), and non-local copper overlaps.
                req_self = hw + tw / 2.0 + 0.04
                if sx1 + req_self < tx0 or tx1 + req_self < sx0 or sy1 + req_self < ty0 or ty1 + req_self < sy0:
                    continue
                shared = None
                for c_end, c_other in (((ax, ay), (bx, by)), ((bx, by), (ax, ay))):
                    if not any(math.hypot(c_end[0] - se[0], c_end[1] - se[1]) <= 0.02 for se in shared_end_pts_mm):
                        continue
                    for t_end, t_other in (((tax, tay), (tbx, tby)), ((tbx, tby), (tax, tay))):
                        if math.hypot(c_end[0] - t_end[0], c_end[1] - t_end[1]) <= 0.02:
                            shared = (c_end, c_other, t_other)
                            break
                    if shared is not None:
                        break
                if shared is not None:
                    v0, v1, v2 = shared
                    ux, uy = v1[0] - v0[0], v1[1] - v0[1]
                    vx, vy = v2[0] - v0[0], v2[1] - v0[1]
                    ul, vl = math.hypot(ux, uy), math.hypot(vx, vy)
                    if ul > 1e-9 and vl > 1e-9:
                        cos_a = (ux * vx + uy * vy) / (ul * vl)
                        # Reject acute fold-back (< 45 deg, cos > cos(45)=0.707) or collinear reversal.
                        if cos_a > 0.72:
                            return False
                        # Check non-shared endpoints don't cross or graze the other segment.
                        if _pt_seg_dist(v1[0], v1[1], v0[0], v0[1], v2[0], v2[1]) < req_self and ul > req_self:
                            return False
                        if _pt_seg_dist(v2[0], v2[1], v0[0], v0[1], v1[0], v1[1]) < req_self and vl > req_self:
                            return False
                else:
                    if _seg_seg_dist(ax, ay, bx, by, tax, tay, tbx, tby) < req_self:
                        return False
        return clen > 1e-6

    def power_width_ok(net_name, ax_nm, ay_nm, bx_nm, by_nm, w_nm):
        """On power nets, segments extending outside fine_pitch_power_pads courtyards must stay >= power_min_nm."""
        if net_name not in power_nets or w_nm >= power_min_nm:
            return w_nm
        ls = LineString([(pcbnew.ToMM(ax_nm), pcbnew.ToMM(ay_nm)), (pcbnew.ToMM(bx_nm), pcbnew.ToMM(by_nm))])
        if any(p.contains(ls) for p in fine_pitch_polys):
            return w_nm
        return power_min_nm

    # 3. Extract maximal degree-2 chains per (net, layer) with 0.015 mm endpoint snapping and greedily smooth them.
    modified_nl = {}
    total_removed = 0
    total_saved_nm = 0.0

    for (nc, lyr), tid_tracks in sorted(orig_unlocked_by_nl.items()):
        if len(tid_tracks) < 2:
            continue
        net_obj = tid_tracks[0][1].GetNet()
        net_name = net_obj.GetNetname()
        if not net_name or net_name in pour_nets:
            continue
        length_matched = _is_length_matched_net(net_name, frame)

        # Snap endpoints within SMOOTH_SNAP_NM (0.015 mm) without shifting pad/via anchor coordinates.
        net_pads = pads_by_nl.get((nc, lyr), ())
        net_vias = vias_by_net.get(nc, ())
        net_locked = locked_by_nl.get((nc, lyr), ())

        verts = []   # list of [x_nm, y_nm, is_anchor]

        def get_vid(px, py, _verts=verts, _pads=net_pads, _vias=net_vias):
            for idx, (vx, vy, _) in enumerate(_verts):
                if math.hypot(px - vx, py - vy) <= SMOOTH_SNAP_NM:
                    return idx
            v2 = pcbnew.VECTOR2I(px, py)
            pinned_anchor = (
                any(p.HitTest(v2) or math.hypot(px - p.GetPosition().x, py - p.GetPosition().y) <= SMOOTH_SNAP_NM
                    for p in _pads)
                or any(math.hypot(px - vx, py - vy) <= vr + SMOOTH_SNAP_NM for vx, vy, vr in _vias)
            )
            _verts.append((px, py, pinned_anchor))
            return len(_verts) - 1

        edges = []   # [(tid, u, v, w_nm)]
        adj = {}
        for tid, trk in tid_tracks:
            s, e = trk.GetStart(), trk.GetEnd()
            u = get_vid(s.x, s.y)
            v = get_vid(e.x, e.y)
            if u == v:
                continue
            eid = len(edges)
            edges.append((tid, u, v, trk.GetWidth()))
            adj.setdefault(u, []).append(eid)
            adj.setdefault(v, []).append(eid)

        # Pin degree != 2 junctions, pad/via anchors, and vertices touching locked tracks.
        pinned = set()
        for vid, (vx, vy, is_anc) in enumerate(verts):
            if is_anc or len(adj.get(vid, ())) != 2:
                pinned.add(vid)
                continue
            for lt in net_locked:
                ls, le = lt.GetStart(), lt.GetEnd()
                if _pt_seg_dist(vx, vy, ls.x, ls.y, le.x, le.y) <= SMOOTH_SNAP_NM:
                    pinned.add(vid)
                    break

        # Walk maximal chains between pinned endpoints (never splitting on width changes!).
        used_e = set()
        chains = []   # list of (pts_xy, widths_nm, tids)

        def walk_chain(start_v, start_eid, _edges=edges, _adj=adj, _verts=verts, _pinned=pinned, _used=used_e):
            pts = [_verts[start_v][:2]]
            ws = []
            tids = []
            cur_v = start_v
            cur_e = start_eid
            while True:
                _used.add(cur_e)
                tid, eu, ev, w = _edges[cur_e]
                nxt_v = ev if eu == cur_v else eu
                pts.append(_verts[nxt_v][:2])
                ws.append(w)
                tids.append(tid)
                if nxt_v in _pinned:
                    break
                nxt_edges = [e for e in _adj[nxt_v] if e not in _used]
                if not nxt_edges:
                    break
                cur_v = nxt_v
                cur_e = nxt_edges[0]
            return pts, ws, tids

        for pv in sorted(pinned):
            for eid in adj.get(pv, ()):
                if eid not in used_e:
                    chains.append(walk_chain(pv, eid))
        # Also handle any closed unpinned loop if present.
        for eid in range(len(edges)):
            if eid not in used_e:
                chains.append(walk_chain(edges[eid][1], eid))

        net_changed = False
        new_chains = []
        for pts, ws, tids in chains:
            if len(pts) < 3:
                new_chains.append((pts, ws))
                continue
            chain_min_w = min(ws)
            cur_pts = list(pts)
            cur_ws = list(ws)
            cur_tids = list(tids)

            for _pass in range(2):
                pass_changed = False
                i = 0
                while i < len(cur_pts) - 2:
                    max_j = min(len(cur_pts) - 1, i + SMOOTH_MAX_WINDOW)
                    accepted = False
                    for j in range(max_j, i + 1, -1):
                        a, b = cur_pts[i], cur_pts[j]
                        orig_len = sum(
                            math.hypot(cur_pts[k + 1][0] - cur_pts[k][0], cur_pts[k + 1][1] - cur_pts[k][1])
                            for k in range(i, j)
                        )
                        orig_verts = j - i + 1
                        sub_tids = set(cur_tids[i:j])
                        # Use min(chain_widths) across degree-2 neck-downs (upgraded to power_min_nm outside fine-pitch pads).
                        raw_w = min(chain_min_w, min(cur_ws[i:j]))
                        shared_mm = (
                            (pcbnew.ToMM(a[0]), pcbnew.ToMM(a[1])),
                            (pcbnew.ToMM(b[0]), pcbnew.ToMM(b[1])),
                        )
                        for cand in _octilinear_candidates(a, b):
                            cand_len = sum(
                                math.hypot(cand[k + 1][0] - cand[k][0], cand[k + 1][1] - cand[k][1])
                                for k in range(len(cand) - 1)
                            )
                            if cand_len > orig_len + 1_000:
                                continue
                            if not (cand_len < orig_len - 5_000 or len(cand) < orig_verts):
                                continue
                            if length_matched and (orig_len - cand_len) > SMOOTH_DIFF_MAX_DELTA_NM:
                                continue
                            cand_ws = [power_width_ok(net_name, cand[k][0], cand[k][1], cand[k + 1][0], cand[k + 1][1], raw_w)
                                       for k in range(len(cand) - 1)]
                            ok = True
                            for k in range(len(cand) - 1):
                                end_shared = []
                                if k == 0:
                                    end_shared.append(shared_mm[0])
                                if k == len(cand) - 2:
                                    end_shared.append(shared_mm[1])
                                if not segment_clear(lyr, nc, cand[k][0], cand[k][1], cand[k + 1][0], cand[k + 1][1],
                                                     cand_ws[k], sub_tids, end_shared):
                                    ok = False
                                    break
                            if not ok:
                                continue
                            # Commit shortcut to live obstacle table immediately!
                            for old_tid in sub_tids:
                                live_tracks[lyr].pop(old_tid, None)
                            new_tids = []
                            for k in range(len(cand) - 1):
                                next_tid += 1
                                live_tracks[lyr][next_tid] = (
                                    nc,
                                    pcbnew.ToMM(cand[k][0]), pcbnew.ToMM(cand[k][1]),
                                    pcbnew.ToMM(cand[k + 1][0]), pcbnew.ToMM(cand[k + 1][1]),
                                    pcbnew.ToMM(cand_ws[k]), net_clr_by_code.get(nc, base_clr),
                                )
                                new_tids.append(next_tid)
                            total_removed += (j - i) - (len(cand) - 1)
                            total_saved_nm += max(0.0, orig_len - cand_len)
                            cur_pts[i:j + 1] = cand
                            cur_ws[i:j] = cand_ws
                            cur_tids[i:j] = new_tids
                            accepted = True
                            pass_changed = True
                            net_changed = True
                            break
                        if accepted:
                            break
                    i += 1
                if not pass_changed:
                    break
            new_chains.append((cur_pts, cur_ws))

        if net_changed:
            modified_nl[(nc, lyr)] = (net_obj, [trk for _, trk in tid_tracks], new_chains)

    if not modified_nl:
        return 0, 0.0

    # 4. Apply replacements to board with full snapshot rollback guard on connectivity.
    snapshot = []
    added = []
    for (_nc, lyr), (net_obj, old_tracks, new_chains) in modified_nl.items():
        for t in old_tracks:
            snapshot.append((net_obj, lyr, t.GetStart(), t.GetEnd(), t.GetWidth()))
            board.RemoveNative(t)
        for pts, ws in new_chains:
            for k in range(len(pts) - 1):
                if pts[k] == pts[k + 1]:
                    continue
                seg = pcbnew.PCB_TRACK(board)
                seg.SetStart(pcbnew.VECTOR2I(pts[k][0], pts[k][1]))
                seg.SetEnd(pcbnew.VECTOR2I(pts[k + 1][0], pts[k + 1][1]))
                seg.SetWidth(ws[k])
                seg.SetLayer(lyr)
                board.Add(seg)
                seg.SetNet(net_obj)
                added.append(seg)

    if unconnected(board) > baseline_unconn:
        for seg in added:
            board.RemoveNative(seg)
        for net_obj, lyr, s, e, w in snapshot:
            seg = pcbnew.PCB_TRACK(board)
            seg.SetStart(s)
            seg.SetEnd(e)
            seg.SetWidth(w)
            seg.SetLayer(lyr)
            board.Add(seg)
            seg.SetNet(net_obj)
        fill_zones(board)
        return 0, 0.0

    return total_removed, total_saved_nm / 1e6


def import_ses(board_name):
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    ses = build_dir(board_name) / f"{board_name}.ses"
    if not pcbnew.ImportSpecctraSES(board, str(ses)):
        sys.exit("SES import failed")
    fill_zones(board)
    _split_n, pruned_n = prune_track_loops(board)
    smooth_rm, smooth_mm = smooth_tracks(board, plan)
    save(board, plan, board_name)
    widened = widen_narrow_tracks(board_name)
    notes = (
        ([f"pruned {pruned_n} loop/stub/pour-redundant segments"] if pruned_n else [])
        + ([f"smoothed -{smooth_rm} segs (-{smooth_mm:.1f} mm)"] if (smooth_rm or smooth_mm >= 0.05) else [])
        + ([f"widened {widened} necked power tracks"] if widened else [])
    )
    print(f"{board_name}: imported {ses.relative_to(REPO)}" + (f" ({', '.join(notes)})" if notes else ""))


def widen_narrow_tracks(board_name):
    """fastroute necks a power track down where it squeezes past other copper (probe HEAT_SW on In2 beside the board
    edge: 0.18 mm, the same on every attempt), which only fine-pitch courtyards allow (track_width errors). Widen each
    to its rule's minimum and keep the widening only where DRC finds no new error at it. Not moved to the router input:
    `--no-neckdown-classes` on the power classes left 1 probe and 12 crown connections unrouted (2026-11)."""
    import re
    path = stored_board_path(board_name)
    report = build_dir(board_name) / "track_width_drc.json"

    def errors():
        subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--output", str(report), str(path)],
                       capture_output=True, check=True)
        return [v for v in json.loads(report.read_text())["violations"] if v["severity"] == "error"]

    before = errors()
    narrow = {i["uuid"]: pcbnew.FromMM(float(re.search(r"min width ([\d.]+) mm", v["description"]).group(1)))
              for v in before if v["type"] == "track_width" for i in v["items"]}
    if not narrow:
        return 0
    board = pcbnew.LoadBoard(str(path))
    tracks = {t.m_Uuid.AsString(): t for t in board.GetTracks() if t.m_Uuid.AsString() in narrow}
    old = {u: t.GetWidth() for u, t in tracks.items()}
    for u, t in tracks.items():
        t.SetWidth(narrow[u])
    board.Save(str(path))
    known = {drc_key(v) for v in before}
    clashing = {i["uuid"] for v in errors() if drc_key(v) not in known for i in v["items"]} & tracks.keys()
    if clashing:
        board = pcbnew.LoadBoard(str(path))
        for t in board.GetTracks():
            if t.m_Uuid.AsString() in clashing:
                t.SetWidth(old[t.m_Uuid.AsString()])
        board.Save(str(path))
    return len(tracks) - len(clashing)






LIBRARY_ONLY_DRC = {"lib_footprint_issues", "lib_footprint_mismatch"}


def drc_violations(pcb_path, report):
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--output", str(report), str(pcb_path)],
                   capture_output=True, check=True)
    return [v for v in json.loads(report.read_text())["violations"] if v["type"] not in LIBRARY_ONLY_DRC]


def drc_key(violation):
    return violation["type"], tuple(sorted(item["description"] for item in violation.get("items", [])))


def unrouted_baseline(board_name):
    """The stored board without its unlocked tracks / vias (pours filled), next to its rules: DRC violations already
    here come from footprints or placement, and its unconnected count is what the router has to connect."""
    stored = stored_board_path(board_name)
    board = pcbnew.LoadBoard(str(stored))
    for track in list(board.GetTracks()):
        if not track.IsLocked():
            board.RemoveNative(track)
    connections = unconnected(board)
    path = build_dir(board_name) / f"{board_name}.unrouted.kicad_pcb"
    board.Save(str(path))
    for suffix in (".kicad_dru", ".kicad_pro"):
        path.with_suffix(suffix).write_text(stored.with_suffix(suffix).read_text())
    return connections, drc_violations(path, build_dir(board_name) / "unrouted_drc.json")


# jlcpcb_minimum in DRU_4LAYER: the router never necks a track below what the fab makes
MIN_TRACK_UM = 120


def route(board_name, time_limit=300, attempts=3):
    """Route with fastroute (FANOUT=false); kicad-cli DRC of the imported board (minus the unrouted baseline's
    violations) and the board's connectivity are the pass / fail. Attempt 1 runs deterministically (single-threaded, no
    per-stage wall-clock limits); any retry falls back to parallel multi-start."""
    connections, baseline = unrouted_baseline(board_name)
    known = {drc_key(v) for v in baseline}
    export_dsn(board_name)
    bdir = build_dir(board_name)
    dsn, ses, report = bdir / f"{board_name}.dsn", bdir / f"{board_name}.ses", bdir / "fastroute_report.json"
    for attempt in range(1, attempts + 1):
        ses.unlink(missing_ok=True)
        det_flags = ["--no-time-limits", "--router.autorouter.max_threads=1"] if attempt == 1 else []
        run = subprocess.run(["fastroute", "-de", str(dsn), "-do", str(ses), f"--max-time={time_limit}",
                              "--router.fanout.enabled=false", f"--router.min_trace_width_um={MIN_TRACK_UM}",
                              *det_flags, f"--report={report}", "--diagnose"],
                             capture_output=True, text=True, check=False)
        if run.returncode or not ses.exists():
            sys.exit(f"{board_name}: fastroute failed (exit {run.returncode}):\n{run.stdout[-2000:]}{run.stderr[-2000:]}")
        stats = json.loads(report.read_text())["stats"]
        print(f"{board_name}: attempt {attempt}: fastroute routed {stats['connections'] - stats['unrouted']}/"
              f"{stats['connections']} connections, {stats['vias']} vias")
        import_ses(board_name)
        board = pcbnew.LoadBoard(str(stored_board_path(board_name)))
        tracks = sum(t.Type() != pcbnew.PCB_VIA_T for t in board.GetTracks())
        vias = sum(t.Type() == pcbnew.PCB_VIA_T for t in board.GetTracks())
        left = unconnected(board)
        real = [v for v in drc_violations(stored_board_path(board_name), bdir / "routed_drc.json")
                if drc_key(v) not in known]
        errors = [v for v in real if v["severity"] == "error"]
        kinds = sorted({(v["severity"], v["type"]) for v in real})
        by_kind = ", ".join(f"{sum((v['severity'], v['type']) == k for v in real)} {k[1]} ({k[0]})" for k in kinds)
        print(f"{board_name}: routed {connections - left}/{connections} connections, {tracks} tracks, {vias} vias; "
              f"{len(real)} DRC violations beyond the unrouted baseline ({len(baseline)})" + (f": {by_kind}" if real else ""))
        if not errors and not left:
            return
    sys.exit(f"{board_name}: routing fails DRC after {attempts} attempts: {len(errors)} errors, {left} connections unrouted")


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


def prune_loops(board_name):
    plan = load_plan(board_name)
    board = current_board(board_name, plan)
    fill_zones(board)
    split_n, pruned_n = prune_track_loops(board)
    save(board, plan, board_name)
    print(f"{board_name}: split {split_n}, pruned {pruned_n} redundant/loop/stub track segments")


def smooth_board(board_name):
    """Run the DRC-safe octilinear track smoother on an already-routed board and assert 0 DRC errors / 0 unrouted."""
    plan = load_plan(board_name)
    path = stored_board_path(board_name)
    backup = path.read_text()
    board = current_board(board_name, plan)
    fill_zones(board)
    removed, saved_mm = smooth_tracks(board, plan)
    save(board, plan, board_name)
    left = unconnected(pcbnew.LoadBoard(str(path)))
    report = build_dir(board_name) / "smoothed_drc.json"
    errors = [v for v in drc_violations(path, report) if v["severity"] == "error"]
    if errors or left:
        path.write_text(backup)
        sys.exit(f"{board_name}: smoother rolled back ({len(errors)} DRC errors, {left} unrouted): "
                 f"{[v['type'] for v in errors[:5]]}")
    print(f"{board_name}: smoothed -{removed} segments (-{saved_mm:.2f} mm), 0 DRC errors, 0 unrouted")


if __name__ == "__main__":
    name, command, *args = sys.argv[1:]
    if command == "sync":
        sync(name)
    elif command == "place":
        place(name, args)
    elif command == "autoplace":
        import place_cpsat
        place_cpsat.autoplace_cli(name, args)
    elif command == "tidy-silk":
        tidy_silk(name)
    elif command == "components":
        components(name)
    elif command == "unroute":
        unroute(name)
    elif command == "route":
        route(name, *(int(a) for a in args))
    elif command == "prune-loops":
        prune_loops(name)
    elif command == "smooth-tracks":
        smooth_board(name)
    elif command == "export-dsn":
        export_dsn(name)
    elif command == "import-ses":
        import_ses(name)
    else:
        sys.exit(f"unknown command: {command}")
