-- emf_content.lua: helpers for EML content mods built by EMF (docs/modding/content.md, "The helper").
-- Copy this file into your mod's src folder and load it with:   local content = require("emf_content")
-- Everything here edits the game's data while EMF builds content at start; nothing is changed on disk.
--
-- The rules that keep players' saves safe:
-- * GUIDs are FIXED FOREVER: a save stores items, recipes and placed objects by id (FNV-1a of the GUID). Pick new GUIDs
--   once (any GUID generator) and never change them; never delete something a released version added.
-- * Every player in a world, and the dedicated server, needs the mod. A world loaded without it drops what the mod
--   added (placed objects vanish on the next save, items from inventories).
-- * The game's UI data (the crafting menu, icons, text) only exists in the game, not on the dedicated server: these
--   helpers skip it there (loader.is_server).
local M = {}

M.version = 1

local function fail(fmt, ...) error("emf_content: " .. string.format(fmt, ...), 3) end

-- FNV-1a-32 of a text, as the game hashes its own ids and EMF its custom E prompt words.
function M.fnv1a(text)
	local h = 2166136261
	for i = 1, #text do
		h = h ~ text:byte(i)
		h = (h * 16777619) & 0xFFFFFFFF
	end
	return h
end

-- The loca id EMF gives an E prompt's own words (EMF API v25 EmfOffer.text, Lua emf.offer{text = ...}).
function M.verb_id(words) return M.fnv1a("emf.verb:" .. words) end

-- The id of a GUID (ItemId, RecipeId, ...).
function M.id(guid) return game.guid.hash(guid) end

-- ---- text ----

local keenglish = nil  -- the English loca table, read once and written back by M.finish()

local function loca()
	if keenglish then return keenglish end
	local collection = game.assets.get_resources_by_type("keen::LocaTagCollectionResource")[1].data
	local content = game.assets.get_content(game.guid.from_content_hash(collection.keenglishData.hash))
	keenglish = {collection = collection, data = content:read_data():read_resource("keen::LocaTagCollectionResourceData")}
	return keenglish
end

-- Adds text under a loca id (the game falls back to English for every language). Game only: does nothing on the
-- dedicated server. Call M.finish() once at the end of your mod.lua.
function M.add_text(id, text)
	if loader.is_server then return end
	local tags = loca().data.tags
	local at = #tags + 1
	for i, tag in ipairs(tags) do
		if tag.id.value == id then
			if tag.text == text then return end
			fail("the loca id 0x%08x is taken by %q", id, tag.text)
		end
		if tag.id.value > id then at = i break end
	end
	table.insert(tags, at, {id = {value = id}, text = text, arguments = {}, genericArguments = 0})
end

-- Words for an E prompt of your own (emf.offer{text = words}): the game then shows them for everyone with the mod.
function M.add_prompt_words(words) M.add_text(M.verb_id(words), words) end

-- Writes the text back. Call it once, after your last M.add_* call.
function M.finish()
	if loader.is_server or not keenglish then return end
	local buf = buffer.create()
	buf:write_resource("keen::LocaTagCollectionResourceData", keenglish.data)
	keenglish.collection.keenglishData.hash = game.assets.create_content(buf)
	keenglish = nil
end

-- ---- items ----

local function item_registry() return game.assets.get_resources_by_type("keen::ItemRegistryResource")[1].data end

-- Adds an item copied from another (its model, icon, animations and perks), then your changes.
--   guid, name_guid: fixed GUIDs of your own; debug_name: what EMF's "items" and "give" use; name: what players see;
--   clone_of: the source item's GUID; edit(item): optional changes to the copy (fields of keen::ItemInfo).
-- Returns the item's id.
function M.add_item(def)
	for _, k in ipairs({"guid", "name_guid", "debug_name", "name", "clone_of"}) do
		if not def[k] then fail("add_item: %s is missing", k) end
	end
	local src = game.assets.get_resource(def.clone_of, "keen::ItemInfo")
	if not src then fail("add_item: no item %s to copy (the game changed?)", def.clone_of) end
	local item = game.assets.create_resource(src.data, "keen::ItemInfo", def.guid, 0).data
	local id = M.id(def.guid)
	item.itemId = {value = id}
	item.objectId = def.guid
	item.name = def.name_guid
	item.debugName = def.debug_name
	if def.edit then def.edit(item) end
	if not loader.is_server then  -- the icon (a UiTextureResource under the item's own GUID) and the name
		local icon = game.assets.get_resource(def.icon_of or def.clone_of, "keen::UiTextureResource")
		if not icon then fail("add_item: no icon on %s", def.icon_of or def.clone_of) end
		game.assets.create_resource(icon.data, "keen::UiTextureResource", def.guid, 0)
		M.add_text(M.id(def.name_guid), def.name)
	end
	local reg = item_registry()
	table.insert(reg.itemRefs, def.guid)
	table.insert(reg.dbgNames, def.debug_name)
	return id
end

-- ---- recipes ----

local function recipe_registry() return game.assets.get_resources_by_type("keen::RecipeRegistryResource")[1].data end

local function find_recipe(debug_name)
	for _, r in ipairs(recipe_registry().recipes) do
		if r.debugName == debug_name then return r end
	end
	return nil
end

-- The crafting menu's tree of a workshop (0: the player's own crafting). Game only.
local function menu_tree(workshop)
	local col = game.assets.get_resources_by_type("keen::FbUiBundle")[1].data.menu.crafting.recipes
	for i, w in ipairs(col.workshops) do
		if w.value == workshop then return col.trees[i] end
	end
	return nil
end

-- The menu's groups of a workshop with their sets and recipes (to pick a place for yours). Game only.
function M.menu_groups(workshop)
	local out = {}
	if loader.is_server then return out end
	local tree = menu_tree(workshop or 0)
	if not tree then return out end
	for _, g in ipairs(tree.groups) do
		local sets = {}
		for _, s in ipairs(g.sets) do sets[#sets + 1] = #s.entries end
		out[#out + 1] = {group = g.groupId, sets = sets}
	end
	return out
end

-- Adds a recipe, listed in the crafting menu.
--   guid: fixed; debug_name; output = {guid, count}; inputs = {{guid, count}, ...} (1 to 4);
--   like: a vanilla recipe's debugName whose settings (station, unlock, duration) are copied (default: the campfire,
--   own crafting, known from the start);
--   unlock: "always", {item = guid} (once the player has had that item) or nil (the copied recipe's);
--   menu = {group = "<groupId GUID>", set = 1}: where it is listed (see M.menu_groups); nil: the first set of the
--   copied recipe's group.
-- Returns the recipe's id.
function M.add_recipe(def)
	for _, k in ipairs({"guid", "debug_name", "output", "inputs"}) do
		if not def[k] then fail("add_recipe: %s is missing", k) end
	end
	if #def.inputs < 1 or #def.inputs > 4 then fail("add_recipe: 1 to 4 inputs") end
	local like = def.like or "Consumable_Tool_Campfire_PK"
	local source = find_recipe(like)
	if not source then fail("add_recipe: no recipe %s to copy (the game changed?)", like) end
	local recipes = recipe_registry().recipes
	table.insert(recipes, source)
	local r = recipes[#recipes]
	local id = M.id(def.guid)
	r.recipeGuid = def.guid
	r.recipeId = {value = id}
	r.debugName = def.debug_name
	while #r.output > 1 do table.remove(r.output) end
	r.output[1].itemRef = def.output[1]
	r.output[1].item = {value = M.id(def.output[1])}
	r.output[1].count = def.output[2] or 1
	while #r.input > 1 do table.remove(r.input) end
	local template = r.input[1]
	for i, inp in ipairs(def.inputs) do
		if i > 1 then table.insert(r.input, template) end
		local slot = r.input[i]
		slot.itemStack.itemRef = inp[1]
		slot.itemStack.item = {value = M.id(inp[1])}
		slot.itemStack.count = inp[2] or 1
		slot.inputItemCategory.categoryRef = "00000000-0000-0000-0000-000000000000"
		slot.inputItemCategory.category = {value = 0}
		slot.inputItemCategory.count = 0
	end
	r.requiresSheltered = def.sheltered or false
	r.completionRequirementQuery.knowledgeOrQueryId = {value = id}  -- "crafted once" is tracked per recipe
	if def.unlock == "always" then
		r.knowledgeRequirement.knowledgeOrQueryId = {value = 0}
		r.knowledgeRequirement.compareValue = 0
	elseif type(def.unlock) == "table" and def.unlock.item then
		r.knowledgeRequirement.knowledgeOrQueryId = {value = M.id(def.unlock.item)}
		r.knowledgeRequirement.compareValue = 1
	end
	if not loader.is_server then
		local workshop = r.workshopId and r.workshopId.value or 0
		local tree = menu_tree(workshop)
		if not tree then fail("add_recipe: the crafting menu has no tree for workshop %d", workshop) end
		local placed = false
		for _, g in ipairs(tree.groups) do
			local wanted = def.menu and def.menu.group
			local here = false
			if wanted then
				here = g.groupId == wanted
			else  -- the copied recipe's group
				for _, s in ipairs(g.sets) do
					for _, e in ipairs(s.entries) do
						if e.value == source.recipeId.value then here = true end
					end
				end
			end
			if here then
				local set = g.sets[(def.menu and def.menu.set) or 1]
				if not set then fail("add_recipe: the group has no set %d", def.menu.set) end
				table.insert(set.entries, {value = id})
				placed = true
				break
			end
		end
		if not placed then fail("add_recipe: no menu group %s in workshop %d", tostring(def.menu and def.menu.group), workshop) end
	end
	return id
end

return M
