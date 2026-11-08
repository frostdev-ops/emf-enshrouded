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

-- One run of the script: `server` (no render data), `no_bed_model` (a client whose data lacks the bed's model).
local function run(opts)
	opts = opts or {}
	local calls = { placeables = {}, recipes = {}, finish = 0, created = {} }
	local helper = {
		add_placeable = function(def) calls.placeables[#calls.placeables + 1] = def; return 1 end,
		add_recipe = function(def) calls.recipes[#calls.recipes + 1] = def; return 2 end,
		finish = function() calls.finish = calls.finish + 1 end,
	}
	local bed_model = { guid = BED_MODEL, data = { debugName = "animalprops_farm_animal_sleep_large_01", renderData = { hash = "h" } } }
	_G.loader = { features = { patch = true }, is_server = opts.server or false }
	_G.game = { assets = {
		get_resource = function(guid, type)
			if type == "keen::RenderModel" and guid == BED_MODEL and not opts.server and not opts.no_bed_model then
				return bed_model
			end
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

test("the Stable's render model is a copy of the bed's record under its own GUID (the same mesh data)", function()
	local c = run()
	eq(#c.created, 1, "resources created")
	local m = c.created[1]
	eq(m.type, "keen::RenderModel", "type")
	eq(m.guid, STABLE_MODEL, "guid")
	eq(m.flags, 0, "flags")
	eq(m.data.renderData.hash, "h", "mesh data")
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

test("a client without the bed's model adds nothing (the game changed): the error says what is missing", function()
	local c = run({ no_bed_model = true })
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

io.write(string.format("%d tests, %d failed\n", ran, failures))
os.exit(failures == 0 and 0 or 1)
