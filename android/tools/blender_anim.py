"""
blender_anim.py -- play a Silent Storm animation on a character imported from
gltf_export.py, inside Blender.

    A5_EXPORT_SKELETON=<Complete/Skeletons/8>   A5_EXPORT_OUT=skel.txt  silentstorm_mac
    A5_EXPORT_ANIM=<Complete/Animations/289>    A5_EXPORT_OUT=anim.txt  silentstorm_mac

then, in Blender's text editor or via its Python console:

    import blender_anim
    blender_anim.apply("skel.txt", "anim.txt", armature="Sniper_Armature")

What the engine does, and so what this has to reproduce:

  * An animation drives a SUBSET of the skeleton -- SAnimationHeader::indices
    says which bone each animated bone is -- and CAnimator::GetBoneFrame leaves
    everything else at the skeleton's rest pose.
  * A rotation-only bone keeps its rest offset and its rest parent; only the
    root carries a position, and only an add-bone carries a parent, which is
    how a weapon moves between the hand and the belt inside one clip.
  * The equipment sockets are not joints of the skin, so Blender imports them
    as Empties.  They are keyed by world matrix rather than by bone, which
    also copes with the animation re-parenting them.

The forward travel of the root is removed by default so a locomotion cycle
loops on the spot; pass in_place=False to keep it.
"""
import bpy
from mathutils import Quaternion, Vector, Matrix


def read_skeleton(path):
    rest, order = {}, []
    for ln in open(path):
        if ln.startswith('#'):
            continue
        f = ln.split()
        i = int(f[0])
        rest[i] = (int(f[1]), f[2], [float(x) for x in f[3:6]], [float(x) for x in f[6:10]])
        order.append(i)
    return rest, order


def read_animation(path):
    anim = {}
    for ln in open(path):
        if not ln.startswith("k "):
            continue
        p = ln.split()
        anim.setdefault(int(p[1]), {})[int(p[2])] = (
            int(p[3]), [float(x) for x in p[4:7]], [float(x) for x in p[7:11]])
    return anim


def _q(v):                       # engine (x, y, z, w) -> Blender Quaternion
    return Quaternion((v[3], v[0], v[1], v[2]))


def _resolve(i, keys, rest):
    """(parent, position, rotation) for one bone in one frame."""
    par, _name, rpos, rrot = rest[i]
    k = keys.get(i)
    if k is None:
        return par, list(rpos), rrot
    kpar, pos, rot = k
    if kpar == -2:               # rotation only: rest offset, rest parent
        return par, list(rpos), rot
    if kpar >= 0:                # add-bone: the animation re-parents it
        return kpar, list(pos), rot
    return par, list(pos), rot   # -1 root, -3 scaled


def _fk(frame, drift, anim, rest, order):
    keys, out = anim[frame], {}

    def world(i):
        if i in out:
            return out[i]
        par, pos, rot = _resolve(i, keys, rest)
        if i == 0:
            pos = [pos[0] - drift, pos[1], pos[2]]
        m = Matrix.Translation(Vector(pos)) @ _q(rot).to_matrix().to_4x4()
        out[i] = (world(par) @ m) if par >= 0 else m
        return out[i]

    for i in order:
        world(i)
    return out


def apply(skeleton_txt, animation_txt, armature=None, in_place=True, play=True):
    rest, order = read_skeleton(skeleton_txt)
    anim = read_animation(animation_txt)
    frames = sorted(anim)
    arm = bpy.data.objects[armature] if armature else next(
        o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
    Ainv = arm.matrix_world.inverted()
    sc = bpy.context.scene

    x0 = anim[frames[0]][0][1][0]
    x1 = anim[frames[-1]][0][1][0]
    span = float(frames[-1] - frames[0]) or 1.0

    gear = [o for o in sc.objects if o.type == 'EMPTY']
    by_name = {rest[i][1]: i for i in rest}
    for o in gear:
        o.rotation_mode = 'QUATERNION'
    bpy.context.view_layer.objects.active = arm

    for fr in frames:
        sc.frame_set(fr)
        drift = (x1 - x0) * (fr - frames[0]) / span if in_place else 0.0
        W = _fk(fr, drift, anim, rest, order)
        bpy.ops.object.mode_set(mode='POSE')
        for i in order:
            _par, name, rpos, rrot = rest[i]
            pb = arm.pose.bones.get(name)
            if pb is None:
                continue
            _p, pos, rot = _resolve(i, anim[fr], rest)
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = _q(rrot).inverted() @ _q(rot)
            pb.keyframe_insert("rotation_quaternion", frame=fr)
            if i == 0:
                d = Vector((pos[0] - drift - rpos[0], pos[1] - rpos[1], pos[2] - rpos[2]))
                pb.location = (arm.data.bones[name].matrix_local.to_3x3().inverted()
                               @ (Ainv.to_3x3() @ d))
                pb.keyframe_insert("location", frame=fr)
        bpy.ops.object.mode_set(mode='OBJECT')
        bpy.context.view_layer.update()
        for o in gear:
            i = by_name.get(o.name)
            if i is None:
                continue
            o.matrix_world = W[i]
            o.keyframe_insert("location", frame=fr)
            o.keyframe_insert("rotation_quaternion", frame=fr)

    #  The engine lerps between keys, so do the same.  Blender 4.4+ keeps
    #  f-curves under action.layers[].strips[].channelbags[]; action.fcurves
    #  is gone.
    for act in bpy.data.actions:
        bags = ([cb for layer in act.layers for strip in layer.strips for cb in strip.channelbags]
                if hasattr(act, "layers") else [act])
        for bag in bags:
            for fc in bag.fcurves:
                for kp in fc.keyframe_points:
                    kp.interpolation = 'LINEAR'

    sc.frame_start, sc.frame_end = frames[0], frames[-1]
    sc.render.fps = 30
    sc.frame_set(frames[0])
    if play:
        for w in bpy.context.window_manager.windows:
            for a in w.screen.areas:
                if a.type == 'VIEW_3D':
                    with bpy.context.temp_override(window=w, area=a):
                        bpy.ops.screen.animation_play()
                    return len(frames)
    return len(frames)
