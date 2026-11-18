# LA-Online 16.0: what changed from V15.2

LA-Online 16.0 is V15.2 moved onto a newer LAS Recomp (v0.6) together with drop-in local co-op (2 to 8 players on one
PC). This page is the short version, for players and for whoever runs a relay. Every change to V15.2's own files, and
why, is in [laonline-port.md](laonline-port.md); the relay protocol the game speaks is in
[laonline-protocol.md](laonline-protocol.md).

## For players

- **Local co-op and online together.** Up to 8 players on one PC, each with their own controller, and each one is a
  player online of their own: their own Link, name ("Name P2", ...), hearts and colour on everyone else's screen.
- **The Online page** in the game's menu (F1, or Minus + Plus): host a private session (with a room code) or a public
  one, join by code or from the list of public sessions, Ready, and the host starts it. Its other tabs: **Global**
  (everyone online at once, each in their own world) and **Leaderboards** (completed runs; submit your own).
- **Co-op Randomizer:** the host's seed comes to the others' games by itself (patches against each player's own game
  files - no game files are sent) and their games switch to it.
- **PvP and Z-targeting for every player.** Each one turns PvP on with their own R3 (F6 for Player 1 too) and locks on
  with their own ZL.
- **Safer saves.** Shared items, upgrades, rupees and hearts are given through the game's own item code, and PvP damage
  goes through the game's own damage, instead of being written into the save.
- **Game 1.0.0 and 1.0.1** both work.
- **Set up:** a `laonline.ini` next to the game with `server = <your host's address>:47620` and `name = <your name>`
  (V15.2's installer writes this file; its other lines are fine to keep). Without one, the game plays as if online play
  weren't there, and the menu has no Online page.
- **Not in 16.0:** V15.2's own start screen and menus (the Online page replaces them), the self-updater, sending logs
  from the game, the D-pad shortcuts and V15.2's artwork.
- **16.0 plays only with 16.0.** Rooms only match the same version (V14.1's rule): 16.0's saves and shared items work
  differently from V15.2's, so the two don't share a room.

## For relay hosts

Nothing in the protocol is new - 16.0 sends and expects what V15.2 does (the same HELLO, session service, seed
transfer, leaderboards, Global and PvP messages). What is new is *who* connects:

1. **Version 16.0.** Each connection's HELLO carries `LAOV 16.0`. A relay that only refuses versions older than its
   minimum lets it in; one that wants an exact version needs 16.0 added.
2. **Several players from one PC.** Each local co-op player 2..8 opens a connection of their own to the same relay,
   with the same lobby and password as that PC's Player 1, named "<Player 1's name> P<n>" (P2..P8). In a session
   (a room) only Player 1 is a member of the room; their local players join the room's lobby with its lobby name and
   password but are **not room members**. A relay that only admits a room's members to its lobby needs to admit these
   too (or count them as the member's). A lobby can then hold more Links than the room's 4 places: 4 PCs x up to 8.
3. **Many connections from one address.** Those players' connections all come from the same IP address.
4. **Their PvP** (switch, attacks, hits, knock-outs) comes on their own connection, so the relay names each of them
   as the attacker or the one hit, as for any player.

None of this could be checked against the real relay yet (it isn't public): the game was tested against a stand-in
written from V15.2's client code (`tools/laonline_relay.py`, `tools/coop_tests/online_*.sh`).

## For the developer

- **Where things are:** the online code is V15.2's (`runtime/hle/laonline*`, `runtime/ui/laonline_*`,
  `runtime/hle/ztarget.c`, `tools/rando/lao_seed_transfer.py`), with each change marked "LAS Recomp Co-Op's port" in a
  comment and listed in [laonline-port.md](laonline-port.md). New files of ours: `runtime/hle/laonline_local.c` (local
  players on the relay), `runtime/ui/laonline_lobby.mm` (the Online page, from V15.2's `session_ui.mm`).
- **Addresses:** every game address is named, for game 1.0.0 and 1.0.1, in `runtime/game_addresses/`
  (`tools/test_game_addresses.py` checks them).
- **Tests:** `tools/coop_tests/online*.sh` run two copies of the game on one PC against the stand-in relay - lobby,
  Global, leaderboards, PvP, the seed transfer, leaving and rejoining, the relay restarting, an 8-minute run.
  V15.2's unit tests (`tests/*.c`) aren't carried: they check what 16.0 changed on purpose (shared items written into
  the save, PvP for one player per PC) and build against V15.2's own start screen; they stay in this repository's
  history.
- **Before release:** the log's `[fork]` line (`runtime/hle/coop.c`) and the README's bug-report link name this
  repository; change them if it moves.
