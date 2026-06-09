#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
import_mp3_assets.py -- bring Max Payne 3 HUD/UI textures into the cs16-client gamedir.

Pipeline:
    pc.rpf  --(mp3-rpf-tools: extract)-->  the needed .wtd
            --(mp3-rpf-tools: extract-textures)-->  PNG
            --(this script + scripts/mp3_assets.json)-->  <gamedir>/gfx/mp3/*.png

The decoded textures are Rockstar (Max Payne 3) assets: they are NOT committed to this
repo. Each user runs this against their own copy of the game. Only this script and the
manifest are tracked; the generated output and the work cache (.mp3cache/) are gitignored.

Defaults assume the sibling layout used in this project:
    repositories/cs16-client                 (this repo)
    repositories/mp3-rpf-tools               (the RPF/WTD extractor)
    repositories/xash3d-fwgs-win32-amd64     (the Xash install; gamedir = cstrike/)

Usage:
    python scripts/import_mp3_assets.py            # use all defaults
    python scripts/import_mp3_assets.py --force    # re-extract + re-decode
    python scripts/import_mp3_assets.py --rpf "<path to pc.rpf>" --gamedir "<...>/cstrike"

Optional per-asset transforms in the manifest (require Pillow):
    "resize": [w, h]      resample to w x h
    "tint":   [r, g, b]   multiply RGB (0-255) -- e.g. recolor a white glyph
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def repo_path(*parts):
    return os.path.normpath(os.path.join(REPO, *parts))


def run_tool(tool_entry, *args):
    cmd = [sys.executable, tool_entry] + list(args)
    print("  $ " + " ".join('"%s"' % a if " " in a else a for a in cmd))
    subprocess.run(cmd, check=True)


def main():
    ap = argparse.ArgumentParser(
        description="Import Max Payne 3 HUD/UI textures into the cs16-client gamedir")
    ap.add_argument("--rpf",
                    default="C:/Program Files (x86)/Steam/steamapps/common/"
                            "Max Payne 3/Max Payne 3/pc.rpf",
                    help="path to Max Payne 3 pc.rpf")
    ap.add_argument("--tools", default=repo_path("..", "mp3-rpf-tools"),
                    help="path to the mp3-rpf-tools checkout")
    ap.add_argument("--gamedir",
                    default=repo_path("..", "xash3d-fwgs-win32-amd64", "cstrike"),
                    help="target gamedir (assets go under <gamedir>/gfx/mp3)")
    ap.add_argument("--manifest", default=repo_path("scripts", "mp3_assets.json"))
    ap.add_argument("--work", default=repo_path(".mp3cache"),
                    help="scratch dir for raw .wtd + decoded PNG (gitignored)")
    ap.add_argument("--force", action="store_true",
                    help="re-extract and re-decode even if the cache exists")
    args = ap.parse_args()

    tool_entry = os.path.join(args.tools, "rpf4_extract.py")
    if not os.path.isfile(tool_entry):
        sys.exit("error: mp3-rpf-tools not found at %s (pass --tools)" % tool_entry)
    if not os.path.isfile(args.rpf):
        sys.exit("error: pc.rpf not found at %s (pass --rpf)" % args.rpf)
    if not os.path.isdir(args.gamedir):
        sys.exit("error: gamedir not found at %s (pass --gamedir)" % args.gamedir)

    with open(args.manifest, encoding="utf-8") as fh:
        manifest = json.load(fh)
    sources = manifest["sources"]
    assets = manifest["assets"]

    raw = os.path.join(args.work, "raw")
    png = os.path.join(args.work, "png")
    os.makedirs(raw, exist_ok=True)
    os.makedirs(png, exist_ok=True)

    # 1) extract the needed .wtd from pc.rpf (cached unless --force)
    print("[1/3] extracting %d .wtd from %s" % (len(sources), os.path.basename(args.rpf)))
    extracted_any = False
    for src in sources:
        out_path = os.path.join(raw, src.replace("/", os.sep))
        if args.force or not os.path.isfile(out_path):
            run_tool(tool_entry, "extract", args.rpf, src, "-o", raw)
            extracted_any = True
        else:
            print("  cached  " + src)

    # 2) decode every cached .wtd to PNG in one pass (the tool dedupes by content)
    print("[2/3] decoding textures -> PNG")
    if args.force or extracted_any or not os.listdir(png):
        run_tool(tool_entry, "extract-textures", raw, "--format", "png", "-o", png)
    else:
        print("  cached (use --force to re-decode)")

    # 3) copy / transform per manifest into the gamedir
    print("[3/3] installing assets into %s" % os.path.join(args.gamedir, "gfx", "mp3"))
    try:
        from PIL import Image
        have_pil = True
    except ImportError:
        have_pil = False

    ok = miss = 0
    for a in assets:
        src_png = os.path.join(png, a["src"] + ".png")
        dest = os.path.join(args.gamedir, a["dest"].replace("/", os.sep))
        if not os.path.isfile(src_png):
            print("  [MISS] %s.png was not decoded -- check the source .wtd list" % a["src"])
            miss += 1
            continue

        os.makedirs(os.path.dirname(dest), exist_ok=True)
        ops = [k for k in ("resize", "tint") if k in a]
        if ops and not have_pil:
            print("  [warn] %s needs Pillow for %s; copying as-is" % (a["src"], ops))
            ops = []

        if ops:
            img = Image.open(src_png).convert("RGBA")
            if "resize" in a:
                img = img.resize(tuple(a["resize"]), Image.LANCZOS)
            if "tint" in a:
                tr, tg, tb = a["tint"]
                px = img.load()
                for y in range(img.height):
                    for x in range(img.width):
                        r, g, b, al = px[x, y]
                        px[x, y] = (r * tr // 255, g * tg // 255, b * tb // 255, al)
            img.save(dest)
        else:
            shutil.copyfile(src_png, dest)
        print("  %-20s -> %s" % (a["src"], a["dest"]))
        ok += 1

    print("\ndone: %d installed, %d missing -> %s"
          % (ok, miss, os.path.join(args.gamedir, "gfx", "mp3")))
    if miss:
        print("(missing assets: add the .wtd that contains them to \"sources\" in the manifest)")


if __name__ == "__main__":
    main()
