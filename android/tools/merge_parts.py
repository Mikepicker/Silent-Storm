#!/usr/bin/env python3
"""
merge_parts.py -- merge a character's four geometry parts into one OBJ.

A skinned model's mesh is not one file.  CGameView::CreateSkin draws it as
N_MODEL_MATERIALS parts, and the resource key is

    GetID(SPartKey{id, part}) = id + (part << 16)          GResource.cpp:74

so part N of geometry G lives in Geometries/<G + (N<<16)>, and part N is drawn
with the model's MaterialN.  For a character that works out as

    part 0  Material0  body        part 2  Material2  legs
    part 1  Material1  head/face   part 3  Material3  extras

usage: merge_parts.py <geometries-dir> <geometry-id> <out.obj> [mtl name]
       (expects each part already exported to OBJ beside this script's caller)
"""
import os, subprocess, sys, tempfile

PART_NAMES = ["Body", "Face", "Legs", "Extra"]

def export_part(binary, path, base):
    env = dict(os.environ, A5_EXPORT_GEOM=path, A5_EXPORT_OUT=base)
    subprocess.run([binary], env=env, capture_output=True)
    return base + ".obj"

def merge(objs, out, mtllib):
    vo = 0
    with open(out, "w") as w:
        w.write(f"# Silent Storm character, {len(objs)} material parts merged\n")
        w.write(f"mtllib {mtllib}\n")
        for name, path in objs:
            if not os.path.exists(path):
                continue
            vs, vns, vts, fs = [], [], [], []
            for ln in open(path):
                if ln.startswith("v "):   vs.append(ln)
                elif ln.startswith("vn "): vns.append(ln)
                elif ln.startswith("vt "): vts.append(ln)
                elif ln.startswith("f "):  fs.append(ln)
            w.writelines(vs); w.writelines(vns); w.writelines(vts)
            w.write(f"o {name}\nusemtl {name}\n")
            for ln in fs:
                corners = []
                for c in ln.split()[1:]:
                    i = int(c.split('/')[0]) + vo
                    corners.append(f"{i}/{i}/{i}")
                w.write("f " + " ".join(corners) + "\n")
            vo += len(vs)
    return vo

if __name__ == "__main__":
    gdir, gid, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    mtllib = sys.argv[4] if len(sys.argv) > 4 else os.path.basename(out).replace(".obj", ".mtl")
    binary = os.environ.get("A5_BIN", "silentstorm_mac")
    tmp = tempfile.mkdtemp()
    objs = []
    for part in range(4):
        src = os.path.join(gdir, str(gid + (part << 16)))
        if not os.path.exists(src):
            continue
        objs.append((PART_NAMES[part], export_part(binary, src, os.path.join(tmp, f"p{part}"))))
    n = merge(objs, out, mtllib)
    print(f"{out}: {len(objs)} parts, {n} vertices")
