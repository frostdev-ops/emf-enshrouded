"""A copy of a game skeleton with a creature's own proportions (fit_to_game_skeleton.py --own-proportions).

The game skeleton (keen::ModelHierarchyResource, as EML shows it) keeps its joints, names and rest rotations; only
the joints' rest positions change, so the game's clips (rotations) bend the creature's limbs as they bent the game
creature's. A rest pose is per joint (rotation x, y, z, w; position; scale), local to the parent, in game space
(left-handed, y up). Inverse binds (skinToJointMatrices) are the inverse of each joint's world matrix in the rest
pose: a rotation stored as its three columns (rot.x, rot.y, rot.z) and a position.
"""


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


def qrot(q, v):
    return qmul(qmul(q, (v[0], v[1], v[2], 0.0)), qconj(q))[:3]


def qmat(q):
    """The 3x3 rotation matrix of a unit quaternion (rows)."""
    x, y, z, w = q
    return ((1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
            (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
            (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)))


def rest_of(hier):
    """(rest, parents) of a skeleton as EML shows it: rest per joint (rotation, position, scale); parent index or
    None at a root."""
    n = hier["jointCount"]
    rest = []
    for j in hier["defaultPoseJoints"][:n]:
        o, p, s = j["orientation"], j["position"], j["scale"]
        rest.append(((o["x"], o["y"], o["z"], o["w"]), (p["x"], p["y"], p["z"]), (s["x"], s["y"], s["z"])))
    parents = [None if p == 65535 else p for p in hier["jointParentIndices"][:n]]
    return rest, parents


def world(rest, parents):
    """Per joint its world (rotation, position, scale) in the rest pose (parents come before their children)."""
    out = []
    for j, (q, t, s) in enumerate(rest):
        p = parents[j]
        if p is None:
            out.append((q, t, s))
            continue
        pq, pt, ps = out[p]
        local = qrot(pq, tuple(a * b for a, b in zip(t, ps)))
        out.append((qmul(pq, q), tuple(a + b for a, b in zip(pt, local)), tuple(a * b for a, b in zip(ps, s))))
    return out


def rest_from_world(rest, parents, positions):
    """The rest pose whose joints stand at the given world positions, with the rest rotations and scales kept."""
    w = world(rest, parents)
    out = []
    for j, (q, t, s) in enumerate(rest):
        p = parents[j]
        if p is None:
            out.append((q, tuple(positions[j]), s))
            continue
        pq, _, ps = w[p]
        d = tuple(a - b for a, b in zip(positions[j], positions[p]))
        local = qrot(qconj(pq), d)
        out.append((q, tuple(a / b for a, b in zip(local, ps)), s))
    return out


def inverse_binds(rest, parents):
    """skinToJointMatrices for a rest pose: per joint the inverse of its world matrix."""
    out = []
    for q, p, s in world(rest, parents):
        m = qmat(q)
        inv = [[m[c][r] / s[r] for c in range(3)] for r in range(3)]  # (R S)^-1 = S^-1 R^T
        pos = [-sum(inv[r][c] * p[c] for c in range(3)) for r in range(3)]
        out.append({"rot": {col: {k: inv[ki][ci] for ki, k in enumerate("xyz")} for ci, col in enumerate("xyz")},
                    "pos": {k: pos[ki] for ki, k in enumerate("xyz")}})
    return out


def blender_to_game(p):
    """A position in Blender after gamegltf.py's glTF (forward -y, up +z) in game space (y up, x mirrored)."""
    return (-p[0], p[2], -p[1])


def _turn(u, v):
    """The shortest rotation taking direction u to direction v (identity for one shorter than 0.1 mm)."""
    lu = sum(a * a for a in u) ** 0.5
    lv = sum(a * a for a in v) ** 0.5
    if lu < 1e-4 or lv < 1e-4:  # a bone shorter than 0.1 mm has no direction (rounding noise)
        return (0.0, 0.0, 0.0, 1.0)
    u = tuple(a / lu for a in u)
    v = tuple(a / lv for a in v)
    d = sum(a * b for a, b in zip(u, v))
    if d < -1 + 1e-12:  # opposite: half a turn about any axis across u
        axis = (0.0, -u[2], u[1]) if abs(u[0]) < 0.9 else (-u[2], 0.0, u[0])
        la = sum(a * a for a in axis) ** 0.5
        return tuple(a / la for a in axis) + (0.0,)
    c = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    q = c + (1.0 + d,)
    n = sum(a * a for a in q) ** 0.5
    return tuple(a / n for a in q)


def align_rest(rest, parents, positions, main):
    """The rest pose whose joints stand at `positions` (world), with every bone of the main chains (`main`: the
    spine and the legs, quad_landmarks.main_bones) turned to point at its main child's new place, so a clip's
    rotations bend the new limbs the way they bent the old ones. A joint off the main chains, or with no single main
    child (a paw with several toes), turns with its parent; a root keeps its rotation (the creature's own frame)."""
    n = len(rest)
    kids = [[] for _ in range(n)]
    for j, p in enumerate(parents):
        if p is not None:
            kids[p].append(j)
    size = [1] * n
    for j in reversed(range(n)):
        for c in kids[j]:
            size[j] += size[c]
    src = world(rest, parents)
    target = [None] * n
    for j in range(n):
        p = parents[j]
        if p is None:
            target[j] = src[j][0]
            continue
        cands = [c for c in kids[j] if c in main] if j in main else []
        best = max((size[c] for c in cands), default=0)
        top = [c for c in cands if size[c] == best]
        if len(top) == 1:
            c = top[0]
            u = tuple(a - b for a, b in zip(src[c][1], src[j][1]))
            v = tuple(a - b for a, b in zip(positions[c], positions[j]))
            target[j] = qmul(_turn(u, v), src[j][0])
        else:
            follow = qmul(target[p], qconj(src[p][0]))  # the parent's turn
            target[j] = qmul(follow, src[j][0])
    out = []
    for j, (q, t, s) in enumerate(rest):
        p = parents[j]
        if p is None:
            out.append((q, tuple(positions[j]), s))
            continue
        local_q = qmul(qconj(target[p]), target[j])
        d = tuple(a - b for a, b in zip(positions[j], positions[p]))
        local_t = qrot(qconj(target[p]), d)
        out.append((local_q, tuple(a / b for a, b in zip(local_t, src[p][2])), s))
    return out


def retarget(pose, rest_src, rest_tgt, parents):
    """A clip's pose on another rest pose of the same skeleton: every joint turns in the world as it turned away from
    its rest on the source; positions a clip sets move by their offset from the source rest, carried into the
    target's frame; scales stay."""
    ws, wt, wa = world(rest_src, parents), world(rest_tgt, parents), world(pose, parents)
    tq = []
    for j in range(len(pose)):
        delta = qmul(wa[j][0], qconj(ws[j][0]))
        tq.append(qmul(delta, wt[j][0]))
    out = []
    for j, (q, t, s) in enumerate(pose):
        p = parents[j]
        d = tuple(a - b for a, b in zip(t, rest_src[j][1]))
        if p is None:
            out.append((tq[j], tuple(a + b for a, b in zip(rest_tgt[j][1], d)), s))
            continue
        d_world = qrot(wa[p][0], d)
        d_tgt = qrot(qconj(tq[p]), d_world)
        out.append((qmul(qconj(tq[p]), tq[j]), tuple(a + b for a, b in zip(rest_tgt[j][1], d_tgt)), s))
    return out


def lengths_rest(rest, parents, positions):
    """The copy's default pose: the game creature's posture (rest rotations and bone directions kept) with the bone
    lengths of the creature (the distance from each joint to its parent at `positions`). Clips set the game
    creature's rotations, so every limb points where the game creature's points, at its own length; the inverse
    binds come from the creature's own pose (align_rest), so its mesh follows."""
    out = []
    for j, (q, t, s) in enumerate(rest):
        p = parents[j]
        if p is None:
            out.append((q, t, s))
            continue
        length = sum((a - b) ** 2 for a, b in zip(positions[j], positions[p])) ** 0.5
        old = sum(a * a for a in t) ** 0.5
        out.append((q, tuple(a * length / old for a in t) if old > 1e-9 else t, s))
    return out


def scale_positions(pose, old_rest, new_rest):
    """A clip's pose on the copy's default pose (lengths_rest): positions the clip sets scale by their bone's length
    ratio; rotations and scales stay."""
    out = []
    for (q, t, s), (_, t0, _), (_, t1, _) in zip(pose, old_rest, new_rest):
        l0 = sum(a * a for a in t0) ** 0.5
        k = (sum(a * a for a in t1) ** 0.5) / l0 if l0 > 1e-9 else 1.0
        out.append((q, tuple(a * k for a in t), s))
    return out


def reproportion(pose, old_rest, new_rest):
    """A clip's pose on the copy: positions move with their joint's rest position; rotations and scales stay."""
    out = []
    for (q, t, s), (_, t0, _), (_, t1, _) in zip(pose, old_rest, new_rest):
        out.append((q, tuple(a + (c - b) for a, b, c in zip(t, t0, t1)), s))
    return out
