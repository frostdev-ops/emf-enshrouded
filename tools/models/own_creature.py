"""A creature on its own proportions, for make_mod.py: a copy of the game skeleton fitted to the creature, and every
clip of the game creature's animation graph retargeted to it.

The creature's pack entry has "own_proportions":
  sidecar          the fitter's sidecar (blender/fit_to_game_skeleton.py --own-proportions): every game joint moved
                   into the creature's shape
  skeleton_guid    the skeleton copy's GUID (fixed forever, like the model's)
  reference_clip   a game clip whose first frame is the game creature standing (its idle): motions are taken
                   relative to it and put onto the creature standing in its own shape
  clips            a glob of the game clips to retarget (EML dumps of keen::JointAnimation, named <owner>_<guid>.json):
                   every clip the graph can play
  graph            the game creature's animation graph GUID; graph_guid: its copy's GUID

The skeleton copy keeps the game skeleton's joints and names (so its hash, and the clips' link to it, stay) and every
bone's frame (the rest rotations); only the joints move, to where the creature stands in its own shape, and its
inverse binds are that pose's. A clip frame becomes: each joint turned in the world as the game creature's turned away
from its standing pose (own_skeleton.retarget), which does not depend on the bones' frames. Turning frames to point
along the creature's limbs (own_skeleton.align_rest) is not used: the game graph's own nodes turn bones about their
axes, and a turned frame tilts them (seen in game: a pelvis turned 90 degrees, from a bone of zero length, sat the
panther upright).
"""
import glob
import json
import os
import uuid

import joint_animation as ja
import own_skeleton as osk


def build(cfg, hier):
    """{"bind": rest pose, "skeleton": {source, guid, rest, skinToJointMatrices}, "clips": [{source, guid, clip}]}.
    cfg: the pack entry's own_proportions (paths already resolved); hier: the game skeleton as EML shows it."""
    rest, parents = osk.rest_of(hier)
    with open(cfg["sidecar"], encoding="utf-8") as f:
        joints = json.load(f)["joints"]
    if len(joints) != len(rest):
        raise SystemExit("own_proportions: the sidecar has %d joints, the skeleton %d" % (len(joints), len(rest)))
    # only the joints move: every bone keeps the game skeleton's frame, so the game graph's own nodes (foot IK,
    # leaning, aiming) turn bones about the axes they expect; the motion does not depend on the frames (retarget
    # works in the world)
    bind = osk.rest_from_world(rest, parents, [osk.blender_to_game(j["own"]) for j in joints])
    with open(cfg["reference_clip"], encoding="utf-8") as f:
        ref = ja.sample(json.load(f), 0, rest)
    clips = []
    for path in sorted(glob.glob(cfg["clips"])):
        with open(path, encoding="utf-8") as f:
            clip = json.load(f)
        if clip["jointCount"] != len(rest):
            raise SystemExit("own_proportions: %s has %d joints, the skeleton %d" % (path, clip["jointCount"], len(rest)))
        source = os.path.splitext(os.path.basename(path))[0].split("_", 1)[-1]
        n = clip["frameCount"]
        poses = [ja.sample(clip, f, rest) for f in range(n)]
        poses.append(ja.sample(clip, n, rest) if clip["flags"] == 1 else ja.sample_end(clip, rest))
        poses = [osk.retarget(p, ref, bind, parents) for p in poses]
        enc = ja.encode(poses, bind, fps=clip["fps"], loop=clip["flags"] == 1, hierarchy_hash=clip["hierarchyHash"])
        clips.append({"source": source, "guid": str(uuid.uuid5(uuid.UUID(cfg["skeleton_guid"]), source)), "clip": enc})
    if not clips:
        raise SystemExit("own_proportions: no clips match %s" % cfg["clips"])
    skeleton = {"source": hier["hierarchyId"], "guid": cfg["skeleton_guid"], "rest": bind,
                "skinToJointMatrices": osk.inverse_binds(bind, parents)}
    return {"bind": bind, "skeleton": skeleton, "clips": clips}
