-- Unit tests for tools/models/packs/beastmaster_stable.lua (the beasts pack's craftable Stable), run by
-- test_beast_stable.py with a Lua 5.4 interpreter: lua beast_stable_spec.lua <path of beastmaster_stable.lua>.
-- A stand-in for EMF's content helper records what the script asks of it; a stand-in for EML's game API holds the
-- large animal bed's render model.
local script = assert(arg[1], "usage: lua beast_stable_spec.lua <beastmaster_stable.lua>")

local BED_ITEM = "a8076625-0529-415a-bf32-a9e0619e8ce5"   -- Animal_Farm_Housing_Sleeping_Bed_YAK_Sanddigger
local BED_MODEL = "7dd9139a-fba7-4e60-8b5a-fa853c250ef7"  -- animalprops_farm_animal_sleep_large_01
local STABLE_ITEM = "a84e96fa-f27a-48b8-9815-89093bd142b4"
local STABLE_TEMPLATE = "96a057d5-a1d9-4fc1-b313-9b923e9302fe"
local STABLE_MODEL = "1a49170a-b3d2-498d-9062-8946f6469613"
local STABLE_RECIPE = "0410f9da-8a00-4b5e-a537-33b52d43ccd9"
local WOOD_LOGS = "3f4093ae-75ed-4e5d-bd0b-784ba83b88f5"
local PLANT_FIBRE = "ff00a9f6-fd7e-4933-9c6b-33c36223cd2b"
local ANIMAL_FUR = "66ce7d4c-ce66-4ad4-800a-ab37f7e604a2"

local failures, ran = 0, 0
local function test(name, fn)
	ran = ran + 1
	local ok, err = pcall(fn)
	if not ok then
		failures = failures + 1
		io.stderr:write("FAIL ", name, ": ", tostring(err), "\n")
	end
end
local function eq(a, b, what)
	if a ~= b then error(string.format("%s: %s ~= %s", what, tostring(a), tostring(b)), 2) end
end

-- EML clones a value inserted into a game array: the stand-in does the same, or a copied box would share its table
local function deepcopy(v)
	if type(v) ~= "table" then return v end
	local t = {}
	for k, x in pairs(v) do t[k] = deepcopy(x) end
	return t
end
local raw_insert = table.insert
table.insert = function(t, a, b)
	if b == nil then return raw_insert(t, deepcopy(a)) end
	return raw_insert(t, a, deepcopy(b))
end

-- One run of the script: `server` (no render data), `no_bed_model` (a client whose data lacks the bed's model),
-- `no_prop_model` (the pack's prop model of the Stable was not made).
local function run(opts)
	opts = opts or {}
	local calls = { placeables = {}, recipes = {}, finish = 0, created = {} }
	local helper = {
		add_placeable = function(def)
			if opts.no_bed_item then error("add_placeable: no item " .. def.clone_of .. " to copy (the game changed?)") end
			calls.placeables[#calls.placeables + 1] = def
			return 1
		end,
		add_recipe = function(def)
			if opts.no_chair and def.like == "Prop_Decoration_Comfort1_T0_Chair" then
				error("add_recipe: no recipe Prop_Decoration_Comfort1_T0_Chair to copy (the game changed?)")
			end
			calls.recipes[#calls.recipes + 1] = def
			return 2
		end,
		finish = function() calls.finish = calls.finish + 1 end,
	}
	local bed_model = { guid = BED_MODEL, data = { debugName = "animalprops_farm_animal_sleep_large_01", renderData = { hash = "h" } } }
	local prop_model = { guid = STABLE_MODEL, data = { debugName = "beastmaster_stable", renderData = { hash = "p" } } }
	_G.loader = { features = { patch = true }, is_server = opts.server or false }
	_G.game = { assets = {
		get_resource = function(guid, type)
			if type ~= "keen::RenderModel" or opts.server then return nil end
			if guid == BED_MODEL and not opts.no_bed_model then return bed_model end
			if guid == STABLE_MODEL and not opts.no_prop_model then return prop_model end
			return nil
		end,
		get_resources_by_type = function(type)
			if type == "keen::RenderModel" and not opts.server then return { { guid = "other" } } end
			return {}
		end,
		create_resource = function(data, type, guid, flags)
			local copy = {}
			for k, v in pairs(data) do copy[k] = v end
			calls.created[#calls.created + 1] = { type = type, guid = guid, flags = flags, data = copy }
			return { guid = guid, data = copy }
		end,
	} }
	package.loaded["emf_content"] = helper
	local ok, err = pcall(dofile, script)
	calls.ok, calls.err = ok, err
	return calls
end

local function has(list, value)
	for _, v in ipairs(list) do
		if v == value then return true end
	end
	return false
end

test("the Stable is a copy of the large animal bed's object, without the parts farm animals and villagers use", function()
	local c = run()
	assert(c.ok, c.err)
	eq(#c.placeables, 1, "placeables")
	local p = c.placeables[1]
	eq(p.clone_of, BED_ITEM, "clone_of")
	eq(p.guid, STABLE_ITEM, "item")
	eq(p.template_guid, STABLE_TEMPLATE, "template")
	eq(p.name, "Stable", "name")
	assert(p.name_guid and p.description_guid and p.description and p.debug_name, "name, description, debug name")
	assert(has(p.strip, "keen::ecs::AnimalTarget"), "AnimalTarget stripped")
	assert(has(p.strip, "keen::ecs::ActiveNpcTarget"), "ActiveNpcTarget stripped")
end)

test("its template names the Stable's own render model", function()
	local c = run()
	local tpl = { components = {
		{ type = "keen::ecs::StaticCollider", value = {} },
		{ type = "keen::ecs::ModelResource", value = { model = BED_MODEL } },
	} }
	c.placeables[1].edit_template(tpl)
	eq(tpl.components[2].value.model, STABLE_MODEL, "model")
end)

test("the Stable's render model is the one the pack built under its GUID (its prop): nothing is copied", function()
	local c = run()
	assert(c.ok, c.err)
	eq(#c.created, 0, "resources created")
end)

test("without the pack's model (its prop failed) the Stable takes a copy of the bed's record under its GUID", function()
	local c = run({ no_prop_model = true })
	assert(c.ok, c.err)
	eq(#c.created, 1, "resources created")
	local m = c.created[1]
	eq(m.type, "keen::RenderModel", "type")
	eq(m.guid, STABLE_MODEL, "guid")
	eq(m.flags, 0, "flags")
	eq(m.data.renderData.hash, "h", "the bed's mesh data")
	eq(#c.placeables, 1, "placeables")
end)

-- The bed's collider as the game has it: one box in a ColliderResourceComponent
local function bed_template()
	return { components = {
		{ type = "keen::ecs::ModelResource", value = { model = BED_MODEL } },
		{ type = "keen::ecs::ColliderResourceComponent", value = { colliders = { {
			dataArray = { { type = "keen::ecs::BoxColliderData", value = {
				offset = { x = 0, y = 0.165, z = 0 }, orientation = { x = 0, y = 0, z = 0, w = 1 },
				shape = { halfSize = { x = 1.45, y = 0.19, z = 1.45 } } } } },
			joltObjects = { { shapeIndexList = { 0 } } },
		} } } },
	} }
end

test("its collider: the plank floor, the back and left walls, the low rail on the right and the roof, one solid", function()
	local c = run()
	local tpl = bed_template()
	c.placeables[1].edit_template(tpl)
	local col = tpl.components[2].value.colliders[1]
	eq(#col.dataArray, 5, "boxes")
	local floor = col.dataArray[1].value
	assert(math.abs(floor.offset.y + floor.shape.halfSize.y - 0.19) < 0.01, "the floor's top is 0.19 m high")
	for i, d in ipairs(col.dataArray) do
		eq(tostring(d.type), "keen::ecs::BoxColliderData", "box " .. i)
		assert(d.value.shape.halfSize.x > 0 and d.value.shape.halfSize.y > 0 and d.value.shape.halfSize.z > 0, "size " .. i)
	end
	local rail = col.dataArray[4].value
	assert(rail.offset.y + rail.shape.halfSize.y < 1.0, "the rail is low (under 1 m)")
	local roof = col.dataArray[5].value
	assert(roof.offset.y - roof.shape.halfSize.y >= 2.2, "the roof starts above head height")
	assert(col.dataArray[2].value ~= col.dataArray[3].value, "each box its own")
	local list = col.joltObjects[1].shapeIndexList
	eq(#list, 5, "shape indices")
	for i = 1, 5 do eq(list[i], i - 1, "shape index " .. i) end
end)

test("a collider that is not the bed's one box is kept as it is", function()
	local c = run()
	local tpl = bed_template()
	local col = tpl.components[2].value.colliders[1]
	col.dataArray[1].type = "keen::ecs::CapsuleColliderData"
	c.placeables[1].edit_template(tpl)
	eq(#col.dataArray, 1, "boxes")
	eq(col.dataArray[1].value.shape.halfSize.x, 1.45, "unchanged")
end)

test("on a dedicated server (no render data) no model is made and the template still names it", function()
	local c = run({ server = true })
	assert(c.ok, c.err)
	eq(#c.created, 0, "resources created")
	eq(#c.placeables, 1, "placeables")
	local tpl = { components = { { type = "keen::ecs::ModelResource", value = { model = BED_MODEL } } } }
	c.placeables[1].edit_template(tpl)
	eq(tpl.components[1].value.model, STABLE_MODEL, "model")
	eq(#c.recipes, 1, "recipes")
end)

test("a client with neither the pack's model nor the bed's adds nothing (the game changed): the error says what is missing", function()
	local c = run({ no_bed_model = true, no_prop_model = true })
	assert(not c.ok, "an error")
	assert(tostring(c.err):find(BED_MODEL, 1, true), "names the model: " .. tostring(c.err))
	eq(#c.placeables, 0, "placeables")
	eq(#c.recipes, 0, "recipes")
end)

test("its recipe: at the Workbench like the crude chair, 6 wood logs, 8 plant fibre, 2 animal fur; names written once", function()
	local c = run()
	eq(#c.recipes, 1, "recipes")
	local r = c.recipes[1]
	eq(r.guid, STABLE_RECIPE, "recipe")
	eq(r.like, "Prop_Decoration_Comfort1_T0_Chair", "like")
	eq(r.output[1], STABLE_ITEM, "output")
	eq(r.output[2], 1, "output count")
	eq(#r.inputs, 3, "inputs")
	eq(r.inputs[1][1], WOOD_LOGS, "wood")
	eq(r.inputs[1][2], 6, "wood count")
	eq(r.inputs[2][1], PLANT_FIBRE, "fibre")
	eq(r.inputs[2][2], 8, "fibre count")
	eq(r.inputs[3][1], ANIMAL_FUR, "fur")
	eq(r.inputs[3][2], 2, "fur count")
	eq(c.finish, 1, "finish")
end)

test("without the crude chair's recipe (a game update) the Stable is crafted in the own crafting menu, named", function()
	local c = run({ no_chair = true })
	assert(c.ok, c.err)
	eq(#c.placeables, 1, "placeables")
	eq(#c.recipes, 1, "recipes")
	eq(c.recipes[1].like, nil, "like: the helper's default (the own crafting menu, next to the campfire)")
	eq(c.recipes[1].output[1], STABLE_ITEM, "output")
	eq(#c.recipes[1].inputs, 3, "inputs")
	eq(c.finish, 1, "finish")
end)

test("an item that cannot be added still writes the names given so far, then stops with the error", function()
	local c = run({ no_bed_item = true })
	assert(not c.ok, "an error")
	assert(tostring(c.err):find(BED_ITEM, 1, true), "names the item: " .. tostring(c.err))
	eq(#c.recipes, 0, "recipes")
	eq(c.finish, 1, "finish")
end)

io.write(string.format("%d tests, %d failed\n", ran, failures))
os.exit(failures == 0 and 0 or 1)
