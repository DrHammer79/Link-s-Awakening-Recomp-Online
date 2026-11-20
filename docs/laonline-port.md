# Online play (LA-Online) in the co-op build

LegendarySavageGamer's **LA-Online** puts players on other PCs into your game: each one is a
Shadow Link actor dressed as Link ("puppet"), moved to where that player is, with story progress,
items, keys, souls and a randomizer seed shared between the PCs' saves, plus chat, PvP and
Z-targeting. Source: <https://github.com/LegendarySavageGamer/Link-s-Awakening-Recomp-Online>.

This branch carries its **online core** from **V15.2** (repository commit d2712f2a, 2026-11-13),
moved from upstream `beta_5.4` (fb45ad6d) onto v0.6.0 with local co-op. Online play is off unless
`laonline.ini` next to the game (or `LAONLINE_MODE`) sets a mode: `echo` (one PC, a puppet replays
your own path 1.5 s late), `host` / `join` (two PCs, direct), `server` (his relay).

## What came over, and what didn't

- **Taken, as he wrote them** (line endings aside): `runtime/hle/laonline*.c/.h`,
  `dev_flag_names.inc`, `ztarget.c`; `runtime/dev_events.c/.h`, `laonline_build.h`,
  `laonline_progression.cpp/.h`, `laonline_seed.cpp`; `runtime/nvn/pool_registry.h`; `runtime/ui/laonline_overlay.mm`, `laonline_map_art.mm/.h`,
  `laonline_item_icons.mm/.h`, `laonline_map_state.h`, `laonline_chat_ui.h`,
  `laonline_ui_policy.h`, `coop_version.h`, `laonline_seed_share.h`, `lao_icons.h`.
- **Taken, redrawn:** his lobby (`session_ui.mm`: host / join / browse sessions, the room's Ready and Start) as the
  menu's **Online** page, `runtime/ui/laonline_lobby.mm` - its relay protocol and rules as he wrote them, drawn with
  the menu's own widgets, in the game instead of before it (below).
- **Not taken:** his start screen and menus (`launcher.mm`, `menu.mm` tabs, `tropical_ui`,
  `v10_ui`, `settings_look`, `settings_reset`; Global and the leaderboards are tabs of the Online page instead, its seed
  picker the Randomizer page), the self-updater
  (`lao_updater`, `release_updates.h`), log upload and the report screen (`laonline_logs`,
  `laonline_report`, `laonline_report_send`), the D-pad shortcuts, his artwork, and his randomizer
  tracker fixes in `tracker_overlay.mm` (missing logic rooms, checks without a collected flag, the
  bag rules, hand marks: upstream bugs, better offered there on their own), and `shadow_skin.c` (Shadow
  Link's textures swapped for Link's as they upload: its hook in the texture upload wasn't taken, and the
  puppets take Link's own model resource instead, `laonline_look.c`). No images: his item icons are drawn
  in code, and his map art loads from files the player supplies.

## Changes to his files (for merging his later versions)

- **Game addresses** (all files): every raw `MAIN_BASE + 0x...` is a named entry in
  `runtime/game_addresses/{patches,runtime}_v1_0_{0,1}.h` (`LAONLINE_*`), and every
  `strcmp(version->key, "v1_0_0")` gate is gone: a feature needs its entries, not a version name.
  His instruction checks before each call are kept. Game 1.0.1 / 1.1.0 values were checked against
  that game's code: the same instructions, calls and vtable slots (through its relocations); two
  functions moved by 0xd0 (`LAONLINE_COLLIDER_SET_ENABLED` 0x10b5d40 -> 0x10b5e10,
  `LAONLINE_MESH_SET_VISIBLE` 0x104fae0 -> 0x104fbb0). Before, on 1.0.1 these were off: item and
  pickup sharing (`laonline_coop.c`), shared progress / PvP (`laonline_v12.c` `save_ready`), the
  remote players' looks, recovery hearts, death notices. `tools/test_game_addresses.py --game
  v1_0_0=<1.0.0 files> --game v1_0_1=<1.0.1 files>` re-derives the 1.0.1 table from 1.0.0's.
  Kept on 1.0.0 only: `laonline_look_diag` (a log of a few 1.0.0 strings, research only).
- **His hooks** (`tools/patches.py`): `LAONLINE_SHADOW_UPDATE_SITE`, `LAONLINE_ANIMATION_SITE`
  (patches), `LAONLINE_GAMEOVER_SITE` (a hook, through `runtime/hle/hooks.c`'s table - his
  `rt_hook` checked a raw address on 1.0.0).
- `laonline.c`: `launch_from_config` (a mode in `laonline.ini` / `LAONLINE_MODE` starts online
  co-op at the first game frame - his start screen did this); `report_url` and the report include
  removed (the report sender isn't taken). The relay (`mode = server`) only with a lobby named by
  `LAONLINE_LOBBY` (tests; his installer writes a `lobby =` into every player's laonline.ini, and his game never
  joined it by itself, so the ini's doesn't count): otherwise the session is picked on the menu's Online page, which starts online play
  in it (`laonline_session_override` + `laonline_launch_mode`, as his start screen did), and configuration alone never
  starts it, as in his build. Online play counts as set up (`laonline_configured`) when `laonline.ini` names a relay
  (`server`) as well as when it sets a mode: his installer writes the relay. No default relay (it was a LAN address
  of his own).
- `runtime/ui/laonline_lobby.mm` (from his `session_ui.mm`): the session service (relay opcode 0x30,
  `docs/laonline-protocol.md` 6a) as he wrote it - the requests, the 2 s room poll, a press during a poll sent after
  it, the rejoin token. Changed: a page of the game's menu (`menu.mm`: the bar's Online entry, tab 12, shown only while
  online play is set up), drawn with the menu's widgets; the room polled each frame the game draws
  (`laonline_lobby_frame`), so a session the host starts begins with the menu closed too; a room being left is
  no longer this player's from that moment (`g_leaving`), not once the relay's answer comes - in between, the room
  still listed as started began the session again (`tools/coop_tests/online_flows.sh`); the seed a Co-op
  Randomizer room needs is the one being played (his launcher downloaded a missing one: not taken - the page says to
  play the host's seed - taken since: below). `laonline_lobby_config` gives it the ini's name and relay.
  `tools/coop_tests/online_lobby.sh`.
- A Co-op Randomizer room's seed (his V13 / V15.2): his transfer, native since LAS Recomp's November 2026 randomizer
  took Python out of every build (`runtime/laonline_seed_transfer.cpp`; his `tools/rando/lao_seed_transfer.py` ran in
  the Python seed helper, which is gone). The host's game uploads
  the room's seed while hosting (his keepalive: 15 s after the room is known, every minute for 10 minutes, then every
  5); a joiner's downloads it (every 20 s until it is there) and switches to it (`rando_select` + the in-place reboot,
  as the Randomizer page does) - the room stays joined through the reboot, and the session begins once the seed
  matches. A seed is now its record (`seed.json`: every decision its game files are built from, on each machine,
  from that player's own files) and its code patches (`exefs/*.pchtxt`), so the package is the seed folder's own
  files (not its saves); his patches against each player's game files had been for seeds that held their changed game
  files (`romfs/`). No game file travels either way. The world checksum (`runtime/laonline_seed.cpp`) hashes a
  record seed's `seed.json` and `exefs/` (its own schema); an older seed is hashed as V15.2 did - and isn't sent
  (made before the record, its folder holds game files: the page says to make it again). The relay side is his,
  unchanged: the manifest names LAO-WORLD-PATCH-1 and the baseline the relay accepts, plus
  `"seed_format": "lasrecomp-seed-1"`. `tools/coop_tests/online_seed.sh` (the game's upload, the package's contents,
  the download checked against the checksum, the switch).
  Two more tabs, his start screen's: **Global** (Join / Leave Global: launch mode 3 with his variant - Global
  Randomizer while a seed is played - and the player count, relay 0x33) and **Leaderboards** (the completed runs,
  filters, pages, Submit a run: relay 0x32, his V12 fallback kept). `tools/coop_tests/online_global.sh`.
- `laonline_ui.c`: no D-pad shortcut condition. Its menu / pause check (the game's UI page list, read
  fault-safe with Windows' `ReadProcessMemory`) was Windows-only: elsewhere the game always counted as
  paused - grants never handed over, chat closed at once, death notices never re-armed. It now reads
  fault-safe through a pipe on macOS, Linux and Android (the kernel answers EFAULT for an unmapped
  address), the same walk everywhere. Not yet run off Windows.
- `laonline.c`: `laonline_configured` (and `g_laonline_on`): whether online play is set up at all,
  worked out once at boot (below).
- `laonline.c`: this PC's player id (`g_my_id`) comes from the OS's random source (`arc4random`). It was
  `rand()` (never seeded) ^ a variable's address ^ the frame: two copies started alike got the same id and
  each dropped the other's states as its own - direct host / join never connected on one PC
  (`tools/coop_tests/online_net.sh`, 2026-11-17). A fix for him too.
- `laonline_v12.c`, `laonline_coop.c`: what the team shares is granted through the game's own `AddItem`
  (`native_grant`), not written into the save (LAS Recomp's review, point 2): the item bits (k_item_bit_id),
  the capacity upgrades 123..125, the Seashell Sensor 116, golden leaves (23, index = the leaf), the trade
  step (`trade_item`: 1..12 -> 29..40, 13 / 14 -> 42 / 43, as the game's SetWarashibeItem), the
  bracelet / shield / sword levels (13/14, 2/3, 0/1), shared rupees and ammo (`laonline_v12_resource`,
  now given the Cpu), the shared recovery heart (`AddItem` 100). Each checked on 1.0.1 by giving it
  (`LARECOMP_GIVE_ITEM`) and comparing the save before and after. A grant made this way is a teammate's:
  `laonline_coop_remote_grant` keeps it from being reported back as this player's pickup. The rupee
  "undone a frame later" rewrite is gone: an `AddItem` of rupees from the same tick stayed (100 -> 156,
  kept 5 s); a revert is logged ("shared rupees undone"), not written over.
- `keysanity.c` `rt_add_item`: his observer (as V15.2 has it): the item a pickup adds, reported to the team
  (`laonline_coop_pickup`) and checked by the journal - missing from the first port, so this PC's
  pickups weren't shared. Behind `laonline_configured`.
- `laonline.c`: `LAONLINE_SERVER`, `LAONLINE_NAME` and `LAONLINE_LOBBY` override the ini's, as `LAONLINE_MODE` /
  `PEER` / `PORT` do (two copies on one PC share the data root's `laonline.ini`).
- Players 2..8 of this PC (local co-op) on the relay as players of their own:
  `runtime/hle/laonline_local.c` (ours). His protocol has one player per relay connection, so each
  local player in the game gets a connection of his own (name "<Player 1's> P<n>", his colour, his
  Link's position, facing, animation and hearts, and his PvP - below); nothing else goes out on it (progress,
  pickups, deaths, chat stay Player 1's: one save per PC). No change to the protocol or the relay.
  In `laonline.c`: `laonline_relay_login` (the login the main connection uses), the main connection
  leaving these ids out (`laonline_local_peer_message` in `srv_handle`, `laonline_local_hide` after
  each message), the animation hook recording the other local Links' too, the map / room passed on.
  Each connection keeps the login it joined with: when Player 1's changes (online play stopped, another session picked
  on the Online page) it leaves at once and joins the new one.
  In Global too (`laonline_relay_login` gives Global's lobby and no password; its HELLO is 0x31).
  Relay mode only; direct host / join stays one player a side (its state packet has one).
- PvP for each of this PC's players (`laonline_v12.c`): what V15.2 kept for Player 1 alone - his buttons
  (`laonline_v12_input`), switch, attacks, cooldowns, hits taken, knockback, stagger - is a `PvpSelf` each, [0]
  Player 1 and [1..7] players 2..8 (`laonline_v12_input_of` from `input.c` `coop_fill`, the stagger there too). Each
  turns his own PvP on with his R3 (Player 1: F6 too); a player 2..8's switch, attack visuals and hits go out on his
  own connection (`laonline_local_push`), so the relay names him the attacker; hits on him come in on it
  (`laonline_local.c` -> `laonline_v12_hit_for`). His rules are unchanged (ranges, cones, delays, cooldowns, the spin,
  bomb arrows, the dash, the stomp, knock-outs and the kill feed). The flash and the rumble stay Player 1's (his
  screen, his pad).
- PvP damage (`pvp_take`) is taken as the game takes its own hits, no longer written (LAS Recomp's review, point 2):
  the difficulty setting (`rt_damage`: Hero Mode doubles it), local co-op's split (`rt_coop_damage_of`, as at the
  game's damage sites: with own hearts a player 2..8's hit comes off his own and knocks him out at 0, for a revive),
  then what is left off the game's hearts as its damage command takes it (main+0xd7c870: PlayerStatus+0x84). His two
  writes (`pvp_set_hp`: PlayerStatus+0x84 and the save's 0x5544) were one value - PlayerStatus is in the live save at
  0x54C0 (`docs/save-format.md`). The 6 s safety net after a knock-out still puts the hearts back if the game's own
  death never starts.
- Z-targeting (`ztarget.c`) for each of this PC's players: his own lock (his ZL; Player 1's L3 / F8 too), his Link
  turned to it (`laonline_link_set_yaw`, his facing `laonline_link_yaw`), his stick circling it
  (`ztarget_stick_of`), his PvP attacks aimed at it, his own reticle (players 2..8 in their co-op colour). What is
  learned (the position in a transform, the yaw convention, the stick directions) is the same for every Link and the
  one camera: still learned from Player 1. This PC's own Links are never targets - they move, so they were taken for
  enemies.
- `laonline_overlay.mm`: no update notice (his updater), no lobby seed keepalive (the lobby), the
  drawn circle for the death-notice portrait (his menu art).
- Kept byte-identical for compatibility with his players: the seed-share strings
  (`game=1.0.0` in `laonline_seed_share.h`, `laonline_seed.cpp`'s schema).

## Where it meets the game and the rest of the build

Every call from the rest of the game into online play is behind `laonline_configured()` (in game code
patches, `g_laonline_on`): without a mode or a relay in `laonline.ini` (or `LAONLINE_MODE`) none of it runs, and the
menu has no Online entry.

`warp.c` (the per-frame ticks, `laonline_level_ready`), `input.c` (PvP / Z-targeting / stagger on
each of this PC's players, F6-F8, `input_hit_rumble`), `bindings.c` (F6-F8 reserved), `miniboss.c`
(puppets: no Appear event, no chamber music; Z-target actors), `souls.c` (`souls_flag_at`, actor
notes), `movie.c` (the ending), `fs.c` / `main.c` / `dispatch.c` / `vulkan/device.c` (his
diagnostic journal), `settings.c/.h` (`k_online`: the online settings, one table row each),
`widescreen.c` (`widescreen_world_to_ndc`), `menu.mm` (the chat box's state, the overlay and the
Z-target reticle drawn), `tracker_overlay.mm` (the map page's transform for his markers),
`vulkan/imgui_glue.cpp` (UI images freed with their context; their generation).

## Against LAS Recomp's review (Peiikai, 2026-11-12)

The rules the review set for this fork's co-op, checked for online play (2026-11-17):

- **Hooks and versions** (its "What works well"): his three sites are in `tools/patches.py` with an
  instruction check each, the hook through `hooks.c`'s table; every address is named, for 1.0.0 and
  1.0.1, in `runtime/game_addresses` (`tools/test_game_addresses.py` checks them and the patch output).
- **Off by default, play identical with it off** (point 4): nothing of online play runs unless a mode or a
  relay is set (and a session runs only once picked on the Online page, or a lobby named) - no journal (`logs/`, `dev-events-log.txt`: his diagnostic journal used to start for everyone,
  read the live save every 6 frames and re-read every save file written), no Z-targeting (ZL used to lock
  on in normal play), F6 to F8 free to bind, no per-animation or per-actor calls (`g_laonline_on` in the
  patches). `tools/coop_tests/online.sh` checks it (no online log line, no journal file).
- **The global player** (point 1): online play never writes `GAME_MGR+0x4b8`.
- **Actors by name**: none - its string checks are level names, UI page names and config keys.
- **Game files**: none shipped; his item icons are drawn in code, his map art loads from the player's own
  files; `shadow_skin.c` (textures copied from the player's own game files) isn't taken.
- **General fixes as their own changes** (point 5): `imgui_glue.cpp` (UI images freed with their context,
  an image leaked when its view failed) is its own commit, for upstream on its own.
- **Tests in the repository** (point 9): `tools/coop_tests/online.sh`, `online_net.sh`, `online_relay.sh`,
  `online_lobby.sh`, `online_pvp.sh`, `online_global.sh`, `online_seed.sh`, `online_flows.sh`, `online_soak.sh`.
- **Mobile** (point 10): the menu / pause check above now runs off Windows too.
- **Licence and credit**: MIT (c) ThyHeroOfTime, `LICENSE-LA-Online` (shipped beside `LICENSE`), the
  README's online paragraph, and the log's `[fork]` line names it.

- **Save safety** (point 2): shared items, levels, rupees, ammo and recovery hearts through the game's
  `AddItem` (above); PvP damage as the game's own hits (above).

The save-safe grants, local players on the relay and the Online page above were tested against a stand-in relay
(`tools/laonline_relay.py`, written from his client: `docs/laonline-protocol.md`), not his relay, which isn't public.
PvP and Z-targeting act for every player of a PC (`tools/coop_tests/online_pvp.sh`).

## Against LA-Online's own relay (2026-11-18)

Its developer sent the relay he runs (Docker: `server.py`, `protocol.py`, `report_backend.py`; V15.2's - not in this
repository). Read in full, then run here through `tools/laonline_real_relay.py` (it unchanged; wrapped only to log the
stand-in's lines, so every online test runs against it: `LAO_RELAY=<its folder>`, `tools/coop_tests/README.md`).

- **A session room's lobby admits only the room's members, by name** (`Directory.admit`). Local players 2..8 were
  turned away from it every few seconds ("Join this session through its room code or public listing first.") - the
  stand-in had let them in. Each now takes a place in the room (`runtime/ui/laonline_lobby.mm`, "this PC's other
  players' places"): joins it by its code with his online name and colour, is Ready when Player 1 is, keeps the place
  with a poll every 5 s (the relay takes Ready from a member unseen for 10 s, the place after 2 minutes), gives it back
  when he leaves the game or Player 1 the room; his token is kept in `laonline-rejoin-local.txt`. His connection
  (`laonline_local.c`) opens only with it (`laonline_lobby_seat_ok`). The relay's 4 places then count every player, as
  its 4-player lobby (`--max-players 4`) does anyway; one who finds it full plays on his PC only (a note on the page).
  The host's Start waits for a player from another PC (this PC's own don't count).
- **PvP frames only after the V12 query** - the relay drops a connection that sends one before. Each local player's
  connection now asks, as Player 1's does.
- **In a Co-op Randomizer lobby a STATE counts only once its sender's world is registered and matches.** Each local
  player's connection registers Player 1's world (`laonline_coop_world_frame`: the same WORLD frame).
- **Local connections open once Player 1's is in the same lobby** (`laonline_relay_on`: the lobby of his last WELCOME),
  so his is the lobby's first - the relay's lobby host, whose world the lobby takes. His connected status alone wasn't
  enough: moving from a session to Global, his old connection still counted while it closed, and his Player 2 was in
  Global before him.
- **A PvP knock-out froze both games** (his V15.2 too): `online_soak.sh` against it knocked Bob out (half a heart a
  hit there - the stand-in gave a quarter, so no one had been), and both games stopped drawing frames. gdb on the
  frozen games (`tools/coop_tests/online_pvp_ko.sh` makes it happen every time: Bob put at half a heart before a hit,
  `LARECOMP_TEST_HP`): Player 1's relay thread handles a message with `g_srv_lock` held, and a death notice's handler
  (`laonline_death_message`) names a PvP knock-out's killer (`laonline_v12_pvp_killer`) through `laonline_v12_peer`,
  which takes `g_srv_lock` - the thread waited on itself, and the game thread on it at its next frame. The relay sends
  a death notice to everyone in the lobby, its sender too: both games. Now `srv_handle` passes the notice on once it
  has let the lock go (the handler reads nothing of the session); that also ends the other order the two locks were
  taken in there (`g_srv_lock` then PvP's `mx`, where the game thread takes `mx` then `g_srv_lock`). The stand-in
  sends death notices to their sender too, as the relay does.
- The stand-in now follows these rules too (room lobbies, 4 per lobby, a name once per lobby and room, PvP's query and
  rate limits, start once all are Ready, members pruned), and names room lobbies as the relay does (`V10-<code>`).

## LAS Recomp's November 2026 develop merged (2026-11-19)

LAS Recomp moved its game code generator to C++ (`runtime/gen/`, run by the app's setup and the `lasgen` tool; the
Python `tools/gen.py` / `tools/patches.py` stay as the reference it is checked against) and its randomizer into the
app (no Python in any build: seeds are a record, `seed.json`, built into game files in memory).

- **Co-op's and online play's patches in the C++ generator** (`runtime/gen/patches.cpp`): the damage split
  (`damage_lsl`, `damage_fixed`), online play's two patches (`laonline_shadow_update`, `laonline_animation`) and Game
  Over hook, every co-op hook site, and the reads of "Link" (`player_read`). The reads' list is a header now,
  `runtime/game_addresses/coop_player_reads_v1_0_1.h` (`COOP_PLAYER_READ(address, word)`), compiled into the
  generator and read by the Python the same: the app makes its game code with no repository files beside it.
  `tools/test_gen_cpp.py`: the C++ output is the Python's, every file, main in both SIMD modes (on Windows gen.py
  writes CRLF: compared without).
- **Seed transfer native** (above): the Python seed helper is gone.
- **Settings**: co-op's and online play's keys in the new `settings_apply` (so `--set` and `--list-settings` know them).
- **Menu sticks**: the new rule (one direction at a time, `ui_stick_dir`) fed by every controller's furthest push.
