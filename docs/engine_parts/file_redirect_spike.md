# The content redirect spike: which files the game opens, and can it read them from a shadow copy

Phase 0 of "EMF loads EML content itself". Game build **1273043**. Status: in the loader, **off by default**, waiting
for its first run in the game (results go at the end of this file).

Two questions:

- **A.** Which files does `enshrouded.exe` open while it starts, and how (path form, access, sharing, flags; does it
  list or probe the `enshrouded_NNN.dat` containers)?
- **B.** Does the game run on its data when the opens of `enshrouded.kfc`, `enshrouded.kfc_resources` and
  `enshrouded_NNN.dat` are sent to a shadow copy in `<game>\emf\content\client\`? If yes, a later phase can patch
  copies there and never touch the game's own files.

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
- [inf] `ReadFileEx`, `CreateIoCompletionPort` and `GetOverlappedResult` suggest the containers are read
  asynchronously (`FILE_FLAG_OVERLAPPED`, maybe `NO_BUFFERING`). The log's `flags=` will say.

## What the loader does

Code: `src/loader/file_hooks.h` (the hooks and the log), `src/loader/file_redirect.h` (path mapping, log lines, the
switches; pure functions), `src/loader/iat_patch.h` (the import table). Tests: `tests/test_file_redirect.cpp`,
`tests/test_file_hooks.cpp`.

- **Where:** in `dinput8.dll`'s `DllMain` (`start_file_hooks` in `src/loader/dllmain.cpp`), after the chained DLLs and
  the Vulkan layer variables. Only in `enshrouded.exe` (not the dedicated server), only when EMF's core starts (not
  with EMF.exe's "Play without EMF mods"), and only with a switch on (below). Without a switch the loader does what it
  did before: no hook.
- **How:** it replaces the exe's import table slots of `CreateFileW`, `CreateFileA`, `GetFileAttributesW`,
  `GetFileAttributesA`, `GetFileAttributesExW`, `FindFirstFileW`, `FindFirstFileExW`, `FindNextFileW` and
  `SetCurrentDirectoryW` (imports from kernel32, kernelbase or `api-ms-win-core-*`). Each hook calls what was in the
  slot before (kernel32, or a hook a chained DLL put there) and leaves the caller the Windows error that call left.
  The slots are never put back. Other modules (EMF's core, the game's DLLs, drivers, Steam's overlay) have tables of
  their own and are not hooked; imports by ordinal and delay-loaded ones are not covered.
- **Why no hold of the entry point:** `dinput8.dll` is a static import of the exe, so Windows fills the exe's import
  table before any `DllMain` runs, and the exe's own code (TLS callbacks, C runtime start-up, `WinMain`) runs after.
  The hooks are in place before the exe's first instruction. The hold in `entry_hold.h` exists for something else: the
  dedicated server waits there until EMF's core (started on a thread) has hooked its world setup. The logging writes
  straight from the hooks and needs no thread.
- **The log:** `<game>\emf\file_opens.log`, emptied at each start. Header lines start with `#`: the time, the exe's PE
  time stamp, the switches, whether the shadow folder is there, every DLL the exe imports from, what was hooked, what
  is not imported, and the other file functions it imports. Then one line per call:

  ```
  +1234ms t5678 CreateFileW "<game>\enshrouded.kfc" access=0x80000000(GENERIC_READ) share=0x1(READ) disp=OPEN_EXISTING flags=0x80(NORMAL) -> handle 0x1a4
  +1300ms t5678 CreateFileW "<game>\enshrouded_017.dat" => "<game>\emf\content\client\enshrouded_017.dat" access=... -> handle 0x2b0
  +1301ms t5678 CreateFileW "<game>\enshrouded_018.dat" [not redirected: no shadow copy] access=... -> handle 0x2b4
  +1400ms t5678 GetFileAttributesW "<game>\enshrouded_000.dat" -> attrs 0x20
  +1500ms t5678 FindFirstFileExW "<game>\*" level=1 search=0 flags=0x2 -> handle 0x77 first "."
  +1501ms t5678 FindNextFileW handle 0x77 -> "enshrouded_000.dat"
  +1600ms t5678 SetCurrentDirectoryW "<game>" -> ok
  ```

  Milliseconds since the hooks went in, the thread id, the function, the path as the game passed it (UTF-8), then
  `=> "<shadow path>"` when it was redirected or `[not redirected: <why>]`, the arguments, and the result
  (`handle`/`attrs`/`ok`, or `error <Windows error>`). Every call is logged for the first `log_seconds`; after that
  only the content files (with `redirect_test=1`) and `SetCurrentDirectoryW`. At most 50,000 lines (about 10 MB).
- **The redirect** (`redirect_test=1`): an open (`CreateFileW`/`A`) or attribute query (`GetFileAttributesW`/`A`/`ExW`)
  of `enshrouded.kfc`, `enshrouded.kfc_resources` or `enshrouded_<digits>.dat` **directly in the game folder** goes
  to `<game>\emf\content\client\<the same name>` when that file is there. Any case, either slash, relative to the
  current folder at that moment, `\\?\` kept (and added when the shadow path is longer than a plain path may be).
  An open is redirected only when it reads a file that is there: no write, append, delete or attribute change, no
  create or truncate, no delete-on-close (the shadow's `.dat` files are hard links: a write would change the game's
  own file). Folder listings are never redirected: a container that is only in the shadow would not show in a
  listing of the game folder (the log tells whether the game lists at all).

Settings, in `<game>\emf\emf_core.ini` (only `1` turns a switch on; EMF's installer keeps the section):

```
[content]
log_opens=1       ; log the exe's file calls for the first log_seconds
log_seconds=300   ; 1-86400, default 300
redirect_test=1   ; send the content files' opens to <game>\emf\content\client\ (and log them)
```

`emf\emf_loader.log` gets one line: `content spike: hooked N file functions in the exe's import table (...)`.

## The shadow copy: `scripts\content_shadow.ps1`

- `content_shadow.ps1 -Make [-Game <folder>] [-Copy]` makes `<game>\emf\content\client\`: a **hard link** to every
  `enshrouded_NNN.dat` (the same file under a second name: no space used, needs NTFS; `-Copy` copies them instead,
  about 55 GB) and **real copies** of `enshrouded.kfc` (9 MB) and `enshrouded.kfc_resources` (214 MB), so a later
  phase can patch the copies. `shadow.txt` there records each file and folder it made. It refuses while the game runs
  and when the shadow is already there.
- `content_shadow.ps1 -Remove [-Game <folder>]` takes out exactly what `shadow.txt` records (a hard link is removed
  as a link: the game's file stays) and keeps, and names, any file it did not make.
- It never changes, moves or deletes the game's own files. Tested against fake game folders:
  `scripts\content_shadow_test.ps1` (in `verify.sh`).
- `uninstall_probe.ps1` (and `game_session.ps1 stop`) removes the whole `emf` folder, the shadow included; that also
  only removes the links. Run `-Remove` before a game update and `-Make` again after it: the copies are of the files
  at the time of `-Make`.
- Do not combine it with an EML patch (`content_test.ps1`) in the same folder: the copies would be of patched files,
  and EML's `dbghelp.dll` would patch the game folder's files, not the shadow.

## Running it (the game closed; the other session's game lock rules apply)

1. **Logging only:** with no `<game>\emf` folder (a clean folder after `game_session.ps1 stop`), create
   `<game>\emf\emf_core.ini` with `[content]`, `log_opens=1`; `game_session.ps1 start` installs EMF and keeps that
   section; it stops at the main menu. Read `<game>\emf\file_opens.log`. `stop` archives it to
   `analysis\runs\<stamp>-file_opens.log` and removes EMF.
2. **Redirect:** `content_shadow.ps1 -Make`, then the ini with `log_opens=1`, `redirect_test=1`, `log_seconds=600`;
   start, load the LOCAL world, play a minute; `stop -KeepInstalled`, `content_shadow.ps1 -Remove`, `stop`.

What to look for:

- The header: the hooked list (expected: `CreateFileW`, `CreateFileA`, `GetFileAttributesW`, `FindFirstFileW`,
  `FindFirstFileExW`, `FindNextFileW`, `SetCurrentDirectoryW`, all from `KERNEL32.dll`).
- The lines that name `enshrouded.kfc`, `enshrouded.kfc_resources`, `enshrouded_0`: the path form (absolute, relative,
  `\\?\`), access, share, flags (`OVERLAPPED`, `NO_BUFFERING`), which thread, when, how many times each file is opened
  (all containers at start, or one at a time later), and whether any `.dat` is probed (`GetFileAttributesW`, a listing
  like `enshrouded_*.dat`, an open of a number that does not exist, `-> error 2`).
- **No content file in the log at all** means the game opens them some other way (another module, or `NtCreateFile`):
  then the import-table route does not work and the next step is a hook in the game's own file code.
- With the redirect: every content line has `=> "...\emf\content\client\..."` and a `handle`, no `[not redirected: ...]`
  (if `opened to write` shows up: which file, which access), and the world loads and plays as usual.

## Results

(Not run yet.)
