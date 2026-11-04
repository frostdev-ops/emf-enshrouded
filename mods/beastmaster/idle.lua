-- Idle: while a player stands still, their beasts out pass the time, each in a scene of its own; they get up and follow
-- the moment the player moves on.
--   Idling starts when the player has moved less than STILL_M for idle_s seconds, and ends at once when they are
--   WAKE_M from where it began.
--   Settle: a beast whose graph has sleep states (species.sleeps) lies down where it is. Sniffing about: nothing is
--   written; the game's own roaming goes on.
-- A beast stays put through the game's own sleep, on its runtime DynamicEnemy01BehaviorState: the Sleep feature bit on
-- in enabledFeatures (and off in disabledFeatures), sleepEndTime far ahead, state Sleep; the game then plays the sleep
-- animation of a graph that has one. The wake gives the masks back as they were, sleepEndTime 0, and state Idle while it
-- is still Sleep: the game's AI follows its owner again from the next step.
-- M.new(env) where env has: now(world), setting(key), ids() (the beasts out: entity id -> {account, beast}).
local species = require("species")

local M = {}
local I = {}
I.__index = I

M.SLEEP_BIT = 1 << 21  -- EnemyFeature Sleep, in DynamicEnemy01BehaviorState.enabledFeatures and disabledFeatures
local SLEEP, IDLE = 25, 2  -- enemy01 State Sleep, Idle
local SLEEP_AHEAD_NS = 1000000000000000000  -- sleepEndTime this far after the state's last change: asleep until woken
local STILL_M = 1  -- the player moved less than this from where their stillness began: still
local WAKE_M = 2   -- idling ends when the player is this far from where it began
-- the scenes a beast picks from, in this order, with their weights (a pick among the ones it can have)
local WEIGHTS = { { "settle", 4 }, { "fire", 3 }, { "watch", 3 }, { "sniff", 1 } }

function M.new(env)
	local i = setmetatable({ env = env }, I)
	i:reset()
	return i
end

-- A new world (or a reload): entity ids from before mean nothing now.
function I:reset()
	self.players = {}  -- account -> {owner, x, y, z (where their stillness began), since, idle, sx, sy, sz (where idling
	                   -- began)}
	self.scenes = {}   -- beast entity id -> {account, scene, held, had_bit, had_off}
end

local function dist(ax, ay, az, bx, by, bz)
	return math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2 + (az - bz) ^ 2)
end

-- Holds beast `id` in the game's Sleep state; `sc` keeps what its masks had. True when held.
local function lie(world, id, sc)
	if sc.held then return true end
	local ai = world:get("DynamicEnemy01BehaviorState", id)
	if not ai then return false end
	sc.had_bit = (ai.enabledFeatures & M.SLEEP_BIT) ~= 0
	sc.had_off = (ai.disabledFeatures & M.SLEEP_BIT) ~= 0
	if not sc.had_bit then ai.enabledFeatures = ai.enabledFeatures | M.SLEEP_BIT end
	if sc.had_off then ai.disabledFeatures = ai.disabledFeatures & ~M.SLEEP_BIT end
	ai.sleepEndTime = ai.stateChangeTime + SLEEP_AHEAD_NS
	ai.state = SLEEP
	sc.held = true
	return true
end

-- Lets beast `id` up: its masks as they were, no sleep end, and its AI back to Idle while it is still asleep (one that
-- a fight moved on already is left as it is).
local function wake(world, id, sc)
	if not sc.held then return end
	sc.held = false
	local ai = world:get("DynamicEnemy01BehaviorState", id)
	if not ai then return end
	if not sc.had_bit then ai.enabledFeatures = ai.enabledFeatures & ~M.SLEEP_BIT end
	if sc.had_off then ai.disabledFeatures = ai.disabledFeatures | M.SLEEP_BIT end
	ai.sleepEndTime = 0
	if ai.state == SLEEP then ai.state = IDLE end
end

-- A scene for beast `id` of `account`, picked by WEIGHTS among the ones it can have, and set up.
function I:pick(world, id, account)
	local e = self.env.ids()[id]
	local sp = species.by_id(e.beast.species)
	local can = { settle = species.sleeps(sp) and species.stays(sp), sniff = true }
	local total = 0
	for _, w in ipairs(WEIGHTS) do
		if can[w[1]] then total = total + w[2] end
	end
	local r, scene = math.random() * total, "sniff"
	for _, w in ipairs(WEIGHTS) do
		if can[w[1]] then
			if r < w[2] then
				scene = w[1]
				break
			end
			r = r - w[2]
		end
	end
	local sc = self.scenes[id] or { account = account }
	self.scenes[id] = sc
	sc.scene = scene
	if scene == "settle" then
		lie(world, id, sc)
	else
		wake(world, id, sc)
	end
end

-- Idling ends for `account`: every beast of theirs is let up, and their stillness counts from now.
function I:stop(world, account)
	for id, sc in pairs(self.scenes) do
		if sc.account == account then
			wake(world, id, sc)
			self.scenes[id] = nil
		end
	end
	local p = self.players[account]
	if not p then return end
	p.idle = false
	local x, y, z
	if p.owner then x, y, z = world:position(p.owner) end
	if x then p.x, p.y, p.z = x, y, z end
	p.since = self.env.now(world)
end

-- Beast `id` died (or is gone): nothing more for it.
function I:forget(id)
	self.scenes[id] = nil
end

-- One player (`owner`, nil when not in the world) with `list`, their beasts out.
function I:step_player(world, account, owner, list, now)
	local x, y, z
	if owner then x, y, z = world:position(owner) end
	local p = self.players[account]
	if not x then
		if p then self:stop(world, account) end
		self.players[account] = nil
		return
	end
	if not p then
		self.players[account] = { owner = owner, x = x, y = y, z = z, since = now }
		return
	end
	p.owner = owner
	if p.idle then
		if dist(x, y, z, p.sx, p.sy, p.sz) >= WAKE_M then self:stop(world, account) end
		return
	end
	if dist(x, y, z, p.x, p.y, p.z) >= STILL_M then
		p.x, p.y, p.z, p.since = x, y, z, now
		return
	end
	if now - p.since < self.env.setting("idle_s") then return end
	p.idle, p.sx, p.sy, p.sz = true, p.x, p.y, p.z
	for _, id in ipairs(list) do self:pick(world, id, account) end
end

-- Every server step: `owners` (account -> player entity) the players in the world as of the last look.
function I:step(world, owners)
	local now = self.env.now(world)
	local ids = self.env.ids()
	for id in pairs(self.scenes) do
		if not ids[id] then self.scenes[id] = nil end  -- sent home, fallen, aside or not ours: the id may name another now
	end
	local out = {}  -- account -> the entity ids of their beasts out
	for id, e in pairs(ids) do
		out[e.account] = out[e.account] or {}
		table.insert(out[e.account], id)
	end
	for account in pairs(self.players) do
		if not out[account] then self.players[account] = nil end
	end
	for account, list in pairs(out) do
		table.sort(list)
		self:step_player(world, account, owners[account], list, now)
	end
end

return M
