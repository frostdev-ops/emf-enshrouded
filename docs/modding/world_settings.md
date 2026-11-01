# World settings while the world runs

The world's settings are the values of `enshrouded_server.json`'s `"gameSettings"` (or of the world's preset): day
and night length, loot, XP, enemy damage, weather and the rest. The game reads them when a world loads and has no way
to change them while it runs. EMF changes them live, the game's own way: it writes the running world's settings on
the server and lets the game send them to every player, as it does when a player joins.

- **Who**: the host of a game, or a dedicated server. A player who joined someone else's game cannot change them (the
  commands answer that no world is hosted here).
- **From where**: the in-game console, `emf\commands.txt`, the game's chat for the accounts in `[chat] admins`
  ([server.md](server.md)), and mods (C API v21, Lua 1.10).
- **Safe values only**: each setting takes the range the dedicated server's readme gives it. A value outside it is
  refused, never cut to fit, and the reply names the allowed range.
- **For this session**: a change lasts until the world is loaded again. Add `persist` to keep it for this world: EMF
  applies it again every time the world loads, with a line in `emf_core.log`.
- **Your files stay**: EMF never edits `enshrouded_server.json` (it holds passwords) or the save's own settings.
  `world.export` prints a block you can paste into `enshrouded_server.json` yourself.

## Commands

| Command | What it does |
|---|---|
| `world.get` | what was changed from this world's own values |
| `world.get <setting>` | the value now, the world's own value and the allowed range |
| `world.set <setting> <value> [persist]` | changes it from the next server step, for every player; `persist` keeps it for this world |
| `world.reset [<setting>\|all] [persist]` | back to this world's own value (no setting: every setting); `persist` also forgets the kept value |
| `world.list` | every setting by group, `*` for the changed ones |
| `world.export` | the settings as `"gameSettingsPreset": "Custom"` and a `"gameSettings"` block for `enshrouded_server.json` |

A setting is named as `enshrouded_server.json` names it (`playerStaminaFactor`, any case) or by its short name
(`loot`, `day_minutes`). Values: factors as numbers (`1.5`), durations in minutes (`world.set night_minutes 5`),
choices by name or number (`world.set weather Rare`, `world.set weather 1`), and `on`/`off` (also `1`/`0`,
`true`/`false`, `yes`/`no`).

```text
world.set loot 2
world.set day_minutes 45 persist
world.get night_minutes
world.reset all
```

In the game's chat, players type them with a `/` (`/world.set loot 2`); only the accounts in `[chat] admins` may.

## How a change reaches the players

A change is queued and written on the next step of the server world. Several changes in one step are one write. After
the write EMF sets the game's own flag that sends the settings to every player (at most once a second), so players'
games get the new values within a second or two; a player who joins later gets them from the game at login. The
settings marked "players' games read it" below are also used by each player's own game (their stamina, the day length
they see, glider turbulence): they are right for a player once that resync arrives.

On a game build EMF does not know yet, the flag may not be found: the write on the server still happens, and the
reply and `emf_core.log` say that players' games keep the old values of the settings they read until they reconnect.
There EMF also checks first that the world's settings look as it knows them (factors 0-10, durations 1-240 minutes,
valid choices); if they do not, changes are off for that world and the reply says why: EMF needs an update for that
game version.

Kept values are stored per world save slot in `<game>\emf\config\world_settings\<slot>.txt` (on a dedicated server in
the server folder's `emf\config\world_settings\`), one `key=value` line each (durations in minutes, choices as numbers).
A line with an unknown setting or a value the setting does not allow is skipped with a log line; the rest apply. A new
world started in the same slot gets the kept values too: `world.reset all persist` clears them. Until EMF has seen
the world's save load, `persist` is refused (the change itself works without it).

## The settings

The ranges are those of the dedicated server's readme. A world whose preset already lies outside one (a hand-edited
`enshrouded_server.json`) still loads as it is; only the setting you change is checked against its range.

**Player**

| Setting | Short name | Allowed | Players' games read it |
|---|---|---|---|
| `playerHealthFactor` |  | 0.25 to 4 | yes |
| `playerManaFactor` |  | 0.25 to 4 | yes |
| `playerStaminaFactor` |  | 0.25 to 4 | yes |
| `playerBodyHeatFactor` |  | 0.5 to 2 | yes |
| `playerDivingTimeFactor` |  | 0.5 to 2 | yes |
| `shroudTimeFactor` |  | 0.5 to 2 | yes |

**Survival**

| Setting | Short name | Allowed | Players' games read it |
|---|---|---|---|
| `enableDurability` | `durability` | on or off | yes |
| `enableStarvingDebuff` | `starving` | on or off |  |
| `foodBuffDurationFactor` | `food_buff` | 0.5 to 2 |  |
| `fromHungerToStarving` | `hunger_minutes` | 5 to 20 minutes |  |
| `tombstoneMode` | `tombstone` | 0 AddBackpackMaterials, 1 Everything, 2 NoTombstone |  |
| `enableGliderTurbulences` | `glider_turbulence` | on or off | yes |

**Enemies**

| Setting | Short name | Allowed | Players' games read it |
|---|---|---|---|
| `aggroPoolAmount` |  | 0 Few, 1 Normal, 2 Many, 3 Extreme |  |
| `enemyDamageFactor` |  | 0.25 to 5 |  |
| `enemyHealthFactor` |  | 0.25 to 4 | yes |
| `enemyStaminaFactor` |  | 0.5 to 2 | yes |
| `enemyPerceptionRangeFactor` |  | 0.5 to 2 |  |
| `bossDamageFactor` |  | 0.2 to 5 |  |
| `bossHealthFactor` |  | 0.2 to 5 | yes |
| `threatBonus` |  | 0.25 to 4 |  |
| `pacifyAllEnemies` |  | on or off |  |
| `curseModifier` |  | 0 Easy, 1 Normal, 2 Hard | yes |

**Rates**

| Setting | Short name | Allowed | Players' games read it |
|---|---|---|---|
| `miningDamageFactor` | `mining` | 0.5 to 2 |  |
| `plantGrowthSpeedFactor` | `plant_growth` | 0.25 to 2 |  |
| `resourceDropStackAmountFactor` | `loot` | 0.25 to 2 |  |
| `factoryProductionSpeedFactor` | `factory_speed` | 0.25 to 2 |  |
| `perkUpgradeRecyclingFactor` | `salvage_runes` | 0 to 1 | yes |
| `perkCostFactor` | `upgrade_cost` | 0.25 to 2 | yes |
| `experienceCombatFactor` | `xp_combat` | 0.25 to 2 |  |
| `experienceMiningFactor` | `xp_mining` | 0 to 2 |  |
| `experienceExplorationQuestsFactor` | `xp_explore` | 0.25 to 2 |  |

**Time**

| Setting | Short name | Allowed | Players' games read it |
|---|---|---|---|
| `dayTimeDuration` | `day_minutes` | 2 to 60 minutes | yes |
| `nightTimeDuration` | `night_minutes` | 2 to 60 minutes | yes |

**World**

| Setting | Short name | Allowed | Players' games read it |
|---|---|---|---|
| `weatherFrequency` | `weather` | 0 Disabled, 1 Rare, 2 Normal, 3 Often |  |
| `randomSpawnerAmount` | `enemy_amount` | 0 Few, 1 Normal, 2 Many, 3 Extreme, 4 Automatic |  |
| `flameLevelFogRemoval` | `altar_shroud_level` | 0 to 20 (whole) | yes |

**Other**

| Setting | Short name | Allowed | Players' games read it |
|---|---|---|---|
| `fishingDifficulty` |  | 0 VeryEasy, 1 Easy, 2 Normal, 3 Hard, 4 VeryHard | yes |
| `tamingStartleRepercussion` |  | 0 KeepProgress, 1 LoseSomeProgress, 2 LoseAllProgress |  |

## For mods

C and C++ ([reference.md](reference.md), v21):

| C++ (`emf/emf.hpp`) | C | What it does |
|---|---|---|
| `emf::world_setting(key)` | `world_setting_get(key, &value)` | the value now (durations in minutes); nothing / false for an unknown key or without a hosted world |
| `emf::set_world_setting(key, value, persist)` | `world_setting_set(key, value, flags)` | queued for the next server step; false (logged with the allowed range) when refused. `persist` / `EMF_WS_PERSIST` keeps it for this world |
| `emf::reset_world_setting(key, persist)` | `world_setting_reset(key, flags)` | back to the world's own value; `nullptr` / `NULL` or `"all"`: every setting |
| — | `on_world_setting_changed(cb, user)` | `cb(key, value, user)` once per setting the server step wrote, whoever changed it (a command, a mod, a kept value after a load), on the server world's thread |
| — | `world_setting_info(index, &info)` | the settings one by one from index 0: key, short name, group, type, range, the value now, the world's own value, changed |
| `emf::has_v21()` | — | true when the core has them |

Lua ([lua.md](lua.md), 1.10):

```lua
local ws = emf.world_settings
ws.get("loot")                      --> 1.0 (nil: unknown, or no world hosted here)
ws.set("night_minutes", 5)          --> true (queued for the next server step)
ws.set("loot", 10)                  --> false, "loot: allowed 0.25 to 2"
ws.set("day_minutes", 45, true)     -- true: keep it for this world
ws.reset("loot")                    -- one setting; ws.reset() every setting; ws.reset(true) also forgets kept values
for _, s in ipairs(ws.list()) do    -- {key, alias, group, type, min, max, value, world, changed, needs_client}
  emf.log(s.key .. " = " .. s.value)
end
emf.on_world_setting_changed(function(key, value) emf.log(key .. " is now " .. value) end)
```

A mod that changes settings should say so in its README: players expect the world to keep the settings they chose.
