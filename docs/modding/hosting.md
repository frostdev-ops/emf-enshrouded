# Running EMF mods in another host

EMF's C API and Lua API are MIT: another framework may load EMF mods with its own implementation (Kiln does). This page
lists the rules an EMF mod relies on, so a host gives it what it expects, and players and mod authors know where a
report belongs. The details of each function are in [reference.md](reference.md) and [lua.md](lua.md).

## Say what you are

- **Report the version you really implement.** `EmfApi.version` is the newest `EMF_API_VERSION` whose functions you
  all provide, and `EmfApi.size` is `sizeof(EmfApi)` of that version's header. Mods check these before they call a
  newer function (`emf::has_v40()` and the like in `emf.hpp`), so a table that claims more than it has crashes mods.
  A function you cannot do yet still needs its slot: put a function there that returns the documented failure (null,
  0, false) and logs a line, never a null pointer.
- **Mods built with a newer SDK:** `emf::init(api)` refuses a table smaller than the SDK's own, so a mod built with a
  newer header does not start on your host. A mod that checks `has_vNN()` can say so with `emf::init(api, N)` (since
  EMF 0.6.10) and then starts on a v`N` table, with the newer functions null. Slots you provide that only return the
  documented failure are fine too: that is how EMF itself behaves when a feature is off on a game build.
- **Refuse a mod that needs more.** `mod.json`'s `"emf": N` is the lowest API version it needs: with N above yours, do
  not load it, and say why ("needs EMF API vN").
- **Say in your log that you run EMF mods**, under your own name and version, so a player's report reaches you first.
  EMF's own log is `emf\emf_core.log`; a player without it is not running EMF.
- Do not call your host "EMF", and do not ship EMF's DLLs under your name. Keep EMF's MIT notice with the parts you
  copied.

## The table

- **It only grows.** Each version appends functions at the end of `EmfApi`; nothing moves or changes meaning. A host
  that supports version N provides exactly the layout of N's header (`src/core/api_sizes.h` in EMF has the size of
  every version and `static_assert`s for the layout).
- **Structs carry their size.** A struct a mod passes in (`EmfEnemyOrder`, `EmfMoveOrder`, `EmfMarker`, ...) starts
  with `size`: read only that many bytes, treat missing fields as 0, and refuse one whose size is below the first
  version's (EMF logs "its size field is not sizeof(...)").
- **Refusals return, they never throw**: null, 0 or false, and a line in the log. Where the API has a `why` field,
  fill it.

## Loading

- A mod is `emf\mods\<id>\mod.json` plus the DLL it names, or a loose `emf\mods\<name>.dll`.
- The DLL exports `extern "C" bool emf_mod_load(const EmfApi* api)`. Call it once, after the core is ready; `false`
  means the mod did not load, but the DLL stays loaded (what it registered stays registered).
- Order: by `depends`, then by folder name. Skip a mod whose `[<id>] enabled=0` is set in `emf\config\<id>.ini`.
- Lua mods name a script in `main` and depend on `"lua": ">=1.x"`: the Lua host's version. The Lua host is a mod of
  its own (`mods/lua`); its version list is in lua.md, step 2 of "Your first Lua mod".

## Threads and worlds

- **"World callback"**: `on_tick`, `run_on_world`, `on_event`, a command, a put/take result, a wheel choice, on the
  world that callback received. Functions that change the world (spawn, destroy, teleport, orders, moves, doors) are
  only allowed there, and EMF refuses them elsewhere with a log line.
- **Programs**: a mod program's callback runs inside the game's world step, with the context it received; the
  `program_*` and `emit_event` functions are only valid there.
- **"Any thread"** functions (log, settings, cancels) must be safe everywhere.
- A callback a mod gives (a path answer, a move's end, a put result) is called exactly once.

## Messages between players

API v27 messages travel in the game's own chat as `#!emf1` lines (reference.md, "v27: mod messages", "The chat
transport"). A host that speaks the same wire format can exchange messages with EMF players; one that does not must not
send `#!emf1` lines.

## Game updates

EMF's engine signatures are in [emf_signatures.patterns](emf_signatures.patterns) (MIT), one pattern per line with each
known build's address: take what you need, and tell us when you fix one after an update.

## Checking yourself

EMF's own tests (`tests/` in EMF's repository, run with `bash scripts/verify.sh`) cover each function's contract against
fakes; many test cases read like a spec ("move orders: a point counts as reached within the creature's guard distance
+ 1 m"). A host can port the ones for the functions it implements. Questions and differences you find are welcome as
issues in EMF's repository.
