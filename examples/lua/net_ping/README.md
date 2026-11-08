# Net Ping (Lua example)

The same mod on a player's game and on the server, talking on one channel with `emf.net` (mod messages, EMF API v27):
the game sends, the server answers `pong <what it got>` to that player, and the server can send to every player.

## What it teaches

- **A channel both ways**: `emf.net.channel("ping", {version, to = "both", max, per_10s, on_server, on_player})`
  registers `net_ping/ping`. `on_server` runs on the server world, `on_player` on the player's game.
- **Who sent it**: the server's handler gets `from.player` and `from.account` from the server's own records, never
  from the message, so a player cannot pretend to be someone else.
- **Answering one player or all**: `emf.net.send_to(world, from.player, ...)` and `emf.net.send_all(world, ...)`.
- **Waiting for the server**: `emf.net.peer(nil, 0, "ping")` is the server's channel version as this game sees it;
  it turns from 0 to 1 once the server has greeted the game. Only then can the game send.
- **Any bytes**: text, 3000 bytes (sent as several chat lines) and all 256 byte values arrive whole.

Joined players and a dedicated server use the game's chat as the transport; nobody sees those lines. In a game you
host, messages between your game and your server stay inside the process.

## Install

Copy this folder into `<game>\emf\mods\` on the player's PC and into the dedicated server's `emf\mods\`, and start
both. They need EMF's `lua` mod 1.19 or newer.

## Try it

1. Join the server. In the game's `emf\emf_core.log`: "NETPING the server greeted this game", then three
   "NETPING player got ..." lines: the server's answers to the game's three test messages.
2. In the server's log: "NETPING server got ... from player 1 account 0x...", one line for each.
3. Write `net_ping.all hello` into the server's `emf\commands.txt`: every player's log shows "NETPING player got 5
   bytes ... hello".
4. In a game you host, `net_ping.send hi` in the console goes to your own server in process.
