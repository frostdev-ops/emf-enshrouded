-- Emberbrand: an example of new content for Enshrouded (an EML data patch; see docs/modding/content.md).
-- It adds a sword cloned from the Bronze Sword with its own name and stats, and a placeable "Ember Table" (a Workbench
-- copy without its crafting and its E prompt), each with a recipe the own crafting menu lists (wood logs; known once
-- you have had wood logs). It uses EMF's helper, emf_content.lua (a copy of content/lib/emf_content.lua next to this
-- file).
--
-- The GUIDs below are FIXED FOREVER: a save stores items by ItemId = FNV-1a(GUID) and placed objects by their
-- template's GUID, so a new GUID would make every saved Emberbrand and Ember Table vanish. Copy this mod for your own
-- content and pick new GUIDs once (any GUID generator), then never change them.
if not loader.features.patch then
	return
end

local content = require("emf_content")

local ITEM_GUID   = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e11"
local NAME_GUID   = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e12"
local RECIPE_GUID = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e13"
local DEBUG_NAME  = "Weapon_T1_1H_Sword_Emberbrand"   -- what EMF's "items" and "give" commands use

local TABLE_ITEM_GUID     = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e21"
local TABLE_TEMPLATE_GUID = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e22"
local TABLE_NAME_GUID     = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e23"
local TABLE_DESC_GUID     = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e24"
local TABLE_RECIPE_GUID   = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e25"
local TABLE_DEBUG_NAME    = "Prop_Emberbrand_Table"

local BRONZE_SWORD = "1d622bd9-8170-41b1-822e-d3984b386d95" -- Weapon_T1_1H_SwordEpic_01
local WORKBENCH    = "2f04a05f-697d-489b-a32f-76bb3439f5bc" -- Craftingstation_Workbench (the item that places it)
local WOOD_LOGS    = "3f4093ae-75ed-4e5d-bd0b-784ba83b88f5" -- Material_T1_Wood_Logs
local WEAPONS      = "2632c31c-37fe-48f6-b788-dfa649065edb" -- the own crafting menu's weapons group

local item = content.add_item{
	guid = ITEM_GUID, name_guid = NAME_GUID, debug_name = DEBUG_NAME, name = "Emberbrand",
	clone_of = BRONZE_SWORD,
	level = 3,  -- as the own crafting's other weapons (the Bronze Sword's range is 1 to 55: a craft would pick in it)
	edit = function(it)
		it.damageSetup.dmgMod = 1.75                          -- the Bronze Sword: 1.0
		it.damageSetup.distribution.magicalFireDamage = 0.5   -- and some fire
	end,
}

local recipe = content.add_recipe{
	guid = RECIPE_GUID, debug_name = "Craft_" .. DEBUG_NAME,
	output = {ITEM_GUID, 1},
	inputs = {{WOOD_LOGS, 5}},
	like = "Consumable_Tool_Campfire_PK",  -- own crafting, unlocked once you have had wood logs
	menu = {group = WEAPONS},
}

-- A table to place in your base: the Workbench's model and collider, no crafting, no villager work, no E prompt.
local table_item = content.add_placeable{
	guid = TABLE_ITEM_GUID, template_guid = TABLE_TEMPLATE_GUID, name_guid = TABLE_NAME_GUID,
	debug_name = TABLE_DEBUG_NAME, name = "Ember Table",
	description_guid = TABLE_DESC_GUID, description = "A sturdy table for your base. It crafts nothing.",
	clone_of = WORKBENCH,
	strip = {content.STATION_PARTS, content.PROMPT_PARTS},
}

local table_recipe = content.add_recipe{
	guid = TABLE_RECIPE_GUID, debug_name = "Craft_" .. TABLE_DEBUG_NAME,
	output = {TABLE_ITEM_GUID, 1},
	inputs = {{WOOD_LOGS, 4}},
	like = "Consumable_Tool_Campfire_PK",  -- listed next to the campfire
}

content.finish()

print(string.format("emf_emberbrand: added %s (item 0x%08x, recipe 0x%08x) and %s (item 0x%08x, recipe 0x%08x)%s",
	DEBUG_NAME, item, recipe, TABLE_DEBUG_NAME, table_item, table_recipe,
	loader.is_server and " on the dedicated server (no icon, name or menu entry: no UI there)" or ""))
