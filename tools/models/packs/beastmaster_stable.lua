-- The Stable: a bed for tamed beasts that players craft at the Workbench and place in their base; Beastmaster opens its
-- Beasts window with E at it (mods/beastmaster/stable.lua knows it by its render model). A content script of the beasts
-- pack (make_mod.py "content_scripts"), run with EMF's helper emf_content.lua (docs/modding/content.md).
--
-- It is a copy of the large animal bed's object without the parts farm animals and villagers use, with the pack's prop
-- model beastmaster_stable (a stable with a thatched roof; make_mod "props") under the Stable's own model GUID, which
-- Beastmaster knows it by, and boxes of its own to stand on and bump into. When that model was not made, the Stable
-- takes a copy of the bed's record under the same GUID (the bed's look). A dedicated server has no render data: there
-- the template names the model all the same, as the game's own templates do.
--
-- The GUIDs below are FIXED FOREVER: saves keep the item by its GUID and placed Stables by their template's GUID, and
-- Beastmaster knows the model by its GUID.
local content = require("emf_content")

local ITEM_GUID     = "a84e96fa-f27a-48b8-9815-89093bd142b4"
local TEMPLATE_GUID = "96a057d5-a1d9-4fc1-b313-9b923e9302fe"
local NAME_GUID     = "3c51352b-ce1b-437b-805f-f2eaa4da7930"
local DESC_GUID     = "4869485a-5566-4137-95b8-115e1ccccc89"
local RECIPE_GUID   = "0410f9da-8a00-4b5e-a537-33b52d43ccd9"
local MODEL_GUID    = "1a49170a-b3d2-498d-9062-8946f6469613"
local DEBUG_NAME    = "Prop_Beastmaster_Stable"

local BED_ITEM    = "a8076625-0529-415a-bf32-a9e0619e8ce5" -- Animal_Farm_Housing_Sleeping_Bed_YAK_Sanddigger
local BED_MODEL   = "7dd9139a-fba7-4e60-8b5a-fa853c250ef7" -- animalprops_farm_animal_sleep_large_01
local WOOD_LOGS   = "3f4093ae-75ed-4e5d-bd0b-784ba83b88f5" -- Material_T1_Wood_Logs
local PLANT_FIBRE = "ff00a9f6-fd7e-4933-9c6b-33c36223cd2b" -- Material_T1_Plant_Fibre
local ANIMAL_FUR  = "66ce7d4c-ce66-4ad4-800a-ab37f7e604a2" -- Material_T1_Animal_Fur

-- The Stable's solid parts, in metres from its middle on the ground (the prop 4.5 x 3.4 x 2.95 m, its open side +z):
-- centre and half size of each box. The first is the plank floor (its top 0.19 m up, walkable); then the back wall,
-- the left wall, the low rail on the right (0.95 m high; the front is open) and the roof (from 2.25 m up).
local BOXES = {
	{ at = { -0.01, 0.096, -0.017 }, half = { 2.01, 0.096, 1.434 } },
	{ at = { -0.06, 1.23, -1.125 }, half = { 1.86, 1.035, 0.225 } },
	{ at = { -1.69, 1.23, -0.11 }, half = { 0.225, 1.035, 1.24 } },
	{ at = { 1.69, 0.57, 0.0 }, half = { 0.135, 0.38, 1.125 } },
	{ at = { 0.0, 2.84, 0.0 }, half = { 2.25, 0.59, 1.6 } },
}

-- The bed's collider (one box) becomes the Stable's boxes: the box copied (EML copies a value put into a game array)
-- and each placed, all in the collider's one solid. A collider that is not one box is kept as it is.
local function fit_collider(tpl)
	for _, c in ipairs(tpl.components) do
		if c.type == "keen::ecs::ColliderResourceComponent" then
			local col = c.value.colliders[1]
			local data = col and col.dataArray
			if not (data and #data == 1 and tostring(data[1].type) == "keen::ecs::BoxColliderData") then
				print("beastmaster stable: the bed's collider is not one box: kept as it is")
				return
			end
			for _ = 2, #BOXES do table.insert(data, data[1]) end
			for i, b in ipairs(BOXES) do
				local v = data[i].value
				v.offset.x, v.offset.y, v.offset.z = b.at[1], b.at[2], b.at[3]
				v.shape.halfSize.x, v.shape.halfSize.y, v.shape.halfSize.z = b.half[1], b.half[2], b.half[3]
			end
			local list = col.joltObjects[1].shapeIndexList
			for i = #list + 1, #BOXES do table.insert(list, i - 1) end
		end
	end
end

-- the render model: only where the game has render data (not on a dedicated server)
if #game.assets.get_resources_by_type("keen::RenderModel") > 0
	and not game.assets.get_resource(MODEL_GUID, "keen::RenderModel") then
	local bed = game.assets.get_resource(BED_MODEL, "keen::RenderModel")
	if not bed then error("the large animal bed's model " .. BED_MODEL .. " is not in the game (the game changed?)") end
	local m = game.assets.create_resource(bed.data, "keen::RenderModel", MODEL_GUID, 0).data
	m.debugName = "beastmaster_stable"
	print("beastmaster stable: the pack's Stable model was not made: the Stable has the large animal bed's look")
end

local function recipe(like)
	return {
		guid = RECIPE_GUID, debug_name = "Craft_" .. DEBUG_NAME,
		output = {ITEM_GUID, 1},
		inputs = {{WOOD_LOGS, 6}, {PLANT_FIBRE, 8}, {ANIMAL_FUR, 2}},
		like = like,
	}
end

local ok, err = pcall(function()
	content.add_placeable{
		guid = ITEM_GUID, template_guid = TEMPLATE_GUID, name_guid = NAME_GUID, debug_name = DEBUG_NAME,
		name = "Stable", description_guid = DESC_GUID,
		description = "A bed for your tamed beasts. Press E at it to open the Beasts window, where you choose which "
			.. "beasts go with you (needs the Beastmaster mod).",
		clone_of = BED_ITEM,
		strip = {"keen::ecs::AnimalTarget", "keen::ecs::ActiveNpcTarget"},  -- farm animals and villagers leave it alone
		edit_template = function(tpl)
			for _, c in ipairs(tpl.components) do
				if c.type == "keen::ecs::ModelResource" then c.value.model = MODEL_GUID end
			end
			fit_collider(tpl)
		end,
	}
	-- the Workbench, known with the first furniture, listed after the crude chair; a game without that recipe gets the
	-- Stable in the own crafting menu (the helper's default), so it can still be crafted
	local done, why = pcall(content.add_recipe, recipe("Prop_Decoration_Comfort1_T0_Chair"))
	if not done then
		print("beastmaster stable: the Workbench recipe could not be copied (" .. tostring(why) .. "): the Stable is "
			.. "crafted in the own crafting menu instead")
		content.add_recipe(recipe(nil))
	end
end)
content.finish()  -- the names of what was added, also when a later step failed
if not ok then error(err, 0) end
print("beastmaster stable: added " .. DEBUG_NAME .. " (template " .. TEMPLATE_GUID .. ", model " .. MODEL_GUID .. ")"
	.. (loader.is_server and " on the dedicated server (no model, icon, name or menu entry there)" or ""))
