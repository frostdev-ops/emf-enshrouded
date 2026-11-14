# Gate (Lua example)

Three commands for the host: open, close or lock every door within 10 m of you, player doors and gates alike.

## What it teaches

- **Doors** (Lua 1.30, EMF API v38): `emf.door(world, id)` reads a door: `"open"` or `"closed"` and a table
  `{locked, logic}`, or nil when the entity is no door. `emf.door_open(world, id [, "front" | "back"])`,
  `emf.door_close` and `emf.door_lock(world, id, true | false)` change it at the next server step; players see it swing,
  and enemies' paths change with it.
- **Checking before acting**: a door that is already open is counted, not opened again (the core would refuse it). A
  door driven by a switch (`logic`) belongs to the game's logic system: EMF leaves it alone, and so does the mod.
- **Results**: each action returns true when the door will change and false when refused; the reason is in the log,
  so the answer only counts the refused doors and points there.
- **Finding things near you**: there is no "doors near me" call. The mod walks `world:entities()`, keeps those within
  10 m on the ground (`world:position`) and asks `emf.door` about each.
- **Host only**: doors change on the server world. Without `{players = true}` only the console, `emf\commands.txt`,
  the host's own chat and admins may run the commands; `emf.sender()` says whose position counts.

## Install

Copy this folder into `<game>\emf\mods\` (or the dedicated server's `emf\mods\`) and start the game or the server. It
needs EMF's `lua` mod 1.30 or newer, which the EMF installer puts in and switches on. Only the game that runs the
world (the host, or the dedicated server) needs it. On an EMF older than API v38 (no doors yet) `emf.door` gives nil,
so the commands answer `No door within 10 m of you`: update EMF.

## Try it

1. Load a world you host and stand near a door or a gate you built. Open the console (the key under Esc) and type
   `gate.open`: `Opened 1 of 1 door(s) within 10 m`, and the door swings open.
2. `gate.close` closes it. `gate.close` again says `Closed 0 of 1 door(s) within 10 m, 1 already so`. Then
   `gate.open front` opens it the other way.
3. `gate.lock`: press E at the door; there is no prompt. `gate.open` and `gate.close` still work on it.
   `gate.lock off` gives the prompt back. The lock is kept with the world (EMF 0.6.8 or newer): load the world again and it is still there.
4. In the game's chat, `/gate.open` does the same (the host's own chat runs every command). On a dedicated server an
   admin (`[chat] admins` in `emf\emf_core.ini`) types it; the doors near them change.
5. Doors a switch opens and closes are counted as `driven by a switch` and left alone; `gate.lock off` still
   unlocks one that was locked before a switch was wired to it.

Lua commands always start with the mod's id. To type `/g.open`, copy the folder as `g` and change `"id"` in
`mod.json` to `"g"`.
