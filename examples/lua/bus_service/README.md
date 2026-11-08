# Notes Service (Lua example)

A tiny key-value service that other Lua mods call over `emf.bus`: it saves, reads, lists and deletes short notes.
Use it with `bus_client` (same folder level), or write your own client from the guide.

## What it teaches

- **A service on the bus**: it listens on `notes.request`, answers on `notes.reply`, and announces itself on
  `notes.hello` when it starts and whenever someone publishes `notes.ping`.
- **A JSON envelope**: requests carry `v`, `id`, `op`, `args`, `from`; replies carry `v`, `id`, `to`, and `ok` with
  `result` or `error` (`emf.json`, Lua host 1.18).
- **Careful input handling**: bad JSON or a missing `id` is ignored, a wrong version or unknown `op` is answered with an
  error, and a crash inside an op (`pcall`) becomes an error reply instead of a silent timeout.
- **Honest limits**: at most 200 notes of 1000 bytes. A full bus (`publish` returns false) is logged.

The full pattern, with timeouts and versions, is in `docs/modding/services.md`.

## Install

Copy this folder and `bus_client` into `<game>\emf\mods\` (or the dedicated server's `emf\mods\`) and start the game or
the server. They need EMF's `lua` mod 1.18 or newer, which the EMF installer puts in and switches on.

## Try it

1. Open the console and type `bus_client.set apple red`, then `bus_client.get apple`: the answer shows on screen
   and in the log. `bus_client.list` shows the keys.
2. Delete the `bus_service` folder (or switch the mod off) and type `bus_client.get apple` again: after 3 seconds you
   get a timeout message, not silence.
3. Type `bus_client.ping`: the log shows the service announcing itself.
