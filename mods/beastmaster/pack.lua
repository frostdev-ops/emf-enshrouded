-- The beasts out in the world: calling (mana, cooldown, limits), dismissing, resting after a fall, and every second:
-- checking each beast is still ours, bringing beasts back when their owner is in the world (after a reload, or a
-- guest who returns), and taking them out of the world while their owner is away or they step aside (aside.lua: while
-- a player builds next to them, or after beastmaster.aside).
-- M.new(env) where env has: setting(key), now(world), tell(world, player, text), beasts() (the roster), save(),
-- and optionally on_spawn(world, id, species, health_mul) after a beast was put into the world or its loyalty grew.
local species = require("species")
local loyalty = require("loyalty")
local aside_mod = require("aside")

local M = {}
local P = {}
P.__index = P

local PLAYER_ALLY = 11
local PLAYER_SIDE = { [7] = true, [8] = true, [PLAYER_ALLY] = true }  -- Player, AutomatedPlayer, PlayerAlly
-- The factions aggressive beasts go after: Fogger, Vukah, Scavenger, WildBeast, Skeleton, Cyclops, Drak, Ancients (never
-- wildlife or farm animals: goats and hares, a player's animals)
local HOSTILE = { [2] = true, [3] = true, [4] = true, [6] = true, [10] = true, [13] = true, [14] = true, [15] = true }
local SENT_S = 20       -- a passive beast that Attack sent at a creature fights it this long
local RECALL_S = 3      -- a beast that dropped a chase fights nothing this long while it comes back
local HEAL_AFTER_S = 5  -- a beast heals between fights: this long after its last hit to or from it,
local HEAL_SHARE = 0.05 -- this share of its maximum health a second
local KILL_HIT_S = 30   -- a foe a beast hit this long before it dies counts as its kill (a loyalty point)
local STRAY_CHECKS = 3  -- a beast that guards someone else this many checks (seconds) in a row is not ours any more
local REFUSALS = 3      -- a pack beast's call-back refused this many checks in a row: its pack is not in this game

function M.new(env)
	local p = setmetatable({ env = env, aside = aside_mod.new({ setting = env.setting }) }, P)
	p:reset()
	return p
end

-- A new world (or a reload): entity ids from before mean nothing now.
function P:reset()
	self.ids = {}        -- entity id -> {account, beast} for every beast out with a body
	self.last_call = {}  -- account -> time of the last call
	self.bodies = {}     -- fallen beasts' bodies to remove: id -> time, or true (not timed yet)
	self.fallen = {}     -- {account, beast} fallen since the last step (to tell their owner)
	self.last = nil
	self.next_check = 0
	self.next_save = 0
	self.far = {}        -- entity id -> when it was first seen beyond leash_m
	self.moved = {}      -- entity id -> when it was last teleported back
	self.credited = {}   -- foes whose killing blow went to an owner already
	self.strays = {}     -- entity id -> checks in a row it guarded someone else
	self.refused = {}    -- beast -> checks in a row the game refused to bring it back
	self.sent = {}       -- entity id -> until when Attack's order holds (a passive beast fights it until then)
	self.recall = {}     -- entity id -> until when a beast that dropped a chase fights nothing
	self.base = {}       -- entity id -> {damage, scale}: its template's damage multiplier and size, before role and loyalty
	self.fought = {}     -- entity id -> when it last hit or was hit (it heals HEAL_AFTER_S after that)
	self.hits = {}       -- hostile creature id -> {beast entity id -> when it hit it} (loyalty for the kill)
	self.aside:reset()
end

local function mmss(s)
	return string.format("%d:%02d", math.floor(s / 60), math.floor(s % 60))
end

-- Why a pack beast cannot come. The game has no such creature when its pack is missing and also when EML (or
-- ShroudForge), which loads the packs at game start, is missing; the mod cannot tell the two apart, so it names both.
local function no_pack(sp)
	return string.format("%s come with the EMF creature packs, and those need EML or ShroudForge to load",
		sp.plural or (sp.id .. "s"))
end
local INSTALL = "Install EML first, then the packs, and restart the game."

-- Clears what a beast's AI fights (every aggro target and every chosen target), so it turns from a fight: the game
-- picks targets again from what it senses and what hits it, so a passive beast is cleared every step.
local function calm(world, id)
	local a = world:get("DynamicAggro", id)
	if a and a.targets then
		for i = 1, #a.targets do
			local t = a.targets[i]
			if t.state ~= 0 then t.state = 0 end
			if t.aggro ~= 0 then t.aggro = 0 end
		end
	end
	local s = world:get("SelectedTargets", id)
	if s and s.targets then
		for i = 1, #s.targets do
			local t = s.targets[i]
			if t.targetEntityId ~= 0 then
				t.targetEntityId = 0
				t.targetRootEntityId = 0
			end
		end
	end
end

-- Still our beast: there, alive, and an ally.
local function ours(world, id)
	if not id or not world:position(id) then return false end
	local cur = emf.health(world, id)
	if not cur or cur <= 0 then return false end
	local f = world:get("Faction", id)
	return f ~= nil and f.faction == PLAYER_ALLY
end

local function level_of(world, id)
	local l = world:get("Level", id)
	return l and l.dataStorage[1] or nil
end

function P:out_count(account)
	local n = 0
	for _, b in ipairs(self.env.beasts():list(account)) do
		if b.out then n = n + 1 end
	end
	return n
end

-- Puts the beast into the world beside its owner, as their ally at their level. Returns the id, or nil, why and
-- whether the game refused the spawn (rather than the owner having no position yet).
function P:spawn(world, owner, account, beast)
	local sp = species.by_id(beast.species)
	if not sp then return nil, "unknown species " .. tostring(beast.species) end
	local x, y, z = world:position(owner)
	if not x then return nil, "your position is not known" end
	local id = emf.spawn(world, sp.template, x + 2, y + 0.5, z)
	if not id or id == 0 then
		if sp.pack then return nil, no_pack(sp), true end
		return nil, "the game refused the call (see the log)", true
	end
	local f = world:get("Faction", id)
	if f then f.faction = PLAYER_ALLY end
	local lvl, mine = world:get("Level", id), level_of(world, owner)
	if lvl and mine then lvl.dataStorage[1] = mine end
	local scaling = world:get("DynamicEnemyScaling", id)
	if scaling then scaling.nextUpdateTime = 0 end  -- the game rescales health and damage to the new level
	emf.emit("server", "StartGuardingEvent", { guardingEntityId = id, guardedEntityId = owner })
	self:fresh(id)
	self.fought[id] = self.env.now(world)
	self:strengthen(world, id, beast)
	beast.entity = id
	beast.out = true
	self.ids[id] = { account = account, beast = beast }
	return id
end

-- A beast the player just tamed (already an ally in the world).
-- A fresh entity for a beast: whatever was kept for an earlier creature on the same id (the game gives ids out again)
-- goes.
function P:fresh(id)
	self.base[id], self.sent[id], self.recall[id], self.fought[id] = nil, nil, nil, nil
end

-- When the game sets a beast's damage multiplier again (its rescale to a level), its role and loyalty go on top of the
-- game's new value.
function P:keep_damage(world, id, beast)
	local base = self.base[id]
	local dm = base and base.wrote and world:get("DamageMod", id)
	if dm and dm.dataStorage and math.abs(dm.dataStorage[3] - base.wrote) > 1e-4 then
		base.damage = dm.dataStorage[3]
		self:strengthen(world, id, beast)
	end
end

-- A beast out as strong as its role and loyalty: its health through the health watch (on_spawn with the multiplier),
-- its DamageMod multiplier and its ScaleComponent (when it has one) against its template's, kept from the first time.
function P:strengthen(world, id, beast)
	local sp = species.by_id(beast.species)
	local role_health, role_damage = species.role(sp)
	local lvl = loyalty.level(beast.points, self.env.setting("loyalty_pace"))
	local f = loyalty.factor(lvl)
	local base = self.base[id]
	if not base then
		local dm, sc = world:get("DamageMod", id), world:get("ScaleComponent", id)
		base = { damage = dm and dm.dataStorage and dm.dataStorage[3] or nil, scale = sc and sc.uniformScale or nil }
		self.base[id] = base
	end
	if self.env.on_spawn then self.env.on_spawn(world, id, sp, role_health * f) end
	local dm = base.damage and world:get("DamageMod", id)
	if dm and dm.dataStorage then
		base.wrote = base.damage * role_damage * f
		dm.dataStorage[3] = base.wrote
	end
	local sc = base.scale and world:get("ScaleComponent", id)
	if sc then sc.uniformScale = base.scale * loyalty.size(lvl) end
end

function P:adopt(account, beast, id, world)
	beast.entity = id
	beast.out = true
	self.ids[id] = { account = account, beast = beast }
	self.aside:release(account)  -- it stays, and the beasts that stepped aside by the command come back
	if world then
		self:fresh(id)
		self.fought[id] = self.env.now(world)
		self:strengthen(world, id, beast)
	end
end

-- The entity ids of `account`'s beasts in the world (where another player's building counts).
function P:spots(account)
	local out = {}
	for _, b in ipairs(self.env.beasts():list(account)) do
		if b.out and b.entity then out[#out + 1] = b.entity end
	end
	return out
end

-- What a player hears while building keeps the beasts aside ("build": their own, "near": another player's).
local function waits(why, what)
	if why == "near" then return "Your beasts wait while a player builds near you: " .. what .. " a few seconds after that." end
	return "Your beasts wait while you build: " .. what .. " a few seconds after you stop building."
end

-- beastmaster.call: returns the reply text.
function P:call(world, owner, account, beast)
	local s = self.env.setting
	if beast.out then return beast.name .. " is out already." end
	local busy, busy_why = self.aside:building(world, account, owner, self.env.now(world), self:spots(account))
	if busy then return waits(busy_why, "call again") end
	for _, b in ipairs(self.env.beasts():list(account)) do
		if b.out and b.species == beast.species then
			return string.format("Your %s %s is out already: one of each kind at a time.", species.word(b.species), b.name)
		end
	end
	if beast.rest > 0 then return string.format("%s rests for %s.", beast.name, mmss(beast.rest)) end
	local n, max = self:out_count(account), s("max_out")
	if n >= max then return string.format("You have %d beasts out (at most %d): dismiss one first.", n, max) end
	local now = self.env.now(world)
	local since = now - (self.last_call[account] or -math.huge)
	if since < s("call_cooldown_s") then
		return string.format("Calling again needs a moment: wait %d s.", math.ceil(s("call_cooldown_s") - since))
	end
	local mana, max_mana = emf.attribute(world, owner, "mana"), emf.attribute(world, owner, "max_mana")
	local cost = max_mana and math.ceil(max_mana * s("call_mana_pct") / 100) or 0
	if cost > 0 and (not mana or mana < cost) then return string.format("Not enough mana: calling costs %d.", cost) end
	local id, why, refused = self:spawn(world, owner, account, beast)
	if not id then
		local sp = species.by_id(beast.species)
		if refused and sp and sp.pack then return "The call failed: " .. why .. ". " .. INSTALL end
		return "The call failed: " .. why
	end
	if cost > 0 then emf.attribute_add(world, owner, "mana", -cost) end
	self.last_call[account] = now
	self.aside:release(account)  -- the beasts that stepped aside come back too
	self.env.save()
	return string.format("%s the %s comes.", beast.name, species.word(beast.species))
end

-- The reply when the command's aside ends: they come at once, or once the building next to them stops.
local function coming_back(self, world, account, owner)
	local busy, why = self.aside:building(world, account, owner, self.env.now(world), self:spots(account))
	if not busy then return "Your beasts come back." end
	if why == "near" then return "Your beasts come back a few seconds after the player near you stops building." end
	return "Your beasts come back a few seconds after you stop building."
end

-- beastmaster.aside: the beasts that are out step aside (leave the world, still out) or come back. Returns the reply.
function P:toggle_aside(world, account, owner)
	if self:out_count(account) == 0 then
		self.aside:release(account)
		return "No beast of yours is out."
	end
	if self.aside:toggle(account) then
		return "Your beasts step aside. Choose Come back on the wheel, type /beastmaster aside again, or call one, to "
			.. "bring them back."
	end
	return coming_back(self, world, account, owner)
end

-- The wheel's Come back: the command's aside ends.
function P:come_back(world, account, owner)
	if not self.aside:release(account) then return "Your beasts are not aside." end
	return coming_back(self, world, account, owner)
end

-- Aside now (by the command, or while building): the HUD and the list say so.
function P:is_aside(account)
	return self.aside:is_aside(account)
end

-- Aside by the command: the wheel offers Come back.
function P:aside_by_command(account)
	return self.aside:by_command(account)
end

-- beastmaster.dismiss: the beast leaves the world and waits at home.
function P:dismiss(world, account, beast)
	if not beast.out then return beast.name .. " is not out." end
	if beast.entity and ours(world, beast.entity) then emf.destroy(world, beast.entity) end
	if beast.entity then
		self.ids[beast.entity] = nil
		self.sent[beast.entity], self.recall[beast.entity], self.base[beast.entity] = nil, nil, nil
		self.fought[beast.entity] = nil
	end
	beast.entity = nil
	beast.out = false
	if self:out_count(account) == 0 then self.aside:release(account) end  -- none left to step aside
	self.env.save()
	return beast.name .. " goes home."
end

-- beastmaster.attack: every beast of `account` that is out attacks `target`.
function P:attack(world, account, target)
	local n = 0
	local now = self.env.now(world)
	for _, b in ipairs(self.env.beasts():list(account)) do
		if b.out and b.entity and ours(world, b.entity) then
			self.sent[b.entity] = now + SENT_S
			emf.emit("server", "AddThreatTargetEvent",
				{ attackerId = b.entity, targetId = target, threatValue = 1000, threatPoolId = 0 })
			n = n + 1
		end
	end
	return n
end

-- The nearest living hostile creature (HOSTILE) within `range` metres of `owner`, never a boss or a player; nil when
-- there is none.
local function nearest_foe(world, owner, range)
	local ox, oy, oz = world:position(owner)
	if not ox then return nil end
	local best, best_d
	for _, id in ipairs(world:entities()) do
		local f = world:get("Faction", id)
		if f and HOSTILE[f.faction] and not world:get("PlayerState", id) then
			local x, y, z = world:position(id)
			local d = x and math.sqrt((x - ox) ^ 2 + (y - oy) ^ 2 + (z - oz) ^ 2)
			if d and d <= range and (not best_d or d < best_d) and not world:get("Boss", id)
				and not world:get("BossHealthBar", id) then
				local cur = emf.health(world, id)
				if cur and cur > 0 then best, best_d = id, d end
			end
		end
	end
	return best
end

-- Aggressive beasts: every beast of `account` that is out goes at the nearest hostile creature near its owner.
function P:hunt(world, account, owner)
	local foe = nearest_foe(world, owner, self.env.setting("aggro_m"))
	if foe then self:attack(world, account, foe) end
end

-- Every hit to or from our beast is noted (it heals HEAL_AFTER_S after the last one).
-- XP credit: when our beast's hit leaves a foe at or under 25 % health, its owner lands the killing blow (a real game
-- hit: kill credit, XP and loot go to the owner). Once per foe; never on players or allies.
function P:on_hit(world, source, target)
	if self.ids[source] or self.ids[target] then
		local now = self.env.now(world)
		if self.ids[source] then self.fought[source] = now end
		if self.ids[target] then self.fought[target] = now end
		local f = self.ids[source] and world:get("Faction", target)
		if f and HOSTILE[f.faction] and not world:get("PlayerState", target) then
			self.hits[target] = self.hits[target] or {}
			self.hits[target][source] = now
		end
	end
	if not self.env.setting("xp_credit") then return end
	local e = self.ids[source]
	if not e or self.credited[target] then return end
	if world:get("PlayerState", target) then return end
	-- never a boss or a mini-boss (the killing blow would skip the last quarter of the fight)
	if world:get("Boss", target) or world:get("BossHealthBar", target) then return end
	local f = world:get("Faction", target)
	if not f or PLAYER_SIDE[f.faction] then return end
	local cur, max = emf.health(world, target)
	if not cur or not max or max <= 0 or cur <= 0 or cur * 100 > max * 25 then return end
	local owner = self:owner(world, e.account)
	if not owner then return end
	self.credited[target] = true
	emf.damage(world, owner, target, cur)
end

-- A loyalty point for the beast of entity `e`: a new loyalty is told to its owner and makes it stronger at once.
function P:gain(world, e)
	local b = e.beast
	local pace = self.env.setting("loyalty_pace")
	local before = loyalty.level(b.points, pace)
	b.points = (b.points or 0) + 1
	local after = loyalty.level(b.points, pace)
	self.env.save()
	if after > before then
		local owner = self:owner(world, e.account)
		if owner then self.env.tell(world, owner, string.format("%s's loyalty grows: %d.", b.name, after)) end
		if b.entity then self:strengthen(world, b.entity, b) end
	end
end

function P:on_died(id, world)
	local hitters = self.hits[id]
	self.hits[id] = nil
	if hitters and world then
		local now = self.env.now(world)
		for entity, at in pairs(hitters) do
			local e = self.ids[entity]
			if e and now - at <= KILL_HIT_S then self:gain(world, e) end
		end
	end
	self.credited[id] = nil
	self.sent[id], self.recall[id], self.base[id], self.fought[id] = nil, nil, nil, nil
	local e = self.ids[id]
	if not e then return end
	self.ids[id] = nil
	e.beast.entity = nil
	e.beast.out = false
	e.beast.rest = self.env.setting("rest_s")
	self.bodies[id] = true
	self.fallen[#self.fallen + 1] = e
	self.env.save()
end

-- Brings a beast that strayed back beside its owner: beyond leash_m for 3 s, or beyond leash_far_m at once. Through
-- the game's own teleport (ground search); when that is refused, the beast is called again beside its owner.
function P:leash(world, owner, b, now)
	local ox, oy, oz = world:position(owner)
	local bx, by, bz = world:position(b.entity)
	if not ox or not bx then return end
	local d = math.sqrt((ox - bx) ^ 2 + (oy - by) ^ 2 + (oz - bz) ^ 2)
	local s = self.env.setting
	-- beyond chase_m it drops its chase and runs back: it fights nothing for RECALL_S and guards its owner again
	if d > s("chase_m") and d <= s("leash_m") and now >= (self.recall[b.entity] or 0) then
		self.recall[b.entity] = now + RECALL_S
		calm(world, b.entity)
		emf.emit("server", "StartGuardingEvent", { guardingEntityId = b.entity, guardedEntityId = owner })
	end
	if d <= s("leash_m") then
		self.far[b.entity] = nil
		return
	end
	self.far[b.entity] = self.far[b.entity] or now
	if d <= s("leash_far_m") and now - self.far[b.entity] < 3 then return end
	if now - (self.moved[b.entity] or -math.huge) < 3 then return end  -- a teleport takes a moment: no repeats
	self.moved[b.entity] = now
	self.far[b.entity] = nil
	if not emf.teleport(world, b.entity, ox + 2, oy + 0.5, oz, { ground = true }) then
		local account
		for id, e in pairs(self.ids) do
			if id == b.entity then account = e.account end
		end
		emf.destroy(world, b.entity)
		self.ids[b.entity] = nil
		b.entity = nil
		if account then self:spawn(world, owner, account, b) end
	end
end

-- account text -> player entity, for every player in the world
local function players(world)
	local out = {}
	for _, id in ipairs(world:entities()) do
		if world:get("PlayerState", id) then
			local account = world:account(id)
			if account then out[account] = id end
		end
	end
	return out
end

-- The entity of the player with `account` in the world, or nil.
function P:owner(world, account)
	return players(world)[account]
end

function P:step(world)
	local now = self.env.now(world)
	local dt = self.last and math.max(0, now - self.last) or 0
	self.last = now
	local beasts = self.env.beasts()
	local resting = false
	for _, account in ipairs(beasts:accounts()) do
		for _, b in ipairs(beasts:list(account)) do
			if b.rest > 0 then
				b.rest = math.max(0, b.rest - dt)
				resting = true
				if b.rest == 0 then self.env.save() end
			end
		end
	end
	if resting and now >= self.next_save then
		self.next_save = now + 10
		self.env.save()  -- the rest left survives a reload
	end
	for id, due in pairs(self.bodies) do
		if due == true then
			self.bodies[id] = now + 1
		elseif now >= due then
			self.bodies[id] = nil
			local cur = emf.health(world, id)
			-- no loot from our own beasts; only while it is still a body (the game may have given the id away)
			if world:position(id) and (not cur or cur <= 0) then emf.destroy(world, id) end
		end
	end
	-- every step: passive beasts (unless Attack sent them) and beasts coming back from a chase fight nothing
	for id, e in pairs(self.ids) do
		local passive = self.env.mode and self.env.mode(e.account) == "passive" and now >= (self.sent[id] or 0)
		if passive or now < (self.recall[id] or 0) then calm(world, id) end
	end
	if self.aside:watch(world, now) then self.next_check = now end  -- someone started building: step aside now
	if now < self.next_check then return end
	self.next_check = now + 1
	-- hits on foes that did not die within KILL_HIT_S count for nothing any more
	for target, hitters in pairs(self.hits) do
		for entity, at in pairs(hitters) do
			if now - at > KILL_HIT_S then hitters[entity] = nil end
		end
		if next(hitters) == nil then self.hits[target] = nil end
	end
	local present = players(world)
	self.aside:set_players(present)
	for _, e in ipairs(self.fallen) do
		local owner = present[e.account]
		if owner then
			self.env.tell(world, owner, string.format("%s fell; it rests for %s.", e.beast.name, mmss(e.beast.rest)))
		end
	end
	self.fallen = {}
	for _, account in ipairs(beasts:accounts()) do
		local owner = present[account]
		if self:out_count(account) == 0 then self.aside:release(account) end  -- none left to step aside
		local away, why = false, nil
		if owner then away, why = self.aside:check(world, account, owner, now, self:spots(account)) end
		local stepped = false
		for _, b in ipairs(beasts:list(account)) do
			if b.out and b.entity then
				if not ours(world, b.entity) then
					self.ids[b.entity] = nil  -- gone, or the id names another entity now: never touched again
					b.entity = nil
					b.out = false
					self.env.save()
				elseif not owner or away then
					emf.destroy(world, b.entity)  -- the owner left, or it steps aside: back when that ends (still out)
					self.ids[b.entity] = nil
					b.entity = nil
					stepped = stepped or away
				else
					local ai = world:get("DynamicEnemy01BehaviorState", b.entity)
					local guard = ai and ai.guardEntityId or 0
					if guard ~= 0 and guard ~= owner then
						-- it guards someone else: after STRAY_CHECKS in a row, the id names another entity now
						self.strays[b.entity] = (self.strays[b.entity] or 0) + 1
					else
						self.strays[b.entity] = nil
					end
					if (self.strays[b.entity] or 0) >= STRAY_CHECKS then
						self.strays[b.entity] = nil
						self.ids[b.entity] = nil  -- never touched again
						b.entity = nil
						b.out = false
						self.env.save()
					else
						self:leash(world, owner, b, now)
						-- its level follows its owner's (a level up while it is out); the leash may have let it go
						local lvl, mine = b.entity and world:get("Level", b.entity), level_of(world, owner)
						if lvl and mine and lvl.dataStorage[1] ~= mine then
							lvl.dataStorage[1] = mine
							local scaling = world:get("DynamicEnemyScaling", b.entity)
							if scaling then scaling.nextUpdateTime = 0 end
						end
						if b.entity then
							self:keep_damage(world, b.entity, b)
							-- between fights it heals
							local cur, max = emf.health(world, b.entity)
							if cur and max and cur > 0 and cur < max and now - (self.fought[b.entity] or now) >= HEAL_AFTER_S then
								emf.attribute_add(world, b.entity, "health", math.min(math.ceil(max * HEAL_SHARE), max - cur))
							end
						end
						if guard ~= owner then
							emf.emit("server", "StartGuardingEvent", { guardingEntityId = b.entity, guardedEntityId = owner })
						end
					end
				end
			elseif b.out and owner and b.rest == 0 and not away then
				local id, why, refused = self:spawn(world, owner, account, b)  -- after a reload, or its owner came back
				local sp = species.by_id(b.species)
				if id then
					self.refused[b] = nil
				elseif not sp or (sp.pack and refused) then
					-- refused REFUSALS checks in a row: this game has no such beast (its pack is not installed), so it
					-- waits at home instead of being tried every second; an unknown species at once
					self.refused[b] = (self.refused[b] or 0) + 1
					if not sp or self.refused[b] >= REFUSALS then
						self.refused[b] = nil
						b.out = false
						self.env.save()
						self.env.tell(world, owner, string.format("%s stays home: %s.", b.name, why))
					end
				end
			end
		end
		if owner and not away and self.env.mode and self.env.mode(account) == "aggressive" and self:out_count(account) > 0 then
			self:hunt(world, account, owner)
		end
		if stepped and why == "build" and self.aside:first(account, why) then
			self.env.tell(world, owner, "Your beasts step aside while you build (the game most likely blocks building "
				.. "next to them). They come back a few seconds after you stop building.")
		elseif stepped and why == "near" and self.aside:first(account, why) then
			self.env.tell(world, owner, "Your beasts step aside while a player builds near them (the game most likely "
				.. "blocks building next to them). They come back a few seconds after that.")
		end
	end
end

return M
