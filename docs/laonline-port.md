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
- **Not taken:** his start screen and menus (`launcher.mm`, `menu.mm` tabs, `tropical_ui`,
  `v10_ui`, `settings_look`, `settings_reset`, `session_ui` - the lobby browser), the self-updater
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
  removed (the report sender isn't taken).
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
- `laonline_overlay.mm`: no update notice (his updater), no lobby seed keepalive (the lobby), the
  drawn circle for the death-notice portrait (his menu art).
- Kept byte-identical for compatibility with his players: the seed-share strings
  (`game=1.0.0` in `laonline_seed_share.h`, `laonline_seed.cpp`'s schema).

## Where it meets the game and the rest of the build

Every call from the rest of the game into online play is behind `laonline_configured()` (in game code
patches, `g_laonline_on`): without a mode in `laonline.ini` / `LAONLINE_MODE` none of it runs.

`warp.c` (the per-frame ticks, `laonline_level_ready`), `input.c` (PvP / Z-targeting / stagger on
this PC's Player 1, F6-F8, `input_hit_rumble`), `bindings.c` (F6-F8 reserved), `miniboss.c`
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
- **Off by default, play identical with it off** (point 4): nothing of online play runs unless a mode is
  set - no journal (`logs/`, `dev-events-log.txt`: his diagnostic journal used to start for everyone,
  read the live save every 6 frames and re-read every save file written), no Z-targeting (ZL used to lock
  on in normal play), F6 to F8 free to bind, no per-animation or per-actor calls (`g_laonline_on` in the
  patches). `tools/coop_tests/online.sh` checks it (no online log line, no journal file).
- **The global player** (point 1): online play never writes `GAME_MGR+0x4b8`.
- **Actors by name**: none - its string checks are level names, UI page names and config keys.
- **Game files**: none shipped; his item icons are drawn in code, his map art loads from the player's own
  files; `shadow_skin.c` (textures copied from the player's own game files) isn't taken.
- **General fixes as their own changes** (point 5): `imgui_glue.cpp` (UI images freed with their context,
  an image leaked when its view failed) is its own commit, for upstream on its own.
- **Tests in the repository** (point 9): `tools/coop_tests/online.sh`.
- **Mobile** (point 10): the menu / pause check above now runs off Windows too.
- **Licence and credit**: MIT (c) ThyHeroOfTime, `LICENSE-LA-Online` (shipped beside `LICENSE`), the
  README's online paragraph, and the log's `[fork]` line names it.

Still open, his to decide (asked 2026-11-17):

- **Save safety** (point 2): `laonline_v12.c`'s shared items write the live save directly - the item bits
  (`ITEMS`), bombs / arrows / powder set to 10, the capacity upgrades, the Seashell Sensor, golden leaves,
  the trade item, rupees. `laonline_coop.c` already grants through the game's `AddItem` (`ADD_ITEM`,
  main+0x7e18b0 on 1.0.1: item id, count, index, auto-equip), and story flags go through `flags_put`;
  moving the shared items there too is the change (rupees need another place than the hook: an `AddItem`
  from it was undone a frame later, his note).
- **Several local players per PC**: each PC sends only its Player 1 (`GAME_MGR+0x4b8`); PvP and
  Z-targeting act for him alone.
