#!/usr/bin/env python3
"""
gltf_export.py -- build a rigged glTF 2.0 character from the exported pieces.

Inputs are what the engine's own loaders read, dumped by silentstorm_mac:

  part OBJ + .weights.txt   one per material part   (id + (part<<16))
  skeleton .txt             SBone: name, parent, rest pos/rot/scale
  binds .txt                inverse bind matrices, paired 1:1 with bones[]
  PNG textures              one per part

Conventions handled here:
  * SHMatrix is row-major (DirectX _11.._44); glTF wants column-major, so the
    inverse bind matrices are transposed.
  * The engine is Z-up, glTF is Y-up.  A root node rotated -90 deg about X
    converts, so Blender's importer lands the model upright again.
  * Weight bone indices index bones[] directly; only the bones that have an
    inverse bind matrix can be skinned, so joints are bones[0:len(binds)].
"""
import json, struct, sys, os, math
from collections import defaultdict

def read_skeleton(path):
    bones = []
    for ln in open(path):
        if ln.startswith('#'):
            continue
        f = ln.split()
        bones.append(dict(index=int(f[0]), parent=int(f[1]), name=f[2],
                          pos=[float(x) for x in f[3:6]],
                          rot=[float(x) for x in f[6:10]],
                          scale=[float(x) for x in f[10:13]]))
    return bones

def read_binds(path):
    out = []
    for ln in open(path):
        if ln.startswith('#'):
            continue
        m = [float(x) for x in ln.split()]
        if len(m) == 16:
            # row-major -> column-major
            # file is row-major (m[4*r+c]); glTF accessors are column-major
            out.append([m[4*r + c] for c in range(4) for r in range(4)])
    return out

def read_obj(path):
    pos, nrm, uv, idx = [], [], [], []
    for ln in open(path):
        if ln.startswith('v '):   pos.append([float(x) for x in ln.split()[1:4]])
        elif ln.startswith('vn '): nrm.append([float(x) for x in ln.split()[1:4]])
        elif ln.startswith('vt '): uv.append([float(x) for x in ln.split()[1:3]])
        elif ln.startswith('f '):
            c = [int(p.split('/')[0]) - 1 for p in ln.split()[1:]]
            for k in range(1, len(c) - 1):          # fan-triangulate n-gons
                idx += [c[0], c[k], c[k+1]]
    return pos, nrm, uv, idx

def read_weights(path, nverts, njoints):
    per = defaultdict(list)
    if os.path.exists(path):
        for ln in open(path):
            if ln.startswith('#'):
                continue
            v, b, w = ln.split()
            v, b, w = int(v), int(b), float(w)
            if 0 <= v < nverts and 0 <= b < njoints and w > 0:
                per[v].append((w, b))
    J, W = [], []
    for v in range(nverts):
        e = sorted(per.get(v, []), reverse=True)[:4]
        tot = sum(w for w, _ in e) or 1.0
        e += [(0.0, 0)] * (4 - len(e))
        J.append([b for _, b in e])
        W.append([w / tot for w, _ in e])
    return J, W

class Buf:
    def __init__(self): self.data = bytearray(); self.views = []; self.accs = []
    def add(self, raw, target=None):
        while len(self.data) % 4: self.data.append(0)
        off = len(self.data); self.data += raw
        v = {"buffer": 0, "byteOffset": off, "byteLength": len(raw)}
        if target: v["target"] = target
        self.views.append(v); return len(self.views) - 1
    def acc(self, view, ctype, count, typ, mn=None, mx=None):
        a = {"bufferView": view, "componentType": ctype, "count": count, "type": typ}
        if mn: a["min"] = mn
        if mx: a["max"] = mx
        self.accs.append(a); return len(self.accs) - 1

def build(parts, bones, binds, out_gltf):
    base = os.path.splitext(os.path.basename(out_gltf))[0]
    bin_name = base + ".bin"
    b = Buf()
    njoints = len(binds)
    meshes, materials, images, textures = [], [], [], []

    prims = []
    for name, obj, wts, png in parts:
        pos, nrm, uv, idx = read_obj(obj)
        J, W = read_weights(wts, len(pos), njoints)
        vp = b.add(struct.pack('<%df' % (len(pos)*3), *[c for p in pos for c in p]), 34962)
        vn = b.add(struct.pack('<%df' % (len(nrm)*3), *[c for p in nrm for c in p]), 34962)
        vt = b.add(struct.pack('<%df' % (len(uv)*2),  *[c for p in uv  for c in p]), 34962)
        vj = b.add(struct.pack('<%dH' % (len(J)*4),   *[c for p in J   for c in p]), 34962)
        vw = b.add(struct.pack('<%df' % (len(W)*4),   *[c for p in W   for c in p]), 34962)
        vi = b.add(struct.pack('<%dI' % len(idx), *idx), 34963)
        mn = [min(p[i] for p in pos) for i in range(3)]
        mx = [max(p[i] for p in pos) for i in range(3)]
        a_pos = b.acc(vp, 5126, len(pos), "VEC3", mn, mx)
        a_nrm = b.acc(vn, 5126, len(nrm), "VEC3")
        a_uv  = b.acc(vt, 5126, len(uv),  "VEC2")
        a_j   = b.acc(vj, 5123, len(J),   "VEC4")
        a_w   = b.acc(vw, 5126, len(W),   "VEC4")
        a_i   = b.acc(vi, 5125, len(idx), "SCALAR")
        images.append({"uri": os.path.basename(png)})
        textures.append({"source": len(images) - 1})
        materials.append({"name": name, "pbrMetallicRoughness": {
            "baseColorTexture": {"index": len(textures) - 1},
            "metallicFactor": 0.0, "roughnessFactor": 0.9}})
        prims.append({"attributes": {"POSITION": a_pos, "NORMAL": a_nrm,
                                     "TEXCOORD_0": a_uv, "JOINTS_0": a_j, "WEIGHTS_0": a_w},
                      "indices": a_i, "material": len(materials) - 1})
    meshes.append({"name": base, "primitives": prims})

    ibm = b.add(struct.pack('<%df' % (njoints*16), *[c for m in binds for c in m]))
    a_ibm = b.acc(ibm, 5126, njoints, "MAT4")

    # bone nodes first, then the mesh node, then the Z-up -> Y-up root
    nodes = []
    for bo in bones:
        n = {"name": bo["name"], "translation": bo["pos"], "rotation": bo["rot"]}
        if bo["scale"] != [1.0, 1.0, 1.0]:
            n["scale"] = bo["scale"]
        nodes.append(n)
    for i, bo in enumerate(bones):
        if bo["parent"] >= 0:
            nodes[bo["parent"]].setdefault("children", []).append(i)
    roots = [i for i, bo in enumerate(bones) if bo["parent"] < 0]
    mesh_node = len(nodes)
    nodes.append({"name": base, "mesh": 0, "skin": 0})
    conv = len(nodes)
    s = math.sin(math.radians(-90) / 2); c = math.cos(math.radians(-90) / 2)
    nodes.append({"name": "ZUpToYUp", "rotation": [s, 0.0, 0.0, c],
                  "children": roots + [mesh_node]})

    g = {"asset": {"version": "2.0", "generator": "silent-storm gltf_export.py"},
         "scene": 0, "scenes": [{"nodes": [conv]}], "nodes": nodes,
         "meshes": meshes, "materials": materials, "textures": textures, "images": images,
         "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
         "skins": [{"inverseBindMatrices": a_ibm, "joints": list(range(njoints)),
                    "skeleton": roots[0] if roots else 0}],
         "buffers": [{"uri": bin_name, "byteLength": len(b.data)}],
         "bufferViews": b.views, "accessors": b.accs}
    for t in g["textures"]:
        t["sampler"] = 0
    json.dump(g, open(out_gltf, "w"), indent=1)
    open(os.path.join(os.path.dirname(out_gltf) or ".", bin_name), "wb").write(bytes(b.data))
    tris = sum(a["count"] for a in b.accs if a["type"] == "SCALAR") // 3
    print(f"{out_gltf}: {len(prims)} primitives, {tris} triangles, "
          f"{len(bones)} bones ({njoints} skinned), {len(b.data)} bytes of buffer")

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--complete', required=True, help='the Complete/ directory')
    ap.add_argument('--geom', type=int, required=True)
    ap.add_argument('--skeleton', type=int, required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--textures', nargs='*', default=[])
    a = ap.parse_args()
    binary = os.environ.get('A5_BIN', 'silentstorm_mac')
    import subprocess, tempfile
    tmp = tempfile.mkdtemp()
    names = ['Body', 'Face', 'Legs', 'Extra']
    parts = []
    for p in range(4):
        src = os.path.join(a.complete, 'Geometries', str(a.geom + (p << 16)))
        if not os.path.exists(src):
            continue
        base = os.path.join(tmp, 'p%d' % p)
        subprocess.run([binary], env=dict(os.environ, A5_EXPORT_GEOM=src, A5_EXPORT_OUT=base),
                       capture_output=True)
        png = a.textures[p] if p < len(a.textures) else ''
        parts.append((names[p], base + '.obj', base + '.weights.txt', png))
    sk = os.path.join(tmp, 'skel.txt'); bd = os.path.join(tmp, 'binds.txt')
    subprocess.run([binary], env=dict(os.environ,
                   A5_EXPORT_SKELETON=os.path.join(a.complete, 'Skeletons', str(a.skeleton)),
                   A5_EXPORT_OUT=sk), capture_output=True)
    subprocess.run([binary], env=dict(os.environ,
                   A5_EXPORT_BINDS=os.path.join(a.complete, 'Binds', str(a.geom)),
                   A5_EXPORT_OUT=bd), capture_output=True)
    build(parts, read_skeleton(sk), read_binds(bd), a.out)
