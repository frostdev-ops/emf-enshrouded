"""glTF animations made into game clips (keen::JointAnimation) for a skeleton from new_skeleton.py.

A clip has a key at every frame at 60 fps (the rate of every game clip) from the animation's first key to its last:
N frames (the duration times the rate, rounded), the animation sampled at N + 1 evenly spaced times, so a duration
that is not a whole number of frames plays slightly faster or slower and still ends on its last key. Channels are
sampled as glTF defines them (LINEAR: linear, rotations by slerp; STEP: held; CUBICSPLINE: Hermite with the stored
tangents), nodes without a channel keep their own pose, and every frame is a pose of the skeleton
(new_skeleton.pose: each joint as in the file's world, in game space).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gltf_in  # noqa: E402
import joint_animation as ja  # noqa: E402
import new_skeleton as ns  # noqa: E402

PATHS = ("translation", "rotation", "scale")


def names(gltf):
    return [a.get("name", "animation%d" % i) for i, a in enumerate(gltf.get("animations", []))]


def _closed(channels):
    """Whether every channel ends on its first value (a loop exported with its closing frame)."""
    for _, path, interp, _, values in channels:
        a, b = (values[1], values[-2]) if interp == "CUBICSPLINE" else (values[0], values[-1])
        if path == "rotation":
            if 1 - abs(sum(x * y for x, y in zip(a, b))) > 1e-6:
                return False
        elif max(abs(x - y) for x, y in zip(a, b)) > 1e-6:
            return False
    return True


def _step(channels):
    """The animation's key step: the most common time between keys."""
    steps = {}
    for c in channels:
        for t0, t1 in zip(c[3], c[3][1:]):
            k = round(t1 - t0, 5)
            steps[k] = steps.get(k, 0) + 1
    return max(steps, key=lambda k: (steps[k], k)) if steps else 0.0


def _travel(gltf, channels, node):
    """The node's travel along the ground over the animation (glTF world, y up: x and z only), from its first key to
    its last."""
    start, end = _span(channels)
    w0 = ns.node_worlds(gltf, _frame(channels, start))
    w1 = ns.node_worlds(gltf, _frame(channels, end))
    return (w1[node][0][3] - w0[node][0][3], 0.0, w1[node][2][3] - w0[node][2][3]), w0


def _take_travel_out(gltf, channels, node):
    """The channels with the node's travel along the ground taken out of its translation keys, evenly over the
    animation (in its parent's space at the first key: a parent that holds still); its bobbing and swaying stay."""
    d, w0 = _travel(gltf, channels, node)
    start, end = _span(channels)
    length = end - start
    parent = ns.parents_of(gltf).get(node)
    inv = gltf_in.mat_inv_affine(w0[parent]) if parent is not None else [[float(r == c) for c in range(4)]
                                                                         for r in range(4)]
    local = tuple(sum(inv[r][c] * d[c] for c in range(3)) for r in range(3))
    out, found = [], False
    for nd, path, interp, times, values in channels:
        if nd == node and path == "translation" and length > 0:
            found = True
            values = [tuple(v) for v in values]
            if interp == "CUBICSPLINE":
                new = []
                for k, t in enumerate(times):
                    a, v, b = values[3 * k:3 * k + 3]
                    f = (t - start) / length
                    new += [tuple(x - y / length for x, y in zip(a, local)),
                            tuple(x - y * f for x, y in zip(v, local)),
                            tuple(x - y / length for x, y in zip(b, local))]
                values = new
            else:
                values = [tuple(x - y * (t - start) / length for x, y in zip(v, local)) for t, v in zip(times, values)]
        out.append((nd, path, interp, times, values))
    if not found and any(abs(c) > 1e-9 for c in d):
        raise ValueError("node %s travels but has no translation keys of its own: name the joint that carries the "
                         "travel" % gltf["nodes"][node].get("name", node))
    return out


def _node_of(sk, joint):
    if joint not in sk["names"]:
        raise KeyError("no joint %r in the skeleton" % joint)
    return sk["nodes"][sk["names"].index(joint)]


def travel_speed(gltf, binc, sk, name, joint):
    """How fast the joint travels along the ground in the animation (metres a second, times the skeleton's scale):
    the speed the game must move the creature at for its feet not to slide, once the travel is taken out."""
    channels = _read(gltf, binc, name)
    start, end = _span(channels)
    d, _ = _travel(gltf, channels, _node_of(sk, joint))
    return math.hypot(d[0], d[2]) * sk["scale"] / (end - start) if end > start else 0.0


def _channels(gltf, binc, name, loop=False, in_place=None):
    """[(node, path, interpolation, times, values)] of the animation called name; in_place: a node whose travel
    along the ground is taken out (_take_travel_out). For a loop exported without its closing frame (its last key is
    not its first again), every channel gets one more key a key step after the last, with its first value: the loop
    then wraps over that step."""
    out = _read(gltf, binc, name)
    if in_place is not None:
        out = _take_travel_out(gltf, out, in_place)
    if loop and not _closed(out):
        start, end = _span(out)
        end += _step(out)
        out = [(node, path, interp, list(times) + [end], list(values) + list(values[:3] if interp == "CUBICSPLINE"
                                                                              else values[:1]))
               for node, path, interp, times, values in out]
    return out


def _read(gltf, binc, name):
    for i, a in enumerate(gltf.get("animations", [])):
        if a.get("name", "animation%d" % i) == name:
            break
    else:
        raise KeyError("no animation %r (the file has %s)" % (name, ", ".join(names(gltf)) or "none"))
    out = []
    for ch in a["channels"]:
        target = ch["target"]
        if target.get("path") not in PATHS or "node" not in target:
            continue
        s = a["samplers"][ch["sampler"]]
        times = gltf_in.accessor(gltf, binc, s["input"])
        values = gltf_in.accessor(gltf, binc, s["output"])
        out.append((target["node"], target["path"], s.get("interpolation", "LINEAR"), times, values))
    if not out:
        raise ValueError("animation %r moves no node" % name)
    return out


def _span(channels):
    return min(c[3][0] for c in channels), max(c[3][-1] for c in channels)


def duration(gltf, binc, name, loop=False):
    start, end = _span(_channels(gltf, binc, name, loop))
    return end - start


def frame_count(gltf, binc, name, fps=60, loop=False):
    return max(1, int(round(duration(gltf, binc, name, loop) * fps)))


def _slerp(a, b, s):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0:
        b, d = tuple(-x for x in b), -d
    if d > 0.9995:
        q = tuple(x + (y - x) * s for x, y in zip(a, b))
    else:
        th = math.acos(min(1.0, d))
        wa, wb = math.sin((1 - s) * th) / math.sin(th), math.sin(s * th) / math.sin(th)
        q = tuple(wa * x + wb * y for x, y in zip(a, b))
    n = math.sqrt(sum(x * x for x in q))
    return tuple(x / n for x in q)


def _sample(interp, times, values, t, rotation):
    cubic = interp == "CUBICSPLINE"

    def value(k):
        return tuple(values[3 * k + 1] if cubic else values[k])

    if t <= times[0] or len(times) == 1:
        v = value(0)
    elif t >= times[-1]:
        v = value(len(times) - 1)
    else:
        k = max(i for i in range(len(times) - 1) if times[i] <= t)
        d = times[k + 1] - times[k]
        s = (t - times[k]) / d if d > 0 else 0.0
        if interp == "STEP":
            v = value(k)
        elif cubic:
            s2, s3 = s * s, s * s * s
            out_tan, in_tan = values[3 * k + 2], values[3 * (k + 1)]
            v = tuple((2 * s3 - 3 * s2 + 1) * a + (s3 - 2 * s2 + s) * d * b + (-2 * s3 + 3 * s2) * c
                      + (s3 - s2) * d * e for a, b, c, e in zip(value(k), out_tan, value(k + 1), in_tan))
        elif rotation:
            return _slerp(value(k), value(k + 1), s)
        else:
            v = tuple(a + (b - a) * s for a, b in zip(value(k), value(k + 1)))
    if rotation:
        n = math.sqrt(sum(x * x for x in v)) or 1.0
        v = tuple(x / n for x in v)
    return v


def _frame(channels, t):
    pose = {}
    for node, path, interp, times, values in channels:
        pose.setdefault(node, {})[path] = _sample(interp, times, values, t, path == "rotation")
    return pose


def pose_at(gltf, binc, sk, name, t, loop=False, in_place=None):
    """The skeleton's pose (per joint local rotation, position, scale, game space) t seconds into the animation;
    in_place: a joint whose travel along the ground is taken out (clip)."""
    channels = _channels(gltf, binc, name, loop, _node_of(sk, in_place) if in_place else None)
    start, _ = _span(channels)
    return ns.pose(sk, ns.node_worlds(gltf, _frame(channels, start + t)), gltf)


def clip(gltf, binc, sk, name, hierarchy_hash, loop=False, fps=60, speed=1.0, in_place=None):
    """The animation as a game clip for the skeleton (hierarchy_hash: new_skeleton.clip_hash of its hierarchy); a
    loop ends on its first pose; speed above 1 plays it faster (the same poses in fewer frames). in_place: the joint
    that carries the creature forward in an animation with root motion (a walk that travels): its travel along the
    ground is taken out, the game moving creatures itself (travel_speed: how fast it went)."""
    channels = _channels(gltf, binc, name, loop, _node_of(sk, in_place) if in_place else None)
    start, end = _span(channels)
    n = max(1, int(round((end - start) / float(speed) * fps)))
    poses = [ns.pose(sk, ns.node_worlds(gltf, _frame(channels, start + (end - start) * f / n)), gltf)
             for f in range(n + 1)]
    return ja.encode(poses, sk["rest"], fps=fps, loop=loop, hierarchy_hash=hierarchy_hash)
