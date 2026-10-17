"""Embossed (`crown_embossed.stl/.step`) and multi-colour (`crown_color.3mf` / `crown_color_obj.zip`)
top crown exports with cardinal N/E/S/W compass markings."""

import io
import zipfile
from pathlib import Path

import build_enclosure as be
import cadquery as cq
import enclosure_features as ef
import front_legend as fl
from matplotlib.path import Path as MplPath

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "mechanical/build" / ef.VARIANT
INLAY_DEPTH = 0.4
TOLERANCE = 0.08
COLOURS = {"crown": (0.94, 0.95, 0.96), "legend": (0.10, 0.12, 0.15)}


def rings(path):
    polygons = [p for p in path.to_polygons(closed_only=True) if len(p) > 3]
    nested = []
    for i, ring in enumerate(polygons):
        depth = sum(MplPath(other).contains_point(ring[0]) for j, other in enumerate(polygons) if j != i)
        nested.append((depth, [tuple(map(float, pt)) for pt in ring[:-1]]))
    return sorted(nested, key=lambda item: item[0])


def prism_xz(points, y0, height):
    return cq.Workplane("XZ", origin=(0, y0, 0)).polyline(points).close().extrude(-height)


def inlay(path, y0, height):
    solid = None
    for depth, points in rings(path):
        body = prism_xz(points, y0, height)
        if depth % 2 == 0:
            solid = body if solid is None else solid.union(body)
        elif solid is not None:
            solid = solid.cut(body)
    return solid


def legend_bodies():
    crown = be.build_crown()
    raw = None
    for lbl in fl.labels():
        b = inlay(lbl.path(), 188.0, 12.0)
        raw = b if raw is None else raw.union(b)
    # Intersect with outer surface slice of crown dome so text conforms to the 12-deg dome slope
    crown_shifted = crown.translate((0, -INLAY_DEPTH, 0))
    surface_shell = crown.cut(crown_shifted)
    return raw.intersect(surface_shell)


def mesh(shape):
    vertices, triangles = shape.val().tessellate(TOLERANCE, 0.25)
    return [(v.x, v.y, v.z) for v in vertices], triangles


def write_3mf(parts, path):
    materials = "".join(f'<base name="{name}" displaycolor="#{"".join(f"{round(c * 255):02X}" for c in rgb)}"/>'
                        for name, rgb in COLOURS.items())
    objects, items = [], []
    for index, (name, (vertices, triangles)) in enumerate(parts.items(), start=2):
        v = "".join(f'<vertex x="{x:.4f}" y="{y:.4f}" z="{z:.4f}"/>' for x, y, z in vertices)
        t = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in triangles)
        objects.append(f'<object id="{index}" name="{name}" type="model" pid="1" pindex="{list(COLOURS).index(name)}">'
                       f'<mesh><vertices>{v}</vertices><triangles>{t}</triangles></mesh></object>')
        items.append(f'<item objectid="{index}"/>')
    model = ('<?xml version="1.0" encoding="UTF-8"?>'
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             f'<resources><basematerials id="1">{materials}</basematerials>{"".join(objects)}</resources>'
             f'<build>{"".join(items)}</build></model>')
    types = ('<?xml version="1.0" encoding="UTF-8"?>'
             '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
             '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
             '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("[Content_Types].xml", types)
        archive.writestr("_rels/.rels", rels)
        archive.writestr("3D/3dmodel.model", model)


def write_obj_zip(parts, path):
    obj, mtl = io.StringIO(), io.StringIO()
    obj.write("mtllib crown_color.mtl\n")
    offset = 1
    for name, (vertices, triangles) in parts.items():
        mtl.write(f"newmtl {name}\nKd {' '.join(f'{c:.3f}' for c in COLOURS[name])}\nKa 0 0 0\nKs 0 0 0\nd 1\nillum 1\n\n")
        obj.write(f"o {name}\nusemtl {name}\n")
        obj.writelines(f"v {x:.4f} {y:.4f} {z:.4f}\n" for x, y, z in vertices)
        obj.writelines(f"f {a + offset} {b + offset} {c + offset}\n" for a, b, c in triangles)
        offset += len(vertices)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("crown_color.obj", obj.getvalue())
        archive.writestr("crown_color.mtl", mtl.getvalue())


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    crown = be.build_crown()
    legend = legend_bodies()
    embossed = crown.cut(legend)
    parts = {"crown": mesh(embossed), "legend": mesh(legend)}
    write_3mf(parts, OUT / "crown_color.3mf")
    write_obj_zip(parts, OUT / "crown_color_obj.zip")
    print("Exported crown_color.3mf and crown_color_obj.zip")


if __name__ == "__main__":
    main()
