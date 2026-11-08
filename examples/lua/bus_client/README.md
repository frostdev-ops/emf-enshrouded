# Notes Client (Lua example)

Console commands that call the notes service of `bus_service` over `emf.bus`, and cope with a service that is missing,
reloading or slow.

## What it teaches

- **Calling a service**: publish a JSON request on `notes.request` with a unique `id` and your mod id in `from`,
  listen on `notes.reply`, and match answers by `id` (and `to`).
- **Timeouts**: the answer may never come. Every call gets a deadline counted in server world steps (60 a second); the
  callback runs exactly once, with the answer, an error or a timeout. A late answer is ignored.
- **Refusals**: `emf.bus.publish` returns false when the bus is full or the payload too big; the call fails right away.
- **Seeing the result**: `emf.notify` for the player, `emf.log` for the file.

The full pattern is in `docs/modding/services.md`.

## Install

Copy this folder and `bus_service` into `<game>\emf\mods\` (or the dedicated server's `emf\mods\`) and start the game
or the server. They need EMF's `lua` mod 1.18 or newer.

## Try it

1. `bus_client.set apple red`, `bus_client.get apple`, `bus_client.list` in the console.
2. Remove `bus_service` and run `bus_client.get apple`: "timeout: no answer from the notes service" after 3 seconds.
3. Change `TIMEOUT_STEPS` in `main.lua`, save, and watch the mod reload.
