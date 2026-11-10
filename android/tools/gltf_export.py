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
        elif ln.startswith('vt '):
            u, v = (float(x) for x in ln.split()[1:3])
            uv.append([u, 1.0 - v])   # OBJ vt counts up from the bottom, glTF TEXCOORD_0 down from the top
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


# -- bone-space maths, for meshes the engine hangs off a bone ----------------
def bone_world(bones):
    """Rest world matrix of every bone, as row-major 4x4 lists (engine Z-up)."""
    def mul(A, B):
        return [[sum(A[r][k]*B[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
    def local(bo):
        x, y, z, w = bo["rot"]
        M = [[1-2*(y*y+z*z), 2*(x*y-z*w),   2*(x*z+y*w),   bo["pos"][0]],
             [2*(x*y+z*w),   1-2*(x*x+z*z), 2*(y*z-x*w),   bo["pos"][1]],
             [2*(x*z-y*w),   2*(y*z+x*w),   1-2*(x*x+y*y), bo["pos"][2]],
             [0.0, 0.0, 0.0, 1.0]]
        for r in range(3):
            for c in range(3):
                M[r][c] *= bo["scale"][c]
        return M
    out = [None] * len(bones)
    def w(i):
        if out[i] is None:
            m = local(bones[i])
            out[i] = mul(w(bones[i]["parent"]), m) if bones[i]["parent"] >= 0 else m
        return out[i]
    for i in range(len(bones)):
        w(i)
    return out

def bone_index_of(bones, name):
    for i, bo in enumerate(bones):
        if bo["name"] == name:
            return i
    raise SystemExit("no bone called %r in this skeleton" % name)

def transform(pos, M):
    return [[M[r][0]*p[0] + M[r][1]*p[1] + M[r][2]*p[2] + M[r][3] for r in range(3)] for p in pos]

def read_head_obj(path):
    """The head dump has positions, UVs and polygons but no normals."""
    pos, uv, polys = [], [], []
    for ln in open(path):
        if ln.startswith('v '):    pos.append([float(x) for x in ln.split()[1:4]])
        elif ln.startswith('vt '):
            u, v = (float(x) for x in ln.split()[1:3])
            uv.append([u, 1.0 - v])   # OBJ vt counts up from the bottom, glTF TEXCOORD_0 down from the top
        elif ln.startswith('f '):  polys.append([int(p.split('/')[0]) - 1 for p in ln.split()[1:]])
    idx = []
    for c in polys:
        for k in range(1, len(c) - 1):
            idx += [c[0], c[k], c[k+1]]
    return pos, uv, idx

def face_normals(pos, idx):
    nrm = [[0.0, 0.0, 0.0] for _ in pos]
    for t in range(0, len(idx), 3):
        a, bb, c = (pos[idx[t]], pos[idx[t+1]], pos[idx[t+2]])
        u = [bb[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        for k in range(3):
            for i in range(3):
                nrm[idx[t+k]][i] += n[i]
    for n in nrm:
        L = math.sqrt(sum(c*c for c in n)) or 1.0
        n[0] /= L; n[1] /= L; n[2] /= L
    return nrm

#  CGameView::CreateLSHead puts the LifeStudio head into head-bone space with
#  move (0,-0.035,0), scale 0.014 and rotate (0,-pi/2,0); MakeMatrix reads that
#  middle angle as a rotation about Z, and CMSRConvert pushes the scale inside,
#  so the point order is scale, rotate, move -- then the bone.
HEAD_SCALE  = 0.014
HEAD_ROT_Z  = -math.pi / 2
HEAD_MOVE   = (0.0, -0.035, 0.0)

def head_to_bone_space(pos, W):
    c, s = math.cos(HEAD_ROT_Z), math.sin(HEAD_ROT_Z)
    out = []
    for p in pos:
        x, y, z = (p[0]*HEAD_SCALE, p[1]*HEAD_SCALE, p[2]*HEAD_SCALE)
        v = (c*x - s*y + HEAD_MOVE[0], s*x + c*y + HEAD_MOVE[1], z + HEAD_MOVE[2])
        out.append([W[r][0]*v[0] + W[r][1]*v[1] + W[r][2]*v[2] + W[r][3] for r in range(3)])
    return out

def material(name, texture_index, alpha_mode):
    m = {"name": name, "pbrMetallicRoughness": {
             "baseColorTexture": {"index": texture_index},
             "metallicFactor": 0.0, "roughnessFactor": 0.9}}
    if alpha_mode == "MASK":
        m["alphaMode"] = "MASK"; m["alphaCutoff"] = 0.5; m["doubleSided"] = True
    elif alpha_mode == "BLEND":
        m["alphaMode"] = "BLEND"; m["doubleSided"] = True
    return m

def build(parts, bones, binds, out_gltf, head=None, gear=()):
    base = os.path.splitext(os.path.basename(out_gltf))[0]
    bin_name = base + ".bin"
    b = Buf()
    njoints = len(binds)
    meshes, materials, images, textures = [], [], [], []

    prims, groups = [], []
    for name, obj, wts, png, amode in parts:
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
        materials.append(material(name, len(textures) - 1, amode))
        prims.append({"attributes": {"POSITION": a_pos, "NORMAL": a_nrm,
                                     "TEXCOORD_0": a_uv, "JOINTS_0": a_j, "WEIGHTS_0": a_w},
                      "indices": a_i, "material": len(materials) - 1})
        groups.append((name, pos, nrm, uv, idx, png))
    if head:
        head_obj, head_png, head_bone, head_alpha = head
        bone_index = next(i for i, bo in enumerate(bones) if bo["name"] == head_bone)
        if bone_index >= njoints:
            raise SystemExit("head bone %r is not a skinned joint" % head_bone)
        hpos, huv, hidx = read_head_obj(head_obj)
        hpos = head_to_bone_space(hpos, bone_world(bones)[bone_index])
        hnrm = face_normals(hpos, hidx)
        J = [[bone_index, 0, 0, 0]] * len(hpos)
        W = [[1.0, 0.0, 0.0, 0.0]] * len(hpos)
        vp = b.add(struct.pack('<%df' % (len(hpos)*3), *[c for p in hpos for c in p]), 34962)
        vn = b.add(struct.pack('<%df' % (len(hnrm)*3), *[c for p in hnrm for c in p]), 34962)
        vt = b.add(struct.pack('<%df' % (len(huv)*2),  *[c for p in huv  for c in p]), 34962)
        vj = b.add(struct.pack('<%dH' % (len(J)*4),    *[c for p in J    for c in p]), 34962)
        vw = b.add(struct.pack('<%df' % (len(W)*4),    *[c for p in W    for c in p]), 34962)
        vi = b.add(struct.pack('<%dI' % len(hidx), *hidx), 34963)
        mn = [min(p[i] for p in hpos) for i in range(3)]
        mx = [max(p[i] for p in hpos) for i in range(3)]
        images.append({"uri": os.path.basename(head_png)})
        textures.append({"source": len(images) - 1})
        materials.append(material("Head", len(textures) - 1, head_alpha))
        prims.append({"attributes": {
                          "POSITION":   b.acc(vp, 5126, len(hpos), "VEC3", mn, mx),
                          "NORMAL":     b.acc(vn, 5126, len(hnrm), "VEC3"),
                          "TEXCOORD_0": b.acc(vt, 5126, len(huv),  "VEC2"),
                          "JOINTS_0":   b.acc(vj, 5123, len(J),    "VEC4"),
                          "WEIGHTS_0":  b.acc(vw, 5126, len(W),    "VEC4")},
                      "indices": b.acc(vi, 5125, len(hidx), "SCALAR"),
                      "material": len(materials) - 1})
        groups.append(("Head", hpos, hnrm, huv, hidx, head_png))
    meshes.append({"name": base, "primitives": prims})

    #  Equipment is not skinned.  CSetRender::AddMesh draws each bound mesh
    #  with CreateMesh under a CAddBoneFilter, so its vertices are already in
    #  the bone's own space; in glTF that is a plain mesh node parented to the
    #  bone's node, which also works for the sockets that carry no weights and
    #  so are not joints of the skin.
    gear_nodes = []
    for name, bone, gparts in gear:
        gprims = []
        for pname, obj, png, amode in gparts:
            pos, nrm, uv, idx = read_obj(obj)
            if not nrm:
                nrm = face_normals(pos, idx)
            vp = b.add(struct.pack('<%df' % (len(pos)*3), *[c for p in pos for c in p]), 34962)
            vn = b.add(struct.pack('<%df' % (len(nrm)*3), *[c for p in nrm for c in p]), 34962)
            vt = b.add(struct.pack('<%df' % (len(uv)*2),  *[c for p in uv  for c in p]), 34962)
            vi = b.add(struct.pack('<%dI' % len(idx), *idx), 34963)
            mn = [min(p[i] for p in pos) for i in range(3)]
            mx = [max(p[i] for p in pos) for i in range(3)]
            images.append({"uri": os.path.basename(png)})
            textures.append({"source": len(images) - 1})
            materials.append(material(pname, len(textures) - 1, amode))
            gprims.append({"attributes": {"POSITION": b.acc(vp, 5126, len(pos), "VEC3", mn, mx),
                                          "NORMAL":   b.acc(vn, 5126, len(nrm), "VEC3"),
                                          "TEXCOORD_0": b.acc(vt, 5126, len(uv), "VEC2")},
                           "indices": b.acc(vi, 5125, len(idx), "SCALAR"),
                           "material": len(materials) - 1})
            groups.append((pname, transform(pos, bone_world(bones)[bone_index_of(bones, bone)]),
                           nrm, uv, idx, png))
        meshes.append({"name": name, "primitives": gprims})
        gear_nodes.append((name, bone, len(meshes) - 1))

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
    for name, bone, mesh_id in gear_nodes:
        nodes.append({"name": name, "mesh": mesh_id})
        nodes[bone_index_of(bones, bone)].setdefault("children", []).append(len(nodes) - 1)
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
    return groups

def write_obj(groups, out_obj):
    """The same pieces as one Wavefront OBJ, one group per material."""
    base = os.path.splitext(os.path.basename(out_obj))[0]
    mtl = base + ".mtl"
    o = open(out_obj, "w")
    m = open(os.path.join(os.path.dirname(out_obj) or ".", mtl), "w")
    o.write("# Silent Storm character, %d parts\nmtllib %s\n" % (len(groups), mtl))
    v0 = 1
    for name, pos, nrm, uv, idx, png in groups:
        m.write("newmtl %s\nKd 1 1 1\nmap_Kd %s\n\n" % (name, os.path.basename(png)))
        o.write("g %s\nusemtl %s\n" % (name, name))
        for p in pos: o.write("v %.6f %.6f %.6f\n" % tuple(p))
        for t in uv:  o.write("vt %.6f %.6f\n" % (t[0], 1.0 - t[1]))
        for n in nrm: o.write("vn %.6f %.6f %.6f\n" % tuple(n))
        for t in range(0, len(idx), 3):
            c = [idx[t] + v0, idx[t+1] + v0, idx[t+2] + v0]
            if nrm:
                o.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (c[0],c[0],c[0], c[1],c[1],c[1], c[2],c[2],c[2]))
            else:
                o.write("f %d/%d %d/%d %d/%d\n" % (c[0],c[0], c[1],c[1], c[2],c[2]))
        v0 += len(pos)
    o.close(); m.close()
    print(f"{out_obj}: {sum(len(g[1]) for g in groups)} vertices in {len(groups)} groups")


# -- manifest-driven export --------------------------------------------------
#  A5_EXPORT_UNIT writes a flat list of resource ids, because getting from a
#  character's name to its meshes is five levels of database pointer:
#
#      name  <UserName>
#      body  <geometry> <skeleton> <tex0..3>
#      head  <head record> <texture>
#      gear  <bone> <geometry> <skeleton> <tex0..3>
#  CMaterial::EAlpha, in declaration order.
ALPHA_MODES = ["OPAQUE", "MASK", "BLEND", "BLEND", "OPAQUE", "MASK", "BLEND", "OPAQUE", "BLEND"]

def tex(token):
    """A manifest texture is "<texture id>:<CMaterial::EAlpha>"."""
    nID, _, nAlpha = token.partition(':')
    mode = ALPHA_MODES[int(nAlpha)] if nAlpha and int(nAlpha) < len(ALPHA_MODES) else "OPAQUE"
    return (int(nID), mode)

def read_manifest(path):
    m = {"name": os.path.splitext(os.path.basename(path))[0], "head": None, "gear": []}
    for ln in open(path):
        f = ln.split()
        if not f or f[0].startswith('#'):
            continue
        if f[0] == 'name':   m["name"] = f[1]
        elif f[0] == 'body': m["body"] = (int(f[1]), int(f[2]), [tex(x) for x in f[3:7]])
        elif f[0] == 'head': m["head"] = (int(f[1]), tex(f[2]))
        elif f[0] == 'gear': m["gear"].append((f[1], int(f[2]), int(f[3]), [tex(x) for x in f[4:8]]))
    return m

PART_NAMES = ['Body', 'Face', 'Legs', 'Extra']

def export_unit(complete, manifest, out_gltf, binary='silentstorm_mac', tmp=None):
    import subprocess, tempfile, shutil
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import mmp2png
    tmp = tmp or tempfile.mkdtemp()
    outdir = os.path.dirname(os.path.abspath(out_gltf))
    base = os.path.splitext(os.path.basename(out_gltf))[0]

    def run(**env):
        subprocess.run([binary], env=dict(os.environ, **env), capture_output=True)

    def texture(t, name):
        """Decode one MMP texture next to the glTF, and return its file name."""
        nID = t[0] if t else 0
        if not nID:
            return None
        png = "%s_%s.png" % (base, name)
        mmp2png.convert(os.path.join(complete, 'Textures', str(nID)), os.path.join(outdir, png))
        return png

    def geometry(nGeom, texIDs, prefix):
        """Every part of one geometry that exists, with its material's texture."""
        out = []
        for p in range(4):
            src = os.path.join(complete, 'Geometries', str(nGeom + (p << 16)))
            if not os.path.exists(src):
                continue
            stem = os.path.join(tmp, '%s_%d' % (prefix, p))
            run(A5_EXPORT_GEOM=src, A5_EXPORT_OUT=stem)
            name = '%s%s' % (prefix, PART_NAMES[p] if prefix == 'Hanke' else '')
            name = PART_NAMES[p] if prefix == 'body' else '%s%d' % (prefix, p)
            t = texIDs[p] if p < len(texIDs) else (0, "OPAQUE")
            out.append((name, stem + '.obj', stem + '.weights.txt', texture(t, name), t[1]))
        return out

    nGeom, nSkel, texIDs = manifest["body"]
    parts = [g for g in geometry(nGeom, texIDs, 'body') if g[3]]

    sk = os.path.join(tmp, 'skel.txt'); bd = os.path.join(tmp, 'binds.txt')
    run(A5_EXPORT_SKELETON=os.path.join(complete, 'Skeletons', str(nSkel)), A5_EXPORT_OUT=sk)
    run(A5_EXPORT_BINDS=os.path.join(complete, 'Binds', str(nGeom)), A5_EXPORT_OUT=bd)

    head = None
    if manifest["head"]:
        nHead, nTex = manifest["head"]
        hobj = os.path.join(tmp, 'head.obj')
        run(A5_EXPORT_HEAD=os.path.join(complete, 'Heads', str(nHead)), A5_EXPORT_OUT=hobj)
        if os.path.exists(hobj):
            head = (hobj, texture(nTex, 'Head'), 'Head', nTex[1])

    gear = []
    for bone, nG, _nS, gtex in manifest["gear"]:
        gparts = [(n, o, png, mode) for n, o, w, png, mode in geometry(nG, gtex, bone) if png]
        if gparts:
            gear.append((bone, bone, gparts))

    bones = read_skeleton(sk)
    groups = build(parts, bones, read_binds(bd), out_gltf, head=head, gear=gear)
    return groups

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='build a rigged glTF character from the game data')
    ap.add_argument('--complete', required=True, help='the Complete/ directory')
    ap.add_argument('--manifest', required=True,
                    help='a unit manifest, from A5_EXPORT_UNIT=<UserName>')
    ap.add_argument('--out', required=True, help='the .gltf to write')
    ap.add_argument('--obj', help='also write the merged mesh as Wavefront OBJ')
    a = ap.parse_args()
    groups = export_unit(a.complete, read_manifest(a.manifest), a.out,
                         binary=os.environ.get('A5_BIN', 'silentstorm_mac'))
    if a.obj:
        write_obj(groups, a.obj)
