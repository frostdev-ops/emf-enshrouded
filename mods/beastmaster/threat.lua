-- Threat: the monsters in a fight with a player or their beasts out split between them. A monster joins a player's
-- fight when it hits them or one of their beasts, is hit by them, or goes for them (the game's pick, seen once a second).
-- Beastmaster then picks its target between the player and their beasts in reach, by share and closeness (a beast's
-- share: share() below; the player keeps the rest), keeps at least 1 in OWNER_EVERY of the fight's monsters on the
-- player, and holds the pick at least threat_hold_s: every server step it writes it as the monster's pick
-- (SelectedTargets.targets[1] targetEntityId and targetRootEntityId) with the monster's target switches off
-- (DynamicAggro.allowTargetSwitches). It picks again at once when the target falls, is sent home or is out of the
-- monster's reach. The game's own pick comes back (switches on, nothing written) on every way out: no beast left in
-- reach, the monster tamed or no longer hostile, its player gone, passive mode, the threat setting off.
-- Taunt (talents.lua): when a monster hits a player within a Taunt beast's radius of it, the nearest of their beasts
-- with Taunt ready roars: every monster of that player's fight within its radius is held on it for its hold time, over
-- the shares (a boss in the middle of an attack is left alone); then they pick as usual.
-- M.new(env) where env has: now(world), setting(key), tell(world, player, text), ids() (the beasts out: entity id ->
-- {account, beast}), mode(account), hostile(world, id) (a living hostile creature, not a player), boss(world, id).
local species = require("species")
local talents = require("talents")

local M = {}
local T = {}
T.__index = T

local REACH_M = 30     -- a target farther from the monster than this is out of its reach
local CLOSE_M = 5      -- a target weighs its share / (its distance + CLOSE_M): a nearer one weighs more
local OWNER_EVERY = 4  -- at least 1 in OWNER_EVERY of a fight's monsters stay on the player
local PLAYER_ALLY = 11
local NOT_REACHABLE = 2  -- DynamicAggro target reachableState: the game finds no way to it
local MID_ATTACK = 14    -- enemy01 State ControlledByAttack (DynamicEnemy01BehaviorState.state): in an attack

-- The share of the monsters in a fight `beast` draws (0..1): a tank's (boar, bear, aurochs) or a damage beast's
-- setting, and Menace on top; the settings and caps for all of a player's beasts are applied when a monster picks.
function M.share(beast, setting)
	local sp = species.by_id(beast.species)
	local pct = (sp and sp.tank) and setting("tank_share_pct") or setting("damage_share_pct")
	return math.min(1, (pct or 0) / 100 + talents.menace(beast))
end

function M.new(env)
	local t = setmetatable({ env = env }, T)
	t:reset()
	return t
end

-- A new world (or a reload): entity ids from before mean nothing now.
function T:reset()
	self.held = {}    -- monster id -> {account, player, target, until_s, taunt}
	self.owners = {}  -- account -> player entity, for the players in the world (from the last look)
	self.roared = setmetatable({}, { __mode = "k" })  -- beast -> when it roared last
end

local function dist(world, a, b)
	local ax, ay, az = world:position(a)
	local bx, by, bz = world:position(b)
	if not ax or not bx then return nil end
	return math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2 + (az - bz) ^ 2)
end

-- Whether the game marks `target` as not reachable for monster `m`.
local function unreachable(world, m, target)
	local a = world:get("DynamicAggro", m)
	if not (a and a.targets) then return false end
	for i = 1, #a.targets do
		local t = a.targets[i]
		if t.entityId == target and t.reachableState == NOT_REACHABLE then return true end
	end
	return false
end

-- Whether `id` can be monster `m`'s target in `account`'s fight: the player (`is_player`) or one of their beasts out,
-- alive, within REACH_M and not marked unreachable.
function T:usable(world, m, id, account, is_player)
	if not id or id == 0 then return false end
	if is_player then
		if not world:get("PlayerState", id) then return false end
	else
		local e = self.env.ids()[id]
		local f = e and e.account == account and world:get("Faction", id)
		if not (f and f.faction == PLAYER_ALLY) then return false end
	end
	local cur = emf.health(world, id)
	if (cur == nil and not is_player) or (cur ~= nil and cur <= 0) then return false end  -- a player's may be unknown
	local d = dist(world, m, id)
	if not d or d > REACH_M then return false end
	return not unreachable(world, m, id)
end

-- Monster `m`'s target in `account`'s fight (`player` their entity): nil when no beast of theirs is in its reach.
function T:pick(world, m, account, player)
	local s = self.env.setting
	local beasts, total = {}, 0
	for id, e in pairs(self.env.ids()) do
		if e.account == account and self:usable(world, m, id, account, false) then
			local share = M.share(e.beast, s)
			beasts[#beasts + 1] = { id = id, share = share }
			total = total + share
		end
	end
	if #beasts == 0 then return nil end
	table.sort(beasts, function(a, b) return a.id < b.id end)
	local cap = (self.env.boss(world, m) and s("boss_share_max_pct") or s("beasts_share_max_pct")) / 100
	local scale = total > cap and cap / total or 1
	local on_player = self:usable(world, m, player, account, true)
	if on_player then
		local n, there = self.held[m] and 0 or 1, 0  -- the fight's monsters, this one counted
		for id, h in pairs(self.held) do
			if h.account == account then
				n = n + 1
				if id ~= m and h.target == player then there = there + 1 end
			end
		end
		if there < math.floor(n / OWNER_EVERY) then return player end
	end
	local cands, sum = {}, 0
	local function add(id, share)
		local w = share / ((dist(world, m, id) or REACH_M) + CLOSE_M)
		if w > 0 then
			cands[#cands + 1] = { id = id, w = w }
			sum = sum + w
		end
	end
	if on_player then add(player, 1 - math.min(total, cap)) end
	for _, c in ipairs(beasts) do add(c.id, c.share * scale) end
	if sum <= 0 then return nil end
	local r = math.random() * sum
	for _, c in ipairs(cands) do
		r = r - c.w
		if r < 0 then return c.id end
	end
	return cands[#cands].id
end

-- Writes `target` as monster `m`'s pick, its switches off; false when it has no pick to write.
local function force(world, m, target)
	local a = world:get("DynamicAggro", m)
	local s = a and world:get("SelectedTargets", m)
	if not (s and s.targets) then return false end
	local t = s.targets[1]
	if t.targetEntityId ~= target then t.targetEntityId = target end
	if t.targetRootEntityId ~= target then t.targetRootEntityId = target end
	if a.allowTargetSwitches then a.allowTargetSwitches = false end
	return true
end

-- The game's own pick back for monster `m`: its switches on, nothing written any more.
function T:release(world, m)
	self.held[m] = nil
	local a = world:position(m) and world:get("DynamicAggro", m)
	if a and not a.allowTargetSwitches then a.allowTargetSwitches = true end
end

-- Whether Beastmaster picks targets in `account`'s fights now.
function T:active(account)
	return self.env.setting("threat") and self.env.mode(account) ~= "passive"
end

-- Monster `m` joins `account`'s fight (`player` their entity): its target picked and held.
function T:join(world, m, account, player)
	if self.held[m] or not player or not self:active(account) or not self.env.hostile(world, m) then return end
	if not (world:get("DynamicAggro", m) and world:get("SelectedTargets", m)) then return end
	local target = self:pick(world, m, account, player)
	if not target then return end
	self.held[m] = { account = account, player = player, target = target,
		until_s = self.env.now(world) + self.env.setting("threat_hold_s") }
	force(world, m, target)
end

-- The account (and player entity) whose side `id` is on: a beast out, or a player with a beast out.
function T:side_of(world, id)
	local e = self.env.ids()[id]
	if e then return e.account, self.owners[e.account] end
	if world:get("PlayerState", id) then
		local account = world:account(id)
		if account then
			for _, x in pairs(self.env.ids()) do
				if x.account == account then return account, id end
			end
		end
	end
	return nil
end

-- Whether `beast` can roar now: Taunt, and its cooldown (for its points now) over since it roared last.
function T:ready(world, beast)
	local t, cooldown = talents.taunt(beast)
	if not t then return nil end
	local now, last = self.env.now(world), self.roared[beast]
	if last and now >= last and now < last + cooldown then return nil end
	return t
end

local function mid_attack(world, m)
	local s = world:get("DynamicEnemy01BehaviorState", m)
	return s ~= nil and s.state == MID_ATTACK
end

-- Monster `hitter` hit `player` (of `account`): the nearest of their beasts with Taunt ready within its radius of the
-- hitter roars, and the monsters of their fight within that radius of it turn on it.
function T:taunt(world, hitter, account, player)
	if not self:active(account) then return end
	local ids = self.env.ids()
	local best, best_d, best_t
	for id, e in pairs(ids) do
		local t = e.account == account and self:ready(world, e.beast)
		if t and self:usable(world, hitter, id, account, false) then
			local d = dist(world, hitter, id)
			if d and d <= t.radius_m and (not best_d or d < best_d) then best, best_d, best_t = id, d, t end
		end
	end
	if not best then return end
	local beast = ids[best].beast
	local now = self.env.now(world)
	self.roared[beast] = now
	local side = { [player] = true }
	for id, e in pairs(ids) do
		if e.account == account then side[id] = true end
	end
	for _, m in ipairs(world:entities()) do
		if not side[m] and self.env.hostile(world, m) then
			local d = dist(world, best, m)
			local h = self.held[m]
			local fighting = m == hitter or (h and h.account == account)
			if not fighting and not h then
				local s = world:get("SelectedTargets", m)
				fighting = s and s.targets and side[s.targets[1].targetEntityId]
			end
			if fighting and d and d <= best_t.radius_m and not (self.env.boss(world, m) and mid_attack(world, m))
				and force(world, m, best) then
				self.held[m] = { account = account, player = player, target = best, until_s = now + best_t.hold_s, taunt = true }
			end
		end
	end
	self.env.tell(world, player, string.format("%s roars: the monsters turn on it.", beast.name))
end

-- A HitEvent: a monster that hits a player's side, or that their side hits, joins their fight; one that hits the
-- player may make a beast of theirs roar (Taunt).
function T:on_hit(world, source, target)
	if source == target or not self.env.setting("threat") then return end
	local account, player = self:side_of(world, target)
	if account then
		if self.env.hostile(world, source) then
			self:join(world, source, account, player)
			if target == player then self:taunt(world, source, account, player) end
		end
		return
	end
	account, player = self:side_of(world, source)
	if account and self.env.hostile(world, target) then self:join(world, target, account, player) end
end

-- Once a second, `present` (account -> player entity) the players in the world: a hostile creature whose own pick is a
-- player with a beast out, or one of their beasts, joins that player's fight.
function T:scan(world, present)
	self.owners = present
	if not self.env.setting("threat") then return end
	local side = {}
	for id, e in pairs(self.env.ids()) do
		local p = present[e.account]
		if p and self:active(e.account) then
			side[id], side[p] = e.account, e.account
		end
	end
	if next(side) == nil then return end
	for _, m in ipairs(world:entities()) do
		if not self.held[m] and not side[m] and self.env.hostile(world, m) then
			local s = world:get("SelectedTargets", m)
			local account = s and s.targets and side[s.targets[1].targetEntityId]
			if account then self:join(world, m, account, present[account]) end
		end
	end
end

-- Monster `id` died (or is gone): nothing more for it.
function T:forget(id)
	self.held[id] = nil
end

-- Every server step: each held monster checked and its pick written again; a new pick after the hold, or at once when
-- its target cannot be one any more; the game's own pick back on every way out.
function T:step(world)
	local now = self.env.now(world)
	for m, h in pairs(self.held) do
		local player_here = world:get("PlayerState", h.player) and world:account(h.player) == h.account
		if not (player_here and self:active(h.account) and self.env.hostile(world, m)) then
			self:release(world, m)
		else
			local is_player = h.target == h.player
			if now >= h.until_s or now < h.until_s - 60 or not self:usable(world, m, h.target, h.account, is_player) then
				local target = self:pick(world, m, h.account, h.player)
				if target then
					h.target, h.until_s, h.taunt = target, now + self.env.setting("threat_hold_s"), nil
				else
					self:release(world, m)
				end
			end
			if self.held[m] and not force(world, m, h.target) then self:release(world, m) end
		end
	end
end

-- The monsters held for `account`, for beastmaster.threat: "70 on Boar (5 s)".
function T:held_text(world, account)
	local ids, now, parts = self.env.ids(), self.env.now(world), {}
	for m, h in pairs(self.held) do
		if h.account == account then
			local e = ids[h.target]
			parts[#parts + 1] = string.format("%d on %s (%d s)", m, h.target == h.player and "you" or (e and e.beast.name or "?"),
				math.max(0, math.ceil(h.until_s - now)))
		end
	end
	table.sort(parts)
	return #parts > 0 and ("Monsters held: " .. table.concat(parts, ", ")) or "No monster is held."
end

return M
