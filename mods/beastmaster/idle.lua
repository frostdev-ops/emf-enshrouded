-- Idle: while a player stands still, their beasts out pass the time, each in a scene of its own; they get up and follow
-- the moment the player moves on.
--   Idling starts when the player has moved less than STILL_M for idle_s seconds, with the setting idle on, no hit to
--   or from them or one of their beasts for QUIET_S, no monster held for them by the threat share, and not building
--   (nor their beasts aside). A beast takes part while it guards its owner, has no Attack order on (nor comes back from
--   a chase), is within NEAR_M of them and its AI idles, roams or guards (TAKING_PART; Sleep while a scene holds it); one
--   that meets that later joins, one that stops meeting it (farther than NEAR_M, the game moved its AI on: a fall,
--   water) drops out alone and may join again. Idling ends at once, for every beast of that player, when they are
--   WAKE_M from where it began, on a hit (in the event), a monster held, building, the setting off, Attack, a dismiss
--   or a call; their stillness then counts from that moment.
--   Scenes, one per beast, picked by WEIGHTS among the ones it can have when it joins the idling, and again every
--   SCENE_MIN_S to SCENE_MAX_S (the same scene with the same target stays as it is):
--     Settle: a beast whose graph has sleep states (species.sleeps) lies down where it is.
--     By the fire: a fire within NEAR_M of the beast (a flame altar, or a ComfortProvider of a FIRE_COMFORT category):
--       it turns to face the nearest and stays put (one with sleep states lies down facing it).
--     Watching: a villager within NEAR_M of it: it turns to face the nearest and stays put (lies down facing them).
--     Night watch: at night (NIGHT_FROM to NIGHT_TO) with two or more beasts in the idling, one without sleep states (the
--       first by entity id) keeps watch, turned away from its owner and held; the beasts with sleep states settle. With
--       none without sleep states, or one beast alone, nobody keeps watch.
--     Sniffing about: nothing written; the game's own roaming.
--   A kind with stays = false (species.stays) is never held: it only turns.
--   A turn is the game's teleport in place (its own position, no ground search) with a yaw (0 = +z, 90 = +x); a
--   refused one (a beast spawned a moment ago reads as dead) is no turn. While a turned beast is held, the game's own
--   turning (ROTATE_BITS) is off in its disabledFeatures, or the AI turns it back to its owner within seconds.
-- A beast stays put through the game's own sleep, on its runtime DynamicEnemy01BehaviorState: the Sleep feature bit on
-- in enabledFeatures (and off in disabledFeatures), state Sleep, and sleepEndTime a lease: the game's time plus LEASE_S,
-- renewed every RENEW_S while the scene holds it, so a beast nothing renews gets up by itself. nextSleepCheckTime is
-- pushed far ahead, so the game never puts it to sleep on its own. The game then plays the sleep animation of a graph
-- that has one. The wake clears the Sleep bit, gives back the disabledFeatures bits that were not set before,
-- sleepEndTime 0, and state Idle while it is still Sleep: the game's AI follows its owner again from the next step.
-- The game's time is not given to Lua: it is kept as an offset from env.now, taken from what the game writes as "now":
-- a beast's SpawnTime when Beastmaster called it (env.called_at), and an AI's stateChangeTime the step it changes. With
-- no offset yet nothing is held (a scene that wants to hold waits for one).
-- Fires and villagers come from a search of the server world, SCAN_PER_STEP entities a step: at the first step after a
-- reset, and when a player with a beast out has stood still for half of idle_s while the last search began FIND_EVERY_S
-- ago or more (or none did since the reset). Every search also wakes a player-side creature (Faction PLAYER_ALLY) whose
-- runtime enabledFeatures has the Sleep bit and that no scene of ours holds: no beast's template has the bit, so it is
-- one a scene held before the mod's start, a reload or a world load.
-- An error in the idle code (step, a hit, an end) lets every beast of the idling up, drops the search under way (the
-- next waits FIND_EVERY_S) and is logged once per message; idling starts anew.
-- M.new(env) where env has: now(world), setting(key), ids() (the beasts out: entity id -> {account, beast}),
-- busy(world, id) (an Attack order on the beast, or it comes back from a chase), held(account) (the threat share holds
-- a monster in that player's fight), building(world, account, owner) (the player builds, or their beasts are aside),
-- called_at(id) (env.now when Beastmaster spawned that beast, or nil), kept(id) (optional: a creature another part of
-- the mod holds asleep, a beast at home: the search leaves it as it is).
local species = require("species")

local M = {}
local I = {}
I.__index = I

M.SLEEP_BIT = 1 << 21  -- EnemyFeature Sleep, in DynamicEnemy01BehaviorState.enabledFeatures and disabledFeatures
-- EnemyFeature Rotate, RotateWhileStanding and IdleTurn, in disabledFeatures while a turned beast is held
local ROTATE_BITS = (1 << 5) | (1 << 6) | (1 << 17)
local SLEEP, IDLE = 25, 2  -- enemy01 State Sleep, Idle
local TAKING_PART = { [2] = true, [15] = true, [20] = true }  -- enemy01 State Idle, Roaming, Guard
local LEASE_S = 120  -- sleepEndTime: the game's time plus this; a beast whose lease nothing renews gets up
local RENEW_S = 5    -- the lease is renewed this often while a scene holds the beast
local CHECK_AHEAD_NS = 1000000000000000000  -- nextSleepCheckTime this far after the game's time: no sleep of its own
local NS = 1000000000
local STILL_M = 1  -- the player moved less than this from where their stillness began: still
local WAKE_M = 2   -- idling ends when the player is this far from where it began
local NEAR_M = 15  -- a beast takes part within this of its owner; a fire or a villager within this of a beast
local QUIET_S = 10 -- no hit to or from the player or one of their beasts for this long before idling
local SCENE_MIN_S, SCENE_MAX_S = 30, 60  -- a scene lasts this long, then the beast picks again
local NIGHT_FROM, NIGHT_TO = 18, 6  -- the game's night: its clock's night start and day start (hours)
local SCAN_PER_STEP = 250  -- entities looked at per step while the world is searched
local FIND_EVERY_S = 30    -- s between two searches for fires and villagers while players stand still
local PLAYER_ALLY = 11
-- ComfortProvider categories that are fires: Hearth (campfires, fireplaces) and Flame
local FIRE_COMFORT = { [0] = true, [12] = true }
-- the scenes a beast picks from, in this order, with their weights (a pick among the ones it can have)
local WEIGHTS = { { "settle", 4 }, { "fire", 3 }, { "watch", 3 }, { "sniff", 1 } }

function M.new(env)
	local i = setmetatable({ env = env, logged = {} }, I)
	i:reset()
	return i
end

-- A new world (or a reload): entity ids from before mean nothing now.
function I:reset()
	self.players = {}    -- account -> {owner, x, y, z (where their stillness began), since, idle, sx, sy, sz (where idling
	                     -- began)}
	self.scenes = {}     -- beast entity id -> {account, scene, key (its target), until_s, turned, hold (wanted), held,
	                     -- leased (env.now of the lease), had_off, rot (the ROTATE_BITS it switched off)}
	self.hit_at = {}     -- account -> when the last hit to or from the player or one of their beasts was
	self.fires = {}      -- the fires the last search found: {x, y, z}
	self.villagers = {}  -- the villagers it found: entity ids
	self.walk = nil      -- the search under way: {ids, i, fires, villagers, woke}
	self.found_at = nil  -- when the last search for a standing player began
	self.first = true    -- the first step after the reset searches the world
	self.clock = nil     -- the game's time (ns) minus env.now (s) x 10^9, once known
	self.timed = {}      -- beast entity id -> true: its SpawnTime gave the clock
	self.changes = {}    -- beast entity id -> its AI's stateChangeTime as last seen
end

local function dist(ax, ay, az, bx, by, bz)
	return math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2 + (az - bz) ^ 2)
end

local function get(world, component, id)
	local ok, v = pcall(world.get, world, component, id)
	return ok and v or nil
end

local function template(world, component, id)
	local ok, v = pcall(world.template, world, component, id)
	return ok and v or nil
end

-- Whether entity `id` is a fire: the flame altar of a base that is not abandoned, or a ComfortProvider (its own, else
-- its template's) of a FIRE_COMFORT category.
local function is_fire(world, id)
	local base = get(world, "BaseIdComponent", id)
	if base then return not base.wasAbandoned end
	local c = get(world, "ComfortProvider", id) or template(world, "ComfortProvider", id)
	return c ~= nil and FIRE_COMFORT[c.category] == true
end

-- Whether entity `id` is a villager: its template has the villagers' ActiveNpcState.
local function is_villager(world, id)
	return template(world, "ActiveNpcState", id) ~= nil
end

local function night(world)
	local h = emf.time_of_day(world)
	return h ~= nil and (h >= NIGHT_FROM or h < NIGHT_TO)
end

-- The game's time now in ns (a whole number), or nil while it is not known.
function I:game_now(world, plus_ns)
	if not self.clock then return nil end
	return math.floor(self.env.now(world) * NS + self.clock + (plus_ns or 0) + 0.5)
end

-- The clock from what the game writes as "now": a called beast's SpawnTime, an AI's stateChangeTime the step it changes.
function I:calibrate(world, ids, now)
	for id in pairs(ids) do
		if not self.timed[id] then
			local at = self.env.called_at and self.env.called_at(id)
			local s = at and get(world, "SpawnTime", id)
			if s and s.spawnTime > 0 then
				self.clock = s.spawnTime - at * NS
				self.timed[id] = true
			end
		end
		local ai = get(world, "DynamicEnemy01BehaviorState", id)
		local c = ai and ai.stateChangeTime
		if c then
			local seen = self.changes[id]
			if seen and c ~= seen and c > 0 then self.clock = c - now * NS end
			self.changes[id] = c
		end
	end
	for id in pairs(self.changes) do
		if not ids[id] then self.changes[id], self.timed[id] = nil, nil end
	end
end

-- Holds creature `id` in the game's Sleep state (`sc` keeps what its masks had) on a lease: `lease` and `check` are the
-- game's times (ns) for its sleepEndTime and nextSleepCheckTime, nil while the game's time is not known (then false);
-- `now` (env.now) is when the lease was given. While sc.turned the game's own turning is off.
function M.hold(world, id, sc, now, lease, check)
	local ai = world:get("DynamicEnemy01BehaviorState", id)
	if not ai then return false end
	if not sc.held then
		if not (lease and check) then return false end
		sc.had_off = (ai.disabledFeatures & M.SLEEP_BIT) ~= 0
		ai.enabledFeatures = ai.enabledFeatures | M.SLEEP_BIT
		if sc.had_off then ai.disabledFeatures = ai.disabledFeatures & ~M.SLEEP_BIT end
		ai.sleepEndTime = lease
		ai.nextSleepCheckTime = check
		ai.state = SLEEP
		sc.held, sc.leased = true, now
	end
	if sc.turned and not sc.rot then
		sc.rot = ROTATE_BITS & ~ai.disabledFeatures
		ai.disabledFeatures = ai.disabledFeatures | ROTATE_BITS
	elseif not sc.turned and sc.rot then
		ai.disabledFeatures = ai.disabledFeatures & ~sc.rot
		sc.rot = nil
	end
	return true
end

-- Holds beast `id` in the game's Sleep state on a lease from the idle's clock; false while the game's time is not known.
function I:lie(world, id, sc, now)
	return M.hold(world, id, sc, now, self:game_now(world, LEASE_S * NS), self:game_now(world, CHECK_AHEAD_NS))
end

-- A held beast's lease, renewed every RENEW_S.
function I:renew(world, id, sc, now)
	if now >= sc.leased and now - sc.leased < RENEW_S then return end
	local ai = world:get("DynamicEnemy01BehaviorState", id)
	local lease = self:game_now(world, LEASE_S * NS)
	if ai and lease then
		ai.sleepEndTime = lease
		sc.leased = now
	end
end

-- Lets beast `id` up: the Sleep bit off (no beast's template has it), the disabledFeatures bits back as they were, no
-- sleep end, and its AI back to Idle while it is still asleep (one that the game moved on already is left as it is).
-- The scene counts as holding it until the writes are done.
local function wake(world, id, sc)
	if not sc.held then return end
	local ai = world:get("DynamicEnemy01BehaviorState", id)
	if ai then
		ai.enabledFeatures = ai.enabledFeatures & ~M.SLEEP_BIT
		if sc.had_off then ai.disabledFeatures = ai.disabledFeatures | M.SLEEP_BIT end
		if sc.rot then ai.disabledFeatures = ai.disabledFeatures & ~sc.rot end
		ai.sleepEndTime = 0
		if ai.state == SLEEP then ai.state = IDLE end
	end
	sc.rot = nil
	sc.held = false
end
M.wake = wake

-- Turns beast `id` where it stands to face the point (tx, tz): true when the game took the teleport. A turn the game
-- took but did not carry out (its answer: failed) is asked once more while `still()` holds (the scene is the same).
local function turn(world, id, tx, tz, still, again)
	local x, y, z = world:position(id)
	if not x then return false end
	local dx, dz = tx - x, tz - z
	if dx * dx + dz * dz < 0.01 or not emf.teleport then return false end
	return emf.teleport(world, id, x, y, z, { yaw = math.deg(math.atan(dx, dz)) }, function(moved, _, _, _, _, w)
		if not moved and not again and (not still or still()) then turn(w, id, tx, tz, still, true) end
	end) == true
end

-- Wakes `id` when it is a player-side creature in the Sleep state with the bit on; true when it was one.
local function wake_stray(world, id)
	local f = get(world, "Faction", id)
	if not (f and f.faction == PLAYER_ALLY) then return false end
	local ai = get(world, "DynamicEnemy01BehaviorState", id)
	if not (ai and (ai.enabledFeatures & M.SLEEP_BIT) ~= 0) then return false end
	ai.enabledFeatures = ai.enabledFeatures & ~M.SLEEP_BIT
	ai.sleepEndTime = 0
	if ai.state == SLEEP then ai.state = IDLE end
	return true
end

-- One step of the search for fires and villagers (and creatures left asleep); its finds replace the old ones when it
-- ends.
function I:walk_step(world)
	if not self.walk then self.walk = { ids = world:entities(), i = 1, fires = {}, villagers = {}, woke = 0 } end
	local w = self.walk
	local last = math.min(#w.ids, w.i + SCAN_PER_STEP - 1)
	for k = w.i, last do
		local id = w.ids[k]
		local sc = self.scenes[id]
		local kept = self.env.kept and self.env.kept(id)
		if not (sc and sc.held) and not kept and wake_stray(world, id) then w.woke = w.woke + 1 end
		if is_fire(world, id) then
			local x, y, z = world:position(id)
			if x then w.fires[#w.fires + 1] = { x = x, y = y, z = z } end
		elseif is_villager(world, id) then
			w.villagers[#w.villagers + 1] = id
		end
	end
	w.i = last + 1
	if w.i <= #w.ids then return end
	self.fires, self.villagers, self.walk = w.fires, w.villagers, nil
	if w.woke > 0 then
		emf.log(string.format("idle: %d player-side creature%s left in the Sleep state woken", w.woke, w.woke == 1 and "" or "s"))
	end
end

-- Whether a search starts now: a player with a beast out stands still (half of idle_s, or idles) and the last search
-- for one began FIND_EVERY_S ago or more.
function I:search_due(now)
	if self.found_at and now >= self.found_at and now - self.found_at < FIND_EVERY_S then return false end
	local half = self.env.setting("idle_s") / 2
	for _, p in pairs(self.players) do
		if p.idle or now - p.since >= half then return true end
	end
	return false
end

-- The fire nearest to beast `id` within NEAR_M: x, z and its key; nil when none.
function I:fire_near(world, id)
	local x, y, z = world:position(id)
	if not x then return nil end
	local best, best_d
	for _, f in ipairs(self.fires) do
		local d = dist(x, y, z, f.x, f.y, f.z)
		if d <= NEAR_M and (not best_d or d < best_d) then best, best_d = f, d end
	end
	if not best then return nil end
	return best.x, best.z, string.format("fire %.1f %.1f", best.x, best.z)
end

-- The villager nearest to beast `id` within NEAR_M: x, z and its key; nil when none.
function I:villager_near(world, id)
	local x, y, z = world:position(id)
	if not x then return nil end
	local best, best_d, bx, bz
	for _, v in ipairs(self.villagers) do
		local vx, vy, vz = world:position(v)
		local d = vx and dist(x, y, z, vx, vy, vz)
		if d and d <= NEAR_M and (not best_d or d < best_d) then best, best_d, bx, bz = v, d, vx, vz end
	end
	if not best then return nil end
	return bx, bz, "villager " .. best
end

-- The beast of `group` (entity ids, in order) that keeps watch at night: the first without sleep states; nil when all
-- have them.
function I:watcher(group)
	local ids = self.env.ids()
	for _, id in ipairs(group) do
		local e = ids[id]
		if e and not species.sleeps(species.by_id(e.beast.species)) then return id end
	end
	return nil
end

-- A scene for beast `id` of `account` (`owner` their entity; `group` the beasts of the idling, in order), picked and set
-- up.
function I:pick(world, id, account, owner, group, now)
	local e = self.env.ids()[id]
	local sp = e and species.by_id(e.beast.species)
	local sleeps, stays = species.sleeps(sp), species.stays(sp)
	local scene, fx, fz, key
	if night(world) then
		if #group >= 2 and self:watcher(group) == id then
			local x, _, z = world:position(id)
			local ox, _, oz = world:position(owner)
			scene, key = "night", "away"
			if x and ox then fx, fz = x + (x - ox), z + (z - oz) end
		elseif sleeps and stays then
			scene = "settle"
		end
	end
	if not scene then
		local rx, rz, rkey = self:fire_near(world, id)
		local vx, vz, vkey = self:villager_near(world, id)
		local can = { settle = sleeps and stays, fire = rx ~= nil, watch = vx ~= nil, sniff = true }
		local total = 0
		for _, w in ipairs(WEIGHTS) do
			if can[w[1]] then total = total + w[2] end
		end
		local r = math.random() * total
		scene = "sniff"
		for _, w in ipairs(WEIGHTS) do
			if can[w[1]] then
				if r < w[2] then
					scene = w[1]
					break
				end
				r = r - w[2]
			end
		end
		if scene == "fire" then fx, fz, key = rx, rz, rkey end
		if scene == "watch" then fx, fz, key = vx, vz, vkey end
	end
	local sc = self.scenes[id] or { account = account }
	self.scenes[id] = sc
	local same = sc.scene == scene and sc.key == key
	sc.scene, sc.key = scene, key
	sc.until_s = now + math.random(SCENE_MIN_S, SCENE_MAX_S)
	if not same then
		sc.turned = fx ~= nil and turn(world, id, fx, fz, function() return self.scenes[id] == sc and sc.key == key end)
	end
	sc.hold = stays and scene ~= "sniff"
	if sc.hold then
		self:lie(world, id, sc, now)
	else
		wake(world, id, sc)
	end
end

-- Beast `id` leaves the idling alone: let up when a scene holds it.
function I:drop(world, id, sc)
	wake(world, id, sc)
	self.scenes[id] = nil
end

-- Idling ends for `account`: every beast of theirs is let up, and their stillness counts from now.
function I:run_stop(world, account)
	for id, sc in pairs(self.scenes) do
		if sc.account == account then self:drop(world, id, sc) end
	end
	local p = self.players[account]
	if not p then return end
	p.idle = false
	local x, y, z
	if p.owner then x, y, z = world:position(p.owner) end
	if x then p.x, p.y, p.z = x, y, z end
	p.since = self.env.now(world)
end

-- The account whose side `id` is on: a beast out, or a player; nil for anything else.
function I:account_of(world, id)
	local e = self.env.ids()[id]
	if e then return e.account end
	if world:get("PlayerState", id) then return world:account(id) end
	return nil
end

-- A HitEvent: a hit to or from a player or a beast of theirs (not a fall, nor its own) ends their idling at once, and
-- the next waits QUIET_S.
function I:run_hit(world, source, target)
	if source == 0 or source == target then return end
	local now = self.env.now(world)
	for _, id in ipairs({ source, target }) do
		local account = self:account_of(world, id)
		if account then
			self.hit_at[account] = now
			local p = self.players[account]
			if p and p.idle then self:stop(world, account) end
		end
	end
end

-- Whether `account`'s player (`owner`) may idle now: the setting, no hit for QUIET_S, no monster held, not building.
function I:can_idle(world, account, owner, now)
	if not self.env.setting("idle") then return false end
	local t = self.hit_at[account]
	if t and now >= t and now - t < QUIET_S then return false end
	return not self.env.held(account) and not self.env.building(world, account, owner)
end

-- Whether beast `id` is within NEAR_M of `owner`.
local function near(world, id, owner)
	local x, y, z = world:position(id)
	local ox, oy, oz = world:position(owner)
	return x ~= nil and ox ~= nil and dist(x, y, z, ox, oy, oz) <= NEAR_M
end

-- Whether beast `id` may join its owner's idling: it guards them, no order or chase on it, its AI idles, roams or guards,
-- within NEAR_M.
function I:eligible(world, id, owner)
	if self.env.busy(world, id) then return false end
	local ai = world:get("DynamicEnemy01BehaviorState", id)
	return ai ~= nil and ai.guardEntityId == owner and TAKING_PART[ai.state] == true and near(world, id, owner)
end

-- Whether beast `id` (in scene `sc`) stays in the idling: within NEAR_M, and its AI asleep while held, else idling,
-- roaming or guarding.
local function stays_in(world, id, sc, owner)
	if not near(world, id, owner) then return false end
	local ai = world:get("DynamicEnemy01BehaviorState", id)
	if not ai then return false end
	if sc.held then return ai.state == SLEEP end
	return TAKING_PART[ai.state] == true
end

-- Beast `id` died (or is gone): nothing more for it.
function I:forget(id)
	self.scenes[id] = nil
end

-- One player (`owner`, nil when not in the world) with `list`, their beasts out in order.
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
		if dist(x, y, z, p.sx, p.sy, p.sz) >= WAKE_M or not self:can_idle(world, account, owner, now) then
			self:stop(world, account)
			return
		end
		for id, sc in pairs(self.scenes) do
			if sc.account == account then
				if not stays_in(world, id, sc, owner) then
					self:drop(world, id, sc)
				elseif sc.held then
					self:renew(world, id, sc, now)
				elseif sc.hold then
					self:lie(world, id, sc, now)  -- waits for the game's time
				end
			end
		end
	else
		if dist(x, y, z, p.x, p.y, p.z) >= STILL_M then
			p.x, p.y, p.z, p.since = x, y, z, now
			return
		end
		if now - p.since < self.env.setting("idle_s") or not self:can_idle(world, account, owner, now) then return end
	end
	local group = {}  -- the beasts of the idling: in a scene, or able to join now
	for _, id in ipairs(list) do
		if self.scenes[id] or self:eligible(world, id, owner) then group[#group + 1] = id end
	end
	if #group == 0 then return end
	if not p.idle then p.idle, p.sx, p.sy, p.sz = true, p.x, p.y, p.z end
	for _, id in ipairs(group) do
		local sc = self.scenes[id]
		if not sc or now >= sc.until_s then self:pick(world, id, account, owner, group, now) end
	end
end

-- Every server step: `owners` (account -> player entity) the players in the world as of the last look.
function I:run_step(world, owners)
	local now = self.env.now(world)
	if self.walk then
		self:walk_step(world)
	elseif self.first or self:search_due(now) then
		if not self.first then self.found_at = now end
		self.first = false
		self:walk_step(world)
	end
	local ids = self.env.ids()
	for id in pairs(self.scenes) do
		if not ids[id] then self.scenes[id] = nil end  -- sent home, fallen, aside or not ours: the id may name another now
	end
	self:calibrate(world, ids, now)
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

local SAYS = { settle = "settles", fire = "sits by the fire", watch = "watches a villager", night = "keeps watch",
	sniff = "sniffs about" }

-- What `account`'s beasts do now, for beastmaster.idle: "Your beasts idle: Wolf settles (lying down, 25 s), ...".
function I:text(world, account)
	if not self.env.setting("idle") then return "Idling is off (the setting idle)." end
	local now = self.env.now(world)
	local p = self.players[account]
	if not (p and p.idle) then
		local still = p and math.max(0, math.floor(now - p.since)) or 0
		return string.format("Your beasts are not idling: you have stood still %d s of %d.", still, self.env.setting("idle_s"))
	end
	local ids, list = self.env.ids(), {}
	for id, sc in pairs(self.scenes) do
		if sc.account == account and ids[id] then list[#list + 1] = id end
	end
	table.sort(list)
	local parts = {}
	for _, id in ipairs(list) do
		local sc, e = self.scenes[id], ids[id]
		local how = "free"
		if sc.held then
			how = species.sleeps(species.by_id(e.beast.species)) and "lying down" or "standing still"
		elseif sc.hold then
			how = "waiting for the game's time"
		end
		parts[#parts + 1] = string.format("%s %s (%s%s, %d s)", e.beast.name, SAYS[sc.scene] or sc.scene, how,
			sc.turned and ", turned" or "", math.max(0, math.ceil(sc.until_s - now)))
	end
	if #parts == 0 then return "Your beasts idle: none of them takes part now (each within 15 m of you, following you)." end
	return "Your beasts idle: " .. table.concat(parts, ", ")
end

-- An error in the idle code: every beast of the idling let up (each on its own), the search under way dropped (the
-- next waits FIND_EVERY_S), no one idles (their stillness counts from now), and the message logged once.
function I:fail(world, err)
	local msg = tostring(err)
	if not self.logged[msg] then
		self.logged[msg] = true
		emf.log("idle: " .. msg .. " (every beast of the idling is let up; this message is logged once)")
	end
	for id, sc in pairs(self.scenes) do pcall(wake, world, id, sc) end
	self.scenes = {}
	self.walk, self.first = nil, false
	local ok, now = pcall(self.env.now, world)
	if ok then self.found_at = now end
	for _, p in pairs(self.players) do
		p.idle = false
		if ok then p.since = now end
	end
end

local function guarded(self, world, fn, ...)
	local ok, err = pcall(fn, self, world, ...)
	if not ok then self:fail(world, err) end
end

function I:step(world, owners) guarded(self, world, I.run_step, owners) end
function I:on_hit(world, source, target) guarded(self, world, I.run_hit, source, target) end
function I:stop(world, account) guarded(self, world, I.run_stop, account) end

return M
