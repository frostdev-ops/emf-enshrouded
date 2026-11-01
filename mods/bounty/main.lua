-- bounty (Bounty Board): bounties for every player: slay creatures of a faction, gather a material, slay a champion.
-- The board posts notices (the same for everyone, or each player their own) every few in-game days; a player takes
-- one (holds up to hold_max), works on it anywhere, and claims the reward. Kept per world and per player (by account).
--   /bounty.board   the notices, your bounties, progress and the time to new notices (every player, also from the chat)
--   /bounty.take    <n>: take notice n
--   /bounty.drop    <n>: give up bounty n of yours
--   /bounty.claim   the rewards of your finished bounties
--   /bounty.reroll  <n>: swap notice n for another (notices of your own only)
--   /bounty.top     who finished the most bounties in this world
--   bounty.give <id>, bounty.reset (host / admins)
-- Runs where the world runs (the host or a dedicated server); players who join need nothing.

-- The bounties. Edit the table and save: the mod reloads (bounties already on players' boards keep what they were).
-- Installing EMF again replaces this file and your edits with it: keep your own pool in emf\config\bounty\pool.lua
-- instead (a file that returns a table like POOL; read when the mod starts and at each world load).
--   kind "kill": a kill credited to the player of a creature whose Faction is `faction` (FactionType in
--     sdk/include/emf/generated/components.hpp: 2 Fogger (the shroud's creatures), 3 Vukah, 4 Scavenger, 5 Wildlife,
--     6 WildBeast, 10 Skeleton, 14 Drak...);
--   kind "gather": items the player picks up in the world (not out of a chest or a tombstone) whose debug name holds
--     `match` (lower case); raw materials only (names starting Material_, Food_ or Build_TerrainMaterial_);
--   kind "champion": a kill of a strong creature: CHAMPION_LEVELS levels above the player or more, with at least
--     CHAMPION_HEALTH health.
-- n: how many on "normal" (tiny x0.2, easy x0.6, hard x1.5). weight: how often it is drawn (3 when left out). reward:
-- items by their debug name (`items rune` in the console); gear = true comes at the player's level and the
-- reward_rarity setting; counts are scaled by the reward_scale setting; name: the word the notices show for it
-- (REWARD_NAMES below, else a word of the debug name).
-- starter = true: a quick one for players who have not finished a bounty yet. They see notices drawn from the
--   starters (and their rerolls draw starters), until they finish their first bounty (with fewer starters than the
--   board holds, the rest comes from the other bounties); the others never see starters. A pool without starters
--   gives everyone the same kind of notices.
-- A kill counts for the player who hit, and for the player whose ally hit (the companion, a Beastmaster beast, a
-- Necromancer undead: a creature of the players' side that guards that player).
local RUNES = "Material_T1_Token_WeaponUpgrade_Rune01"
local POOL = {
  {id = "kill_vukah", kind = "kill", faction = 3, name = "Vukah", n = 6, reward = {{item = RUNES, count = 8}}},
  {id = "kill_scavenger", kind = "kill", faction = 4, name = "Scavengers", n = 6, reward = {{item = RUNES, count = 8}}},
  {id = "kill_shroud", kind = "kill", faction = 2, name = "Shroud creatures", n = 8, reward = {{item = RUNES, count = 8}}},
  {id = "kill_wildlife", kind = "kill", faction = 5, name = "Wildlife", n = 4,
   reward = {{item = "Material_T1_Animal_Fur", count = 6}}},
  {id = "kill_beasts", kind = "kill", faction = 6, name = "Wild beasts", n = 3, reward = {{item = RUNES, count = 10}}},
  {id = "kill_skeleton", kind = "kill", faction = 10, name = "Skeletons", n = 6, reward = {{item = RUNES, count = 8}}},
  {id = "kill_drak", kind = "kill", faction = 14, name = "Drak", n = 4, weight = 1, reward = {{item = RUNES, count = 12}}},
  {id = "gather_wood", kind = "gather", match = "wood_logs", name = "Wood logs", n = 60, reward = {{item = RUNES, count = 5}}},
  {id = "gather_stone", kind = "gather", match = "stone", name = "Stone", n = 60, reward = {{item = RUNES, count = 5}}},
  {id = "gather_fibre", kind = "gather", match = "plant_fibre", name = "Plant fibre", n = 40, reward = {{item = RUNES, count = 5}}},
  {id = "gather_metal", kind = "gather", match = "metal_scraps", name = "Metal scraps", n = 20, reward = {{item = RUNES, count = 6}}},
  {id = "gather_fur", kind = "gather", match = "animal_fur", name = "Fur", n = 10, reward = {{item = RUNES, count = 6}}},
  {id = "champion", kind = "champion", name = "champion", n = 1, weight = 1,
   reward = {{item = RUNES, count = 20}, {item = "Weapon_T1_1H_SwordEpic_01", count = 1, gear = true}}},
  -- starters: quick ones until a player finishes their first bounty
  {id = "start_wood", kind = "gather", match = "wood_logs", name = "Wood logs", n = 20, starter = true,
   reward = {{item = RUNES, count = 3}}},
  {id = "start_stone", kind = "gather", match = "stone", name = "Stone", n = 20, starter = true,
   reward = {{item = RUNES, count = 3}}},
  {id = "start_fibre", kind = "gather", match = "plant_fibre", name = "Plant fibre", n = 15, starter = true,
   reward = {{item = RUNES, count = 3}}},
  {id = "start_wildlife", kind = "kill", faction = 5, name = "Wildlife", n = 2, starter = true,
   reward = {{item = "Material_T1_Animal_Fur", count = 3}}},
  {id = "start_shroud", kind = "kill", faction = 2, name = "Shroud creatures", n = 3, starter = true,
   reward = {{item = RUNES, count = 3}}},
  {id = "start_scavenger", kind = "kill", faction = 4, name = "Scavengers", n = 2, starter = true,
   reward = {{item = RUNES, count = 3}}},
}
local CHAMPION_LEVELS = 2     -- a champion: at least this many levels above the player ...
local CHAMPION_HEALTH = 1000  -- ... and at least this much health (when first hit)
local SHARE_SECONDS = 10      -- with share_kills, every player who hit it this recently gets the kill
local MOVE_SECONDS = 2        -- an item a chest got from nothing (a mod's put) is "moving" this long: a player's
                              -- pickup of it from nothing then is the rest coming back, no gathering
local LEFTOVER_SECONDS = 120  -- ... and a loose drop of that item within LEFTOVER_METRES of that chest, picked up this
local LEFTOVER_METRES = 30    -- long after, is what did not fit (quick stack's and sorting chests' drops), no gathering
local DROP_SECONDS = 1800     -- this mod's own drops (a reward at a full player's feet) are known this long

-- The render models that make a placed prop a Notice Board (world:template("ModelResource", id).model). The Notice
-- Board players build (Prop_Decoration_T2_NoticeBoard) and very likely the villages' notice boards
-- (Prop_Deco_Villageprops_NoticeBoard_01) show villageprops_noticeboard_01: base_radius keeps the villages' out.
local BOARD_MODELS = {["0bee6531-ef51-4925-9cd3-a40d11561165"] = "Notice Board"}
local SCAN_PER_STEP = 250   -- entities looked at per server step while the world is searched for boards
local BOARD_RESCAN = 30     -- s between two searches (a new board counts within a minute)
local MARK_SAME_M = 1       -- m: a marked prop is found again at its place within this
local MARK_REACH_M = 8      -- m: bounty.mark and bounty.unmark reach this far
local MARK_GONE_M = 40      -- m: missed by a search with a player this close to its place, a marked prop is gone ...
local LOAD_GRACE = 30       -- s: ... but never this soon after a load (places may still be streaming in)
local PROP_HEIGHT = 1       -- m: the point of a prop the crosshair aims at, above its position
local FIX = 4294967296.0    -- 1 m in the game's fixed-point positions

-- The short words the notices and the wheel show for reward items ("Gather 20 Wood logs (3 runes)").
local REWARD_NAMES = {[RUNES] = "runes", Material_T1_Animal_Fur = "fur", Weapon_T1_1H_SwordEpic_01 = "sword"}

local DIFFICULTY = {tiny = 0.2, easy = 0.6, normal = 1.0, hard = 1.5}
local PLAYER_SIDE = {[7] = true, [8] = true, [11] = true}  -- Player, AutomatedPlayer, PlayerAlly (companions, summons)
local RAW = {"material_", "food_", "build_terrainmaterial_"}
local NEVER = {"processed", "_dish_", "fishingbait", "_bars", "_bar", "powder", "_dust", "dried", "charcoal"}

local players = {}   -- account -> {done, taken = {{id, need, have, state}}, took = {id = true}, took_key, key, rerolls,
                     -- offers}: taken bounties (state 0 open, 1 done; claimed ones go), the notices taken in period
                     -- took_key, and with notices of their own the list `offers` drawn for period `key`
local offset = 0     -- bounty.reset adds 1: new notices for everyone
local dawns = 0      -- dawns seen in this world (reset = game_dawn, game_days)
local posted = 0     -- the dawn count when the notices were last posted (reset = game_days)
local seed = 0       -- this world's draws (kept with the world)
local play_s = 0     -- seconds this world has run (reset = hours)
local hits = {}      -- creature id -> {faction, level, max, by = {player -> play_s}, last}
local gifts = {}     -- "player:item" -> {count, until_s}: rewards on their way (their pickup is no gathering)
local moving = {}    -- item -> play second until which it counts as moving between inventories
local filled = {}    -- item -> list of {x, y, z, until_s}: chests a mod just put that item into (their leftovers)
local dropped = {}   -- pickup entity -> {item, left, until_s}: rewards this mod dropped at a player's feet
local owed = {}    -- account -> list of {item (debug name), count, level, rarity}: rewards that could not be given
                     -- (the player left before they landed); they come with that player's next /bounty.claim
local loaded, dirty = false, false
local last_tick, last_tod = nil, nil
local session = emf.session()
local hud_at, hud_shown, client_steps = -1, false, 0
local purge_at = 0
local boards = {}         -- the Bounty Boards: {id (0: a marked place not found yet), x, y, z, kind "notice" / "mark"}
local found, altars = {}, {}  -- the last search: Notice Boards {id, x, y, z} and flame altars {x, z}
local marks = {}          -- props the host marked: {x, y, z, model ("-": unknown), id (this session, when found)}
local walk, walk_after = nil, 0    -- the search of the server world under way, and the play second of the next
local cwalk, cwalk_after = nil, 0  -- the same on the host's client world
local client_models = {}  -- entity id -> board model the host's client world showed
local loaded_at = 0       -- play second when this world's data came

-- ---- the pool ----
-- emf\config\bounty\pool.lua, when it is there, replaces POOL (that folder stays when EMF is installed again). It runs
-- with nothing in reach (no emf, io or os): it can only build the table. Entries that do not fit are skipped and named.
local BUILT_IN = POOL
local POOL_FILE = (emf.dir:match("^(.*)[\\/][^\\/]+[\\/][^\\/]+$") or ".") .. "\\config\\bounty\\pool.lua"
local KINDS = {kill = true, gather = true, champion = true}

local function pool_entry_ok(e, seen)
  if type(e) ~= "table" or type(e.id) ~= "string" or not e.id:match("^[%w_%-]+$") or seen[e.id] then return false end
  if not KINDS[e.kind] or type(e.n) ~= "number" or e.n < 1 then return false end
  if e.kind == "kill" and type(e.faction) ~= "number" then return false end
  if e.kind == "gather" and (type(e.match) ~= "string" or e.match == "") then return false end
  if e.name ~= nil and type(e.name) ~= "string" then return false end
  if e.weight ~= nil and (type(e.weight) ~= "number" or e.weight < 0) then return false end
  if e.starter ~= nil and type(e.starter) ~= "boolean" then return false end
  if e.reward ~= nil and type(e.reward) ~= "table" then return false end
  for _, r in ipairs(e.reward or {}) do
    if type(r) ~= "table" or type(r.item) ~= "string" or type(r.count) ~= "number" or r.count < 1 then return false end
    if r.name ~= nil and type(r.name) ~= "string" then return false end
  end
  return true
end

local function load_pool()
  local f = io.open(POOL_FILE, "r")
  if not f then
    POOL = BUILT_IN
    return
  end
  f:close()
  local chunk, err = loadfile(POOL_FILE, "t", {})
  local ok, list = false, err
  if chunk then ok, list = pcall(chunk) end
  if not ok or type(list) ~= "table" then
    emf.log("emf\\config\\bounty\\pool.lua could not be read (" .. tostring(list) .. "): main.lua's POOL is used")
    POOL = BUILT_IN
    return
  end
  local pool, seen = {}, {}
  for i, e in ipairs(list) do
    if pool_entry_ok(e, seen) then
      seen[e.id] = true
      e.name = e.name or e.id
      pool[#pool + 1] = e
    else
      emf.log(string.format("pool.lua: entry %d skipped (it needs an id of letters, digits and _, a kind of kill, " ..
        "gather or champion, n, a faction for kill, a match for gather, and rewards with item and count)", i))
    end
  end
  if #pool == 0 then
    emf.log("emf\\config\\bounty\\pool.lua holds no bounty that fits: main.lua's POOL is used")
    POOL = BUILT_IN
    return
  end
  POOL = pool
  emf.log(string.format("the pool: %d bounties from emf\\config\\bounty\\pool.lua", #pool))
end
load_pool()

local function entry(id)
  for _, e in ipairs(POOL) do
    if e.id == id then return e end
  end
  return nil
end

local function need_for(e)
  if e.kind == "champion" then return e.n end
  local f = DIFFICULTY[emf.setting("difficulty") or "normal"] or 1
  return math.max(1, math.floor(e.n * f + 0.5))
end

local function text_of(e, need)
  if e.kind == "kill" then return string.format("Slay %d %s", need, e.name) end
  if e.kind == "gather" then return string.format("Gather %d %s", need, e.name) end
  if need == 1 then return "Slay a champion" end
  return string.format("Slay %d champions", need)
end

local function short_of(e) return e.kind == "champion" and "Champion" or e.name end

local function scaled(r)
  if r.gear then return r.count end
  local s = emf.setting("reward_scale") or 1
  return math.max(1, math.floor(r.count * s + 0.5))
end

local function reward_text(e)
  local parts = {}
  for _, r in ipairs(e.reward or {}) do parts[#parts + 1] = scaled(r) .. " x " .. r.item end
  return table.concat(parts, ", ")
end

-- The word a notice shows for a reward item: its name in the pool, REWARD_NAMES, else the last word of its debug name.
local function item_word(r)
  if r.name then return r.name end
  if REWARD_NAMES[r.item] then return REWARD_NAMES[r.item] end
  local word
  for part in r.item:gmatch("[^_]+") do
    if not part:match("^T%d+$") and not part:match("^%d+$") then word = part end
  end
  return string.lower(word or r.item)
end

-- "8 runes", "20 runes, sword": the rewards as a notice shows them.
local function short_reward(e)
  local parts = {}
  for _, r in ipairs(e.reward or {}) do
    local n = scaled(r)
    parts[#parts + 1] = n == 1 and item_word(r) or (n .. " " .. item_word(r))
  end
  return table.concat(parts, ", ")
end

-- "Gather 20 Wood logs (3 runes)": a notice, at the size it would have if taken now.
local function notice_text(e)
  local rewards = short_reward(e)
  return text_of(e, need_for(e)) .. (rewards ~= "" and (" (" .. rewards .. ")") or "")
end

-- ---- deterministic draws: the same world or account, period and reroll give the same notices ----
local function hash(s)
  local h = 2166136261
  for i = 1, #s do h = ((h ~ s:byte(i)) * 16777619) % 4294967296 end
  return h
end

local function random_from(seed_value)
  local state = seed_value % 2147483648
  return function(n)
    state = (state * 1103515245 + 12345) % 2147483648
    return (state >> 8) % n + 1
  end
end

-- Whether a player who finished `done` bounties sees starters (only while there are any in the pool).
local function starters_for(done)
  if (done or 0) > 0 then return false end
  for _, e in ipairs(POOL) do
    if e.starter then return true end
  end
  return false
end

-- `count` pool ids for `who` (none of `exclude`), weighted; from the starters only when `starter`, else from the rest
-- of the pool.
local function draw(who, key, salt, count, exclude, starter)
  local rand = random_from(hash(who .. "|" .. key .. "|" .. salt))
  local taken = {}
  for id in pairs(exclude or {}) do taken[id] = true end
  for _, e in ipairs(POOL) do
    if (e.starter == true) ~= (starter == true) then taken[e.id] = true end
  end
  local out = {}
  for _ = 1, count do
    local total = 0
    for _, e in ipairs(POOL) do
      if not taken[e.id] then total = total + (e.weight or 3) end
    end
    if total <= 0 then break end
    local r = rand(total)
    for _, e in ipairs(POOL) do
      if not taken[e.id] then
        r = r - (e.weight or 3)
        if r <= 0 then
          taken[e.id] = true
          out[#out + 1] = e.id
          break
        end
      end
    end
  end
  return out
end

-- A list of `count` notices; a starter list with fewer starters than `count` is filled from the other bounties.
local function draw_list(who, key, salt, count, starter)
  local list = draw(who, key, salt, count, nil, starter)
  if starter and #list < count then
    local have = {}
    for _, id in ipairs(list) do have[id] = true end
    for _, id in ipairs(draw(who, key, salt .. "f", count - #list, have, false)) do list[#list + 1] = id end
  end
  return list
end

-- ---- periods ----
local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local function notice_count() return clamp(math.floor(tonumber(emf.setting("bounties_per_period")) or 3), 2, 4) end
local function hold_max() return clamp(math.floor(tonumber(emf.setting("hold_max")) or 1), 1, 4) end
local function each_player() return emf.setting("offers_for") == "each_player" end
local function period_len() return math.max(1, emf.setting("reset_hours") or 4) * 3600 end

local function reset_days() return clamp(math.floor(tonumber(emf.setting("reset_days")) or 4), 1, 10) end

local function period_key()
  local mode = emf.setting("reset")
  if mode == "game_days" then return "n" .. posted .. "." .. offset end
  if mode == "game_dawn" then return "g" .. dawns .. "." .. offset end
  if mode == "hours" then return "h" .. math.floor(play_s / period_len()) .. "." .. offset end
  local t = os.date("*t", os.time() - (emf.setting("reset_hour") or 6) * 3600)
  return string.format("d%04d%03d.%d", t.year, t.yday, offset)
end

local function until_text()
  local mode = emf.setting("reset")
  local left
  if mode == "game_days" then
    local days = reset_days() - (dawns - posted)
    if days <= 1 then return "New notices at the next dawn" end
    return string.format("New notices in %d days", days)
  end
  if mode == "game_dawn" then return "New notices at dawn" end
  if mode == "hours" then
    left = period_len() - (play_s % period_len())
  else
    local hour = emf.setting("reset_hour") or 6
    local now = os.time()
    local t = os.date("*t", now)
    local next_reset = os.time({year = t.year, month = t.month, day = t.day, hour = hour, min = 0, sec = 0})
    if next_reset <= now then next_reset = next_reset + 86400 end
    left = next_reset - now
  end
  local minutes = math.ceil(left / 60)
  return string.format("New notices in %d h %d min", minutes // 60, minutes % 60)
end

-- ---- notices and the bounties players hold ----
local function record(account)
  local rec = players[account]
  if not rec then
    rec = {done = 0, taken = {}, took = {}, rerolls = 0}
    players[account] = rec
  end
  return rec
end

-- The notices `account` sees in this period: the world's list (a starter list while they finished none), or with
-- notices of their own the list drawn for them (kept, as rerolls change it).
local function notices_for(account)
  local rec = record(account)
  local key = period_key()
  local starter = starters_for(rec.done)
  if not each_player() then
    return draw_list("w" .. seed, key, starter and "s" or "0", notice_count(), starter)
  end
  if rec.key ~= key or not rec.offers then
    rec.key, rec.rerolls = key, 0
    rec.offers = draw_list(account, key, "0", notice_count(), starter)
    dirty = true
  end
  return rec.offers
end

-- The notices `rec` took in this period (each once).
local function took_of(rec)
  local key = period_key()
  if rec.took_key ~= key then rec.took_key, rec.took = key, {} end
  return rec.took
end

local function holds(rec, id)
  for _, b in ipairs(rec.taken) do
    if b.id == id then return true end
  end
  return false
end

local function take(account, n)
  local list = notices_for(account)
  n = tonumber(n or "")
  if not n or n % 1 ~= 0 or n < 1 or n > #list then return "Usage: /bounty.take <1-" .. #list .. ">" end
  local id = list[n]
  local e = entry(id)
  if not e then return "That notice is gone: look at the board again." end
  local rec = record(account)
  local took = took_of(rec)
  if took[id] then return "You took that one already. " .. until_text() .. "." end
  if holds(rec, id) then return "You hold that bounty already: " .. text_of(e, need_for(e)) .. "." end
  if #rec.taken >= hold_max() then
    return string.format("You hold %d of %d bounties: finish one, or drop one with /bounty.drop <n>.", #rec.taken, hold_max())
  end
  local need = need_for(e)
  rec.taken[#rec.taken + 1] = {id = id, need = need, have = 0, state = 0}
  took[id] = true
  dirty = true
  hud_at = -1
  return "Taken: " .. text_of(e, need) .. " (reward: " .. short_reward(e) .. ")"
end

local function drop_bounty(account, n)
  local rec = record(account)
  if #rec.taken == 0 then return "You hold no bounty." end
  n = tonumber(n or "")
  if not n or n % 1 ~= 0 or n < 1 or n > #rec.taken then return "Usage: /bounty.drop <1-" .. #rec.taken .. ">" end
  local b = rec.taken[n]
  if b.state == 1 then return "Bounty " .. n .. " is done: claim it with /bounty.claim." end
  table.remove(rec.taken, n)
  dirty = true
  hud_at = -1
  local e = entry(b.id)
  return "Dropped: " .. (e and text_of(e, b.need) or b.id)
end

-- ---- saving ----
local function sorted_keys(t)
  local keys = {}
  for k in pairs(t) do keys[#keys + 1] = k end
  table.sort(keys)
  return keys
end

local function serialize()
  local lines = {"format 2", "offset " .. offset, "dawns " .. dawns, "posted " .. posted, "play " .. math.floor(play_s),
                 "seed " .. seed}
  for _, k in ipairs(sorted_keys(players)) do
    local rec = players[k]
    lines[#lines + 1] = string.format("p %s %d", k, rec.done or 0)
    if rec.offers and rec.key then
      lines[#lines + 1] = string.format("offers %s %s %d %s", k, rec.key, rec.rerolls or 0, table.concat(rec.offers, " "))
    end
    if rec.took_key and next(rec.took) then
      lines[#lines + 1] = string.format("took %s %s %s", k, rec.took_key, table.concat(sorted_keys(rec.took), " "))
    end
    for _, b in ipairs(rec.taken) do
      lines[#lines + 1] = string.format("t %s %s %d %d %d", k, b.id, b.need, b.have, b.state)
    end
  end
  for _, k in ipairs(sorted_keys(owed)) do
    for _, o in ipairs(owed[k]) do
      lines[#lines + 1] = string.format("owe %s %s %d %d %s", k, o.item, o.count, o.level or 0, o.rarity or "-")
    end
  end
  for _, m in ipairs(marks) do lines[#lines + 1] = string.format("mark %.2f %.2f %.2f %s", m.x, m.y, m.z, m.model) end
  return table.concat(lines, "\n") .. "\n"
end

local function save()
  if loaded and emf.save_world_data(serialize()) then dirty = false end
end

local function words(s)
  local out = {}
  for w in (s or ""):gmatch("%S+") do out[#out + 1] = w end
  return out
end

-- A line of a 1.1 world file (no "format 2" line): a player's board ("p <account> <key> <rerolls> <done>") and its
-- bounties ("b <account> <id> <need> <have> <state>"). Open and finished ones become the player's taken bounties, also
-- beyond the hold limit; claimed ones (state 2) and ids no longer in the pool are left out, as 1.1 did.
local function load_old_line(line)
  local account, done = line:match("^p (%S+) %S+ %d+ (%d+)$")
  if account then record(account).done = tonumber(done) end
  local a, id, need, have, state = line:match("^b (%S+) (%S+) (%d+) (%d+) ([012])$")
  if a and state ~= "2" and entry(id) then
    table.insert(record(a).taken, {id = id, need = tonumber(need), have = tonumber(have), state = tonumber(state)})
  end
end

-- The world's data, read line by line; each kind of line is matched whole (anchored), so one never reads as another.
local function load_text(text)
  players, owed, marks = {}, {}, {}
  offset, dawns, play_s, seed, posted = 0, 0, 0, nil, nil
  local old = true
  for line in text:gmatch("[^\r\n]+") do
    if line == "format 2" then old = false end
  end
  for line in text:gmatch("[^\r\n]+") do
    if old then load_old_line(line) end
    local v = line:match("^offset (%d+)$")
    if v then offset = tonumber(v) end
    v = line:match("^dawns (%d+)$")
    if v then dawns = tonumber(v) end
    v = line:match("^posted (%d+)$")
    if v then posted = tonumber(v) end
    v = line:match("^play (%d+)$")
    if v then play_s = tonumber(v) end
    v = line:match("^seed (%d+)$")
    if v then seed = tonumber(v) end
    local account, done = line:match("^p (%S+) (%d+)$")
    if account then record(account).done = tonumber(done) end
    local acc, key, rerolls, ids = line:match("^offers (%S+) (%S+) (%d+) ?(.*)$")
    if acc then
      local rec = record(acc)
      rec.key, rec.rerolls, rec.offers = key, tonumber(rerolls), {}
      for _, id in ipairs(words(ids)) do
        if entry(id) then rec.offers[#rec.offers + 1] = id end
      end
    end
    acc, key, ids = line:match("^took (%S+) (%S+) ?(.*)$")
    if acc then
      local rec = record(acc)
      rec.took_key, rec.took = key, {}
      for _, id in ipairs(words(ids)) do rec.took[id] = true end
    end
    local a, id, need, have, state = line:match("^t (%S+) (%S+) (%d+) (%d+) ([01])$")
    if a and entry(id) then
      table.insert(record(a).taken, {id = id, need = tonumber(need), have = tonumber(have), state = tonumber(state)})
    end
    local mx, my, mz, model = line:match("^mark (%S+) (%S+) (%S+) (%S+)$")
    if mx and tonumber(mx) and tonumber(my) and tonumber(mz) then
      marks[#marks + 1] = {x = tonumber(mx), y = tonumber(my), z = tonumber(mz), model = model}
    end
    local oa, item, count, level, rarity = line:match("^owe (%S+) (%S+) (%d+) (%d+) (%S+)$")
    if oa then
      owed[oa] = owed[oa] or {}
      table.insert(owed[oa], {item = item, count = tonumber(count), level = tonumber(level),
                              rarity = rarity ~= "-" and rarity or nil})
    end
  end
  local fresh = seed == nil or posted == nil
  seed = seed or math.random(1, 2147483646)
  posted = posted or dawns
  return fresh
end


-- A new hosted session (another world, a reload): creature ids of the old one mean other things now.
local function same_session()
  if session == emf.session() then return end
  session = emf.session()
  hits, gifts, moving, filled, dropped = {}, {}, {}, {}, {}
  boards, found, altars, client_models, walk, cwalk = {}, {}, {}, {}, nil, nil
  loaded = false  -- the old world's data is not this one's: commands wait for on_world_data
  emf.hud("board", "")
  hud_shown = false
end

-- ---- reading the game ----
local function has(world, component, id)
  local ok, value = pcall(world.get, world, component, id)
  if not ok then return nil end
  return value ~= nil
end

local function is_player(world, id)
  return id >= 1 and id <= 1023 and has(world, "PlayerState", id) == true
end

local function level_of(world, id)
  local ok, l = pcall(world.get, world, "Level", id)
  return (ok and l) and l.dataStorage[1] or 1
end

local function faction_of(world, id)
  local ok, f = pcall(world.get, world, "Faction", id)
  return (ok and f) and f.faction or nil
end

local function max_health_of(world, id)
  local ok, h = pcall(world.get, world, "NetworkHealth", id)
  return (ok and h) and h.healthMax or 0
end

-- The player a hit counts for: the player who made it, or the player an ally of theirs guards. The companion, a
-- Beastmaster beast and a Necromancer undead are creatures of the players' side that follow their player through the
-- game's guard system (DynamicEnemy01BehaviorState.guardEntityId names the player); so do the game's own summons.
-- nil: no player (a wild creature, an ally guarding a marker or nobody).
local function player_of_hit(world, source)
  if is_player(world, source) then return source end
  if source == 0 or source <= 1023 or not PLAYER_SIDE[faction_of(world, source)] then return nil end
  local ok, ai = pcall(world.get, world, "DynamicEnemy01BehaviorState", source)
  local guarded = (ok and ai) and ai.guardEntityId or 0
  if guarded ~= 0 and is_player(world, guarded) then return guarded end
  return nil
end

-- What a kill leaves: the loot spot the game spawns where a creature died (templates LootContainer_Enemy_*:
-- InventorySetup, DefaultInventory, DestroyOnLoot, DoNotSave), or a corpse the game turned into loot (Health stays,
-- InventorySetup and DestroyOnLoot come). Never a tombstone (CreateItemLoot), anything kept with the world (ForceSave;
-- chests and storage have neither DestroyOnLoot nor DoNotSave) or a player. Unreadable counts against.
local function kill_loot(world, from)
  if has(world, "DestroyOnLoot", from) ~= true or has(world, "InventorySetup", from) ~= true then return false end
  if has(world, "CreateItemLoot", from) ~= false or has(world, "ForceSave", from) ~= false or
     has(world, "PlayerState", from) ~= false then
    return false
  end
  if has(world, "Health", from) == true then return true end
  return has(world, "DefaultInventory", from) == true and has(world, "DoNotSave", from) == true
end

-- Picked up in the world: from nothing (a tree), a loose drop (PickupItem), a plant or rock, a creature's corpse, or
-- what a kill leaves (its loot spot); never out of a chest, a station or a death bag (InventorySetup), nor out of a
-- tombstone (seen in game: an Inventory, CreateItemLoot and DestroyOnLoot, no Health). A component that cannot be read
-- counts against (fails closed).
local function from_the_world(world, from)
  if from == 0 then return true end
  if kill_loot(world, from) then return true end
  if has(world, "InventorySetup", from) ~= false then return false end
  if has(world, "PickupItem", from) == true then return true end
  if has(world, "Inventory", from) ~= false and has(world, "CreateItemLoot", from) ~= false and
     has(world, "DestroyOnLoot", from) ~= false and has(world, "Health", from) ~= true then
    return false
  end
  return true
end

-- A chest a mod put `item` into: its place, kept LEFTOVER_SECONDS (one place per chest and item, at most 16 an item).
local function note_filled(world, chest, item)
  local x, y, z = world:position(chest)
  if not x then return end
  local list = filled[item] or {}
  filled[item] = list
  for _, f in ipairs(list) do
    if (f.x - x) ^ 2 + (f.y - y) ^ 2 + (f.z - z) ^ 2 < 1 then
      f.until_s = play_s + LEFTOVER_SECONDS
      return
    end
  end
  if #list >= 16 then table.remove(list, 1) end
  list[#list + 1] = {x = x, y = y, z = z, until_s = play_s + LEFTOVER_SECONDS}
end

-- A loose drop a mod made, picked up: this mod's own (a reward dropped at a full player's feet: emf.give named its
-- pickup), or one of the same item within LEFTOVER_METRES of a chest a mod put that item into in the last
-- LEFTOVER_SECONDS (what did not fit there: quick stack and sorting chests drop it beside the chest or the player).
local function mod_drop(world, player, from, item, count)
  local d = dropped[from]
  if d and d.item == item then
    d.left = d.left - count
    if d.left <= 0 then dropped[from] = nil end
    return true
  end
  local list = filled[item]
  if not list or has(world, "PickupItem", from) ~= true then return false end
  local x, y, z = world:position(from)
  if not x then x, y, z = world:position(player) end
  if not x then return false end
  for _, f in ipairs(list) do
    if play_s <= f.until_s and (f.x - x) ^ 2 + (f.y - y) ^ 2 + (f.z - z) ^ 2 <= LEFTOVER_METRES ^ 2 then return true end
  end
  return false
end

local item_names = {}
local function raw_name(item)
  local n = item_names[item]
  if n == nil then
    local info = emf.item_info(item)
    if not info then return nil end  -- not known yet: asked again next time
    n = string.lower(info.name)
    local raw = false
    for _, k in ipairs(RAW) do
      if n:sub(1, #k) == k then raw = true end
    end
    for _, p in ipairs(NEVER) do
      if n:find(p, 1, true) then raw = false end
    end
    if not raw then n = false end
    item_names[item] = n
  end
  return n
end

-- ---- Bounty Boards: Notice Boards in a base, and props the host marked ----
-- The world is searched a slice at a time (SCAN_PER_STEP entities per server step, a new search every BOARD_RESCAN
-- seconds): flame altars (BaseIdComponent), props whose model is in BOARD_MODELS, and the props at marked places.
-- Render models are client-side: on the host the client world is searched for the models too (entity ids are shared
-- between the host's two worlds); a dedicated server has no client world, so there only marked props count unless its
-- server world has the model.
local function horizontal2(a, x, z) return (a.x - x) ^ 2 + (a.z - z) ^ 2 end
local function dist(a, x, y, z) return math.sqrt((a.x - x) ^ 2 + (a.y - y) ^ 2 + (a.z - z) ^ 2) end
local function round(v) return math.floor(v + 0.5) end
local function base_radius() return clamp(tonumber(emf.setting("base_radius")) or 40, 10, 100) end

-- The model of an entity's template: on this world, else what the host's client world showed.
local function model_of(world, id)
  local ok, t = pcall(world.template, world, "ModelResource", id)
  if ok and t then return t.model end
  return client_models[id]
end

-- Every player of this world with a position: the entities 1..1023 with PlayerState.
local function players_now(world)
  local out = {}
  for id = 1, 1023 do
    if has(world, "PlayerState", id) == true then
      local x, y, z = world:position(id)
      if x then out[#out + 1] = {id = id, x = x, y = y, z = z} end
    end
  end
  return out
end

-- The boards from the last search and the marks: Notice Boards within base_radius of an altar (with notice_boards
-- on), and every marked place (the prop found there, or the place alone until a search finds it).
local function rebuild_boards()
  boards = {}
  if emf.setting("notice_boards") then
    local r2 = base_radius() ^ 2
    for _, f in ipairs(found) do
      for _, a in ipairs(altars) do
        if horizontal2(a, f.x, f.z) <= r2 then
          boards[#boards + 1] = {id = f.id, x = f.x, y = f.y, z = f.z, kind = "notice"}
          break
        end
      end
    end
  end
  for _, m in ipairs(marks) do
    local twice = false
    for _, b in ipairs(boards) do
      if dist(b, m.x, m.y, m.z) <= MARK_SAME_M then twice = true end
    end
    if not twice then boards[#boards + 1] = {id = m.id or 0, x = m.x, y = m.y, z = m.z, kind = "mark"} end
  end
end

-- A new search at the next step (after a mark, an unmark, a load).
local function search_soon()
  walk, walk_after = nil, 0
end

local function finish_walk(world)
  found, altars = walk.found, walk.altars
  local everyone = nil
  for i = #marks, 1, -1 do
    local m = marks[i]
    local hit = walk.hit[i]
    if hit then
      m.id = hit
    else
      m.id = nil
      -- missed with a player near its place, the world loaded a while ago: taken down
      if play_s - loaded_at >= LOAD_GRACE then
        everyone = everyone or players_now(world)
        for _, p in ipairs(everyone) do
          if dist(m, p.x, p.y, p.z) <= MARK_GONE_M then
            emf.log(string.format("the marked prop at %d, %d, %d is gone: no Bounty Board there now", round(m.x), round(m.y), round(m.z)))
            table.remove(marks, i)
            dirty = true
            break
          end
        end
      end
    end
  end
  walk = nil
  walk_after = play_s + BOARD_RESCAN
  rebuild_boards()
end

local function walk_step(world)
  if not walk then walk = {ids = world:entities(), i = 1, found = {}, altars = {}, hit = {}} end
  local last = math.min(#walk.ids, walk.i + SCAN_PER_STEP - 1)
  for k = walk.i, last do
    local id = walk.ids[k]
    if has(world, "BaseIdComponent", id) == true then
      local x, _, z = world:position(id)
      if x then walk.altars[#walk.altars + 1] = {x = x, z = z} end
    end
    local model = model_of(world, id)
    if model and BOARD_MODELS[model] then
      local x, y, z = world:position(id)
      if x then walk.found[#walk.found + 1] = {id = id, x = x, y = y, z = z} end
    end
    if #marks > 0 and id > 1023 then
      local x, y, z = world:position(id)
      if x then
        for i, m in ipairs(marks) do
          if not walk.hit[i] and dist(m, x, y, z) <= MARK_SAME_M and (m.model == "-" or not model or model == m.model) then
            walk.hit[i] = id
          end
        end
      end
    end
  end
  walk.i = last + 1
  if walk.i > #walk.ids then finish_walk(world) end
end

-- The host's client world: which entities have a board's model (render models are only there).
local function client_walk_step(world)
  if not cwalk then cwalk = {ids = world:entities(), i = 1, seen = {}} end
  local last = math.min(#cwalk.ids, cwalk.i + SCAN_PER_STEP - 1)
  for k = cwalk.i, last do
    local id = cwalk.ids[k]
    local ok, t = pcall(world.template, world, "ModelResource", id)
    if ok and t and BOARD_MODELS[t.model] then
      cwalk.seen[id] = t.model
      client_models[id] = t.model
    end
  end
  cwalk.i = last + 1
  if cwalk.i > #cwalk.ids then
    client_models, cwalk = cwalk.seen, nil
    cwalk_after = play_s + BOARD_RESCAN
  end
end

-- The board nearest to `player` within `range` metres, or nil.
local function board_near(world, player, range)
  local x, y, z = world:position(player)
  if not x then return nil end
  local best, best_d = nil, range
  for _, b in ipairs(boards) do
    local d = dist(b, x, y, z)
    if d <= best_d then best, best_d = b, d end
  end
  return best
end

-- The camera ray of `player` (PlayerInput.fromClient.camera on the server world), or nil when there is none.
local function camera_ray(world, player)
  local ok, input = pcall(world.get, world, "PlayerInput", player)
  if not ok or not input then return nil end
  local cam = input.fromClient.camera
  local q = cam.orientation
  local fx = 2 * (q.x * q.z + q.w * q.y)  -- the camera's +z axis
  local fy = 2 * (q.y * q.z - q.w * q.x)
  local fz = 1 - 2 * (q.x * q.x + q.y * q.y)
  local len = math.sqrt(fx * fx + fy * fy + fz * fz)
  if len < 1e-9 or (q.x == 0 and q.y == 0 and q.z == 0 and q.w == 0) then return nil end
  return {ox = cam.position.x / FIX, oy = cam.position.y / FIX, oz = cam.position.z / FIX,
          dx = fx / len, dy = fy / len, dz = fz / len}
end

-- The prop under `player`'s crosshair within `range` metres (not a player; with a collider or a model), or nil.
local function prop_in_front(world, player, range)
  local ray = camera_ray(world, player)
  local px, py, pz = world:position(player)
  if not ray or not px then return nil end
  local best, best_perp, best_t
  for _, id in ipairs(world:entities()) do
    if id ~= player and not is_player(world, id) then
      local x, y, z = world:position(id)
      if x and math.sqrt((x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2) <= range then
        local vx, vy, vz = x - ray.ox, y + PROP_HEIGHT - ray.oy, z - ray.oz
        local t = vx * ray.dx + vy * ray.dy + vz * ray.dz
        if t > 0 then
          local ex, ey, ez = vx - ray.dx * t, vy - ray.dy * t, vz - ray.dz * t
          local perp = math.sqrt(ex * ex + ey * ey + ez * ez)
          if perp <= 1.5 and (has(world, "StaticCollider", id) == true or model_of(world, id)) and
             (not best or perp < best_perp - 0.25 or (math.abs(perp - best_perp) <= 0.25 and t < best_t)) then
            best, best_perp, best_t = id, perp, t
          end
        end
      end
    end
  end
  return best
end

-- The world's data: after the world loaded (and after a reload of this mod).
emf.on_world_data(function(world, text)
  load_pool()  -- pool.lua may have changed since
  hits, gifts, moving, filled, dropped = {}, {}, {}, {}, {}
  purge_at, last_tod = 0, nil  -- another world's clocks mean nothing here
  local fresh = load_text(text)
  loaded, dirty = true, fresh  -- a new seed is kept at once
  loaded_at = play_s
  found, altars, client_models, cwalk, cwalk_after = {}, {}, {}, nil, 0
  search_soon()
  rebuild_boards()
  session = emf.session()
  hud_at = -1
end)

-- ---- progress and rewards ----
local function account_of(world, player) return world:account(player) end

local function owe(account, item, count, opts)
  owed[account] = owed[account] or {}
  table.insert(owed[account], {item = item, count = count, level = opts and opts.level or 0, rarity = opts and opts.rarity})
  dirty = true
  emf.log(string.format("reward: %d x %s could not be given (the player left?): owed to %s until their next /bounty.claim",
    count, item, account))
end

-- Drops `count` of item `id` at the player's feet; the pickup is remembered, so picking it up later is no gathering.
local function drop(world, player, id, count)
  local pickup = emf.give(world, player, id, count)
  if pickup == 0 then return false end
  dropped[pickup] = {item = id, left = count, until_s = play_s + DROP_SECONDS}
  return true
end

-- One reward item into the player; what does not fit is dropped at their feet; what cannot even be dropped (the player
-- left before it landed) is owed to their account.
local function deliver(world, player, account, name, count, opts)
  local id = emf.item(name)
  if not id then
    emf.log("no item named " .. name .. " (a reward in POOL)")
    return nil
  end
  local key = player .. ":" .. id
  local g = gifts[key] or {count = 0}
  g.count, g.until_s = g.count + count, play_s + 10
  gifts[key] = g
  local queued = emf.put(world, player, id, count, function(w, added)
    if added < count then  -- no room for the rest: at the player's feet, never lost
      local left = gifts[key]
      if left then
        left.count = left.count - (count - added)
        if left.count <= 0 then gifts[key] = nil end
      end
      if drop(w, player, id, count - added) then
        emf.log(string.format("reward: %d of %d x %s fitted; the rest dropped at player %d's feet", added, count, name, player))
      else
        owe(account, name, count - added, opts)
      end
    end
  end, opts)
  if not queued then
    if drop(world, player, id, count) then
      emf.log(string.format("reward: the put of %d x %s was refused; dropped at player %d's feet", count, name, player))
    else
      owe(account, name, count, opts)
    end
  end
  return count .. " x " .. name
end

local function give_rewards(world, player, account, e)
  local got = {}
  local level = level_of(world, player)
  for _, r in ipairs(e.reward or {}) do
    local opts = nil
    if r.gear then
      opts = {level = level}
      local rarity = emf.setting("reward_rarity") or "default"
      if rarity ~= "default" then opts.rarity = rarity end
    end
    local text = deliver(world, player, account, r.item, scaled(r), opts)
    if text then got[#got + 1] = text end
  end
  return table.concat(got, ", ")
end

local function claim_all(world, player, account)
  local rec = record(account)
  local parts = {}
  local waiting = owed[account]  -- rewards owed from before come first
  if waiting then
    owed[account] = nil
    dirty = true
    local got = {}
    for _, o in ipairs(waiting) do
      local text = deliver(world, player, account, o.item, o.count, (o.level > 0 or o.rarity) and {level = o.level, rarity = o.rarity} or nil)
      if text then got[#got + 1] = text end
    end
    if #got > 0 then parts[#parts + 1] = table.concat(got, ", ") end
  end
  for i = 1, #rec.taken do
    local b = rec.taken[i]
    if b.state == 1 then
      local e = entry(b.id)
      if e then parts[#parts + 1] = give_rewards(world, player, account, e) end
    end
  end
  for i = #rec.taken, 1, -1 do
    if rec.taken[i].state == 1 then
      table.remove(rec.taken, i)
      dirty = true
    end
  end
  return parts
end

-- The text a player gets when a bounty of theirs is done (the host on screen; Task 8 tells players who joined).
local function done_note(player, account, text) if player == emf.local_player() then emf.notify(text) end end

local function progress(world, player, kind, match, amount)
  local account = account_of(world, player)
  if not account or not loaded then return end
  local rec = record(account)
  for i = #rec.taken, 1, -1 do
    local b = rec.taken[i]
    local e = entry(b.id)
    if b.state == 0 and e and e.kind == kind and match(e) then
      b.have = math.min(b.need, b.have + amount)
      dirty = true
      hud_at = -1
      if b.have >= b.need then
        b.state = 1
        rec.done = (rec.done or 0) + 1
        local text = "Bounty done: " .. text_of(e, b.need)
        if emf.setting("auto_claim") then
          table.remove(rec.taken, i)
          text = text .. "! " .. give_rewards(world, player, account, e) .. " are yours"
        else
          text = text .. "! /bounty.claim for " .. short_reward(e)
        end
        emf.log(text .. string.format(" (player %d, %s)", player, account))
        done_note(player, account, text)
      end
    end
  end
end

-- ---- events ----
emf.on_event("HitEvent", function(world, h)
  same_session()
  local target = h.targetId
  if target == 0 or target <= 1023 then return end
  local source = player_of_hit(world, h.rootSourceId)  -- the player, or the player whose companion or summon hit
  if not source then return end
  local rec = hits[target]
  if not rec then
    rec = {by = {}, faction = faction_of(world, target), level = level_of(world, target), max = max_health_of(world, target)}
    hits[target] = rec
  end
  rec.by[source] = play_s
  rec.last = source
end, "server")

emf.on_event("EntityDiedEvent", function(world, d)
  same_session()
  local rec = hits[d.deadEntityId]
  if not rec then return end
  hits[d.deadEntityId] = nil
  local credited = {}
  if emf.setting("share_kills") then
    for player, at in pairs(rec.by) do
      if play_s - at <= SHARE_SECONDS then credited[#credited + 1] = player end
    end
  elseif rec.last and play_s - rec.by[rec.last] <= SHARE_SECONDS then
    credited[1] = rec.last
  end
  for _, player in ipairs(credited) do
    if rec.faction then progress(world, player, "kill", function(e) return e.faction == rec.faction end, 1) end
    if rec.level >= level_of(world, player) + CHAMPION_LEVELS and rec.max >= CHAMPION_HEALTH then
      progress(world, player, "champion", function() return true end, 1)
    end
  end
end, "server")

emf.on_event("PickupEvent", function(world, e)
  same_session()
  local player, from, item, count = e.pickedUpByEntityId, e.pickedUpFromEntityId, e.itemId, e.itemCount
  if not is_player(world, player) then
    -- a chest (any storage) got items from nothing: a mod's put (quick stack, a sorting chest...). Whatever of that
    -- item a player gets from nothing in the next moment is the rest coming back, no gathering; a loose drop of it near
    -- that chest a while after is what did not fit there (mod_drop).
    if from == 0 and player ~= 0 and has(world, "InventorySetup", player) == true then
      moving[item] = play_s + MOVE_SECONDS
      note_filled(world, player, item)
    end
    return
  end
  if from == 0 and moving[item] and play_s <= moving[item] then return end
  if from ~= 0 and mod_drop(world, player, from, item, count) then return end
  if from == 0 then  -- a reward on its way is no gathering
    local key = player .. ":" .. item
    local g = gifts[key]
    if g and g.count >= count then
      g.count = g.count - count
      if g.count <= 0 then gifts[key] = nil end
      return
    end
  end
  if not from_the_world(world, from) then return end
  local name = raw_name(item)
  if not name then return end
  progress(world, player, "gather", function(en) return name:find(en.match, 1, true) ~= nil end, count)
end, "server")

-- ---- the HUD (the host's own board) ----
local function show(world)
  local me = emf.local_player()
  local account = me and me ~= 0 and world:account(me)
  if not emf.setting("hud") or not account or not loaded then
    if hud_shown then emf.hud("board", "") end
    hud_shown = false
    return
  end
  local rec = record(account)
  local parts = {}
  for _, b in ipairs(rec.taken) do
    local e = entry(b.id)
    if e then parts[#parts + 1] = string.format("%s %d/%d", short_of(e), b.have, b.need) end
  end
  if #parts == 0 then
    if hud_shown then emf.hud("board", "") end
    hud_shown = false
    return
  end
  emf.hud("board", "Bounty: " .. table.concat(parts, " | "), emf.setting("corner"), emf.setting("color"))
  hud_shown = true
end

-- ---- commands ----
local function sender(world)
  local who = emf.sender()
  local id = (who and who.chat) and who.entity or emf.local_player()
  if not id or id == 0 then return nil end
  local account = world:account(id)
  if not account then return nil end
  return {id = id, account = account}
end

local function unknown(world)
  emf.reply("Bounty Board: EMF could not tell which player you are; try again in a moment")
end

local function not_loaded()
  emf.reply("Bounty Board: this world's data is not loaded yet; try again in a moment")
end

-- The sender, once the world's data is there (else the reply says why and nil comes back).
local function ready(world)
  same_session()
  local me = sender(world)
  if not me then
    unknown(world)
    return nil
  end
  if not loaded then
    not_loaded()
    return nil
  end
  return me
end

-- The notice lines for `account`: "1. Gather 20 Wood logs (3 runes)", " - taken" after the ones they took.
local function notice_lines(account)
  local out = {}
  local took = took_of(record(account))
  for i, id in ipairs(notices_for(account)) do
    local e = entry(id)
    if e then out[#out + 1] = string.format("%d. %s%s", i, notice_text(e), took[id] and " - taken" or "") end
  end
  return out
end

emf.command("board", ": the notices, your bounties and how far you are", function(world, args)
  local me = ready(world)
  if not me then return end
  local rec = record(me.account)
  emf.reply("Notices (take one with /bounty.take <n>):")
  for _, l in ipairs(notice_lines(me.account)) do emf.reply(l) end
  if #rec.taken == 0 then emf.reply("You hold no bounty: take one from the notices.") end
  for i, b in ipairs(rec.taken) do
    local e = entry(b.id)
    if e then
      local state = b.state == 1 and "done" or (b.have .. "/" .. b.need)
      local tail = " (reward: " .. short_reward(e) .. (b.state == 1 and "; /bounty.claim)" or ")")
      emf.reply(string.format("Yours %d. %s: %s%s", i, text_of(e, b.need), state, tail))
    end
  end
  if owed[me.account] then
    local n = #owed[me.account]
    emf.reply(string.format("%d reward%s waiting: /bounty.claim", n, n == 1 and "" or "s"))
  end
  local last = string.format("%s; you hold %d of %d", until_text(), #rec.taken, hold_max())
  if each_player() then
    local left = math.max(0, (emf.setting("rerolls") or 0) - (rec.rerolls or 0))
    last = last .. string.format("; %d reroll%s left (/bounty.reroll <n>)", left, left == 1 and "" or "s")
  end
  emf.reply(last .. ".")
end, {players = true})

emf.command("take", "<n>: take notice n from the board", function(world, args)
  local me = ready(world)
  if not me then return end
  emf.reply(take(me.account, args[1]))
end, {players = true})

emf.command("drop", "<n>: give up bounty n of yours", function(world, args)
  local me = ready(world)
  if not me then return end
  emf.reply(drop_bounty(me.account, args[1]))
end, {players = true})

emf.command("claim", ": the rewards of your finished bounties", function(world, args)
  local me = ready(world)
  if not me then return end
  local parts = claim_all(world, me.id, me.account)
  if #parts == 0 then
    emf.reply("No finished bounty to claim.")
    return
  end
  hud_at = -1
  emf.reply("Claimed: " .. table.concat(parts, "; "))
end, {players = true})

emf.command("reroll", "<n>: swap notice n for another (notices of your own)", function(world, args)
  local me = ready(world)
  if not me then return end
  if not each_player() then
    emf.reply("The notices are the same for everyone: they cannot be rerolled.")
    return
  end
  local rec = record(me.account)
  local list = notices_for(me.account)
  local n = tonumber(args[1] or "")
  if not n or n < 1 or n > #list or n % 1 ~= 0 then
    emf.reply("Usage: /bounty.reroll <1-" .. #list .. ">")
    return
  end
  if (rec.rerolls or 0) >= (emf.setting("rerolls") or 0) then
    emf.reply("No rerolls left until the next notices.")
    return
  end
  local exclude = {}
  for _, id in ipairs(list) do exclude[id] = true end
  for _, b in ipairs(rec.taken) do exclude[b.id] = true end
  local used = (rec.rerolls or 0) + 1
  local salt = "r" .. used .. ":" .. n
  local new = draw(me.account, rec.key, salt, 1, exclude, starters_for(rec.done))[1] or
              draw(me.account, rec.key, salt, 1, exclude, false)[1]  -- no other starter: one of the other bounties
  if not new then  -- nothing to swap to: the reroll is not used up
    emf.reply("The pool has no other bounty.")
    return
  end
  rec.rerolls = used
  rec.offers[n] = new
  dirty = true
  emf.reply("Notice " .. n .. " is now: " .. notice_text(entry(new)))
end, {players = true})

emf.command("top", ": who finished the most bounties in this world", function(world, args)
  same_session()
  local me = sender(world)
  local list = {}
  for account, rec in pairs(players) do
    if (rec.done or 0) > 0 then list[#list + 1] = {account = account, done = rec.done} end
  end
  table.sort(list, function(a, b) return a.done > b.done or (a.done == b.done and a.account < b.account) end)
  if #list == 0 then
    emf.reply("Nobody has finished a bounty in this world yet.")
    return
  end
  for i = 1, math.min(5, #list) do
    local x = list[i]
    emf.reply(string.format("%d. %s: %d%s", i, x.account, x.done, (me and me.account == x.account) and " (you)" or ""))
  end
end, {players = true})

emf.command("give", "<bounty id>: make a bounty from the pool yours at once (host, for tests)", function(world, args)
  same_session()
  local me = sender(world)
  if not me then return unknown(world) end
  local e = entry(args[1] or "")
  if not e then
    local ids = {}
    for _, x in ipairs(POOL) do ids[#ids + 1] = x.id end
    emf.reply("Unknown bounty; the pool has: " .. table.concat(ids, ", "))
    return
  end
  if not loaded then return not_loaded() end
  local rec = record(me.account)
  local b = {id = e.id, need = need_for(e), have = 0, state = 0}
  local replaced = false
  for i, x in ipairs(rec.taken) do
    if x.id == e.id then
      rec.taken[i] = b
      replaced = true
    end
  end
  if not replaced then table.insert(rec.taken, b) end
  dirty = true
  hud_at = -1
  emf.reply("Given: " .. text_of(e, b.need) .. " (reward: " .. reward_text(e) .. ")")
end)

emf.command("reset", ": new notices for everyone now (host)", function(world, args)
  if not loaded then return not_loaded() end
  offset = offset + 1
  posted = dawns
  dirty = true
  hud_at = -1
  emf.reply("New notices for everyone.")
end)

emf.command("mark", ": the prop you look at becomes a Bounty Board (host, admins)", function(world, args)
  local me = ready(world)
  if not me then return end
  local id = prop_in_front(world, me.id, MARK_REACH_M)
  if not id then
    emf.reply("Look at a prop within 8 m (a sign, a post, a table) and try again.")
    return
  end
  local x, y, z = world:position(id)
  for _, b in ipairs(boards) do
    if dist(b, x, y, z) <= MARK_SAME_M then
      emf.reply("That is a Bounty Board already.")
      return
    end
  end
  local model = model_of(world, id) or "-"
  marks[#marks + 1] = {x = x, y = y, z = z, model = model, id = id}
  dirty = true
  search_soon()
  rebuild_boards()
  emf.log(string.format("prop %d at %d, %d, %d (model %s) marked as a Bounty Board", id, round(x), round(y), round(z), model))
  emf.reply(string.format("Marked: this prop is a Bounty Board now (at %d, %d, %d).", round(x), round(y), round(z)))
end)

emf.command("unmark", ": the marked prop nearest to you is no Bounty Board now (host, admins)", function(world, args)
  local me = ready(world)
  if not me then return end
  local x, y, z = world:position(me.id)
  local best, best_d = nil, MARK_REACH_M
  for i, m in ipairs(marks) do
    local d = x and dist(m, x, y, z) or math.huge
    if d <= best_d then best, best_d = i, d end
  end
  if not best then
    emf.reply("No marked prop within 8 m.")
    return
  end
  local m = table.remove(marks, best)
  dirty = true
  search_soon()
  rebuild_boards()
  emf.reply(string.format("Unmarked: the prop at %d, %d, %d is no Bounty Board now.", round(m.x), round(m.y), round(m.z)))
end)

emf.command("boards", ": the Bounty Boards of this world (host)", function(world, args)
  local me = ready(world)
  if not me then return end
  if #boards == 0 then
    emf.reply(string.format("No Bounty Board in this world yet: build a Notice Board within %d m of a flame altar, or " ..
      "mark a prop with bounty.mark (a new board counts within a minute).", round(base_radius())))
    return
  end
  local x, y, z = world:position(me.id)
  for i, b in ipairs(boards) do
    emf.reply(string.format("%d. %s at %d, %d, %d%s", i, b.kind == "notice" and "Notice Board" or "marked prop",
      round(b.x), round(b.y), round(b.z), x and string.format(" (%d m from you)", round(dist(b, x, y, z))) or ""))
  end
end)

-- ---- every step ----
-- A dawn between two readings of the clock: it passed 6:00, or it went back (past midnight: the host set the time, or
-- the players slept through the night) to 6:00 or later. The game keeps no day number, so the mod counts dawns.
local function is_dawn(before, now)
  if before < 6 and now >= 6 then return true end
  return now < before and now >= 6
end

emf.on_tick(function(world)
  if world:is_client() then  -- in someone else's game: the line goes
    client_steps = client_steps + 1
    if client_steps > 180 and hud_shown then
      emf.hud("board", "")
      hud_shown = false
    end
    -- the host's own client world (its server world steps too): the board models are searched here
    if loaded and client_steps <= 180 and session == emf.session() and (cwalk or play_s >= cwalk_after) then
      client_walk_step(world)
    end
    return
  end
  if not world:is_server() then return end
  client_steps = 0
  same_session()
  local t = world:tick()
  if last_tick and t > last_tick and t - last_tick < 600 then play_s = play_s + (t - last_tick) / 60 end
  last_tick = t
  if not loaded then return end
  local tod = emf.time_of_day()
  if tod then
    if last_tod and is_dawn(last_tod, tod) then
      dawns = dawns + 1
      if emf.setting("reset") == "game_days" and dawns - posted >= reset_days() then posted = dawns end  -- new notices
      dirty = true
      hud_at = -1
    end
    last_tod = tod
  end
  if play_s >= purge_at then  -- forget fights nobody finished
    purge_at = play_s + 10
    for id, rec in pairs(hits) do
      local newest = 0
      for _, at in pairs(rec.by) do newest = math.max(newest, at) end
      if play_s - newest > 60 then hits[id] = nil end
    end
    for k, g in pairs(gifts) do
      if play_s > g.until_s then gifts[k] = nil end
    end
    for k, until_s in pairs(moving) do
      if play_s > until_s then moving[k] = nil end
    end
    for item, list in pairs(filled) do
      for i = #list, 1, -1 do
        if play_s > list[i].until_s then table.remove(list, i) end
      end
      if #list == 0 then filled[item] = nil end
    end
    for k, d in pairs(dropped) do
      if play_s > d.until_s then dropped[k] = nil end
    end
  end
  if walk or play_s >= walk_after then walk_step(world) end
  if dirty then save() end
  local s = math.floor(play_s)
  if s ~= hud_at then
    hud_at = s
    show(world)
  end
end)

emf.on_setting_changed(function(key)
  hud_at = -1
  if key == "base_radius" or key == "notice_boards" then rebuild_boards() end
end)
