"""Texture coordinates into a region of a game texture atlas, for a prop that wears the game's own materials (a prop's
model names the game's material; nothing of the game's art is copied).

A face is sorted into a material by the colour of the model's own texture under it (classify), then projected along
its main axis in repeats of a length in metres, moved by whole repeats so that its centre falls in the first repeat,
and mapped into the atlas region (face_uvs). The margin keeps a face that reaches past the repeat's edge inside the
region. Space is glTF's (y up, metres); texture v grows downwards, as in glTF and the game.
"""
import colorsys
import math


def classify(rgb, rules, default):
    """The material of a face whose texture colour is `rgb` (0..1 each): the first rule it meets, else `default`.
    rules: [{"material": n, "min_saturation": s, "min_value": v}] (HSV saturation and value)."""
    _, s, v = colorsys.rgb_to_hsv(*rgb)
    for r in rules:
        if s >= r.get("min_saturation", 0.0) and v >= r.get("min_value", 0.0):
            return r["material"]
    return default


def face_uvs(points, normal, repeat, region, margin=0.25):
    """The texture coordinates (u, v) of a face's corners `points` (metres) with face normal `normal`: the projection
    along the normal's main axis (x: z and y; y: x and z; z: x and y; up is up in the texture), in repeats of `repeat`
    metres (a number, or (across, down) for a region that is not square in metres), moved by whole repeats so its
    centre lies in [0, 1), then [-margin, 1 + margin] mapped onto region = (u0, v0, u1, v1)."""
    axis = max(range(3), key=lambda k: abs(normal[k]))
    if axis == 0:
        flat = [(p[2], -p[1]) for p in points]
    elif axis == 1:
        flat = [(p[0], p[2]) for p in points]
    else:
        flat = [(p[0], -p[1]) for p in points]
    ru, rv = (repeat, repeat) if isinstance(repeat, (int, float)) else repeat
    t = [(a / ru, b / rv) for a, b in flat]
    cu = math.floor(sum(a for a, _ in t) / len(t))
    cv = math.floor(sum(b for _, b in t) / len(t))
    u0, v0, u1, v1 = region
    span = 1.0 + 2.0 * margin
    return [(u0 + (a - cu + margin) / span * (u1 - u0), v0 + (b - cv + margin) / span * (v1 - v0)) for a, b in t]
