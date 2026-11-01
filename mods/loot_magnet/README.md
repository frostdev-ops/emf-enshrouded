# Loot Magnet (`loot_magnet`)

Kill a creature, and a moment later what it carried is in your inventory: the corpses of creatures killed near you are
looted into you. It runs where the world runs, so on the host or a dedicated server it works for **every player**, also
those who joined without EMF; each player switches their own magnet.

It answers the auto-loot request (3.2K votes on the official feature board) for the case no other mod covers: Auto
Loot Plus and the others work for single player or the host only, and every player has to install them.

## Switch it on

Loot Magnet is installed **switched off**. Switch it on in the Mods tab (the key under Esc) or in EMF.exe, then
restart the game. Once it runs, it is on for every player by default (**On for players who never switched it**), and
each player switches their own:

- `/loot.toggle` in the game's chat (or `loot.toggle` in the console): on / off; `/loot.toggle on`, `/loot.toggle off`;
- `/loot.status`: whether yours is on, and the rules.

These two are commands for every player: they work from the chat on a dedicated server, and in a game you host with
`[chat] players=1` in `emf\emf_core.ini` (see `docs/modding/server.md`). Each player's choice is kept with the world,
by account (`emf\config\loot_magnet\<world slot>.txt`). The host can also bind a key (**Your key to switch it**).

## What comes, and what never does

Most creatures do not keep their loot: a moment after they die they fade away, and the game leaves their loot in a
**loot spot** where they died (the small glowing pile you open with E). The magnet takes that loot spot. A creature that
keeps its loot in its own body is looted from the body.

The loot of a kill comes to the nearest player whose magnet is on when all of these hold:

- the creature died (the game's `EntityDiedEvent`) at least **After the kill** ago, and less than **Looks for loot for**
  ago (15 s by default, at least 8 s: the loot appears 1-3 s after the death, so until then the magnet looks again
  every half second);
- the player is within **Reach** of the loot;
- the loot is in the body (dead, its health 0, still the creature that died: the same kind, within 5 m of where it
  died), or in a loot spot that appeared after the death within 5 m of where it died (a loot spot that was already
  there, from an earlier kill or a chest, is not this kill's);
- it holds something;
- with **What comes** = materials: every stack in it is a material or food, by its debug name: starting with
  `Material_`, `Food_` or `_Food_`, or a terrain block (`Build_TerrainMaterial_`); arrows (`Ammo_`) if **Arrows count
  as materials**. Loot with gear in it is left for you to open.

A kill without a hit counts too (the champion's `champion.kill`, or anything else that kills).

Never:

- a corpse twice;
- a player's body or grave, an ally's (the companion, summons), a farm animal, anything tame or with an owner, a
  villager, or anything they leave when they die;
- anything inside a base: within **Never within** metres of a flame altar (raids at your base are looted by hand);
- chests and props (they are not creatures that die, and a loot spot that was there before the kill is not its loot).

The loot moves with the game's own loot event, as when you press "take all": the stacks go into your inventory and
the game shows them. **A full inventory loses nothing**: the game moves the stacks one slot at a time, each in its own
inventory transaction (`docs/engine_parts/re_villages.md`, `immediate_loot_action`), so what fits comes and the rest
stays where it was; the host gets a note "no room in your inventory" (once per session).

## Settings (Mods tab, EMF.exe, or `set loot_magnet <key> <value>`)

| Setting | Default | What it does |
|---|---|---|
| Reach (`radius`) | 8 m | 2-20 |
| After the kill (`delay`) | 1 s | 0-10: the loot comes no sooner |
| Looks for loot for (`window`) | 15 s | 8-60 s after the kill; an older corpse is left to you |
| What comes (`categories`) | materials | materials (and food) only, or everything |
| Arrows count as materials (`include_ammo`) | on | |
| On for players who never switched it (`default_on`) | on | |
| Your key to switch it (`toggle_key`) | none | the host's own magnet |
| Never within (m) of a flame altar (`base_radius`) | 30 m | 10-100 (a flame altar of a higher level builds a bigger zone: raise it to match) |
| Log every decision (`debug_log`) | on | a log line per death noted and per corpse decided |

## Why a corpse was not looted

With **Log every decision** (`debug_log`, on by default) `emf\emf_core.log` has a line for each death the magnet
noticed and for what became of each corpse:

- `[loot_magnet] death of 6938 noted at (3739.5, 841.0, 1473.5), 46 m from the nearest flame altar`
- `[loot_magnet] corpse 6938 looted into player 1 from the loot it left where it died (loot container 7012, 1.0 m)`
- `[loot_magnet] corpse 6938 looted into player 1 (1.0 m; interaction offer no)` (the loot was in the body)
- `[loot_magnet] corpse 6938 skipped: inside a base` (or: a player's or an ally's body, holds something that is not a
  material, already looted...; `skipped: the loot it left (loot container 7012) holds something that is not a
  material`)
- `[loot_magnet] corpse 6938 left: its time ran out while nobody with a magnet within reach (the nearest player with a
  magnet was 14.2 m away, reach 8 m)`
- `[loot_magnet] corpse 6938 left: no loot appeared within 15 s of its death (none on it, and no loot container within 5 m
  of where it died)`
- `[loot_magnet] corpse 6938: nothing came from the loot event yet (try 1 of 5): sent again`: a corpse may become
  lootable a moment after it died, so a loot event nothing came from is sent again, every 2 s, up to 5 times (nothing
  moved, so nothing is taken twice).

Switch it off once you trust it: then only the looted lines are written.

## Good to know

- **Host or dedicated server only.** In someone else's game it does nothing (that game's host decides).
- **Loose drops** (separate pickups lying on the ground) are not picked up: picking up whatever lies near a kill could
  take a quest item or something a player dropped. The loot spot a kill leaves is not a loose drop: it is taken.
- The log (`emf\emf_core.log`, lines `[loot_magnet]`) names every corpse looted and the player it went to.
