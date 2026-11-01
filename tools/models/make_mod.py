"""Make an EML content mod that adds new RenderModels and creature templates that use them.

  python tools/models/make_mod.py pack.json

pack.json:
  {
    "id": "my_beasts", "title": "My beasts", "out": "content/my_beasts",
    "creatures": [
      {
        "name": "beast_wolf",                         # debugName of the new model; also the Lua module names
        "glb": "beast.glb",                           # a skinned glb (bones named jNNN...), or
        "game_dump": "<survey>/dump/pet_dog_01",      # a dumped game model (<stem>.json + .render.bin): round trip
        "hierarchy": "<survey>/hierarchies/<guid>.json",   # the skeleton the glb is skinned to (needed with glb)
        "source_model_guid": "...",                   # the game model whose materials and other fields are kept
        "source_model_json": "<survey>/dump/pet_dog_01.json",  # a dumped record with the same material list
        "source_template": "Animal_Wildlife_Dog_04_black_AG2",  # behaviour, stats, animations
        "template_name": "Beast_Wolf",
        "model_guid": "...", "template_guid": "...",  # FIXED forever once shared (saves refer to them)
        "faction": "PlayerAlly",                      # optional: another Faction
        "texture": {"albedo_dir": "tex/wolf", "slot": 0, "flat_size": 128},  # optional: our albedo (albedo_mipNN.png)
        "template_add": [{"type": "ScaleComponent", "value": {"uniformScale": 1.4}}],  # optional: whole components
        "template_set": {"DamageMod": {"dataStorage[2]": {"mul": 1.5}}}  # optional: fields (a value or {"mul": n})
      }
    ]
  }
Relative paths are relative to pack.json. Missing GUIDs are generated and written back into pack.json.
Component names without "::" are keen::ecs:: ones. template_set field paths are names and 0-based [n] indices
(dataStorage[2] is the third value). template_add replaces a component the template has already.
"""
import json
import os
import re
import struct
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import encode  # noqa: E402
import gltf_in  # noqa: E402
import own_creature  # noqa: E402
import rendermodel as rm  # noqa: E402

NAME = re.compile(r"^[a-z][a-z0-9_]{0,47}$")


def lua_value(v, indent=""):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return repr(v) if v == v else "0.0"
    if isinstance(v, str):
        return '"%s"' % v.replace("\\", "\\\\").replace('"', '\\"')
    if isinstance(v, (list, tuple)):
        if not v:
            return "{}"
        if all(not isinstance(x, (dict, list, tuple)) for x in v):
            return "{" + ", ".join(lua_value(x) for x in v) + "}"
        inner = indent + "  "
        return "{\n" + ",\n".join(inner + lua_value(x, inner) for x in v) + "\n" + indent + "}"
    if isinstance(v, dict):
        return "{" + ", ".join("%s = %s" % (k, lua_value(x, indent)) for k, x in v.items()) + "}"
    raise TypeError(type(v))


def lua_bytes(data):
    """Bytes as a Lua string literal: printable ASCII as is, everything else as a 3-digit decimal escape."""
    out = []
    for c in data:
        if 32 <= c < 127 and c not in (34, 92):
            out.append(chr(c))
        else:
            out.append("\\%03d" % c)
    return '"' + "".join(out) + '"'


MOD_LUA = r'''-- %(title)s: an EML data patch made by EMF's tools/models/make_mod.py.
-- It adds new render models and creature templates that use them. Spawn one with EMF: spawn <template guid>
-- (the GUIDs are in creatures.lua). The GUIDs are fixed: keep them when you change a model, or saves and other
-- mods lose track of it.
if not loader.features.patch then
	return
end

local function log(...) print("%(id)s:", ...) end

local function fill(list, items)
	while #list > 0 do table.remove(list) end
	for _, item in ipairs(items) do table.insert(list, item) end
end

-- textures: a level's bytes in a block format; levels under 4 KB go into the mip tail (as in the game's own files)
local function block_bytes(format)
	if format:find("BC1") or format:find("BC4") then return 8 end
	return 16
end

-- block encoders work on 4x4 blocks: a level under 4x4 (2x2, 1x1) is encoded as a 4x4 image that repeats its pixels,
-- which gives the one block the game stores for such a level
local function at_least_4x4(img)
	if img.width >= 4 and img.height >= 4 then return img end
	local big = image.create(4, 4)
	for y = 0, 3 do
		for x = 0, 3 do
			local r, g, b, a = img:get_pixel(x %% img.width, y %% img.height)
			big:set_pixel(x, y, r, g, b, a)
		end
	end
	return big
end

local function write_texture(tex, levels)
	local format = tostring(tex.format)
	local chain, tail = buffer.create(), buffer.create()
	local starts, sizes, total = {}, {}, 0
	for i, img in ipairs(levels) do
		local enc = image.encode_texture(at_least_4x4(img), tex.format):to_string()
		starts[i], sizes[i] = total, #enc
		total = total + #enc
		chain:write_string(enc)
		if #enc < 4096 then tail:write_string(enc) end
	end
	local ranges = {}
	for i = 1, #levels do
		if sizes[i] >= 4096 then table.insert(ranges, { start = starts[i], count = total - starts[i] }) end
	end
	tex.width = levels[1].width
	tex.height = levels[1].height
	tex.levelCount = #levels
	tex.content.hash = game.assets.create_content(chain)
	local t = game.assets.create_content(tail)
	tex.mipTailContent.hash = t
	tex.mipTailContent.sortKey = t.hash2
	fill(tex.mipChainContentRanges, ranges)
	return format
end

-- the average colour of a game texture: its 1x1 level, the last block of its mip tail
local function average_color(tex)
	local data = game.assets.get_content(game.guid.from_content_hash(tex.mipTailContent.hash)):read_data():to_string()
	local block = data:sub(-block_bytes(tostring(tex.format)))
	local img = image.decode_texture(buffer.wrap(block), tex.format, 4, 4, 0)  -- one block decodes as 4x4
	return img:get_pixel(0, 0)
end

local function flat_levels(size, r, g, b, a)
	local levels, w = {}, size
	while true do
		local img = image.create(w, w)
		for y = 0, w - 1 do
			for x = 0, w - 1 do img:set_pixel(x, y, r, g, b, a) end
		end
		table.insert(levels, img)
		if w == 1 then break end
		w = w // 2
	end
	return levels
end

-- a copy of the model's material in one slot: our albedo (PNG levels), the other maps flat at the source's average
local function make_material(c, m)
	local slot = m.materials[c.texture.slot + 1]
	local srcmat = game.assets.get_resource(tostring(slot.material), "keen::RenderMaterialResource")
	assert(srcmat, "the source material was not found")
	local mat = game.assets.create_resource(srcmat.data, "keen::RenderMaterialResource", c.texture.material_guid, 0).data
	local done = {}
	for _, im in ipairs(mat.images) do
		local name = tostring(im.texture.debugName)
		if name:find("albedo") then
			local levels = {}
			for _, png in ipairs(require(c.name .. "_albedo")) do table.insert(levels, image.decode(buffer.wrap(png), "png")) end
			done[#done + 1] = "albedo " .. write_texture(im.texture, levels)
		else
			local r, g, b, a = average_color(im.texture)
			done[#done + 1] = string.format("flat %%s(%%d,%%d,%%d) %%s", name:match("_([a-z]+)_map") or "?", r, g, b,
				write_texture(im.texture, flat_levels(c.texture.flat_size, r, g, b, a)))
		end
	end
	slot.material = c.texture.material_guid
	return table.concat(done, "; ")
end

local templates = {}
for _, t in ipairs(game.assets.get_resources_by_type("keen::ecs::TemplateResource")) do
	templates[t.data.name] = t
end
local registry = game.assets.get_resources_by_type("keen::GuidRegistryResource")[1]
local registered = {}
if registry then
	for _, g in ipairs(registry.data.guids) do registered[tostring(g)] = true end
end

for _, c in ipairs(require("creatures")) do
	local data = require(c.name .. "_data")

	-- 1) the model: a copy of the source model's record, then our geometry
	local src = game.assets.get_resource(c.source_model_guid, "keen::RenderModel")
	assert(src, c.name .. ": the source model was not found (the game changed?)")
	local m = game.assets.create_resource(src.data, "keen::RenderModel", c.model_guid, 0).data
	m.debugName = data.debugName
	m.boundingBox = data.boundingBox
	m.boundingSphere = data.boundingSphere
	m.positionScale = data.positionScale
	m.positionOffset = data.positionOffset
	if data.wholeModelRadius > m.wholeModelRadius then m.wholeModelRadius = data.wholeModelRadius end
	fill(m.meshlets, {})
	fill(m.meshes, data.meshes)
	fill(m.lods, data.lods)
	fill(m.skinningMap, data.skinningMap)
	fill(m.blendShapes, {})
	m.blendShapeMask = 0
	-- the source's cloth simulation (the mage's robe, the knight's cape) runs over the source's own vertices: never on
	-- our geometry (seen in game: the lich and the bone golem glitched as they walked)
	if m.cloth ~= nil then
		local ok, err = pcall(function() m.cloth = nil end)
		if not ok then log(c.name .. ": the source's cloth could not be removed: " .. tostring(err)) end
	end
	m.jointCount = data.jointCount
	m.indexData = data.indexData
	m.meshletData = data.meshletData
	m.vertexData = data.vertexData
	m.skinningData = data.skinningData
	m.blendShapeData = data.blendShapeData
	m.renderData.hash = game.assets.create_content(buffer.wrap(require(c.name .. "_blob")))
	if c.texture then
		local ok, result = pcall(make_material, c, m)
		log(c.name .. (ok and (": material " .. c.texture.material_guid .. ": " .. result)
			or (": texture failed, keeping the source material: " .. tostring(result))))
	end

	-- 1b) a skeleton of its own (own_proportions): a copy of the game skeleton fitted to the creature, the game
	-- creature's clips retargeted to it, and a copy of its animation graph that plays them
	if c.skeleton then
		local sk = require(c.name .. "_skeleton")
		local src_h = game.assets.get_resource(sk.source, "keen::ModelHierarchyResource")
		assert(src_h, c.name .. ": the game skeleton " .. sk.source .. " was not found (the game changed?)")
		local h = game.assets.create_resource(src_h.data, "keen::ModelHierarchyResource", sk.guid, 0).data
		h.hierarchyId = sk.guid
		local n = #sk.joints
		for i, j in ipairs(sk.joints) do
			local d = h.defaultPoseJoints[i]
			d.orientation.x, d.orientation.y, d.orientation.z, d.orientation.w = j.q[1], j.q[2], j.q[3], j.q[4]
			d.position.x, d.position.y, d.position.z = j.t[1], j.t[2], j.t[3]
			h.defaultPose.data[i] = { data = j.qb }
			h.defaultPose.data[n + i] = { data = j.tb }
			local b = h.skinToJointMatrices[i]
			for _, col in ipairs({ "x", "y", "z" }) do
				for _, k in ipairs({ "x", "y", "z" }) do b.rot[col][k] = j.ibm.rot[col][k] end
			end
			b.pos.x, b.pos.y, b.pos.z = j.ibm.pos.x, j.ibm.pos.y, j.ibm.pos.z
		end
		m.hierarchy = sk.guid
		local map = {}
		for _, cl in ipairs(require(c.name .. "_clips")) do
			local src_c = game.assets.get_resource(cl.source, "keen::JointAnimation")
			assert(src_c, c.name .. ": the game clip " .. cl.source .. " was not found (the game changed?)")
			local a = game.assets.create_resource(src_c.data, "keen::JointAnimation", cl.guid, 0).data
			a.positionFactor, a.positionTangent, a.scaleFactor = cl.positionFactor, cl.positionTangent, cl.scaleFactor
			a.fps, a.frameCount, a.flags = cl.fps, cl.frameCount, cl.flags
			fill(a.headers, cl.headers)
			local units = {}
			for i = 1, #cl.bytes, 16 do units[#units + 1] = { data = { cl.bytes:byte(i, i + 15) } } end
			fill(a.data, units)
			map[cl.source] = cl.guid
		end
		local GRAPH = "keen::anim_graph::runtime_graph::AnimationGraphResource2_0"
		local src_g = game.assets.get_resource(c.animation.graph, GRAPH)
		assert(src_g, c.name .. ": the game graph " .. c.animation.graph .. " was not found (the game changed?)")
		local g = game.assets.create_resource(src_g.data, GRAPH, c.animation.graph_guid, 0).data
		g.hierarchy = sk.guid
		local ours, left = 0, 0
		local function swap(obj, field)
			local old = tostring(obj[field])
			if map[old] then
				obj[field] = map[old]
				ours = ours + 1
			elseif old ~= "nil" and old ~= "00000000-0000-0000-0000-000000000000" then
				left = left + 1
			end
		end
		for _, node in ipairs(g.nodeDefinitions) do
			local t = tostring(node.type)
			if t:find("SampleAnimationClip", 1, true) then
				for _, f in ipairs({ "animation", "animationGenderAlternative", "additiveReferenceAnimation",
					"additiveReferenceAnimationGenderAlternative" }) do swap(node.value, f) end
			elseif t:find("BlendSpace", 1, true) then
				swap(node.value, "additiveReferenceAnimation")
				for _, it in ipairs(node.value.items) do
					swap(it, "animation")
					swap(it, "animationGenderAlternative")
				end
			end
		end
		log(string.format("%%s: skeleton %%s (%%d joints), %%d clips, graph %%s: %%d clip references ours, %%d left as the game's",
			c.name, sk.guid, n, #require(c.name .. "_clips"), c.animation.graph_guid, ours, left))
	end

	-- 2) the creature: a copy of the source template, pointing at the new model
	local source = templates[c.source_template]
	assert(source, c.name .. ": the source template " .. c.source_template .. " was not found (the game changed?)")
	local tpl = game.assets.create_resource(source.data, "keen::ecs::TemplateResource", c.template_guid, 0).data
	tpl.name = c.template_name
	local set = false
	for _, comp in ipairs(tpl.components) do
		if comp.type == "keen::ecs::ModelResource" then
			comp.value.model = c.model_guid
			set = true
		end
	end
	assert(set, c.name .. ": the source template has no ModelResource")
	if c.animation then
		local found = false
		for _, comp in ipairs(tpl.components) do
			if comp.type == "keen::ecs::AnimationGraphResourceComponent2" then
				comp.value.animationGraph = c.animation.graph_guid
				found = true
			end
		end
		assert(found, c.name .. ": the source template has no AnimationGraphResourceComponent2")
	end
	if c.faction then
		for _, comp in ipairs(tpl.components) do
			if comp.type == "keen::ecs::Faction" then comp.value.faction = c.faction end
		end
	end

	-- 3) extra components (a whole component; one the template has already is replaced), then field edits
	for _, add in ipairs(c.template_add or {}) do
		local replaced = false
		for i, comp in ipairs(tpl.components) do
			if comp.type == add.type then
				tpl.components[i] = add
				replaced = true
			end
		end
		if not replaced then table.insert(tpl.components, add) end
	end
	for _, e in ipairs(c.template_set or {}) do
		local done = false
		for _, comp in ipairs(tpl.components) do
			if comp.type == e.component then
				local obj = comp.value
				for i = 1, #e.path - 1 do obj = obj[e.path[i]] end
				local key = e.path[#e.path]
				if e.mul then obj[key] = obj[key] * e.mul else obj[key] = e.value end
				done = true
			end
		end
		if not done then log(c.name .. ": the template has no " .. e.component .. " (the game changed?): not set") end
	end

	-- 4) the GUID registry: if the game lists the source template there, list ours too
	if registry and registered[tostring(source.guid)] then
		table.insert(registry.data.guids, c.template_guid)
	end
	log(string.format("added %%s: model %%s (%%d meshes), template %%s %%s", c.name, c.model_guid, #data.meshes,
		c.template_name, c.template_guid))
end
'''


SEGMENT = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)((?:\[\d+\])*)$")


def component_type(name):
    """"DamageMod" -> "keen::ecs::DamageMod"; a full type name stays as it is."""
    return name if "::" in name else "keen::ecs::" + name


def field_path(text):
    """"dataStorage[2]" -> ["dataStorage", 3]: dotted field names, 0-based [n] indices (as the game's docs write
    them) turned into Lua's 1-based ones."""
    path = []
    for part in text.split("."):
        m = SEGMENT.match(part)
        if not m:
            raise SystemExit("template_set field '%s': use names and [n] indices, like dataStorage[2] or a.b" % text)
        path.append(m.group(1))
        path.extend(int(i) + 1 for i in re.findall(r"\[(\d+)\]", m.group(2)))
    return path


def listing_entry(c):
    """What the generated mod.lua needs to know about one creature (creatures.lua)."""
    entry = {k: c[k] for k in ("name", "model_guid", "template_guid", "source_model_guid",
                               "source_template", "template_name")}
    if c.get("faction"):
        entry["faction"] = c["faction"]  # e.g. "PlayerAlly": the creature never attacks players
    if c.get("template_add"):
        entry["template_add"] = [{"type": component_type(a["type"]), "value": a["value"]} for a in c["template_add"]]
    if c.get("template_set"):
        entry["template_set"] = [set_entry(component_type(comp), field_path(f), v)
                                 for comp, fields in c["template_set"].items() for f, v in fields.items()]
    if c.get("own_proportions"):
        op = c["own_proportions"]
        missing = [k for k in OWN_KEYS if not op.get(k)]
        if missing:
            raise SystemExit("%s: own_proportions needs %s" % (c["name"], ", ".join(missing)))
        entry["skeleton"] = {"guid": op["skeleton_guid"]}
        entry["animation"] = {"graph": op["graph"], "graph_guid": op["graph_guid"]}
    return entry


OWN_KEYS = ("sidecar", "skeleton_guid", "reference_clip", "clips", "graph", "graph_guid")


def write_own(src, name, built):
    """<name>_skeleton.lua and <name>_clips.lua from own_creature.build: the skeleton copy's rest pose and inverse
    binds, and the retargeted clips."""
    joints = []
    for (q, t, _), ibm in zip(built["skeleton"]["rest"], built["skeleton"]["skinToJointMatrices"]):
        joints.append({"q": list(q), "t": list(t), "qb": list(struct.pack("<4f", *q)),
                       "tb": list(struct.pack("<4f", t[0], t[1], t[2], 0.0)), "ibm": ibm})
    with open(os.path.join(src, name + "_skeleton.lua"), "w", encoding="utf-8", newline="\n") as f:
        f.write("-- generated by tools/models/make_mod.py: the skeleton of %s (a copy of %s fitted to it)\nreturn %s\n"
                % (name, built["skeleton"]["source"], lua_value({"source": built["skeleton"]["source"],
                                                                 "guid": built["skeleton"]["guid"], "joints": joints})))
    parts = []
    for cl in built["clips"]:
        k = cl["clip"]
        raw = bytes(x for u in k["data"] for x in u["data"])
        head = {"source": cl["source"], "guid": cl["guid"], "positionFactor": k["positionFactor"],
                "positionTangent": k["positionTangent"], "scaleFactor": k["scaleFactor"], "fps": k["fps"],
                "frameCount": k["frameCount"], "flags": k["flags"], "headers": k["headers"]}
        parts.append(lua_value(head)[:-1] + ", bytes = " + lua_bytes(raw) + "}")
    with open(os.path.join(src, name + "_clips.lua"), "w", encoding="ascii", newline="\n") as f:
        f.write("-- generated by tools/models/make_mod.py: the clips of %s, retargeted from the game creature's\n"
                "return {\n%s\n}\n" % (name, ",\n".join(parts)))


def set_entry(component, path, v):
    """One template_set edit: a value, or {"mul": n} to multiply the template's own value (it follows the game when
    the game changes the source creature)."""
    if isinstance(v, dict):
        if set(v) != {"mul"} or isinstance(v["mul"], bool) or not isinstance(v["mul"], (int, float)):
            raise SystemExit("template_set %s: use a value or {\"mul\": number}, not %r" % (".".join(map(str, path)), v))
        return {"component": component, "path": path, "mul": v["mul"]}
    if not isinstance(v, (int, float, str, bool)):
        raise SystemExit("template_set %s: use a value or {\"mul\": number}, not %r" % (".".join(map(str, path)), v))
    return {"component": component, "path": path, "value": v}


def write_scripts(src, pack, listing):
    """creatures.lua and mod.lua into the mod's src folder."""
    with open(os.path.join(src, "creatures.lua"), "w", encoding="utf-8") as f:
        f.write("-- generated by tools/models/make_mod.py: the creatures this mod adds\nreturn " + lua_value(listing) + "\n")
    with open(os.path.join(src, "mod.lua"), "w", encoding="utf-8") as f:
        f.write(MOD_LUA % {"title": pack.get("title", pack["id"]), "id": pack["id"]})


GENERATED = ("_data.lua", "_blob.lua", "_albedo.lua", "_skeleton.lua", "_clips.lua")


def remove_stale(src, names):
    """Removes the generated files of creatures not in `names` from the mod's src folder (a creature taken out of the
    pack would otherwise ship its model unused)."""
    for f in os.listdir(src):
        for suffix in GENERATED:
            if f.endswith(suffix) and f[: -len(suffix)] not in names:
                os.remove(os.path.join(src, f))


def main():
    pack_path = os.path.abspath(sys.argv[1])
    base = os.path.dirname(pack_path)
    with open(pack_path, encoding="utf-8") as f:
        pack = json.load(f)

    def path(p):
        return p if os.path.isabs(p) else os.path.join(base, p)

    out = path(pack["out"])
    os.makedirs(os.path.join(out, "src"), exist_ok=True)
    remove_stale(os.path.join(out, "src"), {c["name"] for c in pack["creatures"]})
    changed = False
    listing = []
    for c in pack["creatures"]:
        if not NAME.match(c["name"]):
            raise SystemExit("creature name '%s': use lowercase letters, digits and _" % c["name"])
        for key in ("model_guid", "template_guid"):
            if not c.get(key):
                c[key] = str(uuid.uuid4())
                changed = True
        source = rm.load_record(path(c["source_model_json"]))
        if c.get("game_dump"):
            model = rm.load_record(path(c["game_dump"]) + ".json")
            with open(path(c["game_dump"]) + ".render.bin", "rb") as f:
                groups = encode.groups_from_model(model, f.read())
            palette = model["skinningMap"]
        else:
            groups, _ = gltf_in.load_groups(path(c["glb"]), rm.load_record(path(c["hierarchy"])),
                                            material_count=len(source["materials"]))
            palette = None
        blob, fields = encode.build(groups, palette=palette)
        ctr = fields["boundingSphere"]["center"]
        radius = max(((p[0] - ctr["x"]) ** 2 + (p[1] - ctr["y"]) ** 2 + (p[2] - ctr["z"]) ** 2) ** 0.5
                     for g in groups for p in g["pos"])
        data = dict(fields, debugName=c["name"], wholeModelRadius=radius)
        with open(os.path.join(out, "src", c["name"] + "_data.lua"), "w", encoding="utf-8") as f:
            f.write("-- generated by tools/models/make_mod.py: record fields of the model %s\nreturn %s\n"
                    % (c["name"], lua_value(data)))
        with open(os.path.join(out, "src", c["name"] + "_blob.lua"), "w", encoding="ascii", newline="\n") as f:
            f.write("-- generated by tools/models/make_mod.py: render data of %s (vertices, skin, indices), %d bytes\n"
                    "return %s\n" % (c["name"], len(blob), lua_bytes(blob)))
        entry = listing_entry(c)
        if c.get("own_proportions"):
            op = dict(c["own_proportions"])
            for k in ("sidecar", "reference_clip", "clips"):
                op[k] = path(op[k])
            built = own_creature.build(op, rm.load_record(path(c["hierarchy"])))
            write_own(os.path.join(out, "src"), c["name"], built)
            print("%s: own skeleton %s, %d clips retargeted" % (c["name"], op["skeleton_guid"], len(built["clips"])))
        if c.get("texture"):
            t = c["texture"]
            if not t.get("material_guid"):
                t["material_guid"] = str(uuid.uuid4())
                changed = True
            folder = path(t["albedo_dir"])
            pngs = sorted(f for f in os.listdir(folder) if re.match(r"^albedo_mip\d+\.png$", f))
            if not pngs:
                raise SystemExit("%s: no albedo_mipNN.png in %s" % (c["name"], folder))
            parts = []
            for f in pngs:
                with open(os.path.join(folder, f), "rb") as fh:
                    parts.append(lua_bytes(fh.read()))
            with open(os.path.join(out, "src", c["name"] + "_albedo.lua"), "w", encoding="ascii", newline="\n") as f:
                f.write("-- generated by tools/models/make_mod.py: albedo of %s, %d PNG levels (largest first)\n"
                        "return {\n%s\n}\n" % (c["name"], len(parts), ",\n".join(parts)))
            entry["texture"] = {"slot": int(t.get("slot", 1)), "material_guid": t["material_guid"],
                                "flat_size": int(t.get("flat_size", 128))}
        listing.append(entry)
        print("%s: %d meshes, %d bytes, template %s %s" % (c["name"], len(fields["meshes"]), len(blob),
                                                            c["template_name"], c["template_guid"]))
    write_scripts(os.path.join(out, "src"), pack, listing)
    with open(os.path.join(out, "mod.json"), "w", encoding="utf-8") as f:
        json.dump({"id": pack["id"], "name": pack.get("title", pack["id"]), "version": pack.get("version", "0.1.0"),
                   "capabilities": ["patch"],
                   "description": pack.get("description", "New creatures made with EMF tools/models."),
                   "authors": pack.get("authors", ["EMF contributors"])}, f, indent=2)
        f.write("\n")
    if changed:
        with open(pack_path, "w", encoding="utf-8") as f:
            json.dump(pack, f, indent=2)
            f.write("\n")
        print("new GUIDs were written into", pack_path, "- keep them")
    print("mod written to", out)


if __name__ == "__main__":
    main()
