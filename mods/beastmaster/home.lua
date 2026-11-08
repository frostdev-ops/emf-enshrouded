-- Home: the beasts a player left at home (in their collection, not out) live in the Large Stable nearest them (the
-- beasts pack's; stable.lua HOMES) while the player is within HOME_M of it; they leave when the player is farther than
-- LEAVE_M. A Large Stable has room for ROOM beasts, all players together: the beasts shown stay, then the others by
-- account and list order. Server world only.
--   Each is spawned on a spot of its own on the stable's floor (SPOTS, in the stable's frame: x across, z out of its
--   open side, turned with the stable's CurrentTransform) as a player-side creature at its owner's level and its
--   loyalty's size, told to guard the stable (the game's own roaming near it), and turned to face out of the open side.
--   Scenes, in turns: a stroll (the game's roaming; the first FIRST_S, then STROLL_S) and a rest (REST_S, held in the
--   game's Sleep state as idle.lua holds a beast: one whose graph has sleep states lies down, the others stand still),
--   facing out. A stroll that ends off the floor puts the beast back on its spot before it rests; a beast ROAM_M or more
--   from the stable's middle for AWAY_S is put back at once (the game's teleport, with a ground search).
--   It never fights (its aggro and chosen targets are cleared every step) and is healed to full every second; one that
--   dies anyway is removed and shows up again after RESPAWN_S. Its owner loses nothing: the collection is not touched.
--   They leave the world when they are called (leave, before the call's spawn), sent away or released, their owner is
--   away or left, the setting home_beasts is off, the stable is gone, or a player builds within BUILD_M of it (the game
--   blocks building next to creatures); back GRACE_S after the building stops.
--   A kind the game cannot spawn (its pack is not installed) is tried again after RETRY_S, logged once per kind.
--   The game's time (for the Sleep lease) is kept as an offset from env.now, from a home beast's SpawnTime.
--   After the script's start (a reload) the creatures guarding a Large Stable that are none of ours are the beasts the
--   script had there before: taken out of the world at the first check that has a Large Stable.
-- M.new(env) where env has: now(world), setting(key), beasts() (the roster), homes() (the Large Stables: stable.lua
-- S:homes()), present() (account -> player entity, nil before the first check), log(text).
local species = require("species")
local loyalty = require("loyalty")
local idle_mod = require("idle")
local aside_mod = require("aside")
local pack_mod = require("pack")

local M = {}
local H = {}
H.__index = H

local PLAYER_ALLY = 11
local HOME_M, LEAVE_M = 50, 70  -- m from the player: their beasts show up in a Large Stable within HOME_M, leave beyond
local ROOM = 4
-- the spots on the Large Stable's floor (its frame, metres from its middle; the open side +z), side by side
local SPOTS = { { x = -1.75, z = 0.45 }, { x = -0.6, z = 0.45 }, { x = 0.6, z = 0.45 }, { x = 1.75, z = 0.45 } }
local FLOOR = { x0 = -2.3, x1 = 2.45, z0 = -1.4, z1 = 2.25 }  -- its plank floor between the walls (frame metres)
local SPAWN_Y = 0.5          -- m above the stable's place: a beast is spawned just above the floor (0.31 m high)
local ROAM_M, AWAY_S = 6, 3  -- a beast this far from the stable's middle for this long is put back on its spot
local FIRST_S = { 5, 12 }    -- s: the first stroll
local STROLL_S = { 15, 30 }  -- s: a stroll
local REST_S = { 40, 90 }    -- s: a rest
local JITTER = 20            -- degrees: a beast faces out of the open side give or take this
local RESPAWN_S = 60
local RETRY_S = 300
local BUILD_M, GRACE_S = 20, 5
local LEASE_S, RENEW_S = 120, 5  -- the Sleep lease: the game's time plus LEASE_S, renewed every RENEW_S
local CHECK_AHEAD_NS = 1000000000000000000
local NS = 1000000000

function M.new(env)
	local h = setmetatable({ env = env }, H)
	h:reset()
	return h
end

-- A new world (or a reload): entity ids from before mean nothing now.
function H:reset()
	self.at = {}         -- beast (its roster entry) -> {id, beast, account, key, spot, scene, until_s, sc, faced,
	                     -- spawned, far, moved}
	self.ids = {}        -- entity id -> the same entry
	self.bodies = {}     -- a home beast's body -> when it is removed
	self.back_at = {}    -- beast -> when it may show again (it died)
	self.refused = {}    -- kind id -> until when it is not tried (the game refused its spawn)
	self.told = {}       -- kind id -> logged
	self.busy = {}       -- stable key -> until when building there keeps it empty
	self.clock = nil     -- the game's time (ns) minus env.now (s) x 10^9, once known
	self.next_check = 0
	self.swept = false
end

local function key_of(h)
	return string.format("%.1f %.1f %.1f", h.x, h.y, h.z)
end

local function dist(h, x, y, z)
	return math.sqrt((h.x - x) ^ 2 + (h.y - y) ^ 2 + (h.z - z) ^ 2)
end

local function get(world, component, id)
	local ok, v = pcall(world.get, world, component, id)
	return ok and v or nil
end

-- A stable's turn as a unit quaternion (none, or not a turn: none).
local function turn_of(h)
	local q = h.q
	if not q then return 0, 0, 0, 1 end
	local n = math.sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w)
	if n < 0.5 then return 0, 0, 0, 1 end
	return q.x / n, q.y / n, q.z / n, q.w / n
end

-- (vx, vy, vz) turned by the quaternion (qx, qy, qz, qw).
local function rotate(qx, qy, qz, qw, vx, vy, vz)
	local tx, ty, tz = 2 * (qy * vz - qz * vy), 2 * (qz * vx - qx * vz), 2 * (qx * vy - qy * vx)
	return vx + qw * tx + (qy * tz - qz * ty), vy + qw * ty + (qz * tx - qx * tz), vz + qw * tz + (qx * ty - qy * tx)
end

-- The world place of spot `i` of stable `h`.
local function spot_at(h, i)
	local qx, qy, qz, qw = turn_of(h)
	local dx, _, dz = rotate(qx, qy, qz, qw, SPOTS[i].x, 0, SPOTS[i].z)
	return h.x + dx, h.y + SPAWN_Y, h.z + dz
end

-- Whether (x, z) is on stable `h`'s floor.
local function on_floor(h, x, z)
	local qx, qy, qz, qw = turn_of(h)
	local lx, _, lz = rotate(-qx, -qy, -qz, qw, x - h.x, 0, z - h.z)
	return lx >= FLOOR.x0 and lx <= FLOOR.x1 and lz >= FLOOR.z0 and lz <= FLOOR.z1
end

-- The yaw (degrees; 0 = +z, 90 = +x) out of stable `h`'s open side, give or take JITTER.
local function yaw_out(h)
	local qx, qy, qz, qw = turn_of(h)
	local dx, _, dz = rotate(qx, qy, qz, qw, 0, 0, 1)
	return math.deg(math.atan(dx, dz)) + (math.random() - 0.5) * 2 * JITTER
end

local function between(span)
	return math.random(span[1], span[2])
end

-- Still our creature: there and a player-side one (alive or a body).
local function ours(world, id)
	if not world:position(id) then return false end
	local f = get(world, "Faction", id)
	return f ~= nil and f.faction == PLAYER_ALLY
end
local SPAWN_GRACE_S = 5  -- a creature spawned a moment ago reads as dead this long at most

-- The game's time now in ns plus `plus_ns`, or nil while it is not known.
function H:game_now(world, plus_ns)
	if not self.clock then return nil end
	return math.floor(self.env.now(world) * NS + self.clock + plus_ns + 0.5)
end

local function guard(world, id, h)
	if h.entity then emf.emit("server", "StartGuardingEvent", { guardingEntityId = id, guardedEntityId = h.entity }) end
end

-- Turns entry `e`'s beast where it stands to face out of stable `h`'s open side: true when the game took it.
local function face(world, e, h)
	local x, y, z = world:position(e.id)
	if not (x and emf.teleport) then return false end
	return emf.teleport(world, e.id, x, y, z, { yaw = yaw_out(h) }) == true
end

-- Puts entry `e`'s beast back on its spot of stable `h`, facing out.
function H:put_back(world, e, h, now)
	local x, y, z = spot_at(h, e.spot)
	if emf.teleport then emf.teleport(world, e.id, x, y, z, { ground = true, yaw = yaw_out(h) }) end
	e.moved, e.far = now, nil
	e.faced = true
end

-- Entry `e`'s beast leaves the world (when it is still ours; an id the game gave out again is left alone).
function H:remove(world, e)
	if ours(world, e.id) then emf.destroy(world, e.id) end
	self.at[e.beast], self.ids[e.id] = nil, nil
end

-- A beast leaves its Large Stable now (it is called).
function H:leave(world, beast)
	local e = self.at[beast]
	if e then self:remove(world, e) end
end

-- Whether entity `id` is a beast at home.
function H:has(id)
	return self.ids[id] ~= nil
end

-- A home beast died (the game's event): its body goes, the beast shows up again after RESPAWN_S.
function H:on_died(world, id)
	local e = self.ids[id]
	if not e then return end
	self.ids[id], self.at[e.beast] = nil, nil
	local now = self.env.now(world)
	self.back_at[e.beast] = now + RESPAWN_S
	self.bodies[id] = now + 1
end

-- Puts beast `b` of `account` (`owner` their player) on spot `spot` of stable `h`; the entry, or nil.
function H:show(world, b, account, owner, h, spot, now)
	local sp = species.by_id(b.species)
	if not sp then return nil end
	local x, y, z = spot_at(h, spot)
	local id = emf.spawn(world, sp.template, x, y, z, loyalty.spawn_size(sp, b.points, self.env.setting("loyalty_pace")))
	if not id or id == 0 then
		self.refused[sp.id] = now + RETRY_S
		if not self.told[sp.id] then
			self.told[sp.id] = true
			self.env.log(string.format("home: the game has no %s to show in a Large Stable (%s); tried again in %d s",
				species.word(sp), sp.pack and ("its pack: " .. sp.pack) or "see the log", RETRY_S))
		end
		return nil
	end
	local f = get(world, "Faction", id)
	if f then f.faction = PLAYER_ALLY end
	local lvl, mine = get(world, "Level", id), get(world, "Level", owner)
	if lvl and mine and mine.dataStorage then lvl.dataStorage[1] = mine.dataStorage[1] end
	local scaling = get(world, "DynamicEnemyScaling", id)
	if scaling then scaling.nextUpdateTime = 0 end
	guard(world, id, h)
	local e = { id = id, beast = b, account = account, key = key_of(h), spot = spot, scene = "stroll",
		until_s = now + between(FIRST_S), sc = {}, faced = false, spawned = now }
	self.at[b], self.ids[id] = e, e
	return e
end

-- Whether beast `b` may be shown now.
function H:allowed(b, now)
	if b.out then return false end
	local sp = species.by_id(b.species)
	if not sp or now < (self.refused[sp.id] or 0) then return false end
	return now >= (self.back_at[b] or 0)
end

-- The creatures guarding a Large Stable that are none of ours: the script's beasts there before its reload.
function H:sweep(world, homes)
	local guarded = {}
	for _, h in ipairs(homes) do
		if h.entity then guarded[h.entity] = true end
	end
	if not next(guarded) then return end
	local n = 0
	for _, id in ipairs(world:entities()) do
		local ai = not self.ids[id] and get(world, "DynamicEnemy01BehaviorState", id)
		local g = ai and ai.guardEntityId
		if g and guarded[g] and ours(world, id) then
			emf.destroy(world, id)
			n = n + 1
		end
	end
	if n > 0 then self.env.log(string.format("home: %d beast%s left in a Large Stable before a reload taken out", n,
		n == 1 and "" or "s")) end
end

-- Who is shown where: the beasts shown stay while they may, then the others go to the nearest Large Stable with room.
-- Returns the list {beast, account, owner, key, spot} in order.
function H:assign(world, homes, present, now)
	local by_key = {}
	for _, h in ipairs(homes) do by_key[key_of(h)] = h end
	local taken, want, placed = {}, {}, {}
	local function take(k, i)
		taken[k] = taken[k] or {}
		taken[k][i] = true
	end
	local function free(k)
		for i = 1, ROOM do
			if not (taken[k] and taken[k][i]) then return i end
		end
		return nil
	end
	local accounts = {}
	for account in pairs(present) do accounts[#accounts + 1] = account end
	table.sort(accounts)
	local beasts = self.env.beasts()
	for _, account in ipairs(accounts) do
		local owner = present[account]
		local ox, oy, oz = world:position(owner)
		for _, b in ipairs(beasts:list(account)) do
			local e = self.at[b]
			local h = e and by_key[e.key]
			if h and ox and self:allowed(b, now) and now >= (self.busy[e.key] or 0) and dist(h, ox, oy, oz) <= LEAVE_M
				and not (taken[e.key] and taken[e.key][e.spot]) then
				take(e.key, e.spot)
				placed[b] = { beast = b, account = account, owner = owner, key = e.key, spot = e.spot }
			end
		end
	end
	for _, account in ipairs(accounts) do
		local owner = present[account]
		local ox, oy, oz = world:position(owner)
		for _, b in ipairs(beasts:list(account)) do
			if not placed[b] and ox and self:allowed(b, now) then
				local best, best_d
				for _, h in ipairs(homes) do
					local k = key_of(h)
					local d = dist(h, ox, oy, oz)
					if d <= HOME_M and now >= (self.busy[k] or 0) and free(k) and (not best_d or d < best_d) then
						best, best_d = k, d
					end
				end
				if best then
					local i = free(best)
					take(best, i)
					placed[b] = { beast = b, account = account, owner = owner, key = best, spot = i }
				end
			end
			if placed[b] then want[#want + 1] = placed[b] end
		end
	end
	return want, by_key
end

-- The upkeep of entry `e` in stable `h` (every second): gone, the clock, health, facing, roaming off, the scenes.
function H:upkeep(world, e, h, now)
	if not ours(world, e.id) then  -- gone (or the id names another entity now): shown again at the next check
		self.at[e.beast], self.ids[e.id] = nil, nil
		return
	end
	local cur, max = emf.health(world, e.id)
	if not cur or cur <= 0 then
		if now - e.spawned >= SPAWN_GRACE_S then self:on_died(world, e.id) end  -- dead without the game's event
		return
	end
	if not self.clock then
		local s = get(world, "SpawnTime", e.id)
		if s and s.spawnTime > 0 then self.clock = s.spawnTime - e.spawned * NS end
	end
	if max and cur < max then emf.attribute_add(world, e.id, "health", max - cur) end
	if not e.faced then e.faced = face(world, e, h) end
	local x, _, z = world:position(e.id)
	if dist(h, x, h.y, z) >= ROAM_M then
		e.far = e.far or now
		if now - e.far >= AWAY_S and now - (e.moved or -math.huge) >= AWAY_S then self:put_back(world, e, h, now) end
	else
		e.far = nil
	end
	if now < e.until_s then return end
	if e.scene == "stroll" then
		e.sc.turned = true
		local lease, check = self:game_now(world, LEASE_S * NS), self:game_now(world, CHECK_AHEAD_NS)
		if not (lease and check) then return end  -- the game's time is not known yet: it strolls on
		if not on_floor(h, x, z) then self:put_back(world, e, h, now) else face(world, e, h) end
		if idle_mod.hold(world, e.id, e.sc, now, lease, check) then
			e.scene, e.until_s = "rest", now + between(REST_S)
		end
	else
		idle_mod.wake(world, e.id, e.sc)
		guard(world, e.id, h)
		e.scene, e.until_s = "stroll", now + between(STROLL_S)
	end
end

-- Every second: the stables being built at, who is shown where, then each one's upkeep.
function H:check(world, now)
	for id, due in pairs(self.bodies) do
		if now >= due then
			self.bodies[id] = nil
			local cur = emf.health(world, id)
			if world:position(id) and (not cur or cur <= 0) then emf.destroy(world, id) end
		end
	end
	local homes = self.env.setting("home_beasts") and self.env.homes() or {}
	local present = self.env.present() or {}
	if not self.swept and #homes > 0 then
		self.swept = true
		self:sweep(world, homes)
	end
	for _, player in pairs(present) do
		if aside_mod.building(world, player) then
			local px, py, pz = world:position(player)
			if px then
				for _, h in ipairs(homes) do
					if dist(h, px, py, pz) <= BUILD_M then self.busy[key_of(h)] = now + GRACE_S end
				end
			end
		end
	end
	local want, by_key = self:assign(world, homes, present, now)
	local wanted = {}
	for _, w in ipairs(want) do wanted[w.beast] = w end
	for b, e in pairs(self.at) do
		local w = wanted[b]
		if not w or w.key ~= e.key or w.spot ~= e.spot then self:remove(world, e) end
	end
	for _, w in ipairs(want) do
		if not self.at[w.beast] then self:show(world, w.beast, w.account, w.owner, by_key[w.key], w.spot, now) end
	end
	for _, e in pairs(self.at) do
		if e.spawned < now then self:upkeep(world, e, by_key[e.key], now) end
	end
end

-- Every step of the server world: the beasts at home are calmed and their leases renewed; once a second, the check.
function H:step(world)
	local now = self.env.now(world)
	for id, e in pairs(self.ids) do
		pack_mod.calm(world, id)
		if e.sc.held and (now < e.sc.leased or now - e.sc.leased >= RENEW_S) then
			local ai = get(world, "DynamicEnemy01BehaviorState", id)
			local lease = self:game_now(world, LEASE_S * NS)
			if ai and lease then
				ai.sleepEndTime = lease
				e.sc.leased = now
			end
		end
	end
	if now < self.next_check then return end
	self.next_check = now + 1
	self:check(world, now)
end

-- beastmaster.home: where `account`'s beasts at home are and what they do.
function H:text(world, account)
	local lines = {}
	for _, b in ipairs(self.env.beasts():list(account)) do
		local e = self.at[b]
		if e then
			local x, y, z = e.key:match("^(%S+) (%S+) (%S+)$")
			lines[#lines + 1] = string.format("%s: in the Large Stable at %d, %d, %d, %s", b.name,
				math.floor(tonumber(x) + 0.5), math.floor(tonumber(y) + 0.5), math.floor(tonumber(z) + 0.5),
				e.scene == "rest" and "resting" or "strolling")
		end
	end
	if #lines == 0 then
		return string.format("None of your beasts is in a Large Stable now: the beasts you left at home show up in one "
			.. "within %d m of you (the beasts pack's Large Stable, crafted at the Workbench).", HOME_M)
	end
	return table.concat(lines, "\n")
end

return M
