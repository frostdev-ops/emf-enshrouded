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


def reproportion(pose, old_rest, new_rest):
    """A clip's pose on the copy: positions move with their joint's rest position; rotations and scales stay."""
    out = []
    for (q, t, s), (_, t0, _), (_, t1, _) in zip(pose, old_rest, new_rest):
        out.append((q, tuple(a + (c - b) for a, b, c in zip(t, t0, t1)), s))
    return out
