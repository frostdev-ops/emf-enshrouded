# The content redirect spike: which files the game opens, and can it read them from a shadow copy

Game build **1273043** (the import table). Status: in the loader, **off by default**. Run in the game on 2026-11-01:
the game ran on a shadow copy of its content (results below).

Two questions:

- **A.** Which files does `enshrouded.exe` open while it starts, and how (path form, access, sharing, flags; does it
  list or probe the `enshrouded_NNN.dat` containers)?
- **B.** Does the game run on its data when the opens of `enshrouded.kfc`, `enshrouded.kfc_resources` and
  `enshrouded_NNN.dat` are sent to a shadow copy in `<game>\emf\content\client\`? If yes, EMF can patch copies there
  and never touch the game's own files.

Answers (from the run below): **A.** `CreateFileW` with absolute paths, `GENERIC_READ`, share `READ|DELETE`,
`OPEN_EXISTING`; no listing or probing of the containers. **B.** Yes.

## Results (2026-11-01) [obs]

With `redirect_test=1` and a shadow copy made by `content_shadow.ps1 -Make`:

- **Every content open went to the shadow copy.** The log has 169 opens of the game's data, and each content file's
  line has `=> "<game>\emf\content\client\..."` and a handle.
- **How the game opens its data:** `CreateFileW`, absolute paths, `GENERIC_READ`, share `READ|DELETE`, `OPEN_EXISTING`
  (so each of them passes the redirect's read-only rule).
- **In this order:** `enshrouded.kfc`; a probe of `enshrouded.kfc_info` (error 2: no such file; not a content file
  name, so the loader leaves it alone); `enshrouded.kfc_resources` once; then the 55 `enshrouded_NNN.dat` files. That
  is 168 opens of the content files plus the probe.
- **No folder probes:** no `FindFirstFile*` and no `GetFileAttributes*` on the data files. The game does not list the
  containers, it opens the ones it knows.
- The world loaded normally, and the game's own files were unchanged afterwards (the same hashes).

If a game update ever ships an `enshrouded.kfc_info`, it would be read from the game folder: add the name to
`is_content_file_name` (`src/loader/file_redirect.h`) then.

## What the exe imports [obs]

Read from the 1273043 exe's import table (`tools/re/imports.py`, read-only):

- Every file function comes straight from `KERNEL32.dll`, not through `api-ms-win-*` sets. The C runtime is linked in
  (no `ucrtbase.dll` or `msvcp140.dll` among the imports), so its `fopen` and friends go through the same table.
- The DLLs it imports from: ADVAPI32, COMDLG32, CRYPT32, DINPUT8, GDI32, KERNEL32, OLEAUT32, POWRPROF, SHELL32,
  USER32, WLDAP32, WS2_32, bcrypt, dbghelp, ole32, steam_api64. No `dstorage.dll` (DirectStorage).
- File functions it imports: `CreateFileW`, `CreateFileA`, `GetFileAttributesW`, `FindFirstFileW`, `FindFirstFileExW`,
  `FindNextFileW`, `FindClose`, `ReadFile`, `ReadFileEx`, `GetOverlappedResult`, `CreateIoCompletionPort`,
  `GetFileSizeEx`, `GetFileInformationByHandle(Ex)`, `SetFileInformationByHandle`, `SetFilePointerEx`, `GetFileType`,
  `SetCurrentDirectoryW`, `GetCurrentDirectoryW`, `GetFullPathNameW`, `ReadDirectoryChangesW`, `CopyFileW`,
  `MoveFileExW`, `DeleteFileW`, `CreateDirectoryW`, `RemoveDirectoryW`, `FlushFileBuffers`, `SetEndOfFile`,
  `SetFileAttributesW`, `SetFileTime`, `WriteFile`, `GetTempFileNameW`, `GetTempPathW`.
- Not imported: `GetFileAttributesA`, `GetFileAttributesExW`, `CreateFile2`, `CreateFileMapping*`/`MapViewOfFile*`
  (no memory-mapped containers), the I/O ring functions.
- Each file function is imported once, from `KERNEL32.dll` only. (The hooks keep one original per function; an exe
  that imported one function through two descriptors would need a hook per slot.)
- [inf] `ReadFileEx`, `CreateIoCompletionPort` and `GetOverlappedResult` suggest the containers are read
  asynchronously (`FILE_FLAG_OVERLAPPED`); the `flags=` field of the log shows it.

## What the loader does

Code: `src/loader/file_hooks.h` (the hooks, the log, the check of the shadow copy), `src/loader/file_redirect.h` (path
mapping, the record of the game's files, log lines, the switches; no side effects), `src/loader/iat_patch.h` (the
import table). Tests: `tests/test_file_redirect.cpp`, `tests/test_file_hooks.cpp` (with a stand-in game,
`tools/standin_game`, that the tests run as `enshrouded.exe`).

- **Where:** in `dinput8.dll`'s `DllMain` (`start_file_hooks` in `src/loader/dllmain.cpp`), after the chained DLLs and
  the Vulkan layer variables. Only in the game client, an exe named `enshrouded.exe` (not the dedicated server, not
  another program that loads the DLL), only when EMF's core starts (not with EMF.exe's "Play without EMF mods"), and
  only with a switch on (below). Without a switch the loader does what it did before: no hook, and nothing is made.
- **How:** it replaces the exe's import table slots of `CreateFileW`, `CreateFileA`, `GetFileAttributesW`,
  `GetFileAttributesA`, `GetFileAttributesExW`, `FindFirstFileW`, `FindFirstFileExW`, `FindNextFileW` and
  `SetCurrentDirectoryW` (imports from kernel32, kernelbase or `api-ms-win-core-*`; a slot on an executable page stays
  executable while it is written). Each hook calls what was in the slot before (kernel32, or a hook a chained DLL put
  there) and leaves the caller the Windows error that call left. No C++ exception leaves a hook: when deciding throws
  (out of memory while a path is built) the call goes through unchanged; when logging throws, only the line is lost.
  The slots are never put back. Other modules (EMF's core, the game's DLLs, drivers, Steam's overlay) have tables of
  their own and are not hooked; imports by ordinal and delay-loaded ones are not covered.
- **Why no hold of the entry point:** `dinput8.dll` is a static import of the exe, so Windows fills the exe's import
  table before any `DllMain` runs, and the exe's own code (TLS callbacks, C runtime start-up, `WinMain`) runs after.
  The hooks are in place before the exe's first instruction. The hold in `entry_hold.h` exists for something else: the
  dedicated server waits there until EMF's core (started on a thread) has hooked its world setup. The logging writes
  straight from the hooks and needs no thread.
- **Is the shadow copy still the game's?** With `redirect_test=1`, before any hook goes in, the loader reads
  `<game>\emf\content\client\shadow.txt`'s record of the game's files (`content_shadow.ps1 -Make` writes it last) and
  compares it with the game folder: the size and last write time of `enshrouded.exe`, `enshrouded.kfc` and
  `enshrouded.kfc_resources`, and the number of `enshrouded_NNN.dat` files. Steam updates the game by itself: a
  container it replaces stays alive, old, behind the shadow's hard link; one it patches in place is new in the shadow
  too while the shadow's `.kfc` copies are old. Either way the new exe would read content that does not belong
  together. On any difference, or a record that is missing, unreadable or incomplete, the loader does not redirect and
  writes one line to `emf\emf_loader.log` and to `emf\file_opens.log`'s header, for example:

  ```
  content redirect off: the game changed since the shadow copy was made (enshrouded.kfc); run content_shadow.ps1 -Remove and -Make again
  ```

  With `log_opens=1` the hooks still log; with `log_opens=0` there is then nothing to do and no hook goes in. The
  record holds the game's files, never the shadow's: a patched shadow `.kfc` is still checked against the game's own.
- **The log:** `<game>\emf\file_opens.log`, emptied at each start. Header lines start with `#`: the time, the exe's PE
  time stamp, the switches as the ini has them, whether the shadow folder is there, why the redirect is off (if it
  is), every DLL the exe imports from, what was hooked, what is not imported, and the other file functions it imports.
  Then one line per call:

  ```
  +1234ms t5678 CreateFileW "<game>\enshrouded.kfc" access=0x80000000(GENERIC_READ) share=0x5(READ|DELETE) disp=OPEN_EXISTING flags=0x80(NORMAL) -> handle 0x1a4
  +1300ms t5678 CreateFileW "<game>\enshrouded_017.dat" => "<game>\emf\content\client\enshrouded_017.dat" access=... -> handle 0x2b0
  +1301ms t5678 CreateFileW "<game>\enshrouded_018.dat" [not redirected: no shadow copy] access=... -> handle 0x2b4
  +1400ms t5678 GetFileAttributesW "<game>\enshrouded_000.dat" -> attrs 0x20
  +1500ms t5678 FindFirstFileExW "<game>\*" level=1 search=0 flags=0x2 -> handle 0x77 first "."
  +1501ms t5678 FindNextFileW handle 0x77 -> "enshrouded_000.dat"
  +1600ms t5678 SetCurrentDirectoryW "<game>" -> ok
  ```

  Milliseconds since the hooks went in, the thread id, the function, the path as the game passed it (UTF-8), then
  `=> "<shadow path>"` when it was redirected or `[not redirected: <why>]`, the arguments, and the result
  (`handle`/`attrs`/`ok`, or `error <Windows error>`). With `log_opens=1` every call is logged for the first
  `log_seconds`. With `redirect_test=1` the content files' calls and `SetCurrentDirectoryW` (relative paths depend on
  it) are logged for as long as the game runs; with `log_opens=1` alone nothing is logged after `log_seconds`. At most
  50,000 lines (about 10 MB); a line is built only while there is room for it.
- **The redirect** (`redirect_test=1`, the shadow copy checked as above): an open (`CreateFileW`/`A`) or attribute query
  (`GetFileAttributesW`/`A`/`ExW`) of `enshrouded.kfc`, `enshrouded.kfc_resources` or `enshrouded_<digits>.dat`
  **directly in the game folder** goes to `<game>\emf\content\client\<the same name>` when that file is there. Any
  case, either slash, relative to the current folder at that moment, a game folder at a drive root too. A `\\?\` path
  is taken literally, as Windows takes it: one with `/`, `.` or `..` in it is never redirected; the prefix is kept (and
  added when the shadow path is longer than a plain path may be). An open is redirected only when every access right it
  asks for is a read right (`GENERIC_READ`, `GENERIC_EXECUTE`, `FILE_READ_DATA`, `FILE_READ_EA`,
  `FILE_READ_ATTRIBUTES`, `FILE_EXECUTE`, `READ_CONTROL`, `SYNCHRONIZE`: an allowlist), its disposition is
  `OPEN_EXISTING` and it has no delete-on-close; anything else goes to the game's own path (the shadow's `.dat` files
  are hard links: a write would change the game's own file). Folder listings are never redirected: a container that is
  only in the shadow would not show in a listing of the game folder (the game does not list them, see the results).

Settings, in `<game>\emf\emf_core.ini` (only `1` turns a switch on; EMF's installer keeps the section):

```
[content]
log_opens=1       ; log the exe's file calls for the first log_seconds
log_seconds=300   ; 1-86400, default 300
redirect_test=1   ; send the content files' opens to <game>\emf\content\client\ (and log them)
```

`emf\emf_loader.log` gets one line, `content spike: hooked N file functions in the exe's import table (...)`, and
before it the `content redirect off: ...` line when the shadow copy is not used.

## The shadow copy: `scripts\content_shadow.ps1`

- `content_shadow.ps1 -Make [-Game <folder>] [-Copy]` makes `<game>\emf\content\client\`: a **hard link** to every
  `enshrouded_NNN.dat` (the same file under a second name: no space used, needs NTFS; `-Copy` copies them instead,
  about 55 GB) and **real copies** of `enshrouded.kfc` (9 MB) and `enshrouded.kfc_resources` (214 MB), so they can be
  patched. `shadow.txt` there records each file and folder it made (each file before it is made) and, last, the game's
  files it was made from:

  ```
  source enshrouded.exe <size in bytes> <last write time, UTC, as a Windows file time>
  source enshrouded.kfc <size> <time>
  source enshrouded.kfc_resources <size> <time>
  dat_count 55
  ```

  The stamps are taken before anything is copied, so a game file that changes meanwhile no longer matches; a `-Make`
  that stops half-way has no stamps, and the loader does not use that copy. It refuses while the game runs and when the
  shadow is already there.
- `content_shadow.ps1 -Remove [-Game <folder>]` takes out exactly what `shadow.txt` records (a hard link is removed as
  a link: the game's file stays) and keeps, and names, any file it did not make. It never changes a file's attributes:
  a hard link shares its read-only mark with the game's file, so a read-only link is named (clearing the mark would
  change the game's file too) and `shadow.txt` is kept for another `-Remove`.
- Both refuse when `emf`, `emf\content` or `emf\content\client` is a junction or symbolic link (they would write or
  delete in another place). `-Game` may be a PowerShell drive path.
- It never changes, moves or deletes the game's own files. Tested against fake game folders:
  `scripts\content_shadow_test.ps1` (in `verify.sh`).
- `uninstall_probe.ps1` (and `game_session.ps1 stop`) removes the whole `emf` folder, the shadow included; that also
  only removes the links. After a game update the loader stops using the copy by itself (the record no longer
  matches): run `-Remove` and `-Make` again.
- Do not combine it with an EML patch (`content_test.ps1`) in the same folder: the copies would be of patched files,
  and EML's `dbghelp.dll` would patch the game folder's files, not the shadow.

## Rules for patching the shadow copy

The shadow's `.dat` files are hard links: the game's own files under a second name.

- **Write a new file and rename it over the link** (`MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`, .NET's `File.Replace`
  or a delete of the link then a move): the link's name then points to the new file and the game's file stays as it
  was.
- **Never write through the link:** opening it for writing (`CreateFile` with write access, `FileMode.Open` +
  `FileAccess.Write`, `OPEN_ALWAYS`/`TRUNCATE_EXISTING`), or copying onto it with overwrite (`CopyFile` with
  `bFailIfExists=FALSE`, `File.Copy(src, link, true)`), changes the game's own file. So does changing its attributes or
  times.
- The `.kfc` and `.kfc_resources` in the shadow are real copies and may be patched in place.
- `shadow.txt`'s `source` lines stay the game's files: a patcher never rewrites them with the shadow's.

## Running it (with the game closed)

1. **Logging only:** with no `<game>\emf` folder (a clean folder after `game_session.ps1 stop`), create
   `<game>\emf\emf_core.ini` with `[content]`, `log_opens=1`; `game_session.ps1 start` installs EMF and keeps that
   section; it stops at the main menu. Read `<game>\emf\file_opens.log`. `stop` archives it to
   `analysis\runs\<stamp>-file_opens.log` and removes EMF.
2. **Redirect:** `content_shadow.ps1 -Make`, then the ini with `log_opens=1`, `redirect_test=1`, `log_seconds=600`;
   start, load the LOCAL world, play a minute; `stop -KeepInstalled`, `content_shadow.ps1 -Remove`, `stop`.
3. **A shadow copy the game has outgrown:** as 2, but change one `source` line in `shadow.txt` (a size one byte off)
   before the start: `emf_loader.log` and `file_opens.log` say `content redirect off: the game changed since the shadow
   copy was made (...)`, and no line has `=>`.

What to look for:

- The header: the hooked list (expected: `CreateFileW`, `CreateFileA`, `GetFileAttributesW`, `FindFirstFileW`,
  `FindFirstFileExW`, `FindNextFileW`, `SetCurrentDirectoryW`, all from `KERNEL32.dll`), and no `content redirect off`
  line when the shadow copy is fresh.
- The lines that name `enshrouded.kfc`, `enshrouded.kfc_resources`, `enshrouded_0`: the path form, access, share,
  flags, which thread, when, how many times each file is opened.
- **No content file in the log at all** would mean the game opens them some other way (another module, or
  `NtCreateFile`): then the import-table route would not work and the next step would be a hook in the game's own file
  code. (On 2026-11-01 all of them came through `CreateFileW`.)
- With the redirect: every content line has `=> "...\emf\content\client\..."` and a `handle`, no `[not redirected: ...]`
  (if `not a read-only open` shows up: which file, which access and disposition), and the world loads and plays as
  usual.
