"""A creature on a skeleton of its own (make_mod's "new_skeleton"): a skinned, animated glb becomes a game skeleton
(new_skeleton.py), the model on it, its animations game clips (gltf_clips.py), and a copy of a game creature's
animation graph plays them.

The pack names, per creature:
  scale          the size: the file's metres times this
  skeleton_guid  the skeleton's GUID (fixed forever; the clips' GUIDs follow from it and their names)
  graph, graph_guid   the game's animation graph and its copy's GUID
  clips          our clips: {name: {"animation": the glb's animation, "loop": true for a looping clip}}
  roles          {game clip GUID: our clip name}: which of our clips plays where the graph plays that game clip
  default_role   our clip for every other game clip the graph plays: every clip reference of the copy is ours (a
                 game clip carries the game skeleton's hash and cannot play on ours)
  slots          {slot: our joint name}: the graph's attachment slots (gamegltf.SLOTS: Root, Head, Chest, Pelvis,
                 FootL, ...); a slot not named points at the root
"""
import os
import sys
import tempfile
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamegltf  # noqa: E402
import gltf_clips  # noqa: E402
import gltf_in  # noqa: E402
import new_skeleton  # noqa: E402

KEYS = ("scale", "skeleton_guid", "graph", "graph_guid", "clips", "roles", "default_role", "slots")


def clip_guid(skeleton_guid, name):
    return str(uuid.uuid5(uuid.UUID(skeleton_guid), "clip:" + name))


def check(cfg, creature):
    """The pack's mistakes, as SystemExit: missing keys, a role or the default naming a clip the pack has not."""
    missing = [k for k in KEYS if k not in cfg or cfg[k] in (None, "")]
    if missing:
        raise SystemExit("%s: new_skeleton needs %s" % (creature, ", ".join(missing)))
    clips = cfg["clips"]
    for game, ours in list(cfg["roles"].items()) + [("default_role", cfg["default_role"])]:
        if ours not in clips:
            raise SystemExit("%s: new_skeleton role %s names the clip %r, which is not in clips (%s)"
                             % (creature, game, ours, ", ".join(sorted(clips))))


def roles(cfg):
    """({game clip GUID: our clip GUID}, the default's GUID)."""
    g = cfg["skeleton_guid"]
    return ({game: clip_guid(g, ours) for game, ours in cfg["roles"].items()}, clip_guid(g, cfg["default_role"]))


def build(cfg, glb_path, creature="creature"):
    """{sk, hierarchy, clips: [{name, guid, clip}] by name, slots: 36 joint indices}."""
    check(cfg, creature)
    gltf, binc = gltf_in.read_glb(glb_path)
    sk = new_skeleton.build(gltf, binc, scale=float(cfg["scale"]))
    hier = new_skeleton.hierarchy(sk, cfg["skeleton_guid"])
    slots = [0] * len(gamegltf.SLOTS)
    for slot, joint in cfg["slots"].items():
        if slot not in gamegltf.SLOTS:
            raise SystemExit("%s: no slot %r (the slots: %s)" % (creature, slot, ", ".join(gamegltf.SLOTS)))
        if joint not in sk["names"]:
            raise SystemExit("%s: slot %s names %r, which is not a joint of the skeleton" % (creature, slot, joint))
        slots[gamegltf.SLOTS.index(slot)] = sk["names"].index(joint)
    have = gltf_clips.names(gltf)
    out = []
    for name in sorted(cfg["clips"]):
        spec = cfg["clips"][name]
        if spec.get("animation") not in have:
            raise SystemExit("%s: clip %s plays %r, which the glb does not have (%s)"
                             % (creature, name, spec.get("animation"), ", ".join(have)))
        clip = gltf_clips.clip(gltf, binc, sk, spec["animation"], new_skeleton.clip_hash(hier),
                               loop=bool(spec.get("loop")))
        out.append({"name": name, "guid": clip_guid(cfg["skeleton_guid"], name), "clip": clip})
    return {"sk": sk, "hierarchy": hier, "clips": out, "slots": slots}


def model_groups(glb_path, built, material_count=None, warn=print):
    """The model on the skeleton, as gltf_in.load_groups gives it (game space, our joint indices)."""
    gltf, binc = gltf_in.read_glb(glb_path)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "model.glb")
        new_skeleton.write_glb(gltf, binc, built["sk"], path)
        return gltf_in.load_groups(path, built["hierarchy"], material_count=material_count, warn=warn)
