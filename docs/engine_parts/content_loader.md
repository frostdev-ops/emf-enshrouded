# EML content in the loader: the shadow copy, the build at the held start, the redirect

EMF's `dinput8.dll` can apply the content of EML mods (mods made for EML, the Enshrouded Mod Loader) without a separate
EML install. It builds them with `emf\emf_content.dll` into a copy of the game's data and sends the game's data opens
there (the redirect: [file_redirect_spike.md](file_redirect_spike.md)). The game's own files are never written.

Status: the loader side is in place and tested against stand-ins. It does nothing until `emf_content.dll` is installed
next to `emf_core.dll`; that DLL is built separately (it contains EML, so it is GPL).

Code: `src/loader/` — `content_owner.h` (the decision), `content_base.h` and `kfc_header.h` (the base), `eml_mods.h`
(the mods), `content_fingerprint.h` and `content_state.h` (what was built), `content_shadow.h` (the copy),
`content_build.h` and `content_abi.h` (the DLL call), `content_start.h` (the order), `content_splash.h` (the window),
wired in `dllmain.cpp`. The core's side: `src/core/content_status.h`. Tests: `tests/test_content_*.cpp`, with a stand-in
DLL (`tools/standin_content`) and the stand-in game (`tools/standin_game`).

## Who applies the content (every start)

The checks, in this order; the first that says no decides, and the loader writes one line to `emf\emf_loader.log`
(`EML content: ...`):

1. `[content] mode=off` in `emf\emf_core.ini`.
2. EMF.exe's "Play without any mods". ("Play without EMF mods" keeps EML content: the redirect goes in without EMF's
   core.)
3. Another tool applies it: a `dbghelp.dll` in the game folder (EML's or ShroudForge's proxy; `emf\known_tools.txt`
   names it), or EML's or ShroudForge's `dinput8.dll` in `dinput8-chain.ini`. `mode=on` never overrides another tool.
4. `emf\emf_content.dll` is not there.
5. The dedicated server (not yet).
6. No EML mod EMF can build in `<game>\mods\` (below).
7. No base to build from (below).

The facts are gathered in that order too, so a start without `emf_content.dll` reads no DLL and no mod folder: it looks
for `dbghelp.dll` and the DLL, writes its line and goes on exactly as before.

**Usable EML mods:** a folder in `<game>\mods\` whose name does not start with `.`, with a `mod.json` that parses and
has `id`, `name` and `version`, an id no earlier folder has, and `capabilities` of only `patch` and/or `export`. A mod
that needs EML's runtime loader (`runtime`, `runtime-register-dll`) is left out, and so are `.zip` mods and junctions.
Each one left out gets a line (`EML content: left out <folder>: <why>`) when `emf_content.dll` is there.

**The base** (never patch on top of patched data). The KFC4 header of `enshrouded.kfc` (and of `enshrouded.kfc.bak`,
which EML and ShroudForge make when they patch in place) gives a version string and a container count:

| Header | Field |
|---|---|
| 0 `u32` | magic `0x3443464B` ("KFC4") |
| 16 `u32 + u32` | version: offset from byte 16, length |
| 24 `u32 + u32` | containers: offset from byte 24, count |

No `.bak`, or one of another game build (another version): the base is the `.kfc`. The same version, size and count:
not patched, the `.kfc`. The same version but another size or count: the game's files were patched in place, the base
is the `.bak` and only its containers count. A header that does not read means content off.

## The three paths

| Path | When | DllMain | The held entry |
|---|---|---|---|
| Off | any check says no | today's order: chained DLLs, the spike's hooks (only with its switches), EMF's core | not held |
| Current | the fingerprint equals the state's, and that build worked | chained DLLs, the redirect, the core | not held |
| Build | anything else | only the hold of the exe's entry point | the build, then chained DLLs, the redirect (after a good build), the core |

**The build, on the held entry** (the main thread, after every `DllMain`, before any of the game's code):

1. A small topmost window that never takes the focus: "EMF is preparing EML content (first start after a change)..." and
   the build's last log line. The build runs on a worker thread meanwhile; the main thread only keeps the window drawn.
2. The shadow copy (below), then `emf_content.dll`: `LoadLibrary`, `emf_content_abi() == 1`, one
   `emf_content_build` call with a 1 MB reply buffer, a check that no thread started in the DLL still runs
   (`module_threads.h`), `FreeLibrary` on the same thread, and a check that it is gone.
3. The state file, the window goes.
4. A good build (code 0, the reply whole, the DLL unloaded): the chained DLLs, the redirect, EMF's core.
   A failed build (or a cut reply, whose result is unknown): `shadow.txt` is removed, the game starts on its own data,
   the chained DLLs and the core start, the reason goes to the log, the state and the player.
   The DLL still loaded (a thread of its own runs), or the build not back within 15 minutes: neither the chained DLLs
   nor EMF's core start at this start, and the log says why.
5. The entry point's bytes go back and the game starts.

If the client's entry point is not MSVC's usual start-up code (another tool patched it), content is off for that start
with the reason, and the order is today's.

**Licence rules this keeps** (EMF stays MIT; only `emf_content.dll` is GPL): it is loaded only on the held entry,
before any chained DLL, EMF's core or a mod; it is unloaded before they start, or they do not start; it is never loaded
when the shadow copy is current; nothing in EMF's core, EMF.exe or the API loads it. Windows' crypto is never called
while the game starts up (the 2026-11-01 hang): the fingerprint is sizes and times hashed with EMF's own SHA-256, and
the DLL runs only while the main thread waits at the entry point.

## The shadow copy: `<game>\emf\content\client\`

| Name | What |
|---|---|
| `enshrouded.exe`, `enshrouded_000.dat` .. `_(N-1).dat` | hard links to the game's files (N: the base's containers); EML reads its types from the exe |
| `enshrouded.kfc`, `enshrouded.kfc_resources` | private copies of the base; EML patches them in place and keeps its own `enshrouded.kfc.bak` |
| `enshrouded_N.dat` and up | containers EML writes (deleted before each build) |
| `mods\<folder>\` | real folders with copies of the usable mods (EML leaves junctions out) |
| `.cache\` | EML's own (types, Lua definitions; removed before each build, the build is forced) |
| `shadow.txt` | the record of the game's files the redirect checks at every start (taken before anything is copied, written last) |
| `emf_content_state.json` | what the last build did |

Before each build the loader removes the index files, every `.tmp`, `.cache\`, `mods\` and every container at N or
past it; a link is only ever deleted, never opened for writing (a write through it would change the game's file). A
link whose file is no longer the game's (Steam replaced it) is made again. The DLL refuses to build when one of the
index files has a second name. Every file the loader writes is written as `.tmp` and renamed. No hard links (exFAT,
another drive): content is off with that reason; the 55 GB are never copied.

## The fingerprint and the state

`emf_content_state.json` next to the shadow:

```json
{"fingerprint": "sha256:...", "code": 0, "message": "...", "mods": ["beasts 1.0.0"], "errors": [],
 "ms": 28000, "built_at": "2026-11-01T20:00:00Z", "unloaded": true, "truncated": false}
```

The fingerprint covers, by size and last write time: the base (and which file it is), the game's own `.kfc`,
`.kfc_resources` and exe, the base's container count and the game folder's `.dat` count, `emf_content.dll`, each usable
mod sorted by id (id, version, every file's relative path, size and time), and EMF's version. The shadow is current
only when the state's fingerprint equals this start's and its build worked (code 0, unloaded, not cut). A crash in the
middle of a build leaves `.tmp` files and an old or missing state: the next start builds again. A Steam update changes
the fingerprint: the next start rebuilds from the new base (and the redirect's own check refuses the old copy anyway).

## What the player sees

The loader tells EMF's core how it went through the environment variable `EMF_CONTENT_STATUS`
(`<kind>\t<mods>\t<ms>\t<message>`, set only in the game's process):

| Kind | The core |
|---|---|
| `built` | console: `EML content: 3 mods (built in 28 s)` |
| `current` | console: `EML content: 3 mods (unchanged since the last build)` |
| `failed` | console, and on screen once the first world loads: `EML content could not be built: <why>. The game runs without it. Your saves were backed up.`; a save backup first ("before a start without EML content"), not held back by `min_minutes` (mod items vanish when a world loads without their mod) |
| `off` | only with `mode=on`: `EML content is off (<why>)` in the console |

## Settings and logs

```
[content]
mode=auto      ; auto (the default): EMF builds EML content when nothing else applies it; on: the same, and the
               ; console says why when it is off; off: never
log_opens=1    ; the redirect spike's diagnostic log of the exe's file calls (file_redirect_spike.md)
```

- `emf\emf_loader.log`: the decision, the path, the build's outcome (one line when content is off).
- `emf\content.log`: what a build did, the DLL's own lines included; emptied at each build.
- `emf\file_opens.log`: with the redirect on, every open of a content file (`=> "<shadow path>"`).
- `[content] redirect_test=1` (the spike's manual switch with `content_shadow.ps1`) still works while EMF does not own
  the content; once it does, the content state decides.
