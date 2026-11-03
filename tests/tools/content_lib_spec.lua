-- Unit tests for content/lib/emf_content.lua's knowledge-table code, run by test_content_lib.py with a Lua 5.4
-- interpreter: lua content_lib_spec.lua <path of emf_content.lua>. Fakes stand in for EML's game API.
local lib_path = assert(arg[1], "usage: lua content_lib_spec.lua <emf_content.lua>")

-- EML clones a value inserted into a game array (MappedArrayValue::insert): the fake does the same, or a copied entry
-- would share its table with the original.
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

-- A static hash map as the game stores its knowledge tables: entries[bucket] = {start, count} (start counts from 0),
-- keys and values bucket after bucket; bucket = key & (buckets - 1).
local function make_map(buckets, list)
	local by = {}
	for b = 1, buckets do by[b] = {} end
	for _, kv in ipairs(list) do
		local b = (kv[1] & (buckets - 1)) + 1
		by[b][#by[b] + 1] = kv
	end
	local map = {entries = {}, keys = {}, values = {}}
	for b = 1, buckets do
		map.entries[b] = {start = #map.keys, count = #by[b]}
		for _, kv in ipairs(by[b]) do
			raw_insert(map.keys, {id = {value = kv[1]}})
			raw_insert(map.values, {id = {id = {value = kv[1]}}, type = kv[2], expectedFlags = kv[3]})
		end
	end
	return map
end

local function find(map, key)
	local e = map.entries[(key & (#map.entries - 1)) + 1]
	for i = e.start + 1, e.start + e.count do
		if map.keys[i].id.value == key then return map.values[i] end
	end
	return nil
end

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

-- Globals the helper reads (EML's)
local knowledge
local resources = {}  -- [type .. "|" .. guid] = data
local registry = {itemRefs = {}, dbgNames = {}}
local recipe_registry = {recipes = {}}
local ui_bundle = {menu = {crafting = {recipes = {workshops = {}, trees = {}}}}}
_G.loader = {is_server = false, features = {patch = true}}
_G.game = {
	guid = {hash = function(g)
		local h = 2166136261
		for i = 1, #g do h = ((h ~ g:byte(i)) * 16777619) & 0xFFFFFFFF end
		return h
	end},
	assets = {
		get_resources_by_type = function(t)
			if t == "keen::GameKnowledgeResource" then return {{data = knowledge}} end
			if t == "keen::ItemRegistryResource" then return {{data = registry}} end
			if t == "keen::RecipeRegistryResource" then return {{data = recipe_registry}} end
			if t == "keen::FbUiBundle" then return {{data = ui_bundle}} end
			error("the fake has no " .. t)
		end,
		get_resource = function(guid, t)
			local d = resources[t .. "|" .. guid]
			return d and {data = d} or nil
		end,
		create_resource = function(data, t, guid)
			local copy = deepcopy(data)
			resources[t .. "|" .. guid] = copy
			return {data = copy}
		end,
	},
}
local M = dofile(lib_path)

local function fresh()
	knowledge = {
		playerKnowledge = make_map(16, {{0x10, "Flags", 3}, {0x21, "Flags", 3}, {0x31, "Flags", 65535}, {0x05, "Flags", 1}}),
		worldKnowledge = make_map(8, {{0x31, "World", 7}}),
	}
	return knowledge
end

test("a copied entry lands in its key's bucket, after the bucket's keys, and later buckets move up by one", function()
	local k = fresh()
	local map = k.playerKnowledge
	M._knowledge_copy(map, 0x10, 0x41)  -- bucket 1 (0x41 & 15), as 0x31 and 0x21
	eq(#map.keys, 5, "keys")
	eq(#map.values, 5, "values")
	eq(find(map, 0x41).expectedFlags, 3, "the copy's settings")
	eq(find(map, 0x41).id.id.value, 0x41, "the copy's own id")
	eq(find(map, 0x10).id.id.value, 0x10, "the source keeps its id")
	for _, key in ipairs({0x10, 0x21, 0x31, 0x05}) do assert(find(map, key), string.format("0x%x still found", key)) end
	eq(map.entries[2].count, 3, "bucket 1's count")
	eq(map.entries[6].start, 4, "bucket 5 starts after the inserted key")
	assert(M._knowledge_check(map))
end)

test("the check refuses a map whose buckets do not add up (the game's layout changed)", function()
	local map = fresh().playerKnowledge
	map.entries[3].start = map.entries[3].start + 1
	local ok, why = M._knowledge_check(map)
	eq(ok, false, "ok")
	assert(why and why:find("start"), why)
end)

test("the check refuses a key in the wrong bucket", function()
	local map = fresh().playerKnowledge
	map.keys[1].id.value = 0x77  -- bucket 7's key stored in bucket 0's place
	local ok = M._knowledge_check(map)
	eq(ok, false, "ok")
end)

test("a key that exists already is refused", function()
	local map = fresh().playerKnowledge
	local ok = pcall(M._knowledge_copy, map, 0x10, 0x21)
	eq(ok, false, "ok")
	eq(#map.keys, 4, "nothing inserted")
end)

test("a source that is not there copies nothing and says so", function()
	local map = fresh().playerKnowledge
	eq(M._knowledge_copy(map, 0x99, 0x41), false, "copied")
	eq(#map.keys, 4, "nothing inserted")
end)

test("an item gets a player entry and, when its source has one, a world entry", function()
	local k = fresh()
	M._item_knowledge(0x31, 0x51)
	eq(find(k.playerKnowledge, 0x51).expectedFlags, 65535, "player entry copied from the source item's")
	eq(find(k.worldKnowledge, 0x51).expectedFlags, 7, "world entry copied")
	M._item_knowledge(0x05, 0x52)
	assert(find(k.playerKnowledge, 0x52), "player entry")
	eq(find(k.worldKnowledge, 0x52), nil, "no world entry: the source has none")
end)

-- placeables (the dedicated server's view: no icon, text or menu)
local WB_ITEM = "2f04a05f-697d-489b-a32f-76bb3439f5bc"
local WB_TPL = "ebf450c3-fb96-4240-a3be-8ebe0713eccd"
local ZERO = "00000000-0000-0000-0000-000000000000"
local function vanilla()
	fresh()
	resources = {}
	registry = {itemRefs = {}, dbgNames = {}}
	resources["keen::ItemInfo|" .. WB_ITEM] = {itemId = {value = 7}, objectId = WB_ITEM, name = "n", description = "d",
		debugName = "Craftingstation_Workbench", itemLevelRange = {minLevel = 1, maxLevel = 1},
		equipment = {placedEntity = WB_TPL}}
	resources["keen::ecs::TemplateResource|" .. WB_TPL] = {name = "Craftingstation_Workbench", components = {
		{type = "keen::ecs::ModelResource"}, {type = "keen::ecs::Crafting"}, {type = "keen::ecs::InteractionOfferRuntime"},
		{type = "keen::ecs::ModelResource2"}}}
end
local TABLE = {guid = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e21", template_guid = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e22",
	name_guid = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e23", debug_name = "Prop_Ember_Table", name = "Ember Table",
	clone_of = WB_ITEM, strip = {"keen::ecs::Crafting", "keen::ecs::InteractionOfferRuntime"}}

test("a placeable is a template copy without the stripped parts, and an item that places it", function()
	vanilla()
	loader.is_server = true
	local id = M.add_placeable(TABLE)
	loader.is_server = false
	local tpl = resources["keen::ecs::TemplateResource|" .. TABLE.template_guid]
	assert(tpl, "the template")
	eq(tpl.name, "Prop_Ember_Table", "the template's name")
	eq(#tpl.components, 2, "components left")
	eq(tpl.components[1].type, "keen::ecs::ModelResource", "kept")
	eq(tpl.components[2].type, "keen::ecs::ModelResource2", "kept")
	eq(#resources["keen::ecs::TemplateResource|" .. WB_TPL].components, 4, "the Workbench keeps its parts")
	local item = resources["keen::ItemInfo|" .. TABLE.guid]
	eq(item.equipment.placedEntity, TABLE.template_guid, "the item places the new template")
	eq(item.itemId.value, game.guid.hash(TABLE.guid), "the item's id")
	eq(id, item.itemId.value, "returned id")
	eq(registry.itemRefs[1], TABLE.guid, "registered")
	eq(resources["keen::ItemInfo|" .. WB_ITEM].equipment.placedEntity, WB_TPL, "the Workbench item is unchanged")
end)

test("a part to strip that the template does not have is refused (a typo would leave a second Workbench)", function()
	vanilla()
	loader.is_server = true
	local def = deepcopy(TABLE)
	def.strip = {"keen::ecs::Craftin"}
	local ok, err = pcall(M.add_placeable, def)
	loader.is_server = false
	eq(ok, false, "ok")
	assert(tostring(err):find("Craftin"), err)
	eq(resources["keen::ecs::TemplateResource|" .. TABLE.template_guid], nil, "no template made")
end)

test("strip takes lists of names too (the helper's STATION_PARTS and PROMPT_PARTS)", function()
	vanilla()
	local tpl = resources["keen::ecs::TemplateResource|" .. WB_TPL]
	for _, list in ipairs({M.STATION_PARTS, M.PROMPT_PARTS}) do
		for _, name in ipairs(list) do
			if name ~= "keen::ecs::Crafting" and name ~= "keen::ecs::InteractionOfferRuntime" then
				tpl.components[#tpl.components + 1] = {type = name}
			end
		end
	end
	local def = deepcopy(TABLE)
	def.strip = {M.STATION_PARTS, M.PROMPT_PARTS}
	loader.is_server = true
	M.add_placeable(def)
	loader.is_server = false
	local left = resources["keen::ecs::TemplateResource|" .. TABLE.template_guid].components
	eq(#left, 2, "only the two models are left")
end)

test("an item that places nothing cannot be copied as a placeable", function()
	vanilla()
	resources["keen::ItemInfo|" .. WB_ITEM].equipment.placedEntity = ZERO
	loader.is_server = true
	local ok, err = pcall(M.add_placeable, TABLE)
	loader.is_server = false
	eq(ok, false, "ok")
	assert(tostring(err):find("places nothing"), err)
end)

-- recipes: the campfire in the own crafting menu's supplies group, second set (the first, "Unassorted", is empty)
local CAMPFIRE_ID = 0xE00B95DA
local function recipe_world()
	knowledge = {playerKnowledge = make_map(16, {{CAMPFIRE_ID, "Flags", 3}}), worldKnowledge = make_map(8, {})}
	recipe_registry = {recipes = {{
		debugName = "Consumable_Tool_Campfire_PK", recipeGuid = "c", recipeId = {value = CAMPFIRE_ID},
		workshopId = {value = 0}, requiresSheltered = false,
		output = {{itemRef = "o", item = {value = 1}, count = 1}},
		input = {{itemStack = {itemRef = "i", item = {value = 2}, count = 3},
			inputItemCategory = {categoryRef = "x", category = {value = 9}, count = 0}}},
		completionRequirementQuery = {knowledgeOrQueryId = {value = CAMPFIRE_ID}},
		knowledgeRequirement = {knowledgeOrQueryId = {value = 77}, compareValue = 1},
	}}}
	ui_bundle = {menu = {crafting = {recipes = {workshops = {{value = 0}}, trees = {{groups = {
		{groupId = "g-weapons", sets = {{entries = {{value = 0x51}}}}},
		{groupId = "g-supplies", sets = {{entries = {}}, {entries = {{value = 0x111}, {value = CAMPFIRE_ID},
			{value = 0x222}}}}},
	}}}}}}}
	return ui_bundle.menu.crafting.recipes.trees[1].groups
end
local WOOD = "3f4093ae-75ed-4e5d-bd0b-784ba83b88f5"

test("a recipe without a menu place is listed right after the recipe it copies, in that recipe's set", function()
	local groups = recipe_world()
	local id = M.add_recipe{guid = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e25", debug_name = "Craft_Table",
		output = {TABLE.guid, 1}, inputs = {{WOOD, 4}}}
	local supplies = groups[2]
	eq(#supplies.sets[1].entries, 0, "the empty first set stays empty")
	eq(#supplies.sets[2].entries, 4, "the campfire's set")
	eq(supplies.sets[2].entries[3].value, id, "right after the campfire")
	eq(supplies.sets[2].entries[4].value, 0x222, "the next one moved on")
	assert(find(knowledge.playerKnowledge, id), "the recipe's knowledge entry")
end)

test("a recipe with a menu group goes into that group's set 1, or the set named", function()
	local groups = recipe_world()
	local a = M.add_recipe{guid = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e31", debug_name = "Craft_A",
		output = {TABLE.guid, 1}, inputs = {{WOOD, 1}}, menu = {group = "g-weapons"}}
	eq(groups[1].sets[1].entries[2].value, a, "weapons, set 1, at the end")
	local b = M.add_recipe{guid = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e32", debug_name = "Craft_B",
		output = {TABLE.guid, 1}, inputs = {{WOOD, 1}}, menu = {group = "g-supplies", set = 2}}
	eq(groups[2].sets[2].entries[4].value, b, "supplies, set 2, at the end")
end)

io.write(string.format("%d of %d passed\n", ran - failures, ran))
os.exit(failures == 0 and 0 or 1)
