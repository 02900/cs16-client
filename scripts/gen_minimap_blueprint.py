#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
gen_minimap_blueprint.py -- render a Max Payne 3 style "blueprint" minimap from a GoldSrc BSP.

Reads <gamedir>/maps/<map>.bsp (BSP v30), projects every upward-facing world face (floors,
ramps, stairs; sky and liquids excluded) top-down, and paints them light gray -- higher floors
slightly brighter -- on a fully transparent background. The radar draws the result over its
dark disc, giving the MP3 night-blueprint look (light walkable areas on black).

Output (generated locally, NOT committed -- it derives from the game's map data):
    <gamedir>/gfx/mp3/minimaps/<map>.png   RGBA, square
    <gamedir>/gfx/mp3/minimaps/<map>.txt   "minx miny maxx maxy" -- world coords of the PNG
                                           edges. Row 0 of the PNG is world maxy (north up):
                                           u = (wx-minx)/(maxx-minx), v = (maxy-wy)/(maxy-miny)

Usage:
    python scripts/gen_minimap_blueprint.py de_dust2
    python scripts/gen_minimap_blueprint.py --all
    python scripts/gen_minimap_blueprint.py de_dust2 de_aztec --gamedir "<...>/cstrike" --res 1024
"""
import argparse
import glob
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LUMP_PLANES, LUMP_TEXTURES, LUMP_VERTEXES = 1, 2, 3
LUMP_TEXINFO, LUMP_FACES = 6, 7
LUMP_EDGES, LUMP_SURFEDGES, LUMP_MODELS = 12, 13, 14

FLOOR_NZ = 0.55          # min upward normal.z to count as walkable (includes ramps/stairs)
GRAY_LO, GRAY_HI = 140, 230   # height-shaded floor brightness range
ALPHA = 235


def read_lump(data, idx):
    off, ln = struct.unpack_from("<ii", data, 4 + idx * 8)
    return data[off:off + ln]


def parse_bsp(path):
    with open(path, "rb") as fh:
        data = fh.read()
    version = struct.unpack_from("<i", data)[0]
    if version != 30:
        sys.exit("error: %s is BSP v%d, expected v30 (GoldSrc)" % (path, version))

    planes = []
    raw = read_lump(data, LUMP_PLANES)
    for o in range(0, len(raw), 20):
        nx, ny, nz, dist, _ptype = struct.unpack_from("<ffffi", raw, o)
        planes.append((nx, ny, nz))

    raw = read_lump(data, LUMP_VERTEXES)
    verts = [struct.unpack_from("<fff", raw, o) for o in range(0, len(raw), 12)]

    raw = read_lump(data, LUMP_EDGES)
    edges = [struct.unpack_from("<HH", raw, o) for o in range(0, len(raw), 4)]

    raw = read_lump(data, LUMP_SURFEDGES)
    surfedges = struct.unpack_from("<%di" % (len(raw) // 4), raw)

    raw = read_lump(data, LUMP_TEXINFO)
    texinfo_miptex = [struct.unpack_from("<i", raw, o + 32)[0] for o in range(0, len(raw), 40)]

    # miptex names, for the sky/liquid filter
    raw = read_lump(data, LUMP_TEXTURES)
    texnames = []
    if len(raw) >= 4:
        nummip = struct.unpack_from("<i", raw)[0]
        for i in range(nummip):
            moff = struct.unpack_from("<i", raw, 4 + i * 4)[0]
            if moff < 0 or moff + 16 > len(raw):
                texnames.append("")
                continue
            name = raw[moff:moff + 16].split(b"\0")[0].decode("ascii", "ignore")
            texnames.append(name.lower())

    raw = read_lump(data, LUMP_FACES)
    faces = [struct.unpack_from("<HHiHH", raw, o)[:5] for o in range(0, len(raw), 20)]

    raw = read_lump(data, LUMP_MODELS)
    firstface, numfaces = struct.unpack_from("<ii", raw, 64 - 8)  # model 0 (worldspawn)

    return planes, verts, edges, surfedges, texinfo_miptex, texnames, faces, firstface, numfaces


def face_polygon(face, edges, surfedges, verts):
    _planenum, _side, firstedge, numedges, _texinfo = face
    poly = []
    for k in range(numedges):
        se = surfedges[firstedge + k]
        v = edges[se][0] if se >= 0 else edges[-se][1]
        poly.append(verts[v])
    return poly


def collect_floors(bsp):
    planes, verts, edges, surfedges, ti_miptex, texnames, faces, firstface, numfaces = bsp
    floors = []  # (avg_z, [(x, y), ...])
    for fi in range(firstface, firstface + numfaces):
        planenum, side, _firstedge, numedges, texinfo = faces[fi]
        if numedges < 3:
            continue
        nx, ny, nz = planes[planenum]
        if side:
            nz = -nz
        if nz < FLOOR_NZ:
            continue
        mip = ti_miptex[texinfo] if texinfo < len(ti_miptex) else -1
        name = texnames[mip] if 0 <= mip < len(texnames) else ""
        if name.startswith("sky") or name.startswith("!"):
            continue  # skybox tops and liquids are not walkable ground
        poly = face_polygon(faces[fi], edges, surfedges, verts)
        avg_z = sum(p[2] for p in poly) / len(poly)
        floors.append((avg_z, [(p[0], p[1]) for p in poly]))
    return floors


def render(floors, res):
    from PIL import Image, ImageDraw

    minx = min(x for _z, poly in floors for x, _y in poly)
    maxx = max(x for _z, poly in floors for x, _y in poly)
    miny = min(y for _z, poly in floors for _x, y in poly)
    maxy = max(y for _z, poly in floors for _x, y in poly)

    # square extent + a little padding, centered
    cx, cy_ = (minx + maxx) / 2.0, (miny + maxy) / 2.0
    half = max(maxx - minx, maxy - miny) / 2.0 * 1.04
    minx, maxx = cx - half, cx + half
    miny, maxy = cy_ - half, cy_ + half

    # height shading range from the 5th..95th percentile (outliers would flatten it)
    zs = sorted(z for z, _poly in floors)
    zlo = zs[int(len(zs) * 0.05)]
    zhi = zs[int(len(zs) * 0.95) - 1] if len(zs) > 1 else zlo + 1.0
    if zhi - zlo < 1.0:
        zhi = zlo + 1.0

    img = Image.new("RGBA", (res, res), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    scale = (res - 1) / (2.0 * half)

    for z, poly in sorted(floors, key=lambda f: f[0]):  # painter: higher floors drawn last
        t = (z - zlo) / (zhi - zlo)
        t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
        g = int(GRAY_LO + (GRAY_HI - GRAY_LO) * t)
        pts = [((x - minx) * scale, (maxy - y) * scale) for x, y in poly]
        draw.polygon(pts, fill=(g, g, g, ALPHA))

    return img, (minx, miny, maxx, maxy)


def process_map(gamedir, mapname, res):
    bsp_path = os.path.join(gamedir, "maps", mapname + ".bsp")
    if not os.path.isfile(bsp_path):
        print("  [MISS] %s" % bsp_path)
        return False

    floors = collect_floors(parse_bsp(bsp_path))
    if not floors:
        print("  [skip] %s: no floor faces found" % mapname)
        return False

    img, (minx, miny, maxx, maxy) = render(floors, res)

    outdir = os.path.join(gamedir, "gfx", "mp3", "minimaps")
    os.makedirs(outdir, exist_ok=True)
    img.save(os.path.join(outdir, mapname + ".png"))
    with open(os.path.join(outdir, mapname + ".txt"), "w") as fh:
        fh.write("%.1f %.1f %.1f %.1f\n" % (minx, miny, maxx, maxy))
    print("  %-20s %d floors -> gfx/mp3/minimaps/%s.png" % (mapname, len(floors), mapname))
    return True


def main():
    ap = argparse.ArgumentParser(description="Generate MP3 blueprint minimaps from GoldSrc BSPs")
    ap.add_argument("maps", nargs="*", help="map names without extension (e.g. de_dust2)")
    ap.add_argument("--gamedir",
                    default=os.path.normpath(os.path.join(REPO, "..", "xash3d-fwgs-win32-amd64", "cstrike")),
                    help="cstrike gamedir (reads maps/, writes gfx/mp3/minimaps/)")
    ap.add_argument("--res", type=int, default=1024, help="output PNG size (default 1024)")
    ap.add_argument("--all", action="store_true", help="process every maps/*.bsp in the gamedir")
    args = ap.parse_args()

    names = args.maps
    if args.all:
        names = sorted(os.path.splitext(os.path.basename(p))[0]
                       for p in glob.glob(os.path.join(args.gamedir, "maps", "*.bsp")))
    if not names:
        ap.error("pass map names or --all")

    ok = 0
    for name in names:
        ok += 1 if process_map(args.gamedir, name, args.res) else 0
    print("done: %d/%d generated" % (ok, len(names)))


if __name__ == "__main__":
    main()
