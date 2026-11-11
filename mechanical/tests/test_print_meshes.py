"""Every exported print plate is a closed (2-manifold) mesh: each edge is shared by exactly two triangles. A sliver or
knife edge from overlapping cuts shows up as open edges (see-through areas in a slicer, failed MJF / SLS prints)."""

import collections
import struct
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
PLATES = sorted((REPO / "mechanical/build/v1/print").glob("*.stl")) or sorted((REPO / "hardware/releases/v1/enclosure/print").glob("*.stl"))


def _triangles(path):
    data = path.read_bytes()
    if data[:5] == b"solid" and b"facet" in data[:300]:
        verts = [tuple(float(v) for v in line.split()[1:]) for line in data.decode().splitlines() if line.strip().startswith("vertex")]
        return [verts[i:i + 3] for i in range(0, len(verts), 3)]
    n = struct.unpack("<I", data[80:84])[0]
    return [[struct.unpack("<3f", data[84 + 50 * k + 12 + 12 * j:84 + 50 * k + 24 + 12 * j]) for j in range(3)] for k in range(n)]


@pytest.mark.parametrize("plate", PLATES, ids=lambda p: p.stem)
def test_print_plate_mesh_is_closed(plate):
    edges = collections.Counter()
    for tri in _triangles(plate):
        pts = [tuple(round(c, 4) for c in v) for v in tri]
        for i in range(3):
            a, b = pts[i], pts[(i + 1) % 3]
            edges[(min(a, b), max(a, b))] += 1
    open_edges = [e for e, n in edges.items() if n != 2]
    assert not open_edges, f"{plate.name}: {len(open_edges)} open / non-manifold edges, e.g. near {open_edges[0][0]}"
