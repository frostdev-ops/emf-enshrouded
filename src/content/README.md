# emf_content.dll

`emf_content.dll` builds the game content of EML mods (mods made for EML, the Enshrouded Mod Loader) into a game
folder, so those mods work without a separate EML install. It is a small Rust shim (`src/lib.rs`) around EML's own
library crates, which are copied in `third_party/eml/`. One call does what `emm.exe run --patch -g <folder>` does: it
loads the folder's `mods\` (`ModEnvironment::load`) and runs them (`mod_loader::lua::run`), with patching on, export
off and runtime DLLs off.

Nothing in EMF loads it yet. `emf_content_run.exe` (`tools/content_run/`, built next to `emf_tests.exe`, never
packaged) runs one build from the command line:

```
emf_content_run <game_dir> [--force] [--dll <path>]
```

It prints the log lines to stderr as `[level] line` and the reply JSON to stdout, and exits with the return code
(10: wrong arguments, 11: the DLL could not be loaded or has another ABI).

## What it writes

EML works in the folder it is given, so that folder must be a copy EMF made (a shadow folder), never the game's own
folder. EML writes there:

- `mods\` (made when it is missing) and `.cache\` (the game's types, a record of the files it built from);
- `<name>.kfc.bak` (the vanilla `.kfc`), then the patched `<name>.kfc` and `<name>.kfc_resources`;
- new `<name>_NNN.dat` files after the vanilla ones. It never writes a vanilla `.dat` file.

## The ABI

From `emf_content.h`:

```c
#define EMF_CONTENT_ABI 1
enum { EMF_CONTENT_OK = 0, EMF_CONTENT_MODS_FAILED = 1, EMF_CONTENT_RUN_FAILED = 2, EMF_CONTENT_BAD_REQUEST = 3,
       EMF_CONTENT_REPLY_TOO_SMALL = 4, EMF_CONTENT_PANIC = 5 };
/* level: 0 error, 1 warning, 2 info, 3 debug; line: one UTF-8 log line without a newline */
typedef void (*emf_content_log_fn)(void* ctx, int level, const char* line);
uint32_t emf_content_abi(void);
int32_t emf_content_build(const char* request_json, emf_content_log_fn log, void* log_ctx,
                          char* reply, size_t reply_cap, size_t* reply_len);
```

- Request (UTF-8 JSON): `{"game_dir": "<folder>", "file_name": "enshrouded", "force": false, "server": null}`. Only
  `game_dir` is needed. `file_name` defaults to `enshrouded` or `enshrouded_server`, whichever has a `.kfc` or `.exe`
  there. `force` builds even when EML's cache says nothing changed. `server: null` lets EML decide from the file name.
- Reply (NUL-terminated UTF-8 JSON in the caller's buffer): `{"code": n, "message": "...", "mods": [{"id": "...",
  "version": "..."}], "mod_errors": [{"id": "..." or null, "path": "...", "error": "..."}], "dlls": ["..."], "ms": n}`.
  `mods` is sorted by id. `dlls` stays empty, because runtime DLLs are off. `mod_errors` is filled when a mod's
  folder or `mod.json` cannot be read (code 1).
- `*reply_len` is always set to the reply's length with the NUL. When the buffer is too small the call returns
  `EMF_CONTENT_REPLY_TOO_SMALL` and writes nothing; a retry with that size runs the build again, so pass a big buffer
  (64 KB or more).
- A missing folder, or one without `<file_name>.kfc` or `.exe`, gives code 2 and a message that names the folder.
- `log` may be NULL. It is called only on the calling thread, during the call.

## Rules for the caller

- Load it only at the held start of the game, before any chain-loaded DLL, emf_core or mod starts, and only when the
  content needs building. Unload it (`FreeLibrary`) before anything else starts.
- Never load it in EMF.exe, and never while mods run.
- Never load it while `lua.dll` (EMF's Lua host) is loaded: it has its own Lua inside.

What the DLL promises in return, and the tests in `tests/test_content_dll.cpp` check:

- It exports only `emf_content_abi` and `emf_content_build` (no `lua_*`).
- No panic leaves it: a panic becomes code 5 with its message.
- It starts no thread, and keeps no log subscriber after a call (logging is set for the length of one call). 50
  load-build-unload cycles leave the thread count where it was.
- It uses its own static C runtime and never frees the caller's memory.

## Building

CMake builds it with cargo when `EMF_CONTENT` is on (the default): `cargo build --release --locked` in this folder,
with the target folder in `build\<preset>\cargo`, copied to `build\<preset>\content\emf_content.dll`. ctest runs the
Rust tests (`emf_content_cargo_tests`). You need rustup ([rustup.rs](https://rustup.rs)); `rust-toolchain.toml`
pins Rust 1.99.1. Without Rust, configure with `-DEMF_CONTENT=OFF`: then neither the DLL nor `emf_content_run` is
built, and its tests compile to nothing.

By hand, from a Visual Studio developer prompt (`scripts\dev.cmd`; EML's Lua is built as C):

```
cd src\content
cargo test --release --locked
```

`Cargo.lock` started from EML's: the crates EMF shares with EML keep EML's versions.

## EML's copy and its patches

`third_party/eml/` holds EML v0.2.0's library crates. Every change EMF makes to them is its own commit and a line in
`third_party/eml/EMF_PATCHES.md`, which also says how to update the copy.

## Licence

The DLL contains EML, so it is GPL-3.0-or-later as a whole: EML is GPL-3.0-or-later, see
[third_party/eml/LICENSE](../../third_party/eml/LICENSE).
