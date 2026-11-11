# Lua mods: start here

A Lua mod is a folder with two text files. No compiler, no Visual Studio: write the files, start the game, and change
them while the game runs; EMF reloads a Lua mod within a second of you saving it.

Lua mods need the `lua` mod (in the EMF zip, on by default): without it (missing, switched off or failed) a Lua mod
and every mod that depends on it do not run, and the Mods tab says why. They run on the same EMF API as C++ mods, with
the same rules: gameplay changes happen on the **host** (your own game, or the game you host).

## Your first Lua mod (five minutes)

The quickest start is the starter in the EMF zip: copy `template_lua\` into `<game>\emf\mods\` and follow
[first_mod.md](first_mod.md) (a line on screen, a key, one setting of each kind, in 10 minutes). Or by hand:

1. Make a folder `<game>\emf\mods\hello_lua\`.
2. Put `mod.json` into it:

   ```json
   {
     "id": "hello_lua",
     "name": "Hello Lua",
     "version": "1.0.0",
     "emf": 14,
     "main": "main.lua",
     "depends": {"lua": ">=1.1"}
   }
   ```

   (`lua` 1.1 is the Lua host that has `emf.on_world_data`, `emf.save_world_data` and `world:account`; a mod that uses
   none of them can depend on `>=1.0`. 1.2 also takes full type names such as `keen::ecs::HitEvent`, and
   `emf.command` returns true or false. 1.3 adds `emf.on_key` and `emf.key_down`, and `emf.hud` takes a corner name
   and a `#rrggbb` colour: depend on `">=1.3"` for those. 1.4 adds `emf.time_of_day`, `emf.kill`, a level and rarity
   for `emf.put`, and commands for every player (`emf.command(..., {players = true})`): depend on `">=1.4"`. 1.5 adds
   `emf.health`, `emf.attribute`, `emf.attribute_add` and `emf.damage` (the host needs EMF API v18): depend on
   `">=1.5"`. 1.6 adds `emf.tell`, `emf.broadcast`, `world:name`, `emf.on_chat` and `emf.request_save` (EMF API
   v19), and `emf.command(emf.id, ...)` makes the bare command `/<id>`: depend on `">=1.6"`. 1.7 adds `emf.teleport`
   (EMF API v20): depend on `">=1.7"`. 1.8 adds `world:template`: depend on `">=1.8"`. 1.9 adds wheel groups (`emf.wheel_section`, `emf.wheel_group`): depend on `">=1.9"`. 1.10 adds
   `emf.world_settings` and `emf.on_world_setting_changed` (EMF API v21): depend on `">=1.10"`. 1.11 adds a
   player's XP and level (`emf.xp_required`, `emf.level_cap`, `emf.set_level` and the attribute names `"xp_pending"`,
   `"xp"`, `"xp_required"`, `"level"`; EMF API v22): depend on `">=1.11"`. 1.12 adds `emf.add_component`,
   `emf.remove_component` (EMF API v23) and components spawned with `emf.spawn`: depend on `">=1.12"`. 1.13 adds
   mod windows (`emf.window`, `emf.window_update`, `emf.window_close`; EMF API v24): depend on `">=1.13"`. 1.14 adds
   E prompts (`emf.offer`, `emf.spawn_offer`, `emf.offer_remove`; EMF API v25): depend on `">=1.14"`. 1.15 adds
   pictures in windows (`emf.image`, the `image`, `icon`, `fps` and `height` row fields; EMF API v26): depend on
   `">=1.15"`. 1.17 adds the inter-mod message bus (`emf.bus.subscribe`, `.publish`, `.unsubscribe`): mods using it
   depend on `"lua": ">=1.17"`. 1.18 adds `emf.json` (encode and decode tables as JSON text, for messages on the bus): depend on `">=1.18"`. 1.19 adds mod messages (`emf.net`; EMF API v27): depend on `">=1.19"`. 1.20 adds player events (`emf.on_player_joined`, `_left`, `_died`; EMF API v28): depend on `">=1.20"`. 1.21 adds the game's item names and language (`emf.item_name`, `emf.language`; EMF API v29): depend on `">=1.21"`. 1.22 adds `world:character_name` and the `{system = true}` option of `emf.tell` and `emf.broadcast` (EMF API v30): depend on `">=1.22"`. 1.23 adds hits (`emf.on_hit`, `emf.hit_rule`, `emf.hit_rule_remove`; EMF API v31): depend on `">=1.23"`. Ranges are explained in
   [guide.md](guide.md), "Dependencies and version ranges".)

3. Put `main.lua` next to it:

   ```lua
   emf.log("hello from Lua")

   emf.command("where", ": where am I?", function(world, args)
     local x, y, z = world:position(emf.local_player())
     emf.reply(string.format("you are at %.1f, %.1f, %.1f", x, y, z))
   end)

   emf.on_tick(function(world)
     if world:is_client() then
       emf.hud("clock", "Lua says hi", 2)  -- bottom left
     end
   end)
   ```

4. Start the game and load a world. "Lua says hi" shows in the bottom left; open the console (the key under Esc) and
   type `hello_lua.where`. Now change the text in `main.lua` and save: within a second the game shows "Lua mod
   hello_lua reloaded" and the new text.

`emf\mods\kill_counter\` is a complete example (it counts your kills): read it, then copy the folder under a new id
(and change `"id"` in its `mod.json`) to change it. Installing EMF again replaces `kill_counter` itself. It is installed
switched off: switch it on in the Mods tab to see it count.

## What happens when a script fails

A script error or an endless loop does not crash or freeze the game. EMF runs every piece of Lua protected:

- An error stops that call, the log gets `[hello_lua] hello_lua/main.lua:12: attempt to ...` and a note shows on
  screen. The mod is **paused**: its callbacks stop until you save one of its files again, then it reloads.
- A call that runs longer than 250 ms (an endless loop) is stopped the same way, with the line it was running;
  `pcall` cannot catch that stop. The time `emf.save_world_data` spends writing (a slow disk) does not count, up to
  1 s per call.
- A Lua mod may use up to 256 MB of memory. Past that the call fails with "not enough memory" and the mod is paused
  the same way.
- `require` loads `.lua` files from the mod's own folder. Only Lua source runs: precompiled chunks are refused.
- Gone: `os.exit`, `os.execute`, `io.popen`, native modules (`package.loadlib`) and the `debug` library (except
  `debug.traceback`).

This protects the game from mistakes, not your PC from a malicious mod: a Lua mod can read, write and delete files
like any program. Install mods only from people you trust.

`lua.status` in the console lists every Lua mod and whether it runs or why it is paused; `lua.reload` checks the files
right away.

## The `emf` table

| Call | What it does |
|---|---|
| `emf.id` | your mod's id |
| `emf.dir` | your mod's folder (for your own data files) |
| `emf.log(...)` / `print(...)` | a line in `emf\emf_core.log` (and the console), prefixed `[id]` |
| `emf.notify(text)` | a note at the top of the screen for a few seconds |
| `emf.hud(id, text [, place [, rgba]])` | a line kept on screen; `""` removes it; place `"top_right"` (below the quest tracker), `"bottom_right"` or `"bottom_left"` (or 0, 1, 2), so a `select` setting's value fits; `rgba` a `color` setting's `"#rrggbb"`, `"#rrggbbaa"` (with alpha: a line can fade), a number like `0xFFD070FF` or the text `"0xFFD070FF"`; 0 (or none) means the usual colour; the player can move, scale and hide each place in the HUD editor |
| `emf.on_tick(fn)` | `fn(world)` after every world step (server, client). In a game you host, a client step leaves your mod out while it is still busy in the server step (at most 30 steps in a row), so the game's frame seldom waits for your server work (it still does when your mod has a client bus callback with messages due, a client `on_event`, or after 30 steps); keep heavy work in the server world |
| `emf.bus.subscribe(topic, fn [, world])` | (1.17) listen for a topic in `"server"` (default) or `"client"` world; `fn(world, source_mod_id, payload)` runs on that world's next step when a publication arrives; true when registered |
| `emf.bus.publish(topic, payload [, world])` | (1.17) queue a copy of a text/binary payload for `"server"` (default) or `"client"`; true when queued, false if the topic, payload or queue limit is invalid (logged once per rejection streak) |
| `emf.bus.unsubscribe(topic)` | (1.17) remove all this mod's callbacks for a topic in both worlds; true if any were removed, false if none existed |
| `emf.json.encode(value)` | (1.18) a Lua value as JSON text; nil plus a message when it cannot be written (see "JSON (1.18)") |
| `emf.json.decode(text)` | (1.18) JSON text as a Lua value; nil plus a message when the text is not valid JSON |
| `emf.json.null` | (1.18) the value `decode` gives for JSON `null` and `encode` writes as `null` (a Lua table cannot hold nil) |
| `emf.on_event(name, fn [, "server"/"client"])` | `fn(world, event)` for each engine event `name`, with its fields as a table (see below); call it at the top of `main.lua` |
| `emf.command(name, help, fn [, {players = true}])` | a console command `<id>.<name>` (1.6: a `name` that is exactly your mod's id is the command `<id>` itself); `fn(world, args)` (`args` is a table of words); answer with `emf.reply(text)`. The full name is 1-32 letters, digits, `_ . -` (with `<id>.`) and ignores case; another name is not added: false and a log line, the rest of the mod runs. `{players = true}` (1.4): every player may run it from the game's chat, not only the `[chat] admins` ([server.md](server.md)); use it for commands that read or act on their sender (`emf.sender()`). A reload can add or take off the mark. Write `help` as `<args>: what it does` (`"<name>: call a beast"`, `": your life skills"`): the console shows it as players type and offers `a\|b` choices and item names for an `<item>` argument |
| `emf.local_player()` | your player's entity id |
| `emf.sender()` | inside a command: who typed it, `{chat, entity, account, admin}` (`chat` true: another player in the game's chat, `entity` 0 if unknown, `account` a text "0x" + 16 hex digits, `admin` (1.4) true when they may run every command: the console, the host, `[chat] admins`); the host's own chat and the console give `chat` false; nil outside commands |
| `emf.session()` | a number that changes when a world loads (entity ids from an older session mean nothing) |
| `emf.on_world_data(fn)` / `emf.save_world_data(text)` / `emf.player_data(a)` / `emf.save_player_data(a, text)` | your data for the world being played: `fn(world, text)` once after the world loaded / save text for it; (1.20) a text per player (see "Saving data") |
| `emf.setting(key)` | one of your settings from `mod.json`: a `bool` as true/false, `int` and whole `slider` as an integer, `float` and decimal `slider` as a number, `choice` as its index, `select` as the option's value (text), `color` as `"#rrggbb"`, `keybind` as the key's name (`""`: none), `text` as text; players change them in the Mods tab and EMF.exe |
| `emf.set_setting(key, value)` / `emf.on_setting_changed(fn)` | change one / `fn(key)` after each change |
| `emf.item(name)` / `emf.item_info(id)` | an item's id by its debug name (`items wood` in the console lists names) / `{id, name, max_stack, guid}`; inside callbacks only (they need a world), so look ids up in your first `on_tick` or command |
| `emf.item_name(item [, "game"])` / `emf.language()` | (1.21) the name the game shows for an item (an id or a debug name), in the player's language: `name, true`; `debug_name, false` where the game has none (the dedicated server, before its text is known, an older core); `nil, false` for an unknown item. For EMF's screens (windows, wheels, HUD lines) a name EMF's fonts cannot draw comes in English (Japanese, Korean, Chinese and Thai until EMF found a font for them, README_EMF.md); `"game"` gives the player's language always (for the chat). Inside callbacks only / `"en_us"`, `"de_de"`, ... or nil (reference.md "v29") |
| `emf.give(world, player, item, count)` | drops items at a player; `item` is an id or a name |
| `emf.put(world, entity, item, count [, fn] [, {level = n, rarity = r}])` | puts items straight into an inventory; `fn(world, added)` one step later. The table (1.4, before or after `fn`): an item level for gear and a rarity, 0-4 or `"common"`, `"uncommon"`, `"rare"`, `"epic"`, `"legendary"` (without them: the game's defaults) |
| `emf.take(world, entity, item, count [, fn] [, {slot = id, exact = true}])` | takes items out (all or nothing); `fn(world, taken)`. The table (1.6): `slot` an InventorySlotId (the inventory entity in the low 32 bits, the slot index 0-7 above: `inv | (i << 32)`) to take from first; with `exact = true` only from that slot (never another inventory, such as worn equipment). Taken items your callback does not hand on (it raises an error first, or the script paused or reloaded before the answer) go back into `entity` |
| `emf.move(world, from, to, item, count [, opts] [, fn])` | (1.6) moves items without losing any: take, put, give back, drop; `opts`: `slot`, `exact`, `fallback`, `level`, `rarity`; `fn(world, moved, returned, dropped, taken)`. true when started (see "Moving items" below) |
| `emf.spawn(world, guid, x, y, z [, components])` / `emf.destroy(world, id)` | spawns a template at a position in metres / removes an entity (host only). 1.12: `components` = `{Name = {field = value}, ...}` are spawned with the entity (at most 15, by their real field names, absent fields 0; not `CurrentTransform`): `emf.spawn(w, guid, x, y, z, {ScaleComponent = {uniformScale = 1.25}})` makes a creature drawn 1.25x as big (a `ScaleComponent` from 0.1 to 10). An unknown name or field is an error |
| `emf.kill(world, id)` | (1.4) kills a creature without a hit: its current health becomes 0 (1.5: through the game's attribute code) and the game's death program sends `EntityDiedEvent` the next step; the corpse keeps its loot. No attacker, no kill credit (`emf.damage` gives one); never a player; host only. true / false |
| `emf.health(world, id)` | (1.5) the current and maximum health: `local cur, max = emf.health(w, id)`; the game's attributes in a world you host, else the replicated `NetworkHealth`; nil when neither is there |
| `emf.attribute(world, id, attr)` | (1.5) an attribute as the game reads it: `attr` is `"health"`, `"max_health"`, `"stamina"`, `"max_stamina"`, `"mana"`, `"max_mana"`, a player's `"xp_pending"`, `"xp"`, `"xp_required"` and `"level"` (1.11, EMF API v22), or an attribute id; nil when the entity has none. Give XP with `emf.attribute_add(w, player, "xp_pending", n)`: the game levels the player up itself (the other three are read-only). Host only |
| `emf.attribute_add(world, id, attr, delta)` | (1.5) adds `delta` (a whole number) through the game's own attribute code: damage stops at 0, a heal of `"health"`, `"stamina"` or `"mana"` at its maximum (other heals are refused). Not a hit: no kill credit, no hit reaction. Host only. true / false |
| `emf.damage(world, attacker, target, amount)` | (1.5) `amount` of true damage as a real game hit on the next step: a `HitEvent` (damage numbers, always a light reaction, aggro toward the attacker), the killing blow, kill credit and XP for `attacker` when it is a player (0 = no attacker, no aggro). The target must take hits (creatures and players, and since 1.23 placed objects: see "Hits" below) and not be immune. Host only. true / false |
| `emf.xp_required(world, level)` | (1.11) the XP `level` needs before the next one (the game's own curve); nil outside 1..cap or when unknown. Host only |
| `emf.level_cap(world)` | (1.11) the highest level a player can reach; nil when unknown. Host only |
| `emf.set_level(world, player, level [, xp])` | (1.11) sets a player's level (1..cap) with `xp` (default 0) towards the next one: raises or lowers at once, without level-up events (to level up the game's way, give XP with `emf.attribute_add(w, player, "xp_pending", n)`). The skill points follow the level; after lowering, a skill tree that costs more points than the level gives is reset by the game at the next load. Saved with the character. Host only. true (done or queued) / false |
| `emf.add_component(world, id, name [, data] [, opts] [, fn])` | (1.12) adds component `name` to entity `id` through the game's own command, with `data` = `{field = value}` by the real field names (absent fields are 0; pass `nil` for `data` to give `opts` or `fn` without it). In this version only `ScaleComponent` (`uniformScale` from 0.1 to 10) and `InteractionPause`; the components the world save keeps are refused until checked in game ([reference.md](reference.md), "v23"). A `ScaleComponent` added this way is not drawn (the game reads the size when it makes the creature): spawn with it instead (`emf.spawn`'s `components`). Only when absent: one already there is never overwritten. Never a player. Applied two server steps later; `fn(world, status, why)` then runs once: `status` is `"done"`, `"already"` or `"failed"` (not called when the world ends first). An unknown name or field is an error. Host only. true (queued) / false (refused: see the log; `fn` is not called) |
| `emf.remove_component(world, id, name [, opts] [, fn])` | (1.12) removes component `name` from entity `id`: one this mod added in this session (`ScaleComponent`, `InteractionPause`). `fn` as above (`"already"`: it was not there). Host only. true / false |
| `emf.tell(world, id, text [, opts])` | (1.6) a chat line only player `id` sees, under their own name (in a game you host, a note for the host itself); (1.22) `opts` `{system = true}`: shown with no name, like the game's "joined" lines, also with text chat off (a label only as a prefix: `"[Raid] ..."`; an older core shows it under the player's name); `"\n"` splits lines, at most 10. Where players' chat is on: a dedicated server, or `[chat] players=1` in a game you host ([server.md](server.md)). A Lua mod may send 30 lines in 10 s. true when queued, false when refused (the log says why) |
| `emf.broadcast(text [, opts])` | (1.6) the same line for every connected player (a note for the host of a game you host); (1.22) `opts` as `emf.tell`. true / false |
| `emf.on_chat(fn)` | (1.6) `fn(world, account, entity, text)` for every chat line a player sends that is not an EMF command, on the server world after its next step; `account` as text ("0x" + 16 hex digits), `entity` 0 when unknown. Read-only: the line is shown as usual. false where players' chat is off |
| `emf.request_save()` | (1.6) asks the game to save the world now (the save its 30 s timer makes). Host or dedicated server. true when asked |
| `emf.teleport(world, id, x, y, z [, opts] [, fn])` | (1.7) moves an entity as fast travel does (see "Teleport" below) |
| `emf.world_settings.get(key)`, `.set(key, value [, persist])`, `.reset([key] [, persist])`, `.list()` | (1.10) the running world's settings: day length, loot, XP, enemy damage... (see "World settings" below). Host or dedicated server |
| `emf.on_world_setting_changed(fn)` | (1.10) `fn(key, value)` once per world setting the server wrote, whoever changed it, on the server world after that step. false on an older core |
| `emf.time_of_day([world])` | (1.4) the time of day in hours (`19.5` = 19:30) from the world's clock, or nil when it is not known. Inside callbacks only (it needs a world: the callback's, or the one you pass); at the top of `main.lua` it is an error, as `emf.item` is |
| `emf.session_token()` | (1.4) a text that names this run of the game and its hosted session. Save it with entity ids: `emf.session()` restarts at 1 every time the game starts and the ids are renumbered, so only a matching token means the ids still name the same entities |
| `emf.on_key(key, fn [, "server"/"client"])` | `fn(world)` once per press of `key`: one of your `keybind` settings (the player's choice counts, also when changed while the game runs) or a key name (`"F7"`, `"G"`). It runs after the next step of the server world when your game hosts one, else of your client world (or of the kind given). Keys only count while the game window is in front and the console is closed |
| `emf.key_down(key)` | true while the key (or the key in a `keybind` setting) is held |
| `emf.wheel(title, labels, key, fn)` | the radial wheel; `key` is held to choose (`"G"`, `""` for click); `fn(world, choice)`, choice 1..n or nil |
| `emf.wheel_section(group, title, labels, fn)` | (1.9) your part of a wheel shared by several mods: publishes (replaces) your section of `group`; an empty list or nil removes it. `fn(world, index, label)` gets the index into the labels as they were when the wheel opened, and the label shown: if you published another list since, find the item by `label`. A pick from a wheel opened before your mod reloaded is dropped. Publish again when your labels change |
| `emf.wheel_group(group, title, key)` | (1.9) opens one wheel with every mod's section of `group` (each label as "Section: label" when more than one mod has a section); a pick runs only its owner's `fn`. True when it opened (false: no labels, or more than 64) |
| `emf.on_hit(fn [, {kinds = ...}])` | (1.23) `fn(world, hit)` after the server applied each landing hit on an entity (the game's `HitEvent`), on the server world; with `kinds` also fall, shroud, cold and drowning damage; see "Hits" below. Joined players' games see none. true / false (false on an older core) |
| `emf.hit_rule{from, to, crit, backstab, weakspot, types, attack, healing, kinds, scale, add, min_health, cancel}` | (1.23) a rule the core applies to every matching hit before it lands: change its amount or cancel it. Returns its id, or nil when refused (the log says why: 64 rules, the hook not usable on this game build). See "Hits" below |
| `emf.hit_rule_remove(id)` | (1.23) removes a rule of this script. true only for the script's own rule |
| `emf.on_player_joined(fn)`, `emf.on_player_left(fn)`, `emf.on_player_died(fn)` | (1.20) `fn(world, p)` on the server world, `p` = `{player, account, name, host, killer}` (`killer`: died only, the last hitter within 10 s or 0). All join 1 s after the world data; died needs the call in the first run. False on an older core (reference.md "v28") |
| `emf.net.channel(name, def)` | (1.19) a message channel `"<mod id>/<name>"`: `def` = `{version, to = "server" \| "players" \| "both", max, per_10s, on_server = fn(world, from, data), on_player = fn(world, from, data)}`; `from` = `{player, account, admin, is_local, version}`. A reload binds the new functions; a new version needs a restart. In process, and to joined players and the server over the chat (reference.md "v27") |
| `emf.net.send(name, data)`, `.send_to(world, player, name, data)`, `.send_all(world, name, data)`, `.peer(world, player, name)` | (1.19) to the server (true: sent) / to one player's game / to all with the channel (count) / that game's channel version (`peer(nil, 0, name)`: the server's; 0 = none) |
| `emf.offered(world, id)` | (1.15) true while your script's offer on `id` is in EMF's hands (not removed, not dropped by EMF); after a drop, `emf.offer` again takes it back |
| `emf.image(path)` | (1.15) a picture for windows: a .png or a folder of .png frames inside your mod's folder (`"pics/wolf.png"`, `"pics/wolf_turn"`), or `"portrait:<key>"` (EMF's portrait cache); the handle for a row's `image` or `icon`, or 0 (refused: logged; a core before v26), which a row takes as no picture and keeps its place. A path that leaves your mod's folder is an error. reference.md "v26" |
| `emf.window(id, def, fn)` | (1.13) opens a window EMF's overlay draws (one at a time; it takes the mouse and keyboard; Esc, its `close_key` or its X close it); true when it opened. `id` gets your mod's id in front (`"stable"` is `"beastmaster.stable"`). `def`: `title`, `width` (in text heights, about 28 by default), `place` (`"centre"`, `"left"`, `"right"`), `close_key` (a key name, for example `emf.setting("stable_key")`), `world` (`"server"` when you host, else `"client"`: where `fn` runs), `rows` = a list, top to bottom, each one of `{text = ...}`, `{heading = ...}`, `{button = key, text = ...}`, `{row = key, text = "Name (detail)"}`, `{slot = key, text = ..., count = n}`, `{bar = text, value = 0..1}`, `{separator = true}`, `{image = emf.image(...), height = 6, fps = 12, key = ...}` (1.15), with `icon = emf.image(...)` and `fps` on slots and rows (1.15), and any of `same_line`, `selected`, `disabled`, `dim`, `empty`, `locked` (true), `tooltip` and `color` (`"#rrggbb"`). `fn(world, ev)`: `ev.kind` is `"click"`, `"right_click"` or, last, `"closed"` (with `ev.reason` `"player"`, `"mod"` or `"core"`); `ev.key` is the clicked row's key. A reload closes the script's window. An unknown row kind or field is an error. false (logged) while another window or a wheel is open, with no world of its kind, without the overlay (a dedicated server). `close_key = ""` (a key setting left unset) is no close key |
| `emf.window_update(id, def)` | (1.13) new contents for your open window (`fn` stays); false when it is not open (the player closed it): it never reopens one |
| `emf.window_close(id)` | (1.13) closes your window (`fn` gets `"closed"`, reason `"mod"`); false when it is not open |
| `emf.offer(world, id [, opts] [, fn])` | (1.14) the game's own "E <verb>" prompt on entity `id` of the server world you host; `fn(world, player, entity)` runs when any player presses E there. `opts`: `verb` (`"Use"` (default), `"Open"`, `"Talk"`, `"Read"`, `"Sit"`, `"Sleep"`, `"Pet"`, `"Touch"`, `"Activate"`, `"Press"`, `"Enter"`, `"Inspect"`, `"Take"`, `"Collect"`, `"Rest"`, `"Release"` or a loca id), `text` (your own words, shown once a content mod adds them with `add_prompt_words`, content.md; else "E" with no word), `sequence` (`"touch"`, `"button"`), `range`, `low_priority`, `build_mode`. false for a player or an entity with the game's or another mod's offer. Offer again after a load; a reload takes the script's offers off. Host only (reference.md "v25") |
| `emf.spawn_offer(world, guid, x, y, z [, opts] [, fn])` | (1.14) spawns a template with an offer on it (`emf.offer`'s options); the entity id, or nil. The prop may stay in the save without its offer: after a load offer it again, don't spawn another |
| `emf.offer_remove(world, id)` | (1.14) takes your script's offer off the entity; false when it has none of yours |
| `emf.emits(names [, kind])` / `emf.emit(kind, name, fields)` | send engine events (see below) |

### Messages between Lua mods (1.17)

The `emf.bus` table lets separate Lua mods exchange **copies** of messages without sharing their Lua state.
This works on dedicated servers as well as in a hosted game. It does not provide network transport between the
server and clients: `"server"` and `"client"` choose which local world step delivers a message. The default is
`"server"`. A bus-using mod needs `"depends": {"lua": ">=1.17"}` in `mod.json`.

```lua
-- In a second Lua mod (the receiver):
emf.bus.subscribe("example.event", function(world, sender, payload)
  emf.log("received from", sender, payload)
end) -- defaults to the server world

-- In the sender mod:
local ok = emf.bus.publish("example.event", "example-payload")
if not ok then emf.log("message was refused") end
```

`topic` is 1-96 **bytes** (ASCII control characters, including NUL, are refused); `payload` is a Lua string of up to **65,536
bytes** and may contain `\0`. A topic may have several callbacks in one mod. Calling `unsubscribe(topic)` removes
all of them, including server and client callbacks. The callback's `sender` is the source mod's id, not a player id.

Each world delivers accepted messages in **publish order**, on its **next world step**. A publication made *inside*
a bus callback is held for the *following* step, never delivered recursively in the same step. A mod **does receive
its own messages** if subscribed to that topic. During the initial load, a message published by one mod's `main.lua`
can reach another mod even if that receiver's `main.lua` is loaded later, before the first world step. Subscriptions
added at runtime do not consume older publications.

Each publisher may have at most **256** pending messages; all publishers together may have at most **4,096**.
A rejected publication returns `false` and EMF logs the reason once until that mod next publishes successfully.
Messages are held in memory, not saved to disk. On reload, stop or Lua error, a mod's subscriptions
and its pending outgoing messages are cleared. New subscriptions cannot receive messages queued for the previous
load; messages still intended for other subscribed mods are preserved. Handle a refusal by retrying later or
coalescing updates rather than publishing in a tight loop.

### Services on the bus

One mod can offer a *service* that other mods call with request and reply messages (a world database, navigation, ...).
[services.md](services.md) is the short guide: topic names, the JSON envelope, request ids, timeouts, announcing a
service, and the examples `bus_service` and `bus_client`.

### JSON (1.18)

Bus payloads are strings, so a mod that sends a table turns it into text first. `emf.json` does that the same way
for every mod. It needs `"depends": {"lua": ">=1.18"}`.

```lua
local text = emf.json.encode({op = "get", args = {key = "apple"}, tags = {"a", "b"}})
-- {"args":{"key":"apple"},"op":"get","tags":["a","b"]}
local value, err = emf.json.decode(text)
if not value then emf.log("bad json:", err) end
```

Bad input never raises a Lua error: `encode` and `decode` give `nil` and a message, so check the result (a call with
a missing or wrong-typed argument is still a usage error, as for other `emf` functions).

- **Tables**: keys exactly `1..n` (n at least 1) make a JSON array. Any other table is an object and all its keys must
  be strings; a table with other keys (`{1, 2, x = 3}`, a sparse list, a number or table as key) is an error that names
  the problem. An empty table is `{}`, so an empty list decodes and encodes as an object: test `next(t) == nil`.
  Object keys are written in sorted order, so the same table always gives the same text.
- **Numbers**: a Lua integer is written as an integer. A float is written in the shortest form that reads back as the
  same number, and a whole float keeps a `.0` (`3.0`), so it stays a float. NaN and infinity are errors. When reading,
  a number without a fraction or exponent that fits 64 bits is an integer, any other number a float.
- **Null**: `emf.json.null` stands for JSON `null` (in either direction). Compare with `==`. A nil value in a table
  does not exist in Lua, so use `emf.json.null` when "no value" must be sent.
- **Strings** are UTF-8 and are checked: a string with invalid UTF-8 is an error in both directions. Quotes,
  backslashes and control characters are escaped; `\uXXXX` escapes, including surrogate pairs, are decoded; a lone
  surrogate is an error. Other text (`/`, accents, emoji) is written as it is.
- **Also**: `emf.json.null` is truthy (`if value then` is true for null: compare with `== emf.json.null`). Metatables
  are ignored (`__index`, `__pairs` have no effect; only the table's own entries are written). A repeated key in
  the text keeps its last value. A UTF-8 byte order mark at the start of the text is an error. `-0` decodes as the
  integer `0`. `decode` accepts a number as its argument (Lua converts it to text: `decode(5)` is `5`); other
  non-text arguments are a usage error.
- **Booleans** as usual. Functions, threads, userdata (except `emf.json.null`) and `nil` are errors.
- **Limits**: 64 levels of nesting (this also stops a table that contains itself), 1 MiB of input text, 1 MiB of
  output text. The bus carries at most 64 KiB, so encode first and check that `emf.bus.publish` returned true.

### Moving items

`emf.move(world, from, to, item, count [, opts] [, fn])` (1.6) moves items without losing any. It takes `count` from
`from` (all or nothing), puts them into `to`, gives back into `from` what does not fit, and drops what still does not fit
at `from`, else at `to`, else at `opts.fallback`, else at the nearest player (`LOST` in the log only when none of them
can take it).

- `opts`: `slot`, `exact` (as for `emf.take`), `fallback`, `level`, `rarity` (as for `emf.put`).
- `fn(world, moved, returned, dropped, taken)` runs at the end.
- It finishes in the Lua host even when your script errors or reloads meanwhile (only `fn` is skipped then), and does
  nothing with an answer from another session.
- At most 24 moves wait at once (false: try again later). It returns true when started.

### Teleport

`emf.teleport(world, id, x, y, z [, opts] [, fn])` (1.7, EMF API v20) moves a player, villager, animal or creature the
way the game's fast travel does. It returns true when queued, false when refused (the log says why).

- `opts`: `ground = true` (the game's ground search: the nearest walkable spot within about 10 m; when there is none
  the entity stands exactly at x, y, z), `yaw = degrees`, `spawn_point = id` (an entity with `PlayerSpawnPoint`: land
  on its pads as a respawn does; x, y, z may then be nil), `checkpoint = true`, `ready = true`.
- `fn(moved, x, y, z, info, world)` runs after the step that moved it, or when it failed. `x, y, z` is where the entity
  is now. `info` is `{status = "moved"/"failed"/"ready", distance, off_target, raw_target, reason, entity}`.
- With `ready = true`, `fn` runs a second time (`info.status == "ready"`) once a player can play again.
- When the session ends first (a reload of the world, back to the menu) or the script was reloaded meanwhile, `fn` is
  not called.

The rules (what is refused, waits, NPCs) are those of `teleport` in [reference.md](reference.md) (v20).

### World settings

`emf.world_settings` (1.10, EMF API v21) reads and changes the running world's settings, the values of
`enshrouded_server.json`'s `"gameSettings"`. Only where a world is hosted (the host or a dedicated server).

- `get(key)`: the value now, or nil. A key is the setting's name as `enshrouded_server.json` has it
  (`"playerStaminaFactor"`) or its short name (`"loot"`, `"day_minutes"`); durations are in minutes, choices by
  number, on/off as 1/0.
- `set(key, value [, persist])`: true when queued for the next server step (every player gets it); `false, why` when
  refused (`"loot: allowed 0.25 to 2"`, `"unknown setting: x"`, `"no world is hosted here"`). `persist = true`
  keeps it for this world (applied again after each load). A boolean value counts as 1 or 0.
- `reset([key] [, persist])`: back to the world's own value; no key: every setting. `persist` also forgets the kept
  value (`reset(true)` for all).
- `list()`: `{{key, alias, group, type, min, max, value, world, changed, needs_client}, ...}`, empty without a hosted
  world. `type` is `"float"`, `"bool"`, `"enum"`, `"minutes"` or `"whole"`; `world` is the world's own value.

The settings and their ranges are in [world_settings.md](world_settings.md). A mod that changes them should say so
in its README: players expect the world to keep the settings they chose.

## Hits (1.23)

`emf.on_hit(fn)` sees each hit the server world applies to an entity, after it landed. `emf.hit_rule{...}` changes
hits before they land, with no script call in between (a script never runs before a hit lands: the script lock is shared
by the world threads, and a call per hit would slow fights). Both work on the server world only (a game you host or the
dedicated server; see [server.md](server.md)).

`fn(world, hit)` gets a table:

| Field | What |
|---|---|
| `attacker`, `source` | the root source (the shooter of an arrow, the caster of a spell; 0 = none) and the direct source (a projectile, a trap) |
| `target`, `part` | the actor hit and the entity hit (the same for most hits) |
| `amount` | health damage; negative for healing. A blocked or parried hit has 0 |
| `stamina` | stamina damage of a blocked hit |
| `crit`, `blocked`, `parried`, `backstab`, `weakspot`, `healing`, `killed` | booleans (`killed`: the killing blow) |
| `types` | list of damage type names: `"cut"`, `"pierce"`, `"blunt"`, `"fire"`, `"ice"`, `"shroud"`, `"lightning"`, `"poison"`, `"wood"`, `"stone"`, `"metal"`, `"explosion"` |
| `attack`, `attacks` | the attack's kind: `"melee"`, `"ranged"`, `"magic"`, `"tool"`, `"other"`, `"aoe"` or `"skillshot"` (nil for a `emf.damage` hit); `attacks` lists every kind the hit has (most have one) |
| `weapon_item`, `weapon_category`, `program`, `impact`, `reaction` | the weapon's item id (0 = none or not known), the game's weapon category hash, the attack's impact program, the swing or projectile (hits of one share it), the hit reaction type |
| `health_before`, `health_after` | the target's health around the hit; nil when not known |
| `kind` | `"hit"` (an entity hit), `"fall"`, `"shroud"`, `"cold"` or `"drowning"`: see "Fall, shroud, cold and drowning" below |
| `instant` | true when the game set health to 0 at once: deadly shroud, or a creature (not a player) out of air |
| `from_mod` | true for a hit made by `emf.damage` |
| `position` | `{x = , y = , z = }` in metres |

A rule takes: `from` and `to` (an entity id, or `"players"`, `"creatures"`, `"objects"`; absent = anyone; an entity id
as `to` matches the actor hit and the part hit), `crit`, `backstab`, `weakspot` (true = only such hits), `types` (a
list of type names, any of them), `attack` (a name or a list of names), `healing` (true = healing hits only; without it
a rule matches damage hits only, so "no PvP" does not stop a friend's heal), then what it does: `cancel` (the hit is
dropped: no damage, number, reaction, aggro, XP credit or destruction), `scale` (amount times scale; a blocked hit's
stamina too), `add` (added after the scale), `min_health` (the target keeps at least that much health: 1 = it cannot
die from hits). A damage hit never becomes a heal and a heal never becomes damage. Rules apply in id order; cancel
wins. An unknown field or a bad name is an error. A script's rules are removed when it reloads, unloads or pauses.

### Fall, shroud, cold and drowning

`kinds` lets a rule or a callback take part in the damage the game does not make as hits. A kind name is `"hit"`,
`"fall"`, `"shroud"`, `"cold"` or `"drowning"`; `kinds` is one name, a list of names, or `"all"`. An unknown name is an
error.

- `emf.hit_rule{kinds = "fall", to = "players", cancel = true}`. Without `kinds` a rule is for entity hits only, so every
  rule keeps its meaning.
- `emf.on_hit(fn, {kinds = {"fall", "drowning"}})`. Without the option `fn` gets entity hits only: a damage meter does
  not get 60 shroud ticks a second unasked.
- `hit.kind` tells them apart. For these kinds `attacker` and `source` are 0, `target` = `part` = the entity, there are no
  `types` or `attack`, and `health_before` and `health_after` are known. `from` (a player or creature) never matches
  them, nor `crit`, `types` or `attack`.
- Fall is once per landing. Shroud, cold and drowning come every server step (about 60 a second) while the shroud timer is
  out, body heat is 0 while freezing, or oxygen is 0. `scale` is exact there (0.5 halves 1-point ticks: EMF carries the
  fraction). `add` is per event, so about 60 times a second on those three.
- `min_health` works on every kind. A cancelled hit never reaches `on_hit`. A cancelled fall gives the health back, but the
  game's hard-landing stagger still plays.

What never becomes a hit (no hook sees it): regeneration, direct attribute writes, voxel damage (terrain and walls). A
blocked or parried hit changes stamina, not health.

`emf.damage` also hits placed objects (an entity with `Destructible` and no `CombatFlags`, or with `handleHits` on): the
game gives it health on the first hit and removes it at 0. A placed object's health is not saved: a reload heals it.

Recipes: [cookbook.md](cookbook.md), "Hits".

## Worlds and components

Callbacks get a `world` (valid only during that callback):

| Method | |
|---|---|
| `world:is_server()`, `world:is_client()` | which world: gameplay changes go to the server world (the host) |
| `world:tick()` | its tick counter |
| `world:entities()` | every entity id (a table) |
| `world:position(id)` | `x, y, z` in metres, or nil |
| `world:get(component, id)` | the entity's component, or nil |
| `world:template(component, id)` | (1.8) the entity's template half of a component: the data every entity of its template shares (for example `world:template("ModelResource", id).model`, the model GUID as text). Read-only: writing a field is an error. nil when the entity has none |
| `world:name(id)` | (1.6) a player's name, the one the game's chat shows (their platform name, not the character's), or nil (then say `"player #" .. id`); where players' chat is on, server world only |
| `world:character_name(id)` | (1.22) the player's character name (the Social tab's), or nil when not known: it may already be there in `on_player_joined` or come a few seconds later, and it can change during a session, so read it when you need it and never keep it. Text a player's game set, not an identity: key data by `p.account`. Server world only; players' chat need not be on |
| `world:account(id)` | a player entity's account as text ("0x" + 16 hex digits, as `emf.sender()` gives it), or nil: a key that stays the same for that player across sessions (entity ids do not); server world only |

`world:get` returns the component's data with its real field names (`sdk/include/emf/generated/components.hpp` lists
every component and its fields: search for `struct CurrentTransform`; `inspect <entity>` in the console shows what an
entity has):

```lua
local t = world:get("CurrentTransform", id)
emf.log(t.transform.position.x)          -- nested structs
local f = world:get("Faction", id)
f.faction = 11                           -- assign to change it (checked: 300 into a byte is an error)
local inv = world:get("Inventory", id)
emf.log(#inv.slots, inv.slots[1])        -- arrays: # and [1..n]
```

Ids, times and hashes (`EntityId`, `Time`, `HashKey32`) read and write as plain numbers. Reading a field the type
does not have is an error naming the type and the field (a typo would otherwise read nil); event tables check the same
way (`rawget(e, name)` reads one without the check). An unsigned 64-bit field at or above 2^63 reads as a float (so it
stays positive; floats hold such numbers only roughly, so a value within about 1024 of 2^64, like an all-ones
marker, reads as 2^64 and cannot be written back); NaN and infinity cannot be written into a float field.

A component can only be used during the callback that got it: keep ids, not components, between callbacks. Writing
changes the game's own data; what the game makes of it (and what it sends to other players) is the same as for a C++
mod: test in a spare world.

## Engine events

`emf.on_event` gives you each event as a table of its fields:

```lua
emf.on_event("EntityDiedEvent", function(world, e)
  emf.log("died:", e.deadEntityId)
end)
```

Common events: `HitEvent` (`rootSourceId` hit `targetId`), `EntityDiedEvent`, `PickupEvent`, `CraftRecipeEvent`,
`LevelUpEvent`, `ConsumeItemEvent` (`components.hpp` has all 129 with their fields, at its end the list `kEvents`); the console's `event_log` mod (C++) logs many of them, and `docs/engine_parts/re_events.md` explains
them. Call `emf.on_event` at the top of `main.lua`: an event that was not asked for when the game started needs a
restart (a reload tells you so; the rest of the mod runs). Event and component names are the short type names
(`HitEvent`); the game's full names (`keen::ecs::HitEvent`) work too. An event whose size in the game is not the size in
`components.hpp` (after a game update) is not delivered; the log says so once.

To make something happen, send the game's own events: declare them at the top of `main.lua`, then send them from any
callback. The host sends them on the next step of that world:

```lua
emf.emits({"NpcCommandEvent"})
emf.command("come", "<villager entity id>: that villager walks to you", function(world, args)
  local x, y, z = world:position(emf.local_player())
  emf.emit("server", "NpcCommandEvent", {
    type = 1, npcId = tonumber(args[1]),  -- 1 = GoToPosition (NpcCommandType in components.hpp)
    targetTransform = { position = { x = math.floor(x * 2^32), y = math.floor(y * 2^32), z = math.floor(z * 2^32) } },
  })
end)
```

(World positions inside the game's structs are fixed point: 1 m = 2^32. `world:position` already converts.)

## Settings

Declare settings in `mod.json` exactly as for C++ mods (see [reference.md](reference.md)): switches, numbers,
sliders with steps, lists, colours, key binds and text, under `group` headings, greyed out with `enabledWhen` while
another setting does not have some value. They show in the console's Mods tab, in EMF.exe and in `settings <id>`. Read
them with `emf.setting(key)`, every time you need them (the template reads them on each tick), and mark the ones you
apply at once `"live": true`; `emf.on_setting_changed` tells you when one changed. A setting added to or changed in
`mod.json` while the game runs shows in the Mods tab within a second (while a world is loaded); until then
`emf.setting` gives false, 0 or "" for a new one, so read settings when you use them rather than once at the top of
`main.lua`.

```lua
-- mod.json: {"key": "toggle_key", "type": "keybind", "default": "F7", "label": "Toggle", "live": true},
--           {"key": "tint", "type": "color", "default": "#ffd070", "live": true}
local on = true
emf.on_key("toggle_key", function(world) on = not on end)
emf.on_tick(function(world)
  if world:is_client() then emf.hud("line", on and "Hello" or "", "bottom_left", emf.setting("tint")) end
end)
```

## Saving data

A Lua mod keeps its variables while the game runs and loses them on reload or restart. Data that belongs to a world
(a score, a list of places) goes into the world's data; data for every world into a setting or a file of your own.

**Per world** (the `kill_counter` example keeps its count this way):

```lua
emf.on_world_data(function(world, text)   -- after the world loaded; "" the first time
  kills = tonumber(text:match("kills=(%d+)")) or 0
end)

-- whenever it changed:
emf.save_world_data("kills=" .. kills)
```

- `fn` runs once per world load, in the game that hosts the world (yours, or the dedicated server), on the server
  world's step, and again after your mod reloads. Registered later (say in a command, after the world loaded), it gets
  the world's current text on the next server step.
- `save_world_data` returns true when saved; false before a world has loaded, in someone else's game (also after you
  hosted a world earlier), and after your mod reloads until `fn` has run again, so nothing is ever written into the
  wrong world's file or over its data. It writes only when the text changed.
- The data is text; choose your own format. It is kept in `emf\config\<your mod id>\<world slot>.txt`, as for C++
  mods (`emf/storage.hpp`).

**Per player in a world** (`lua` 1.20): a text for each player, by their account (`world:account(id)`, or `p.account`
in `emf.on_player_joined`):

```lua
local visits = {}
emf.on_player_joined(function(world, p)
  local text = emf.player_data(p.account)        -- "" the first time
  visits[p.account] = (tonumber(text) or 0) + 1
  emf.save_player_data(p.account, tostring(visits[p.account]))
end)
```

- `emf.player_data(account)` gives the player's text, "" when none, and nil before the world's data came (the same
  moment `on_world_data` runs), where no world of yours runs, and while the world's players file cannot be read (not
  EMF's format: the log says so; another program had it open: it is read again). `emf.save_player_data(account, text)` saves it
  (`""` removes the player) under the same rules as `save_world_data`; it returns false over 16 KiB for one player or
  2 MiB for the world's file.
- An account is `"0x"` and up to 16 hex digits, as `world:account` gives it; anything else is an error. The all-zero
  account (the console's `emf.sender()`, the host's own `emf.net` messages) gives nil and false: use
  `world:account(emf.local_player())` for the host.
- Kept beside the world's data in `emf\config\<your mod id>\<world slot>.players.txt` (C++: `WorldFile::player` and
  `save_player`). Each save writes the whole file, so save when a player's text changed, not every step.

**Your own files:** write them into your own folder:

```lua
local f = io.open(emf.dir .. "\\scores.txt", "w")
f:write(kills, "\n")
f:close()
```

Use `emf.dir`: a path without a folder lands in the game's folder. File names are UTF-8 text (as `emf.dir` is), so
folders and files with letters such as æ, ø or å work in `io.open`, `require`, `loadfile` and `os.remove`. Give data
files another ending than `.lua`: saving a `.lua` file or `mod.json` reloads the mod.

## Lua or C++?

Lua is the quick way: no build, live reload, and a script error cannot crash the game. C++ (see [README.md](README.md)) reaches everything: mod programs,
hooks into engine functions (`hook_pattern`), template data, memory writes, and it runs faster in tight loops. Both can
live side by side, and a Lua mod can depend on a C++ mod in `mod.json`.
