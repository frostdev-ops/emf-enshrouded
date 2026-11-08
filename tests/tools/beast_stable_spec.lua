-- Unit tests for tools/models/packs/beastmaster_stable.lua (the beasts pack's craftable Stable and Large Stable), run by
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
local LARGE_ITEM = "439e5967-5b96-4c28-afd2-13db09d7e605"
local LARGE_TEMPLATE = "c110e001-4b38-462f-a317-3a6b0d627511"
local LARGE_NAME = "4782e479-fad2-448d-b34f-f77ffa5858af"
local LARGE_DESC = "b9fa7b45-d5ed-47e1-98ab-51e2a7463a18"
local LARGE_RECIPE = "9a2fcde6-2dfc-48f6-92ca-0ad394df23da"
local LARGE_MODEL = "5879f304-0050-4ecb-96f4-5cc0e66155d9"
local LARGE_SCALE = 1.6
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
-- `no_prop_model` / `no_large_model` (the pack's prop model of the Stable / the Large Stable was not made), `fail_item`
-- (adding the placeable with this item GUID fails).
local function run(opts)
	opts = opts or {}
	local calls = { placeables = {}, recipes = {}, finish = 0, created = {} }
	local helper = {
		add_placeable = function(def)
			if opts.no_bed_item then error("add_placeable: no item " .. def.clone_of .. " to copy (the game changed?)") end
			if opts.fail_item == def.guid then error("add_placeable: " .. def.guid .. " failed") end
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
	local large_model = { guid = LARGE_MODEL, data = { debugName = "beastmaster_large_stable", renderData = { hash = "l" } } }
	_G.loader = { features = { patch = true }, is_server = opts.server or false }
	_G.game = { assets = {
		get_resource = function(guid, type)
			if type ~= "keen::RenderModel" or opts.server then return nil end
			if guid == BED_MODEL and not opts.no_bed_model then return bed_model end
			if guid == STABLE_MODEL and not opts.no_prop_model then return prop_model end
			if guid == LARGE_MODEL and not opts.no_large_model then return large_model end
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

-- The placeable / recipe of a run with this item / recipe GUID, or nil.
local function placeable(c, guid)
	for _, p in ipairs(c.placeables) do
		if p.guid == guid then return p end
	end
	return nil
end
local function recipe(c, guid)
	for _, r in ipairs(c.recipes) do
		if r.guid == guid then return r end
	end
	return nil
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
	eq(#c.placeables, 2, "placeables: the Stable and the Large Stable")
	local p = placeable(c, STABLE_ITEM)
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
	placeable(c, STABLE_ITEM).edit_template(tpl)
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
	eq(#c.placeables, 2, "placeables")
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
	placeable(c, STABLE_ITEM).edit_template(tpl)
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
	placeable(c, STABLE_ITEM).edit_template(tpl)
	eq(#col.dataArray, 1, "boxes")
	eq(col.dataArray[1].value.shape.halfSize.x, 1.45, "unchanged")
end)

test("on a dedicated server (no render data) no model is made and the template still names it", function()
	local c = run({ server = true })
	assert(c.ok, c.err)
	eq(#c.created, 0, "resources created")
	eq(#c.placeables, 2, "placeables")
	local tpl = { components = { { type = "keen::ecs::ModelResource", value = { model = BED_MODEL } } } }
	placeable(c, STABLE_ITEM).edit_template(tpl)
	eq(tpl.components[1].value.model, STABLE_MODEL, "model")
	local big = { components = { { type = "keen::ecs::ModelResource", value = { model = BED_MODEL } } } }
	placeable(c, LARGE_ITEM).edit_template(big)
	eq(big.components[1].value.model, LARGE_MODEL, "the Large Stable's model")
	eq(#c.recipes, 2, "recipes")
end)

test("a client with neither the pack's models nor the bed's adds nothing (the game changed): the error says what is missing", function()
	local c = run({ no_bed_model = true, no_prop_model = true, no_large_model = true })
	assert(not c.ok, "an error")
	assert(tostring(c.err):find(BED_MODEL, 1, true), "names the model: " .. tostring(c.err))
	eq(#c.placeables, 0, "placeables")
	eq(#c.recipes, 0, "recipes")
end)

test("a client without the Stable's model and the bed's still adds the Large Stable (its own model), then errs", function()
	local c = run({ no_bed_model = true, no_prop_model = true })
	assert(not c.ok, "an error")
	eq(#c.placeables, 1, "placeables")
	assert(placeable(c, LARGE_ITEM), "the Large Stable")
end)

test("its recipe: at the Workbench like the crude chair, 6 wood logs, 8 plant fibre, 2 animal fur; names written once", function()
	local c = run()
	eq(#c.recipes, 2, "recipes")
	local r = recipe(c, STABLE_RECIPE)
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

test("without the crude chair's recipe (a game update) both stables are crafted in the own crafting menu, named", function()
	local c = run({ no_chair = true })
	assert(c.ok, c.err)
	eq(#c.placeables, 2, "placeables")
	eq(#c.recipes, 2, "recipes")
	for _, guid in ipairs({ STABLE_RECIPE, LARGE_RECIPE }) do
		local r = recipe(c, guid)
		eq(r.like, nil, "like: the helper's default (the own crafting menu, next to the campfire)")
		eq(#r.inputs, 3, "inputs")
	end
	eq(recipe(c, STABLE_RECIPE).output[1], STABLE_ITEM, "output")
	eq(recipe(c, LARGE_RECIPE).output[1], LARGE_ITEM, "output")
	eq(c.finish, 1, "finish")
end)

test("the Large Stable is a second copy of the large animal bed's object, with its own GUIDs, name and description", function()
	local c = run()
	assert(c.ok, c.err)
	local p = placeable(c, LARGE_ITEM)
	assert(p, "the Large Stable is added")
	eq(p.clone_of, BED_ITEM, "clone_of")
	eq(p.template_guid, LARGE_TEMPLATE, "template")
	eq(p.name_guid, LARGE_NAME, "name GUID")
	eq(p.description_guid, LARGE_DESC, "description GUID")
	eq(p.name, "Large Stable", "name")
	assert(p.description:find("at home", 1, true), "the description says the beasts at home live in it: " .. p.description)
	assert(p.debug_name ~= placeable(c, STABLE_ITEM).debug_name, "a debug name of its own")
	assert(has(p.strip, "keen::ecs::AnimalTarget"), "AnimalTarget stripped")
	assert(has(p.strip, "keen::ecs::ActiveNpcTarget"), "ActiveNpcTarget stripped")
end)

test("the Large Stable's template names its own render model; the pack built it, so nothing is copied", function()
	local c = run()
	local tpl = bed_template()
	placeable(c, LARGE_ITEM).edit_template(tpl)
	eq(tpl.components[1].value.model, LARGE_MODEL, "model")
	eq(#c.created, 0, "resources created")
end)

test("without the pack's Large Stable model the Large Stable takes a copy of the bed's record under its GUID", function()
	local c = run({ no_large_model = true })
	assert(c.ok, c.err)
	eq(#c.created, 1, "resources created")
	eq(c.created[1].guid, LARGE_MODEL, "guid")
	eq(c.created[1].data.renderData.hash, "h", "the bed's mesh data")
end)

test("the Large Stable's collider is the Stable's boxes 1.6 times as big, from the same middle", function()
	local c = run()
	local small, big = bed_template(), bed_template()
	placeable(c, STABLE_ITEM).edit_template(small)
	placeable(c, LARGE_ITEM).edit_template(big)
	local a = small.components[2].value.colliders[1]
	local b = big.components[2].value.colliders[1]
	eq(#b.dataArray, #a.dataArray, "boxes")
	for i = 1, #a.dataArray do
		local s, l = a.dataArray[i].value, b.dataArray[i].value
		for _, k in ipairs({ "x", "y", "z" }) do
			assert(math.abs(l.offset[k] - s.offset[k] * LARGE_SCALE) < 1e-6, "box " .. i .. " offset " .. k)
			assert(math.abs(l.shape.halfSize[k] - s.shape.halfSize[k] * LARGE_SCALE) < 1e-6, "box " .. i .. " size " .. k)
		end
	end
	eq(#b.joltObjects[1].shapeIndexList, #a.dataArray, "shape indices")
end)

test("the Large Stable's recipe: at the Workbench like the crude chair, 12 wood logs, 16 plant fibre, 4 animal fur", function()
	local c = run()
	local r = recipe(c, LARGE_RECIPE)
	assert(r, "the Large Stable's recipe")
	eq(r.like, "Prop_Decoration_Comfort1_T0_Chair", "like")
	eq(r.output[1], LARGE_ITEM, "output")
	eq(r.output[2], 1, "output count")
	eq(r.inputs[1][1], WOOD_LOGS, "wood")
	eq(r.inputs[1][2], 12, "wood count")
	eq(r.inputs[2][1], PLANT_FIBRE, "fibre")
	eq(r.inputs[2][2], 16, "fibre count")
	eq(r.inputs[3][1], ANIMAL_FUR, "fur")
	eq(r.inputs[3][2], 4, "fur count")
end)

test("a Large Stable that cannot be added leaves the Stable added, crafted and named; the error comes after", function()
	local c = run({ fail_item = LARGE_ITEM })
	assert(not c.ok, "an error")
	assert(tostring(c.err):find(LARGE_ITEM, 1, true), "names the item: " .. tostring(c.err))
	assert(placeable(c, STABLE_ITEM), "the Stable")
	assert(recipe(c, STABLE_RECIPE), "the Stable's recipe")
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
