"""A snake's clips made by laying its spine along a body curve: a slither wave, a raised front (rearing), an S coil in
the raised part, a roll about its length. Its joints keep their lengths along the body and turn with it, so the
skinned mesh follows. For a rig whose spine is one chain of joints from the head to the tail (the CC0 pit viper's
head_51, v1_50 ... v51_0), its own animations lacking an idle, a strike, a death.

The curve, in game space (x across, y up, z forward), from the head (arc length 0) back to the tail (the body's
length L): at arc length s the body points along heading phi (about y, 0 = +z) and pitch theta (up):
  phi(s)   = wave * sin(2 pi s / (wavelength L) - phase) * (1 - w(s)) + coil * sin(2 pi s / (rise_len L)) * w(s)
  theta(s) = rise * w(s) + head where s is within the first joint
  w(s)     = (1 + cos(pi s / (rise_len L))) / 2 before rise_len L, else 0 (the raised part, easing into the ground)
Its back (the tail) stays at its resting height and the point anchor L (half way by default) where it rests
sideways: a wave travels along a body that stays in place, as the game moves creatures itself. roll turns the body
about its length (a death on its back).

Clips: keys [{"t": seconds, param: value, ...}], the first at t = 0; a parameter a key leaves out is 0 (rise_len 0.3,
wavelength 0.5, anchor 0.5); between keys every parameter eases (smoothstep). travel: how many waves pass along the
body over the clip (the phase advances 2 pi travel; a whole number closes a loop).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import joint_animation  # noqa: E402
import new_skeleton  # noqa: E402
import own_skeleton as osk  # noqa: E402

PARAMS = {"wave": 0.0, "wavelength": 0.5, "phase": 0.0, "rise": 0.0, "rise_len": 0.3, "head": 0.0, "coil": 0.0,
          "roll": 0.0, "anchor": 0.5}
STEPS = 900  # curve samples along the body (and half as far again)


def body(sk, head):
    """The spine: the chain of joints from `head` down through each joint's one child (the tail is the last), their
    arc lengths along the rest pose, its length, the rest world poses and the rest frames along it."""
    if head not in sk["names"]:
        raise ValueError("no joint %r in the skeleton" % head)
    children = {}
    for j, p in enumerate(sk["parents"]):
        if p is not None:
            children.setdefault(p, []).append(j)
    joints = [sk["names"].index(head)]
    while len(children.get(joints[-1], [])) == 1:
        joints.append(children[joints[-1]][0])
    if len(joints) < 3:
        raise ValueError("the chain from %r has fewer than three joints" % head)
    world = osk.world(sk["rest"], sk["parents"])
    pos = [world[j][1] for j in joints]
    s = [0.0]
    for a, b in zip(pos, pos[1:]):
        s.append(s[-1] + math.dist(a, b))
    frames = []
    for i in range(len(joints)):
        a, b = (pos[0], pos[1]) if i == 0 else (pos[i - 1], pos[i])
        frames.append(_frame(tuple(x - y for x, y in zip(a, b)), 0.0))
    return {"joints": joints, "s": s, "length": s[-1], "rest": world, "frames": frames}


def _norm(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _frame(forward, roll_deg):
    """The quaternion of the frame whose z is forward, y up as near the world's up as forward allows, rolled about
    forward by roll_deg."""
    f = _norm(forward)
    r = _norm(_cross((0.0, 1.0, 0.0), f))
    u = _cross(f, r)
    if roll_deg:
        a = math.radians(roll_deg)
        c, s_ = math.cos(a), math.sin(a)
        r, u = (tuple(c * x + s_ * y for x, y in zip(r, u)), tuple(-s_ * x + c * y for x, y in zip(r, u)))
    m = [[r[0], u[0], f[0]], [r[1], u[1], f[1]], [r[2], u[2], f[2]]]
    return new_skeleton._quat(m)


def _curve(length, p, first, extent=1.5):
    """[(arc length, point, direction)] from the head (at the origin) back along the body and on to extent times its
    length (bends shorten the body along the curve)."""
    lam = max(1e-6, p["wavelength"] * length)
    lr = max(1e-6, p["rise_len"] * length)
    out, pt = [], (0.0, 0.0, 0.0)
    ds = length * extent / STEPS
    for k in range(STEPS + 1):
        s = k * ds
        w = (1 + math.cos(math.pi * s / lr)) / 2 if s < lr else 0.0
        phi = (math.radians(p["wave"]) * math.sin(2 * math.pi * s / lam - p["phase"]) * (1 - w)
               + math.radians(p["coil"]) * math.sin(2 * math.pi * s / lr) * w)
        theta = math.radians(p["rise"]) * w + (math.radians(p["head"]) if s < first else 0.0)
        d = (math.cos(theta) * math.sin(phi), math.sin(theta), math.cos(theta) * math.cos(phi))
        out.append((s, pt, d))
        pt = tuple(a - ds * b for a, b in zip(pt, d))
    return out


def _at(curve, s):
    k = min(len(curve) - 2, max(0, int(s / curve[1][0]) if curve[1][0] > 0 else 0))
    (s0, p0, d0), (s1, p1, d1) = curve[k], curve[k + 1]
    x = (s - s0) / (s1 - s0) if s1 > s0 else 0.0
    return tuple(a + (b - a) * x for a, b in zip(p0, p1)), _norm(tuple(a + (b - a) * x for a, b in zip(d0, d1)))


def _chain(curve, bones):
    """[(point, forward)] per joint: the head at the curve's start, each next joint on the curve at its bone's length
    (a straight line) from the one before; forward points from a joint to the one before it (the head: from the next
    joint to it), as the rest frames do."""
    pts, s = [curve[0][1]], 0.0
    for bone in bones:
        prev = pts[-1]
        lo, hi = s, s + bone * 1.5 + 1e-9
        for _ in range(60):
            mid = (lo + hi) / 2
            if math.dist(_at(curve, mid)[0], prev) < bone:
                lo = mid
            else:
                hi = mid
        s = (lo + hi) / 2
        pts.append(_at(curve, s)[0])
    out = []
    for i, pt in enumerate(pts):
        a, b = (pts[0], pts[1]) if i == 0 else (pts[i - 1], pts[i])
        out.append((pt, _norm(tuple(x - y for x, y in zip(a, b)))))
    return out


def pose(sk, bd, params):
    """The skeleton's pose (local rotation, position, scale per joint) with its spine on the curve of params."""
    p = dict(PARAMS)
    for k, v in params.items():
        if k not in PARAMS:
            raise ValueError("no snake parameter %r (%s)" % (k, ", ".join(sorted(PARAMS))))
        p[k] = float(v)
    joints, s, length, rest = bd["joints"], bd["s"], bd["length"], bd["rest"]
    curve = _curve(length, p, s[1])
    placed = _chain(curve, [b - a for a, b in zip(s, s[1:])])
    # the tail at its resting height, the anchor point where it rests sideways
    tail_y = rest[joints[-1]][1][1] - placed[-1][0][1]
    sa = p["anchor"] * length
    rest_a = _rest_point(bd, sa)
    got_a, _ = _at(curve, sa)
    shift = (rest_a[0] - got_a[0], tail_y, rest_a[2] - got_a[2])
    world = [list(w) for w in rest]
    for i, j in enumerate(joints):
        pt, d = placed[i]
        q_new = _frame(d, p["roll"])
        delta = osk.qmul(q_new, osk.qconj(bd["frames"][i]))
        world[j] = [osk.qmul(delta, rest[j][0]), tuple(a + b for a, b in zip(pt, shift)), rest[j][2]]
    out = []
    for j, (q, t, sc) in enumerate(sk["rest"]):
        par = sk["parents"][j]
        if j not in joints or par is None:
            out.append((q, t, sc))
            continue
        pq, pp, ps = world[par]
        inv = osk.qconj(pq)
        local_t = osk.qrot(inv, tuple(a - b for a, b in zip(world[j][1], pp)))
        out.append((osk.qmul(inv, world[j][0]), tuple(v / ps[0] for v in local_t), sc))
    return out


def _rest_point(bd, s):
    xs = bd["s"]
    i = max(0, min(len(xs) - 2, max(k for k in range(len(xs)) if xs[k] <= s)))
    a, b = bd["rest"][bd["joints"][i]][1], bd["rest"][bd["joints"][i + 1]][1]
    x = (s - xs[i]) / (xs[i + 1] - xs[i]) if xs[i + 1] > xs[i] else 0.0
    return tuple(u + (v - u) * x for u, v in zip(a, b))


def clip(sk, head, keys, length, travel=0.0, loop=False, fps=60, hierarchy_hash=0):
    """A clip of `length` seconds from keys (module doc) on the spine from `head`."""
    if not keys or keys[0].get("t") != 0:
        raise ValueError("the first key is at t = 0")
    last = -1.0
    for k in keys:
        if float(k["t"]) <= last:
            raise ValueError("key times must rise")
        last = float(k["t"])
        for name in k:
            if name != "t" and name not in PARAMS:
                raise ValueError("no snake parameter %r (%s)" % (name, ", ".join(sorted(PARAMS))))
    bd = body(sk, head)
    n = max(1, int(round(length * fps)))
    poses = []
    for f in range(n + 1):
        t = length * f / n
        i = max(i for i in range(len(keys)) if float(keys[i]["t"]) <= t)
        a = keys[i]
        if i == len(keys) - 1:
            params = {k: float(a.get(k, PARAMS[k])) for k in PARAMS}
        else:
            b = keys[i + 1]
            x = (t - float(a["t"])) / (float(b["t"]) - float(a["t"]))
            e = x * x * (3 - 2 * x)
            params = {k: float(a.get(k, PARAMS[k])) * (1 - e) + float(b.get(k, PARAMS[k])) * e for k in PARAMS}
        params["phase"] += 2 * math.pi * travel * t / length
        poses.append(pose(sk, bd, params))
    return joint_animation.encode(poses, sk["rest"], fps=fps, loop=loop, hierarchy_hash=hierarchy_hash)
