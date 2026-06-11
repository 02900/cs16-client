#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
import_mp3_map.py -- export a Max Payne 3 level section as a GoldSrc map and
install it into the cs16-client gamedir.

Pipeline:
    <level>.rpf  --(mp3-rpf-tools: cs16-export)-->  <name>.map + <name>.wad
                 --(VHLT: hlcsg/hlbsp/hlvis/hlrad)-->  <name>.bsp   (with --vhlt)
                 --(install)-->  <gamedir>/maps/<name>.bsp + <gamedir>/<name>.wad

The geometry is a Rockstar (Max Payne 3) collision mesh: it is NOT committed to
this repo. Each user runs this against their own copy of the game. Only this
script and scripts/mp3_maps.json are tracked; the work cache (.mp3cache/) and the
installed map (in the gamedir) are gitignored / outside the repo.

Defaults assume the sibling layout used in this project:
    repositories/cs16-client                 (this repo)
    repositories/mp3-rpf-tools               (the RPF/collision extractor)
    repositories/xash3d-fwgs-win32-amd64     (the Xash install; gamedir = cstrike/)

Usage:
    python scripts/import_mp3_map.py                       # export only (no compile)
    python scripts/import_mp3_map.py --vhlt "<VHLT bin>"   # also compile + install .bsp
    python scripts/import_mp3_map.py --only mp3_vofarch    # one map from the manifest
    python scripts/import_mp3_map.py --mp3 "<Max Payne 3 dir>" --gamedir "<...>/cstrike"

Without --vhlt it stops at <name>.map + <name>.wad (in the work cache) and prints
the compile commands (also written to compile.txt). With --vhlt it compiles and
installs the playable .bsp; launch in-game with:  map <name>
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# the four VHLT (Vluzacn's ZHLT) stages, in order
_VHLT_STAGES = ["hlcsg", "hlbsp", "hlvis", "hlrad"]


def repo_path(*parts):
    return os.path.normpath(os.path.join(REPO, *parts))


def run(cmd, cwd=None):
    print("  $ " + " ".join('"%s"' % a if " " in a else a for a in cmd))
    subprocess.run(cmd, check=True, cwd=cwd)


def find_vhlt_tool(vhlt_dir, stage):
    """Locate a VHLT stage exe (hlcsg / hlcsg.exe / hlcsg_x64.exe ...)."""
    for cand in (stage, stage + ".exe", stage + "_x64.exe", stage + "64.exe"):
        p = os.path.join(vhlt_dir, cand)
        if os.path.isfile(p):
            return p
    return None


def export_one(tool_entry, mp3_dir, work, m, force):
    """Run cs16-export for one manifest entry. Returns its output dir."""
    name = m["name"]
    archive = os.path.join(mp3_dir, m["archive"].replace("/", os.sep))
    if not os.path.isfile(archive):
        print("  [skip] %s: archive not found: %s" % (name, archive))
        return None

    outdir = os.path.join(work, name)
    map_path = os.path.join(outdir, name + ".map")
    if os.path.isfile(map_path) and not force:
        print("  cached  %s (use --force to re-export)" % name)
        return outdir

    args = [sys.executable, tool_entry, "cs16-export", archive,
            "-o", outdir, "--name", name]
    if m.get("entry"):                  # unused in --visual mode
        args.insert(4, m["entry"])
    if m.get("visual"):
        args.append("--visual")
    for key, flag in (("scale", "--scale"), ("thickness", "--thickness"),
                      ("spawns", "--spawns"), ("crop", "--crop"),
                      ("max_tris", "--max-tris"), ("tex_scale", "--tex-scale"),
                      ("tex_floor", "--tex-floor"), ("tex_wall", "--tex-wall"),
                      ("tex_ceiling", "--tex-ceiling"), ("voxel", "--voxel"),
                      ("light_scale", "--light-scale")):
        if m.get(key) is not None:
            # "--flag=value" so a negative --crop (leading '-') is not parsed
            # as another option by the tool's argparse.
            args.append("%s=%s" % (flag, m[key]))
    if m.get("lights") is False:
        args.append("--no-lights")

    tex = m.get("texture")
    if tex:
        if not os.path.isfile(tex):
            cached = os.path.join(work, "..", "png", tex + ".png")
            if os.path.isfile(cached):
                tex = cached
        args += ["--texture", tex]

    run(args)
    return outdir


# Per-stage flags. The map is a dense func_detail mesh -> a huge portal set, so
# full VIS is pathologically slow; -fast VIS (approximate PVS) is the difference
# between seconds and tens of minutes. RAD also runs -fast for a quick demo bake.
_VHLT_FLAGS = {
    "hlcsg": ["-nowadtextures", "-wadinclude"],   # -wadinclude takes <name> (added below)
    "hlbsp": [],
    "hlvis": ["-fast"],
    "hlrad": ["-fast"],
}


def compile_one(vhlt_dir, outdir, name):
    """Run the four VHLT stages in outdir. Returns the .bsp path or None."""
    for stage in _VHLT_STAGES:
        tool = find_vhlt_tool(vhlt_dir, stage)
        if not tool:
            print("  [warn] %s not found in %s -- cannot compile" % (stage, vhlt_dir))
            return None
        cmd = [tool] + list(_VHLT_FLAGS[stage])
        if stage == "hlcsg":
            cmd.append(name)              # -wadinclude <name>
        cmd.append(name)
        run(cmd, cwd=outdir)
    bsp = os.path.join(outdir, name + ".bsp")
    return bsp if os.path.isfile(bsp) else None


def install(outdir, name, gamedir):
    """Copy the compiled .bsp (and its .wad) into the gamedir."""
    bsp = os.path.join(outdir, name + ".bsp")
    wad = os.path.join(outdir, name + ".wad")
    maps_dir = os.path.join(gamedir, "maps")
    os.makedirs(maps_dir, exist_ok=True)
    shutil.copyfile(bsp, os.path.join(maps_dir, name + ".bsp"))
    if os.path.isfile(wad):                 # harmless even with -wadinclude
        shutil.copyfile(wad, os.path.join(gamedir, name + ".wad"))
    print("  installed %s.bsp -> %s" % (name, maps_dir))


def main():
    ap = argparse.ArgumentParser(
        description="Export Max Payne 3 level sections as GoldSrc maps into the "
                    "cs16-client gamedir")
    ap.add_argument("--mp3",
                    default="C:/Program Files (x86)/Steam/steamapps/common/"
                            "Max Payne 3/Max Payne 3",
                    help="Max Payne 3 install dir (the folder containing pc/)")
    ap.add_argument("--tools", default=repo_path("..", "mp3-rpf-tools"),
                    help="path to the mp3-rpf-tools checkout")
    ap.add_argument("--gamedir",
                    default=repo_path("..", "xash3d-fwgs-win32-amd64", "cstrike"),
                    help="target gamedir (maps go under <gamedir>/maps)")
    ap.add_argument("--manifest", default=repo_path("scripts", "mp3_maps.json"))
    ap.add_argument("--work", default=repo_path(".mp3cache", "maps"),
                    help="scratch dir for .map/.wad/.bsp (gitignored)")
    ap.add_argument("--vhlt", help="VHLT bin dir (hlcsg/hlbsp/hlvis/hlrad); "
                                    "compile + install when given")
    ap.add_argument("--only", help="export only the named map from the manifest")
    ap.add_argument("--force", action="store_true",
                    help="re-export even if a cached .map exists")
    args = ap.parse_args()

    tool_entry = os.path.join(args.tools, "rpf4_extract.py")
    if not os.path.isfile(tool_entry):
        sys.exit("error: mp3-rpf-tools not found at %s (pass --tools)" % tool_entry)
    if not os.path.isdir(args.mp3):
        sys.exit("error: Max Payne 3 dir not found at %s (pass --mp3)" % args.mp3)
    if args.vhlt and not os.path.isdir(args.gamedir):
        sys.exit("error: gamedir not found at %s (pass --gamedir)" % args.gamedir)

    with open(args.manifest, encoding="utf-8") as fh:
        maps = json.load(fh)["maps"]
    if args.only:
        maps = [m for m in maps if m["name"] == args.only]
        if not maps:
            sys.exit("no map named %r in the manifest" % args.only)

    os.makedirs(args.work, exist_ok=True)
    done = compiled = 0
    for m in maps:
        name = m["name"]
        print("[map] %s  (%s :: %s)" % (name, m["archive"],
                                        m.get("entry") or "visual .wdr"))
        outdir = export_one(tool_entry, args.mp3, args.work, m, args.force)
        if not outdir:
            continue
        done += 1
        if args.vhlt:
            bsp = compile_one(args.vhlt, outdir, name)
            if bsp:
                install(outdir, name, args.gamedir)
                compiled += 1
            else:
                print("  [warn] compile failed for %s -- see %s" % (name, outdir))
        else:
            print("  exported -> %s (run %s/compile.txt, or pass --vhlt to compile)"
                  % (outdir, outdir))

    print("\ndone: %d exported, %d compiled+installed" % (done, compiled))
    if compiled:
        print("launch in-game with:  map <name>   (e.g. map %s)" % maps[0]["name"])
    elif done and not args.vhlt:
        print("to get a playable .bsp, compile with VHLT (see compile.txt) or "
              "re-run with --vhlt \"<VHLT bin dir>\"")


if __name__ == "__main__":
    main()
