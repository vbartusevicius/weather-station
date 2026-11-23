"""Pure copper-extracted SPICE netlist builder and `libngspice` (`ctypes`) runner (`eda` container).

Strictly independent of the schematic/circuit generator (`hardware/circuits/`):
  - Loads only released routed `.kicad_pcb` boards via `pcbnew`, exported `<board>_bom.csv` files, and physical board
    mounting coordinates from `hardware/product/spec.yaml`.
  - Asserts `GetConnectivity().GetUnconnectedCount(False) == 0` on every board.
  - Reconstructs electrical nodes from scratch via Union-Find over physical copper tracks, vias, filled zone islands
    and pads, including copper trace resistance `R` and microstrip/stripline capacitance `C` parasitics.
  - Stitches board-to-board headers (`crown J10 <-> dome J1`, `probe J2 <-> reflector J1`) strictly by 3D `(X, Z)`
    mating pad coordinates, and cable harnesses by physical wire gauge R/C from `cable_length_mm`.
  - Runs `.op`, `.ac`, `.noise` (`setplot noise1`), and `.tran` in-process via `libngspice.so.0` (`ctypes`) using
    smooth `C^inf` `tanh()` macromodels (bounded arguments `<= 10`, all sub-units of multi-unit ICs instantiated,
    `.options rshunt=1e11`).
"""

from __future__ import annotations

import csv
import ctypes
import math
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import pcbnew
import yaml

REPO = Path(__file__).resolve().parents[2]
RELEASE_ROOT = REPO / "hardware/releases/v1"
SPEC_PATH = REPO / "hardware/product/spec.yaml"

# Copper resistivity & 4-layer JLCPCB stackup constants
RHO_CU_OHM_MM = 1.724e-5          # Ohm * mm at 20 C
OUTER_CU_MM = 0.035               # 1 oz outer layers (F.Cu, B.Cu)
INNER_CU_MM = 0.0175              # 0.5 oz inner layers (In1.Cu, In2.Cu)
TRACE_CAP_PF_PER_MM = 0.085       # ~85 fF/mm microstrip over 0.21 mm prepreg (er ~ 4.4)
VIA_RES_OHM = 0.0012              # ~1.2 mOhm plated through-via barrel
B2B_CONTACT_OHM = 0.015           # 15 mOhm gold-plated 2.54 mm header contact
HARNESS_OHM_PER_MM = 0.00034      # 30 AWG stranded copper (~340 mOhm/m)
HARNESS_PF_PER_MM = 0.060         # ~60 pF/m adjacent conductor capacitance

SI_FACTORS = {
    "p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6,
    "m": 1e-3, "": 1.0, "k": 1e3, "M": 1e6, "G": 1e9,
}


def parse_bom_value(text: str) -> float:
    """Parse BOM value strings like `6k49`, `4M7`, `0R1`, `100n 50V`, `22u 25V`, `8.2uH`, `10k NTC`."""
    token = text.strip().split()[0]
    m = re.fullmatch(r"(\d+(?:\.\d+)?)([pnuµmkMGR]?)(\d*)(?:[ΩFHV]|Ohm|uH|nH|mH)?", token)
    if not m:
        raise ValueError(f"cannot parse BOM passive value: {text!r}")
    whole, pfx, frac = m.groups()
    if pfx == "R":
        pfx = ""
    num = float(f"{whole}.{frac}" if frac else whole)
    unit_tail = token[len(whole) + len(m.group(2)) + len(frac):]
    if unit_tail.startswith("uH") and pfx == "":
        return num * 1e-6
    if unit_tail.startswith("mH") and pfx == "":
        return num * 1e-3
    if unit_tail.startswith("nH") and pfx == "":
        return num * 1e-9
    return num * SI_FACTORS[pfx]


# =============================================================================
# In-process `libngspice.so.0` ctypes interface
# =============================================================================

class _NgComplex(ctypes.Structure):
    _fields_ = [("cx_real", ctypes.c_double), ("cx_imag", ctypes.c_double)]


class _VectorInfo(ctypes.Structure):
    _fields_ = [
        ("v_name", ctypes.c_char_p),
        ("v_type", ctypes.c_int),
        ("v_flags", ctypes.c_short),
        ("v_realdata", ctypes.POINTER(ctypes.c_double)),
        ("v_compdata", ctypes.POINTER(_NgComplex)),
        ("v_length", ctypes.c_int),
    ]


_SENDCHAR = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p)
_SENDSTAT = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p)
_CTRLEXIT = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_bool, ctypes.c_bool, ctypes.c_int, ctypes.c_void_p)


class LibNgSpice:
    """Singleton wrapper around `libngspice.so.0`."""

    _instance: LibNgSpice | None = None

    def __init__(self) -> None:
        self.lib = ctypes.CDLL("libngspice.so.0")
        self.logs: list[str] = []

        @_SENDCHAR
        def _on_char(msg: bytes, _ident: int, _user: int) -> int:
            if msg:
                self.logs.append(msg.decode("utf-8", "replace"))
            return 0

        @_SENDSTAT
        def _on_stat(_msg: bytes, _ident: int, _user: int) -> int:
            return 0

        @_CTRLEXIT
        def _on_exit(_status: int, _imm: bool, _quit: bool, _ident: int, _user: int) -> int:
            return 0

        # Keep callback references alive for the lifetime of the process.
        self._cbs = (_on_char, _on_stat, _on_exit)
        self.lib.ngSpice_Init.argtypes = [
            _SENDCHAR, _SENDSTAT, _CTRLEXIT,
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
        ]
        self.lib.ngSpice_Init.restype = ctypes.c_int
        self.lib.ngSpice_Circ.argtypes = [ctypes.POINTER(ctypes.c_char_p)]
        self.lib.ngSpice_Circ.restype = ctypes.c_int
        self.lib.ngSpice_Command.argtypes = [ctypes.c_char_p]
        self.lib.ngSpice_Command.restype = ctypes.c_int
        self.lib.ngGet_Vec_Info.argtypes = [ctypes.c_char_p]
        self.lib.ngGet_Vec_Info.restype = ctypes.POINTER(_VectorInfo)
        self.lib.ngSpice_Init(_on_char, _on_stat, _on_exit, None, None, None, None)
        self._has_circ = False

    @classmethod
    def get(cls) -> LibNgSpice:
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance

    def cmd(self, command: str) -> None:
        rc = self.lib.ngSpice_Command(command.encode("utf-8"))
        if rc != 0:
            tail = "\n".join(self.logs[-40:])
            raise RuntimeError(f"ngSpice_Command({command!r}) failed (rc={rc}):\n{tail}")

    def load_netlist(self, netlist_text: str) -> None:
        if self._has_circ:
            self.lib.ngSpice_Command(b"destroy all")
            self.lib.ngSpice_Command(b"remcirc")
            self._has_circ = False
        self.logs.clear()
        raw_lines = [ln.strip() for ln in netlist_text.splitlines() if ln.strip()]
        if not raw_lines or raw_lines[-1].lower() != ".end":
            raw_lines.append(".end")
        encoded = [ln.encode("utf-8") for ln in raw_lines] + [None]
        arr = (ctypes.c_char_p * len(encoded))(*encoded)
        rc = self.lib.ngSpice_Circ(arr)
        err_lines = [ln for ln in self.logs if ln.startswith("stderr Error:") and "no circuits loaded" not in ln]
        if rc != 0 or err_lines:
            tail = "\n".join(self.logs[-50:])
            raise RuntimeError(f"ngSpice_Circ failed (rc={rc}):\n{tail}")
        self._has_circ = True

    def vec_real(self, name: str) -> list[float]:
        ptr = self.lib.ngGet_Vec_Info(name.encode("utf-8"))
        if not ptr:
            tail = "\n".join(self.logs[-30:])
            raise KeyError(f"ngspice vector {name!r} not found:\n{tail}")
        vi = ptr.contents
        n = vi.v_length
        if bool(vi.v_realdata):
            return [float(vi.v_realdata[i]) for i in range(n)]
        if bool(vi.v_compdata):
            return [float(vi.v_compdata[i].cx_real) for i in range(n)]
        return []

    def vec_complex(self, name: str) -> list[complex]:
        ptr = self.lib.ngGet_Vec_Info(name.encode("utf-8"))
        if not ptr:
            tail = "\n".join(self.logs[-30:])
            raise KeyError(f"ngspice complex vector {name!r} not found:\n{tail}")
        vi = ptr.contents
        n = vi.v_length
        if bool(vi.v_compdata):
            return [complex(vi.v_compdata[i].cx_real, vi.v_compdata[i].cx_imag) for i in range(n)]
        if bool(vi.v_realdata):
            return [complex(vi.v_realdata[i], 0.0) for i in range(n)]
        return []

    def _check_abort(self, context: str) -> None:
        aborts = [ln for ln in self.logs if "simulation(s) aborted" in ln or "doAnalyses:" in ln]
        if aborts:
            tail = "\n".join(self.logs[-45:])
            raise RuntimeError(f"ngspice {context} aborted:\n{tail}")

    def run_op(self, netlist_text: str, nodes: list[str]) -> dict[str, float]:
        self.load_netlist(netlist_text)
        self.cmd("op")
        self._check_abort("op")
        out = {}
        for n in nodes:
            if n == "0":
                out[n] = 0.0
            else:
                expr = n if n.startswith(("i(", "v(")) else f"v({n})"
                out[n] = self.vec_real(expr)[0]
        return out

    def run_ac(self, netlist_text: str, nodes: list[str], f_start: float, f_stop: float,
               pts_per_dec: int = 40) -> tuple[list[float], dict[str, list[complex]]]:
        self.load_netlist(netlist_text)
        self.cmd(f"ac dec {pts_per_dec} {f_start} {f_stop}")
        self._check_abort("ac")
        freqs = self.vec_real("frequency")
        return freqs, {n: self.vec_complex(f"v({n})") for n in nodes}

    def run_noise(self, netlist_text: str, out_expr: str, src_name: str,
                  f_start: float, f_stop: float, pts_per_dec: int = 20) -> dict:
        self.load_netlist(netlist_text)
        self.cmd(f"noise {out_expr} {src_name.lower()} dec {pts_per_dec} {f_start} {f_stop}")
        self._check_abort("noise")
        self.cmd("setplot noise1")
        freqs = self.vec_real("frequency")
        onoise = self.vec_real("onoise_spectrum")
        inoise = self.vec_real("inoise_spectrum")
        self.cmd("setplot noise2")
        onoise_tot = self.vec_real("onoise_total")[0]
        inoise_tot = self.vec_real("inoise_total")[0]
        return {
            "freqs": freqs,
            "onoise_spectrum": onoise,
            "inoise_spectrum": inoise,
            "onoise_total": onoise_tot,
            "inoise_total": inoise_tot,
        }

    def run_tran(self, netlist_text: str, nodes: list[str], t_step: str, t_stop: str,
                 *, uic: bool = False) -> tuple[list[float], dict[str, list[float]]]:
        self.load_netlist(netlist_text)
        self.cmd(f"tran {t_step} {t_stop}" + (" uic" if uic else ""))
        self._check_abort("tran")
        times = self.vec_real("time")
        out = {}
        for n in nodes:
            expr = n if n.startswith(("i(", "v(")) else f"v({n})"
            out[n] = self.vec_real(expr)
        return times, out


# =============================================================================
# Pure Copper Union-Find Extractor + Trace R/C Parasitics + BOM Reader
# =============================================================================

@dataclass
class ExtractedBoard:
    name: str
    bom: dict[str, dict[str, str]]                # ref -> BOM CSV row
    pad_node: dict[tuple[str, str], str]          # (ref, pad_num) -> copper SPICE node name
    pad_xy_mm: dict[tuple[str, str], tuple[float, float]]
    net_trace_r_ohm: dict[str, float]             # copper node -> total trace + via R
    net_trace_c_f: dict[str, float]               # copper node -> total trace-to-plane C
    gnd_node: str                                 # copper node holding GND


def _sanitize_node(raw: str) -> str:
    s = raw.replace("+", "p").replace("-", "m").replace("/", "_").replace(".", "_")
    return re.sub(r"[^A-Za-z0-9_]", "_", s).lower()


def extract_board_copper(board_name: str) -> ExtractedBoard:
    """Reconstruct electrical nodes strictly via Union-Find over physical copper tracks, vias, filled zones and pads."""
    assert "skidl" not in sys.modules, "copper_spice must never import skidl"
    pcb_path = RELEASE_ROOT / board_name / f"{board_name}.kicad_pcb"
    bom_path = RELEASE_ROOT / board_name / f"{board_name}_bom.csv"

    pcb = pcbnew.LoadBoard(str(pcb_path))
    pcbnew.ZONE_FILLER(pcb).Fill(pcb.Zones())
    pcb.BuildConnectivity()
    unconn = pcb.GetConnectivity().GetUnconnectedCount(False)
    assert unconn == 0, f"{board_name}: GetUnconnectedCount(False) == {unconn} != 0"

    bom: dict[str, dict[str, str]] = {}
    with open(bom_path, newline="") as f:
        for row in csv.DictReader(f):
            for ref in [r.strip() for r in row["Designators"].split(",") if r.strip()]:
                bom[ref] = row

    cu_layers = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
    tol_nm = pcbnew.FromMM(0.005)

    parent: dict[tuple, tuple] = {}

    def find(x: tuple) -> tuple:
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a: tuple, b: tuple) -> None:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    # 1. Collect pads, vias, tracks, and filled zone islands.
    pads: list[tuple[str, str, pcbnew.PAD]] = []
    pad_xy_mm: dict[tuple[str, str], tuple[float, float]] = {}
    for fp in pcb.Footprints():
        ref = fp.GetReference()
        for p in fp.Pads():
            num = p.GetNumber()
            if not num:
                continue
            key = (ref, num)
            pads.append((ref, num, p))
            pad_xy_mm[key] = (pcbnew.ToMM(p.GetPosition().x), -pcbnew.ToMM(p.GetPosition().y))
            find(("pad", ref, num))

    vias = [t for t in pcb.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]
    tracks = [t for t in pcb.GetTracks() if t.Type() in (pcbnew.PCB_TRACE_T, pcbnew.PCB_ARC_T)]

    islands_by_lyr: dict[int, list[tuple[tuple, pcbnew.ZONE, pcbnew.SHAPE_POLY_SET]]] = {lyr: [] for lyr in cu_layers}
    for z_idx, z in enumerate(pcb.Zones()):
        if z.GetIsRuleArea():
            continue
        for lyr in cu_layers:
            if not z.IsOnLayer(lyr):
                continue
            ps = z.GetFilledPolysList(lyr)
            for i_idx in range(ps.OutlineCount()):
                sub = pcbnew.SHAPE_POLY_SET()
                sub.AddOutline(ps.Outline(i_idx))
                for h_idx in range(ps.HoleCount(i_idx)):
                    sub.AddHole(ps.Hole(i_idx, h_idx), 0)
                z_key = ("zone", z_idx, lyr, i_idx)
                find(z_key)
                islands_by_lyr[lyr].append((z_key, z, sub))

    # 2. Union pads <-> filled zones & pads <-> vias & overlapping same-footprint pads.
    pads_by_lyr: dict[int, list[tuple[tuple, pcbnew.PAD]]] = {lyr: [] for lyr in cu_layers}
    for ref, num, p in pads:
        pk = ("pad", ref, num)
        pp = p.GetPosition()
        for lyr in cu_layers:
            if p.IsOnLayer(lyr):
                pads_by_lyr[lyr].append((pk, p))
                for zk, z_obj, poly in islands_by_lyr[lyr]:
                    if z_obj.HitTestFilledArea(lyr, pp) and poly.Collide(pp, 0):
                        union(pk, zk)

    # Same-number multi-pads on one footprint (e.g. mechanical shield or dual-pad pin) share one pin number.
    seen_pin_keys: dict[tuple[str, str], pcbnew.PAD] = {}
    for ref, num, p in pads:
        if (ref, num) in seen_pin_keys:
            # Already same key `("pad", ref, num)`, check physical contact if distinct pads share number
            pass
        seen_pin_keys[(ref, num)] = p

    vias_by_pos: list[tuple[tuple, pcbnew.PCB_VIA, pcbnew.VECTOR2I]] = []
    for v_idx, v in enumerate(vias):
        vk = ("via", v_idx)
        vp = v.GetPosition()
        vias_by_pos.append((vk, v, vp))
        for lyr in cu_layers:
            r_via = v.GetWidth(lyr) // 2 + tol_nm
            for pk, p in pads_by_lyr[lyr]:
                if p.HitTest(vp, tol_nm):
                    union(vk, pk)
            for zk, z_obj, poly in islands_by_lyr[lyr]:
                if z_obj.HitTestFilledArea(lyr, vp) and poly.Collide(vp, r_via):
                    union(vk, zk)

    # 3. Union tracks <-> pads, vias, filled zones, and other tracks on the same layer (spatial bucketed).
    tracks_by_lyr: dict[int, list[tuple[tuple, pcbnew.PCB_TRACK]]] = {lyr: [] for lyr in cu_layers}
    for t_idx, t in enumerate(tracks):
        tk = ("trk", t_idx)
        lyr = t.GetLayer()
        if lyr in tracks_by_lyr:
            tracks_by_lyr[lyr].append((tk, t))

    bucket_nm = pcbnew.FromMM(2.0)
    for lyr in cu_layers:
        lyr_tracks = tracks_by_lyr[lyr]
        grid: dict[tuple[int, int], list[tuple[tuple, int, int, int, int]]] = {}
        for tk, t in lyr_tracks:
            s, e = t.GetStart(), t.GetEnd()
            for end in (s, e):
                for pk, p in pads_by_lyr[lyr]:
                    if p.HitTest(end, tol_nm):
                        union(tk, pk)
                for vk, v, vp in vias_by_pos:
                    if math.hypot(end.x - vp.x, end.y - vp.y) <= v.GetWidth(lyr) / 2 + tol_nm:
                        union(tk, vk)
                for zk, z_obj, poly in islands_by_lyr[lyr]:
                    if z_obj.HitTestFilledArea(lyr, end) and poly.Collide(end, tol_nm):
                        union(tk, zk)

            bx0, bx1 = min(s.x, e.x) // bucket_nm, max(s.x, e.x) // bucket_nm
            by0, by1 = min(s.y, e.y) // bucket_nm, max(s.y, e.y) // bucket_nm
            entry = (tk, s.x, s.y, e.x, e.y)
            for gx in range(bx0 - 1, bx1 + 2):
                for gy in range(by0 - 1, by1 + 2):
                    for ok, ox0, oy0, ox1, oy1 in grid.get((gx, gy), ()):
                        if ok == tk:
                            continue
                        # Check endpoint-to-segment distance between `entry` and `(ox0, oy0, ox1, oy1)`.
                        for px, py, ax, ay, bx, by in (
                            (s.x, s.y, ox0, oy0, ox1, oy1),
                            (e.x, e.y, ox0, oy0, ox1, oy1),
                            (ox0, oy0, s.x, s.y, e.x, e.y),
                            (ox1, oy1, s.x, s.y, e.x, e.y),
                        ):
                            dx, dy = bx - ax, by - ay
                            l2 = dx * dx + dy * dy
                            u = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
                            if math.hypot(px - (ax + u * dx), py - (ay + u * dy)) <= tol_nm:
                                union(tk, ok)
                                break
            for gx in range(bx0, bx1 + 1):
                for gy in range(by0, by1 + 1):
                    grid.setdefault((gx, gy), []).append(entry)

    # 4. Name reconstructed copper roots and verify 1-to-1 physical copper isolation & continuity.
    root_pads: dict[tuple, list[tuple[str, str, str]]] = {}
    for ref, num, p in pads:
        rk = find(("pad", ref, num))
        root_pads.setdefault(rk, []).append((ref, num, p.GetNetname()))

    # Verify zero copper shorts between distinct named nets AND zero split islands within any multi-pad net!
    net_to_roots: dict[str, set[tuple]] = {}
    root_name: dict[tuple, str] = {}
    iso_idx = 0
    for rk, plist in root_pads.items():
        named = {n for _, _, n in plist if n and not n.startswith("unconnected-")}
        assert len(named) <= 1, f"{board_name}: physical copper short between nets {sorted(named)} on pads {plist}"
        if named:
            n_str = next(iter(named))
            net_to_roots.setdefault(n_str, set()).add(rk)
            root_name[rk] = f"{board_name}_{_sanitize_node(n_str)}"
        else:
            iso_idx += 1
            root_name[rk] = f"{board_name}_nc_{iso_idx}"

    for n_str, rset in net_to_roots.items():
        assert len(rset) == 1, f"{board_name}: net {n_str!r} is physically split into {len(rset)} disconnected copper islands"

    gnd_roots = net_to_roots.get("GND", set())
    assert len(gnd_roots) == 1, f"{board_name}: expected single GND copper net"
    gnd_node = root_name[next(iter(gnd_roots))]

    pad_node = {(ref, num): root_name[find(("pad", ref, num))] for ref, num, _ in pads}

    # 5. Compute physical copper trace resistance R and capacitance C per extracted copper root.
    trace_r: dict[str, float] = {}
    trace_c: dict[str, float] = {}
    for tk, t in (item for lyr in cu_layers for item in tracks_by_lyr[lyr]):
        rk = find(tk)
        if rk not in root_name:
            continue
        nd = root_name[rk]
        len_mm = pcbnew.ToMM(t.GetLength())
        w_mm = max(pcbnew.ToMM(t.GetWidth()), 0.12)
        thick = OUTER_CU_MM if t.GetLayer() in (pcbnew.F_Cu, pcbnew.B_Cu) else INNER_CU_MM
        trace_r[nd] = trace_r.get(nd, 0.0) + RHO_CU_OHM_MM * len_mm / (w_mm * thick)
        trace_c[nd] = trace_c.get(nd, 0.0) + (TRACE_CAP_PF_PER_MM * (w_mm / 0.20) * len_mm) * 1e-12

    for vk, _v, _vp in vias_by_pos:
        rk = find(vk)
        if rk in root_name:
            nd = root_name[rk]
            trace_r[nd] = trace_r.get(nd, 0.0) + VIA_RES_OHM

    return ExtractedBoard(
        name=board_name,
        bom=bom,
        pad_node=pad_node,
        pad_xy_mm=pad_xy_mm,
        net_trace_r_ohm=trace_r,
        net_trace_c_f=trace_c,
        gnd_node=gnd_node,
    )


def _board_3d_pad_xz(spec: dict, eb: ExtractedBoard, ref: str) -> dict[str, tuple[float, float]]:
    """Map connector pad coordinates `(bx, by)` into 3D station `(enc_x, enc_z)` from `spec.yaml`."""
    cfg = spec["boards"][eb.name]
    w, h = cfg["max_outline"]["width"], cfg["max_outline"]["height"]
    pins = {num: xy for (r, num), xy in eb.pad_xy_mm.items() if r == ref and num.isdigit()}
    assert pins, f"{eb.name}:{ref} has no numbered pads"
    # Connector body centerline in Z cancels alternating +-1.6 mm SMD gull-wing solder-tail offsets.
    out: dict[str, tuple[float, float]] = {}
    mean_by = sum(y for _, y in pins.values()) / len(pins)
    for num, (bx, _by) in pins.items():
        if cfg["plane"] == "horizontal-xz":
            ex = (bx - w / 2.0) + cfg.get("x_offset", 0.0)
            ez = (mean_by - h / 2.0) + cfg.get("z_offset", 0.0)
        else:
            ex = bx - w / 2.0
            ez = cfg["z"]
        out[num] = (ex, ez)
    return out


# =============================================================================
# Smooth C^inf (`tanh`) SPICE Subcircuit Library + Multi-Board Netlist Emitter
# =============================================================================

COMMON_MODELS_AND_SUBCKTS = """\
.options rshunt=1e11 gmin=1e-11 reltol=1e-4 abstol=1e-12 vntol=1e-7 itl1=500 itl2=250 itl4=100 method=trap
* Smooth C^inf helper functions: arguments to tanh() stay strictly bounded <= 10 with zero `? :` ternaries,
* zero `pow(neg, 2)` domain NaNs, and zero `min`/`max` kinks.
.func sq(x) {(x) * (x)}
.func smin(a, b) {0.5 * ((a) + (b) - sqrt(sq((a) - (b)) + 0.002))}
.func smax(a, b) {0.5 * ((a) + (b) + sqrt(sq((a) - (b)) + 0.002))}
.func bnd10(x) {10.0 * tanh((x) / 10.0)}
.func stanh(x) {tanh(bnd10(x))}
.func step01(x, w) {0.5 * (1.0 + stanh((x) / (w)))}

* Real semiconductor diode models
.model D_SMAJ36A D(Is=1e-13 Rs=0.35 N=1.5 Cjo=350p BV=42.0 IBV=1m)
.model D_SS16    D(Is=8e-6  Rs=0.055 N=1.06 Cjo=110p BV=60.0 IBV=1m)
.model D_SS14    D(Is=1.2e-5 Rs=0.045 N=1.05 Cjo=130p BV=40.0 IBV=1m)
.model D_B360A   D(Is=1.5e-5 Rs=0.035 N=1.05 Cjo=180p BV=60.0 IBV=1m)
.model D_BAV99S  D(Is=2.52e-9 Rs=0.568 N=1.752 Cjo=1.5p tt=6n BV=100 IBV=100u)
.model D_ESD     D(Is=1e-14 Rs=5.0 N=1.1 Cjo=3p BV=18.0 IBV=1m)
.model D_UVPD    D(Is=1e-16 Rs=20.0 N=1.2 Cjo=15p BV=20.0 IBV=10u)
.model M_AO3400A NMOS(LEVEL=1 VTO=1.05 KP=18.0 RD=0.020 RS=0.010 LAMBDA=0.02)

* SOT-23 BAV99: pin 1 = Anode D1, pin 2 = Cathode D2, pin 3 = Cathode D1 / Anode D2
.subckt MP_BAV99 p1 p2 p3
D1 p1 p3 D_BAV99S
D2 p3 p2 D_BAV99S
.ends

* SOT-23 PSM712 asymmetric RS-485 surge TVS (-7 V / +12 V): 1=A, 2=B, 3=GND
.subckt MP_PSM712 p1 p2 p3
.model D_TVS12 D(Is=1e-13 Rs=0.8 N=1.2 Cjo=45p BV=13.3 IBV=1m)
.model D_TVS7  D(Is=1e-13 Rs=0.8 N=1.2 Cjo=45p BV=7.8  IBV=1m)
DA1 p3 z1 D_TVS7
DA2 p1 z1 D_TVS12
DB1 p3 z2 D_TVS7
DB2 p2 z2 D_TVS12
.ends

* SOT-23 AO3400A N-MOSFET: 1=Gate, 2=Source, 3=Drain (incl. Ciss/Crss & body diode)
.subckt MP_AO3400A g s d
M1 d g s s M_AO3400A
CGS g s 520p
CGD g d 65p
DBODY s d D_SS16
.ends

* Single smooth C^inf RRIO CMOS op-amp channel (TLV9062: A0=100dB, GBW=10MHz, en=16nV/rtHz, pole referenced to VEE)
.subckt TLV9062_CH inp inm vcc vee out
* Input white noise generator (1 kOhm * 4 k T = 16.6e-18 -> 4.07 nV/rtHz; x3.93 = 16 nV/rtHz)
VN n_src 0 DC 0
RN n_src 0 1k
EN inp_n inp n_src 0 3.93
RINP inp_n vee 1e12
RINM inm   vee 1e12
CINP inp_n vee 3p
CINM inm   vee 3p
* Dominant pole at fp1 = 100 Hz (Gm=1mS, Rp=100Meg -> A0=1e5=100dB, Cp=15.915pF -> GBW=10MHz), referenced to VEE
GM1 vee p1 inp_n inm 1m
RP1 p1 vee 100Meg
CP1 p1 vee 15.915p
* Smooth C^inf tanh rail-to-rail output stage clamped strictly between VEE and VCC
BOUT out_i vee V = 0.5 * smax(0.0, V(vcc, vee)) * (1.0 + stanh(2.0 * V(p1, vee) / smax(0.25, V(vcc, vee))))
ROUT out_i out 55
.ends

* Dual TLV9062 (all 8 pins: 1=OUT1, 2=IN1-, 3=IN1+, 4=VEE, 5=IN2+, 6=IN2-, 7=OUT2, 8=VCC)
.subckt MP_TLV9062 p1 p2 p3 p4 p5 p6 p7 p8
XCH1 p3 p2 p8 p4 p1 TLV9062_CH
XCH2 p5 p6 p8 p4 p7 TLV9062_CH
BIQ p8 p4 I = 1.08m * step01(V(p8, p4) - 1.5, 0.2)
.ends

* Dual LM2903 open-collector comparator (all 8 pins: 1=OUT1, 2=IN1-, 3=IN1+, 4=GND, 5=IN2+, 6=IN2-, 7=OUT2, 8=VCC)
.subckt MP_LM2903 p1 p2 p3 p4 p5 p6 p7 p8
RIN1P p3 p4 50Meg
RIN1M p2 p4 50Meg
RIN2P p5 p4 50Meg
RIN2M p6 p4 50Meg
* Open-collector pull-down conducts smoothly when IN- > IN+ (0.4 mV transition scale ~ 50 V/mV gain)
BOC1 p1 p4 I = V(p1, p4) * (1e-9 + 0.018 * step01(V(p2, p3), 0.0004))
BOC2 p7 p4 I = V(p7, p4) * (1e-9 + 0.018 * step01(V(p6, p5), 0.0004))
RBLD2 p7 p4 100Meg
BIQ p8 p4 I = 0.8m * step01(V(p8, p4) - 2.0, 0.2)
.ends

* Nexperia 74HC4052 dual 4:1 analog mux (all 16 pins instantiated, smooth C^inf tanh switches + ESD diodes)
* 1=Y0, 2=Y2, 3=Y, 4=Y3, 5=Y1, 6=/E, 7=VEE, 8=GND, 9=S1, 10=S0, 11=X3, 12=X0, 13=X, 14=X1, 15=X2, 16=VCC
.subckt MP_74HC4052 p1 p2 p3 p4 p5 p6 p7 p8 p9 p10 p11 p12 p13 p14 p15 p16
B_EN en 0 V = 1.0 - step01(V(p6, p8) - 0.5 * V(p16, p8), 0.08)
B_B0 b0 0 V = step01(V(p10, p8) - 0.5 * V(p16, p8), 0.08)
B_B1 b1 0 V = step01(V(p9, p8) - 0.5 * V(p16, p8), 0.08)
* Smooth channel weights w0..w3 in [0, 1]
BW0 w0 0 V = V(en) * (1.0 - V(b1)) * (1.0 - V(b0))
BW1 w1 0 V = V(en) * (1.0 - V(b1)) * V(b0)
BW2 w2 0 V = V(en) * V(b1) * (1.0 - V(b0))
BW3 w3 0 V = V(en) * V(b1) * V(b0)
* X section switches (Ron = 85 Ohm, Roff = 1 GOhm) + channel capacitances + input protection diodes
BX0 p12 p13 I = V(p12, p13) * (1e-9 + V(w0) / 85.0)
BX1 p14 p13 I = V(p14, p13) * (1e-9 + V(w1) / 85.0)
BX2 p15 p13 I = V(p15, p13) * (1e-9 + V(w2) / 85.0)
BX3 p11 p13 I = V(p11, p13) * (1e-9 + V(w3) / 85.0)
BY0 p1  p3  I = V(p1,  p3)  * (1e-9 + V(w0) / 85.0)
BY1 p5  p3  I = V(p5,  p3)  * (1e-9 + V(w1) / 85.0)
BY2 p2  p3  I = V(p2,  p3)  * (1e-9 + V(w2) / 85.0)
BY3 p4  p3  I = V(p4,  p3)  * (1e-9 + V(w3) / 85.0)
DX0H p12 p16 D_ESD
DX0L p7  p12 D_ESD
DX1H p14 p16 D_ESD
DX1L p7  p14 D_ESD
DX2H p15 p16 D_ESD
DX2L p7  p15 D_ESD
DX3H p11 p16 D_ESD
DX3L p7  p11 D_ESD
CXCOM p13 p8 8p
BIQ p16 p8 I = 20u * step01(V(p16, p8) - 1.5, 0.2)
.ends

* TI UCC27524ADR dual 5 A driver (all 8 pins: 1=ENA, 2=INA, 3=GND, 4=INB, 5=OUTB, 6=VDD, 7=OUTA, 8=ENB)
.subckt MP_UCC27524 p1 p2 p3 p4 p5 p6 p7 p8
* Datasheet SLUSFA9: internal 200 kOhm pull-ups from ENA and ENB to VDD!
RENA p6 p1 200k
RENB p6 p8 200k
RINA p2 p3 200k
RINB p4 p3 200k
B_GA ga 0 V = step01(V(p1, p3) - 1.55, 0.12) * step01(V(p2, p3) - 1.55, 0.12)
B_GB gb 0 V = step01(V(p8, p3) - 1.55, 0.12) * step01(V(p4, p3) - 1.55, 0.12)
* Smooth 0.55 Ohm push-pull totem poles between VDD (p6) and GND (p3)
BOUTA_H p6 p7 I = V(p6, p7) * (1e-9 + V(ga) / 0.55)
BOUTA_L p7 p3 I = V(p7, p3) * (1e-9 + (1.0 - V(ga)) / 0.55)
BOUTB_H p6 p5 I = V(p6, p5) * (1e-9 + V(gb) / 0.55)
BOUTB_L p5 p3 I = V(p5, p3) * (1e-9 + (1.0 - V(gb)) / 0.55)
BIQ p6 p3 I = 0.5m * step01(V(p6, p3) - 4.0, 0.3)
.ends

* TI TPS54360DDAR averaged peak-current-mode buck macromodel (SLVSBB4G, all 9 pins):
* 1=BST, 2=VIN, 3=EN, 4=RT, 5=FB, 6=COMP, 7=GND, 8=SW, 9=EPAD
* Uses the REAL PCB external UVLO divider (pin 3), FB divider (pin 5), Type-II COMP network (pin 6), and L/Cout (pin 8)!
.subckt MP_TPS54360 bst vin en rt fb comp gnd sw epad
REP epad gnd 1m
RBST vin bst 20
* EN threshold = 1.22 V rising + 2.9 uA hysteresis current source above threshold (SLVSBB4G 7.3.12)
B_EN en_ok 0 V = step01(V(en, gnd) - 1.22, 0.025) * step01(V(vin, gnd) - 4.3, 0.15)
BI_EN vin en I = step01(V(vin, gnd) - 1.5, 0.3) * (0.9u + 2.9u * V(en_ok))
* RT pin regulated near 0.5 V
VRT_REF rt_ref gnd DC 0.5
RRT_INT rt_ref rt 100
* Error amplifier: VREF = 0.800 V, gm_ea = 350 uA/V, Ro = 1.5 MOhm, smoothly clamped inside [0.2 V, 2.3 V]
VCOMP_REF comp_bias gnd DC 0.85
RO_EA comp comp_bias 1.5Meg
B_EA gnd comp I = V(en_ok) * 350u * stanh(0.800 - V(fb, gnd)) - (V(comp, gnd) - smin(2.3, smax(0.2, V(comp, gnd)))) / 100.0
* Averaged buck switch stage with smooth dropout pass-through at VIN - 0.75 V and buck input-power conservation
B_VTARG sw_t gnd V = V(en_ok) * smin(smax(0.0, V(vin, gnd) - 0.75), 18.0 * step01(V(comp, gnd) - 1.15, 0.45))
RSW_AVG sw_t sw 0.25
B_IVIN  vin gnd I = smax(0.0, V(sw_t, sw) / 0.25) * smin(1.0, smax(0.0, V(sw, gnd)) / smax(1.0, V(vin, gnd))) + 140u * V(en_ok)
.ends

* 3.3 V LDO macromodels (AP2112K-3.3 SOT-23-5 & XC6206P332MR SOT-23) with smooth dropout, load current conservation & PSRR pole
.subckt MP_AP2112K_3V3 vin gnd en nc vout
RNC nc gnd 100Meg
REN en gnd 3Meg
B_ON on_f 0 V = step01(V(en, gnd) - 1.1, 0.08) * step01(V(vin, gnd) - 2.5, 0.15)
B_TGT tgt gnd V = V(on_f) * smin(3.30, V(vin, gnd) - 0.18)
RPSRR tgt flt 100
CPSRR flt gnd 159n
EOUT out_i gnd flt gnd 1.0
ROUT out_i vout 0.12
BIQ vin gnd I = 55u * V(on_f) + V(on_f) * smax(0.0, V(out_i, vout) / 0.12)
.ends

.subckt MP_XC6206_3V3 gnd vout vin
B_ON on_f 0 V = step01(V(vin, gnd) - 2.2, 0.15)
B_TGT tgt gnd V = V(on_f) * smin(3.30, V(vin, gnd) - 0.24)
RPSRR tgt flt 200
CPSRR flt gnd 159n
EOUT out_i gnd flt gnd 1.0
ROUT out_i vout 0.35
BIQ vin gnd I = 8u * V(on_f) + V(on_f) * smax(0.0, V(out_i, vout) / 0.35)
.ends
"""


@dataclass
class MultiBoardSpiceDeck:
    boards: dict[str, ExtractedBoard]
    lines: list[str] = field(default_factory=list)

    def node(self, board: str, ref: str, pad: str) -> str:
        eb = self.boards[board]
        nd = eb.pad_node[(ref, pad)]
        return "0" if nd == eb.gnd_node else nd

    def find_net_node(self, board: str, sample_ref: str, sample_pad: str) -> str:
        return self.node(board, sample_ref, sample_pad)


def build_extracted_spice_deck(
    board_names: tuple[str, ...],
    *,
    ntc_temp_c: dict[tuple[str, str], float] | None = None,
    extra_lines: list[str] | None = None,
) -> MultiBoardSpiceDeck:
    """Extract copper from `board_names`, stitch B2B connectors by 3D coordinates + harnesses by cable length, and emit
    all BOM components + trace R/C parasitics."""
    spec = yaml.safe_load(SPEC_PATH.read_text())
    ntc_temp_c = ntc_temp_c or {}
    boards = {b: extract_board_copper(b) for b in board_names}
    deck = MultiBoardSpiceDeck(boards=boards, lines=["* Copper-extracted AeroMet SPICE deck", COMMON_MODELS_AND_SUBCKTS])

    # 1. Emit trace-to-ground parasitic capacitances (`C_trace`) on every extracted non-ground copper node,
    #    plus copper trace resistance comments (series R is lumped on interconnect/power branches & trace segments).
    c_idx = 0
    for bname, eb in boards.items():
        for nd, c_val in sorted(eb.net_trace_c_f.items()):
            if nd == eb.gnd_node or c_val <= 0:
                continue
            c_idx += 1
            deck.lines.append(f"CTRACE_{bname}_{c_idx} {nd} 0 {c_val:.4e}")

    # 2. Emit every component on each board strictly from BOM `Value` + `MPN` and extracted copper pad nodes!
    for bname, eb in boards.items():
        for ref, row in sorted(eb.bom.items()):
            mpn = row["MPN"]
            val_str = row["Value"]
            pfx = re.match(r"^([A-Za-z]+)", ref).group(1)

            def pnode(pnum: str, _eb=eb, _ref=ref) -> str:
                nd = _eb.pad_node[(_ref, pnum)]
                return "0" if nd == _eb.gnd_node else nd

            if pfx == "R":
                if "hand-fit" in row["Assembly"] and ref == "R26" and bname == "main":
                    # Optional 120R RS-485 bus-end terminator is DNP by default unless fitted at bus end.
                    continue
                r_ohm = parse_bom_value(val_str)
                # Include half of copper trace resistance on non-zero power/shunt nodes.
                n1, n2 = pnode("1"), pnode("2")
                r_cu = 0.25 * (eb.net_trace_r_ohm.get(n1, 0.0) + eb.net_trace_r_ohm.get(n2, 0.0))
                deck.lines.append(f"R_{bname}_{ref} {n1} {n2} {r_ohm + r_cu:.6g}")
            elif pfx == "TH":
                # 10k B=3950K NTC thermistor: R(T) = 10000 * exp(3950 * (1/(T+273.15) - 1/298.15))
                temp_c = ntc_temp_c.get((bname, ref), 25.0)
                r_ntc = 10000.0 * math.exp(3950.0 * (1.0 / (temp_c + 273.15) - 1.0 / 298.15))
                deck.lines.append(f"R_{bname}_{ref} {pnode('1')} {pnode('2')} {r_ntc:.4f}")
            elif pfx == "F":
                # Littelfuse 2920L110/60V PTC resettable fuse: cold R_typ ~ 0.14 Ohm + copper trace R
                n1, n2 = pnode("1"), pnode("2")
                r_cu = eb.net_trace_r_ohm.get(n1, 0.0) + eb.net_trace_r_ohm.get(n2, 0.0)
                deck.lines.append(f"R_{bname}_{ref} {n1} {n2} {0.14 + r_cu:.5f}")
            elif pfx == "C":
                c_farad = parse_bom_value(val_str)
                # Include realistic MLCC ESR (15 mOhm for >= 1 uF X7R/X5R, 50 mOhm for small C0G/X7R) via series node
                n1, n2 = pnode("1"), pnode("2")
                esr = 0.015 if c_farad >= 1e-6 else 0.050
                int_nd = f"esr_{bname}_{ref.lower()}"
                deck.lines.append(f"RESR_{bname}_{ref} {n1} {int_nd} {esr}")
                deck.lines.append(f"C_{bname}_{ref} {int_nd} {n2} {c_farad:.6g}")
            elif pfx == "L":
                l_henry = parse_bom_value(val_str)
                # Datasheet DCR: ANR6045T8R2M ~ 48 mOhm, SRN6045TA-220M ~ 96 mOhm, MA5532-AE 100 uH ~ 1.6 Ohm (Q~34 @500kHz -> Rp via R26)
                dcr = 1.60 if mpn == "MA5532-AE" else (0.048 if l_henry < 15e-6 else 0.096)
                n1, n2 = pnode("1"), pnode("2")
                int_nd = f"ldcr_{bname}_{ref.lower()}"
                deck.lines.append(f"RDCR_{bname}_{ref} {n1} {int_nd} {dcr}")
                deck.lines.append(f"L_{bname}_{ref} {int_nd} {n2} {l_henry:.6g}")
            elif pfx == "Y":
                # 8 MHz crystal motional/shunt capacitance (C0 ~ 4 pF)
                deck.lines.append(f"CY_{bname}_{ref} {pnode('1')} {pnode('2')} 4p")
            elif pfx == "SW":
                # Tactile push button open in normal operation (1e9 Ohm; note `G` is not a SPICE3 scale suffix!)
                deck.lines.append(f"RSW_{bname}_{ref} {pnode('1')} {pnode('2')} 1e9")
            elif pfx == "D":
                if mpn == "SMAJ36A":
                    deck.lines.append(f"D_{bname}_{ref} {pnode('2')} {pnode('1')} D_SMAJ36A")
                elif mpn == "SS16":
                    deck.lines.append(f"D_{bname}_{ref} {pnode('2')} {pnode('1')} D_SS16")
                elif mpn == "SS14":
                    deck.lines.append(f"D_{bname}_{ref} {pnode('2')} {pnode('1')} D_SS14")
                elif mpn == "B360A-13-F":
                    deck.lines.append(f"D_{bname}_{ref} {pnode('2')} {pnode('1')} D_B360A")
                elif mpn == "BAV99,215":
                    deck.lines.append(f"X_{bname}_{ref} {pnode('1')} {pnode('2')} {pnode('3')} MP_BAV99")
                elif mpn == "PSM712-LF-T7":
                    deck.lines.append(f"X_{bname}_{ref} {pnode('1')} {pnode('2')} {pnode('3')} MP_PSM712")
                elif mpn in ("GUVB-C21SD", "GUVA-C22SD"):
                    # COB2418: pad 1 = Anode, pad 2 = Cathode
                    deck.lines.append(f"D_{bname}_{ref} {pnode('1')} {pnode('2')} D_UVPD")
            elif pfx == "Q" and mpn == "AO3400A":
                deck.lines.append(f"X_{bname}_{ref} {pnode('1')} {pnode('2')} {pnode('3')} MP_AO3400A")
            elif pfx == "U":
                if mpn in ("TLV9062IDR", "TLV9062IDGKR"):
                    pins = " ".join(pnode(str(i)) for i in range(1, 9))
                    deck.lines.append(f"X_{bname}_{ref} {pins} MP_TLV9062")
                elif mpn == "LM2903DR2G":
                    pins = " ".join(pnode(str(i)) for i in range(1, 9))
                    deck.lines.append(f"X_{bname}_{ref} {pins} MP_LM2903")
                elif mpn == "74HC4052D,653":
                    pins = " ".join(pnode(str(i)) for i in range(1, 17))
                    deck.lines.append(f"X_{bname}_{ref} {pins} MP_74HC4052")
                elif mpn == "UCC27524ADR":
                    pins = " ".join(pnode(str(i)) for i in range(1, 9))
                    deck.lines.append(f"X_{bname}_{ref} {pins} MP_UCC27524")
                elif mpn == "TPS54360DDAR":
                    pins = " ".join(pnode(str(i)) for i in range(1, 10))
                    deck.lines.append(f"X_{bname}_{ref} {pins} MP_TPS54360")
                elif mpn == "AP2112K-3.3TRG1":
                    pins = " ".join(pnode(str(i)) for i in range(1, 6))
                    deck.lines.append(f"X_{bname}_{ref} {pins} MP_AP2112K_3V3")
                elif mpn == "XC6206P332MR-G":
                    pins = " ".join(pnode(str(i)) for i in range(1, 4))
                    deck.lines.append(f"X_{bname}_{ref} {pins} MP_XC6206_3V3")
                elif mpn == "102010636":
                    # XIAO ESP32-C6 onboard 3.3 V LDO (from +5V_XIAO pin 14 -> +3V3 pin 12, GND pin 13) + 45 mA MCU load,
                    # plus high-Z ADC inputs (pins 1, 2) and default boot GPIO states.
                    deck.lines.append(
                        f"X_{bname}_{ref}_LDO {pnode('14')} {pnode('13')} {pnode('14')} nc_{bname}_{ref} {pnode('12')} MP_AP2112K_3V3"
                    )
                    deck.lines.append(f"RLOAD_{bname}_{ref} {pnode('12')} {pnode('13')} 73.3")
                    for adc_pin in ("1", "2", "10"):
                        deck.lines.append(f"RADC_{bname}_{ref}_{adc_pin} {pnode(adc_pin)} {pnode('13')} 10Meg")
                elif mpn == "ADS1115IDGSR":
                    # 6 MOhm differential/common-mode input impedance on AIN0..AIN3 (pins 4..7) + 150 uA VDD current
                    for ain in ("4", "5", "6", "7"):
                        deck.lines.append(f"RAIN_{bname}_{ref}_{ain} {pnode(ain)} {pnode('3')} 6Meg")
                    deck.lines.append(f"RIQ_{bname}_{ref} {pnode('8')} {pnode('3')} 22k")
                elif mpn == "SP3485EN-L/TR":
                    # RS-485 unit load (12 kOhm input impedance on A/B pins 6, 7) + 0.5 mA VCC quiescent current
                    deck.lines.append(f"R485A_{bname}_{ref} {pnode('6')} {pnode('5')} 12k")
                    deck.lines.append(f"R485B_{bname}_{ref} {pnode('7')} {pnode('5')} 12k")
                    deck.lines.append(f"RIQ_{bname}_{ref} {pnode('8')} {pnode('5')} 6.6k")
                elif mpn == "STM32G431RBT6":
                    # Crown MCU (~22 mA on +3V3 pin 16, high-Z OPAMP1_VINP pin 13, VINM0 pin 17, ADC2 pin 18)
                    deck.lines.append(f"RIQ_{bname}_{ref} {pnode('16')} {pnode('15')} 150")
                    for hip in ("13", "17", "18"):
                        deck.lines.append(f"RHIZ_{bname}_{ref}_{hip} {pnode(hip)} {pnode('27')} 10Meg")
                elif mpn == "AS3935-BQFT":
                    # AS3935 antenna LNA input impedance (> 100 kOhm // 15 pF internal TUN_CAP mid-code ~56 pF!)
                    deck.lines.append(f"RAS_IN_{bname}_{ref} {pnode('3')} {pnode('2')} 200k")
                    deck.lines.append(f"CAS_TUN_{bname}_{ref} {pnode('3')} {pnode('2')} 60p")
                    deck.lines.append(f"RIQ_{bname}_{ref} {pnode('5')} {pnode('4')} 55k")
                elif mpn in ("SCD41-D-R2", "SGP41-D-R4", "SHT40-AD1F-R2", "BMP585", "TSL25911FN", "MMC5983MA", "LSM6DS3TR-C"):
                    # Realistic sensor quiescent/measurement loads on their VDD pins
                    vdd_p = {"SCD41-D-R2": ("19", "6", 190.0), "SGP41-D-R4": ("5", "2", 660.0),
                             "SHT40-AD1F-R2": ("3", "4", 10000.0), "BMP585": ("8", "6", 2500.0),
                             "TSL25911FN": ("5", "3", 12000.0), "MMC5983MA": ("2", "9", 6000.0),
                             "LSM6DS3TR-C": ("8", "6", 5000.0)}[mpn]
                    deck.lines.append(f"RSENS_{bname}_{ref} {pnode(vdd_p[0])} {pnode(vdd_p[1])} {vdd_p[2]}")

    # 3. Stitch mated boards:
    #    - `b2b_header` (`crown J10 <-> dome J1`, `probe J2 <-> reflector J1`): match strictly by 3D `(X, Z)` coordinates!
    #    - `cable` (`main <-> power`, `main <-> probe`, `main <-> crown`): wire R/C parasitics from `cable_length_mm`.
    stitch_idx = 0
    for lk in spec["boards"]["interconnects"]:
        b_a, b_b = lk["from"], lk["to"]
        if b_a not in boards or b_b not in boards:
            continue
        ref_a, ref_b = lk["refs"][b_a], lk["refs"][b_b]
        eb_a, eb_b = boards[b_a], boards[b_b]

        if lk["type"] == "b2b_header":
            xz_a = _board_3d_pad_xz(spec, eb_a, ref_a)
            xz_b = _board_3d_pad_xz(spec, eb_b, ref_b)
            assert len(xz_a) == len(xz_b) == lk["pins"]
            used_b: set[str] = set()
            for pa, (xa, za) in sorted(xz_a.items(), key=lambda kv: int(kv[0])):
                # Nearest 3D `(X, Z)` mating pin on board B — zero reliance on pin numbers or net names!
                pb, (xb, zb) = min(
                    ((k, v) for k, v in xz_b.items() if k not in used_b),
                    key=lambda kv: math.hypot(xa - kv[1][0], za - kv[1][1]),
                )
                dist_x = abs(xa - xb)
                dist_3d = math.hypot(xa - xb, za - zb)
                assert dist_x < 0.08 and dist_3d < 1.35, (
                    f"3D B2B misalignment {b_a}:{ref_a}.{pa} ({xa:.2f},{za:.2f}) vs "
                    f"{b_b}:{ref_b}.{pb} ({xb:.2f},{zb:.2f}): dx={dist_x:.3f} d={dist_3d:.3f}"
                )
                used_b.add(pb)
                na = "0" if eb_a.pad_node[(ref_a, pa)] == eb_a.gnd_node else eb_a.pad_node[(ref_a, pa)]
                nb = "0" if eb_b.pad_node[(ref_b, pb)] == eb_b.gnd_node else eb_b.pad_node[(ref_b, pb)]
                if na != nb:
                    stitch_idx += 1
                    deck.lines.append(f"RB2B_{stitch_idx} {na} {nb} {B2B_CONTACT_OHM}")
        else:
            len_mm = float(lk["cable_length_mm"])
            r_wire = HARNESS_OHM_PER_MM * len_mm + 2 * B2B_CONTACT_OHM
            c_wire = HARNESS_PF_PER_MM * len_mm * 1e-12
            for p_idx in range(1, lk["pins"] + 1):
                pstr = str(p_idx)
                na = "0" if eb_a.pad_node[(ref_a, pstr)] == eb_a.gnd_node else eb_a.pad_node[(ref_a, pstr)]
                nb = "0" if eb_b.pad_node[(ref_b, pstr)] == eb_b.gnd_node else eb_b.pad_node[(ref_b, pstr)]
                if na != nb:
                    stitch_idx += 1
                    deck.lines.append(f"RCBL_{stitch_idx} {na} {nb} {r_wire:.5f}")
                    deck.lines.append(f"CCBL_{stitch_idx} {nb} 0 {c_wire:.4e}")

    if extra_lines:
        deck.lines.extend(extra_lines)
    return deck
