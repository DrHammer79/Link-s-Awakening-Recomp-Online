# LA-Online relay protocol (what the client speaks)

The client half of LA-Online's TCP relay protocol ("protocol v1", the Docker relay `server.py` /
`protocol.py`), written down from the client code alone so a stand-in relay can be built for tests.
The real server is not in this repository; every server behaviour below is either read from what
the client sends and accepts, or inferred from it, and marked so:

- **[code]** certain from the client code (what it sends, what it accepts, what it does).
- **[comment]** stated about the server in a client comment, not checkable here.
- **[inferred]** the only or most likely server behaviour that fits the client; a stand-in has to choose.

File references are to `runtime/hle/` unless a path is given (`laonline.c:1103` =
`runtime/hle/laonline.c` line 1103; lines as of the port's first version - they move as the code does), LA-Online core V15.2
(`runtime/laonline_build.h:5`, `LAO_MOD_VERSION "15.2"`). Only the relay mode (`mode = server`,
`M_SERVER`) is covered; the direct `host` / `join` / `echo` modes send a 112-byte `LaoState` UDP
datagram (`laonline.c:95-106`) and never meet the relay.

## 1. Transport and framing

| Item | Value | Source |
|---|---|---|
| Transport | One TCP connection per session, IPv4 (`AF_INET`), `TCP_NODELAY` on | `laonline.c:1077-1099` |
| Address | `server = host:port` in `laonline.ini` (default `192.168.1.145:47620`; port `47620` when no colon). No environment override for the address | `laonline.c:114`, `138`, `1068-1071` |
| Frame | `u16 length` (little-endian) + `body`; `body` = `u8 type` + payload; `length` counts the body (type byte included) | `laonline.c:850`, `1116`, `1269-1275` |
| Byte order | Every integer and float little-endian; floats IEEE-754 binary32; structs packed (`#pragma pack(1)`) | `laonline_session.h:13-21`, `laonline.c:105` |
| Max frame | Client frames stay under 614 bytes (the largest is a 600-byte chat line); it buffers 70000 bytes, so a server frame may be up to 65535 | `laonline.c:1119`, `1254`, `laonline_chat.c:29` |
| Zero-length frame | Client treats `length == 0` as a refusal (same as `S_ERROR`) | `laonline.c:1271` |
| Receive buffer full | Client drops the connection and reconnects | `laonline.c:1264` |

Below, layouts give the **body**: byte 0 is the type, offsets count from it.

### Encodings used in payloads

| Name | Layout | Source |
|---|---|---|
| `str` (`put_str`) | `u8 n` + `n` raw bytes, no terminator; truncated to 255 | `laonline.c:910` |
| `name17` | fixed 17 bytes, NUL-padded (at most 16 used) | `laonline_death.c:51`, `laonline_coop.c:144` |
| player id | `u8`, 0..99 (`LAO_PLAYERS 100`) | `laonline_session.h:10` |
| map id | `lao_name_id(level)`: FNV-1a 32 of the level name, folded `h ^ (h >> 16)` to 16 bits, `0xffff` -> `0xfffe`; `0xffff` = unknown | `laonline_session.h:44-50` |
| room id | `lao_room_id(level, file)`: the same hash of the room file's suffix (basename minus `Level_` prefix and `.leb`) | `laonline_session.h:52-64` |
| seed digest | 32 bytes, SHA-256 of the seed folder's files (`runtime/laonline_seed.cpp:8-45`). Without a seed (Story Co-op) it is SHA-256 of `"LAO15-1.0.0-inventory-v1\0vanilla\0"` = `dcc44950 5bb5f1dc 645c8295 cbe841b3 bae991d3 75650fb4 b4bcf822 9f7564d4` | `runtime/laonline_seed.cpp:10`, `40` |

`WireState` (27 bytes, `laonline_session.h:14-20`; size asserted `laonline.c:864`):

| Off | Type | Field |
|---|---|---|
| 0 | u16 | map id (`0xffff` = not in a level) |
| 2 | u16 | room id |
| 4 | f32 x 3 | x, y, z (Link's position) |
| 16 | f32 | rot_y (yaw from Link's rotation quaternion) |
| 20 | u16 | anim_id (last native animation played; default `0x67`) |
| 22 | f32 | anim_frame = ms since that animation started x 0.06 (>= 0) |
| 26 | u8 | flags: `0x40` animation valid, `0x20` toggles on every new play (`laonline_native.h:9-10`) |

Accepted only if all floats are finite, |x|,|y|,|z| < 100000 and anim_frame >= 0 (`laonline_session.h:65-70`).

`Appearance` (18 bytes, `laonline_appearance.h:12-16`): `u8 version` (must be 1), `u8 mask` (bits 0..3:
custom tunic / hair / skin / shield colour, bit 4: mesh schema verified; no other bits), `u8 rgb[4][3]`
(tunic, hair, skin, shield), `u32 meshes` (bit per mesh of 20; non-zero only with mask bit 4)
(`laonline_appearance.h:23-26`).

`MapAnchor` (17 bytes, `laonline_map_anchor.h:9`): `u16 map, u16 room, f32 world[3], u8 flags` (bits 0..2 only;
with bit 0 set: map != `0xffff`, 0 <= x < 240, 0 <= z < 192, |y| < 1000; with bit 0 clear flags must be 0)
(`laonline_map_anchor.h:11-16`).

`Pickup` (18 bytes, `laonline_coop_wire.h:24-29`): `u64 seq, u16 item, u16 count, i16 index, u16 map, u16 room`
(valid: seq != 0, 1 <= count <= 999, item <= 126, -1 <= index <= 999; `laonline_coop_wire.h:50-53`).

`CoopInventory` (67 bytes, `laonline_coop_wire.h:16-23`): `u32 items, u32 keys, u64 shells, u32 pieces,
u16 containers, u8 songs, u8 upgrades, u8 sensor, u8 leaves, u8 dungeon[9], u8 flags[32]`.

## 2. Connection lifecycle (client side) [code]

1. **Connect** (`laonline.c:1072-1099`). On resolve / connect failure: status "cannot connect", retry in 5 s.
2. **HELLO** is the first frame (`laonline.c:1100-1117`).
3. **Before WELCOME** the client may already send `PING` frames carrying the story query
   (`0x4c414f67`) and, when a Randomizer seed with `tracker.tsv` is loaded, the checks query
   (`0x4c414f59`): the co-op send loop is not gated on the welcome (`laonline.c:1243-1248`,
   `laonline_story.c:36`, `laonline_checks.c:40`). Nothing else goes out before WELCOME.
4. **WELCOME** accepted -> status "joined" (`laonline.c:983-987`), toast "`<own name>` joined the session"
   (`laonline.c:1001`). A malformed WELCOME counts as a refusal (`laonline.c:994-997`).
5. **Capability queries**, once per connection right after WELCOME: ten `PING`s with fixed nonces
   (Global: three) (`laonline.c:1145-1171`). See section 3.
6. **Steady state**, loop at least every 50 ms (`select` timeout, `laonline.c:1260`): STATE (+ VITALS,
   PROFILE, APPEARANCE, MAP_ANCHOR, LOCATION piggy-backed after it), up to 16 co-op / death frames, up to
   16 V12 frames, one chat frame, then read (`laonline.c:1129-1278`).
7. **Keepalive** `PING` every 15 s after WELCOME, payload = low 32 bits of the client's ms clock
   (`laonline.c:1172-1177`).
8. **Disconnect**: socket closed or error -> every joined peer toasts "`<name>` left", all session state is
   cleared, reconnect after 3 s with a fresh HELLO (`laonline.c:1280-1284`, `887-902`).
   **Refusal** (`S_ERROR`, malformed WELCOME, zero-length frame): the connection is closed and the client
   does **not** reconnect until the player presses retry (`laonline_connection_retry`), then waits 3 s
   (`laonline.c:1283-1284`).

Launch modes (`g_launch_online`, `laonline.c:75-83`): 1 = Story Co-op, 2 = Co-op Randomizer (a seed folder is
active), 3 = Global. In this build only 1 and 2 are reachable (`launch_from_config`, `laonline.c:1377-1385`);
nothing calls `laonline_launch_mode(3)`, but the Global code paths are documented for completeness.

## 3. Capability negotiation [code]

Newer frame types are only sent after the relay proves it knows them: the client sends `PING` (0x07) with a
32-bit nonce and waits for `PONG` (0x89) carrying the matching reply value. Every reply is nonce + 1.
Unrecognised `PONG` values are ignored, so an old relay that just echoes the nonce enables nothing.
Bytes on the wire: nonce `0x4c414f51` is `51 4f 41 4c`.

| Query (C->S) | Reply (S->C) | Enables | Source |
|---|---|---|---|
| `0x4c414f51` CAP | `0x4c414f52` | APPEARANCE 0x0B out, LOCATION 0x0A out | `laonline_appearance.h:6-7`, `laonline.c:941-944`, `1210`, `1233` |
| `0x4c414f53` COOP | `0x4c414f54` | co-op transport: COOP_SETUP, then pickups / inventory / rewards / death. **Resets** all co-op, death, checks and story capability state (story and checks re-query) | `laonline_coop_wire.h:7-8`, `laonline.c:937`, `laonline_coop.c:153-161` |
| `0x4c414f55` MAP | `0x4c414f56` | MAP_ANCHOR 0x0F out | `laonline_map_anchor.h:4-5`, `laonline.c:936` |
| `0x4c414f57` CHAT | `0x4c414f58` | CHAT 0x10 / TYPING 0x11 out, chat UI | `laonline_chat.h:4-5`, `laonline.c:935` |
| `0x4c414f59` CHECK | `0x4c414f5a` | CHECK 0x12 out | `laonline_checks.h:4-5`, `laonline.c:933` |
| `0x4c414f5b` REWARD | `0x4c414f5c` | REWARD 0x13 out (receipts) | `laonline_reward_rules.h:29-30`, `laonline.c:940` |
| `0x4c414f5d` WORLD | `0x4c414f5e` | WORLD 0x14 out | `laonline.c:931`, `1166` |
| `0x4c414f5f` EQUIPMENT | `0x4c414f60` | equipment items as receipts | `laonline.c:939`, `1162` |
| `0x4c414f61` DEATH | `0x4c414f62` | DEATH 0x19 out, death notices in | `laonline_death.h:12-13`, `laonline.c:934` |
| `0x4c414f63` ADVANCED | `0x4c414f64` | instruments / songs as receipts, ownership bootstrap | `laonline.c:938`, `1164`, `laonline_coop.c:85` |
| `0x4c414f67` STORY | `0x4c414f68` | STORY 0x1A out | `laonline_story.h:6-7`, `laonline.c:932` |
| `0x4c414f70` V12 | `0x4c414f71` | PvP state frames | `laonline_v12.h:8-9`, `laonline_v12.c:541` |
| (none) | `0x4c414f72` PVP135 | PvP hit damage byte, item-attack visuals (kind 2) | `laonline_v12.h:10`, `laonline_v12.c:542` |
| (none) | `0x4c414f73` PVPKO | PvP knock-outs (kind 3), server hit markers | `laonline_v12.h:11`, `laonline_v12.c:545` |
| (none) | `0x4c414f74` GLOBALEND | "saved Koholint" (kind 5) | `laonline_v12.h:12`, `laonline_v12.c:544` |
| (none) | `0x4c414f75` PVPCOOP | PvP in co-op lobbies, weapon byte on hits, kills/losses (kind 6) | `laonline_v12.h:13`, `laonline_v12.c:543` |

Co-op (modes 1/2) sends the queries in this order: CAP, COOP, MAP, CHAT, REWARD, DEATH, EQUIPMENT, ADVANCED,
WORLD, V12 (`laonline.c:1148-1171`), plus STORY (and CHECK) from the co-op loop. Global sends CAP, CHAT, V12
only (`laonline.c:1145-1147`). The last four replies have no query of their own; the relay must send them
unprompted, most likely together with the V12 reply [inferred]. Replies are applied whether or not the
WELCOME came first (`laonline.c:929-945`), but the V12 flags are cleared by WELCOME
(`laonline_v12.c:211-219`, `511-512`).

## 4. Client -> server messages

| Type | Name | Body after the type byte | When sent | Gate | Source |
|---|---|---|---|---|---|
| `0x01` / `0x31` | HELLO | `u16 1` (protocol version), `str name`, `u8 r,g,b`, `str lobby`, `str password`, `'L' 'A' 'O' 'V'`, `str mod_version` | first frame of every connection | - | `laonline.c:1100-1117` |
| `0x02` | STATE | `WireState` (27) | every send-loop pass with a fresh sample (game samples every 3rd frame, ~20/s) | WELCOME accepted; co-op: world ready (5.3) | `laonline.c:1179-1183`, `1491-1520` |
| `0x03` | FLAG_SET | `u32 flag` | progress scan every 20 frames: each flag once per connection; everything re-offered every ~30 s | co-op modes only, after WELCOME | `laonline_v12.c:177-181`, `680-684`, `784-812`, `202` |
| `0x04` | ITEM_GET | `u16 key, i16 value` | when value > the last value sent for that key; re-offered every ~30 s | co-op modes only, after WELCOME | `laonline_v12.c:182-187`, `796-822` |
| `0x07` | PING | `u32 value` | capability nonces (section 3); keepalive every 15 s | - | `laonline.c:1145-1177` |
| `0x08` | PROFILE | `u8 r,g,b` | right after a STATE, when the tunic colour differs from the last sent (initially the HELLO colour) | valid local appearance | `laonline.c:1199-1209` |
| `0x09` | VITALS | `u8 health, u8 max_health` (quarter hearts) | right after a STATE, when either changed and max != 0 | - | `laonline.c:1184-1193` |
| `0x0A` | LOCATION | `u8 n` (1..32), `n` bytes area name | right after a STATE, when it changed | CAP reply (`LAO_V1_LOCATION_SUPPORTED` is 0: "Existing Docker relay v1 rejects C_LOCATION") | `laonline.c:859`, `1229-1240` |
| `0x0B` | APPEARANCE | `Appearance` (18) | right after a STATE: first time, then on change | CAP reply | `laonline.c:1210-1216` |
| `0x0C` | COOP_SETUP | `seed digest` (32) | once per connection | COOP reply, seed known, and world ready when required | `laonline_coop.c:224-225` |
| `0x0D` | PICKUP | `Pickup` (18) | each local pickup that is not a receipt item (resources, keys, hearts...) | COOP_READY | `laonline_coop.c:191-218`, `235-237` |
| `0x0E` | INVENTORY | `CoopInventory` (67) | sampled every 30 frames, sent when changed | COOP_READY | `laonline_coop.c:238-240`, `361-362` |
| `0x0F` | MAP_ANCHOR | `MapAnchor` (17) | right after a STATE: first time, then on change | MAP reply | `laonline.c:1222-1228` |
| `0x10` | CHAT | `u64 seq, u16 n, n bytes UTF-8` | player sends a line (queue of 8) | CHAT reply and WELCOME | `laonline_chat.c:47-50` |
| `0x11` | TYPING | `u8 on` (0/1) | on change, and every 1.5 s while typing | CHAT reply | `laonline_chat.c:46`, `51` |
| `0x12` | CHECK | kind 0: `u8 0` (hello) / kind 1: `u8 1, u16 flag` | hello once, then each newly collected tracker check flag once per connection | CHECK reply, COOP_READY, seed `tracker.tsv` | `laonline_checks.c:38-48` |
| `0x13` | REWARD | kind 0: `u8 0` (request replay) / kind 1: `u8 1, u16 item, i16 index, u16 map, u16 room` (publish) / kind 2: `u8 2, u16 item, i16 index, u8 status` (ack) | see 5.4 | REWARD reply, COOP_READY | `laonline_coop.c:226-234` |
| `0x14` | WORLD | `u8 required, seed digest (32), str seed_id` | once per connection | WORLD reply, seed known | `laonline_coop.c:222-223` |
| `0x15` | SEED_META | separate short connection, section 6 | launcher only (no caller in this build) | - | `laonline.c:1011-1061` |
| `0x19` | DEATH | `u64 seq, u16 map, u16 room, u8 cause` (cause always 0) | the game submits its own Game Over | COOP and DEATH replies | `laonline_death.c:32-47` |
| `0x1A` | STORY | `u8 1, seed digest (32), u8 mask, u8 0` | first time after READY, then when the mask grows | STORY reply, COOP_READY | `laonline_story.c:29-39` |
| `0x1C` | PVP | `u8 kind` + kind fields (5.6) | PvP events | V12 reply (+ others per kind) | `laonline_v12.c:190-209` |

Type numbers not used by the client: `0x05`, `0x06`, `0x16`-`0x18`, `0x1B`, `0x1D`+ (other than `0x31`).

### 4.1 HELLO details [code]

- `name`: up to 16 bytes (`laonline.ini` `name`, default `Player`, or the launcher override) (`laonline.c:113-123`).
- `r,g,b`: tunic colour (default `2f a8 3c`) (`laonline.c:1106-1108`).
- `lobby`: up to 32 bytes. Story Co-op: `lobby` from the ini (default `default`). Co-op Randomizer: the first 26
  bytes of it + `.rando`. Global: `V10-GLOBAL` or `V10-GLOBAL-RANDO` (`laonline.c:1109`, `laonline_v12.c:1257`).
- `password`: up to 64 bytes (32 from the ini); always empty in Global (`laonline.c:1111`).
- Type `0x31` instead of `0x01` in Global (`laonline.c:1103`).
- `LAOV` + `str "15.2"`: "older relays ignore trailing bytes. A V14.1+ relay only lets the same or a newer version
  play" [comment] (`laonline.c:1112-1115`).

Example, name `Alice`, lobby `default`, no password:
`1e 00 | 01 | 01 00 | 05 41 6c 69 63 65 | 2f a8 3c | 07 64 65 66 61 75 6c 74 | 00 | 4c 41 4f 56 | 04 31 35 2e 32`.

## 5. Server -> client messages

All are parsed in `srv_handle` (`laonline.c:920-1004`); the roster ones in `lao_session_message`
(`laonline_session.h:92-176`). "Rejected" means silently ignored unless stated. A message whose byte 1 is a
sender id is dropped when that id is the client's own (its own id is never "joined"), except `0x94` / `0x95`
(accepted from the own id) and `0xA0` (not checked: an echoed death notice would be shown to its own sender).
`0x85` / `0x86` ignore the `from` byte.

| Type | Name | Body after the type byte | Client accepts when | Client reaction | Source |
|---|---|---|---|---|---|
| `0x81` | WELCOME | `u8 your_id, u8 count`, `count` x (`u8 id, u8 n` 1..16, `n` name bytes, `u8 r,g,b`), `u32 flag_count`, `flag_count` x `u32 flag`, `u16 item_count`, `item_count` x (`u16 key, i16 value`) | your_id < 100, count < 100, each id < 100, not your_id, no repeats, name 1..16; the frame ends **exactly** after the items | session reset, own id = `your_id`, listed peers joined (no toasts), status joined, toast "`<own name>` joined the session"; flags / items applied as received progress (5.5). Otherwise: refused | `laonline_session.h:94-118`, `laonline.c:983-1001`, `laonline_v12.c:511-534` |
| `0x82` | PLAYER_JOIN | `u8 id, u8 n` (1..16), `n` name bytes, `u8 r,g,b`; nothing after | connected, id < 100, not own | peer joined; toast "`<name>` joined" only if the id was not joined already (a repeat only updates name / colour) | `laonline_session.h:120-131`, `laonline.c:974-977`, `1002` |
| `0x83` | PLAYER_LEAVE | `u8 id` (body exactly 2) | connected, id < 100, not own | peer cleared; toast "`<name>` left"; chat, pickup sequence, death sequence, PvP state for that id reset | `laonline_session.h:132-137`, `laonline.c:927-928`, `950`, `960`, `1003`, `laonline_v12.c:572-573` |
| `0x84` | STATE | `u8 sender_id, WireState` (body exactly 29) | connected, sender joined, not own, valid floats | stored; the puppet appears once 2 states with the same map arrived <= 1.5 s apart, while fresh (<= 1.5 s) and on the local map | `laonline_session.h:138-148`, `71-80` |
| `0x85` | FLAG_SET | `u8 from, u32 flag` (body 6) | always (even before WELCOME) | progress flag queued (5.5) | `laonline_v12.c:535-536` |
| `0x86` | ITEM_GET | `u8 from, u16 key, i16 value` (body 6) | key 0x7100..0x7DFF, value > 0 | per-key maximum kept (5.5) | `laonline_v12.c:537-538`, `315-319` |
| `0x89` | PONG | `u32 value` (body 5) | always | capability flags (section 3); keepalive echoes ignored | `laonline.c:929-945`, `laonline_v12.c:539-545` |
| `0x8A` | PROFILE | `u8 id, u8 r,g,b` (body 5) | id joined | peer tunic / label colour | `laonline_session.h:156-159` |
| `0x8B` | VITALS | `u8 id, u8 health, u8 max` (body 4) | joined, not own, max != 0, health <= max | peer hearts on its name tag | `laonline_session.h:160-166` |
| `0x8C` | LOCATION | `u8 id, u8 n` (1..32), `n` bytes (body 3+n) | joined, not own | peer's area name | `laonline_session.h:167-174` |
| `0x8D` | APPEARANCE | `u8 id, Appearance` (body 20) | joined, not own, valid | peer colours / meshes | `laonline_session.h:149-155` |
| `0x8F` | ERROR | `u8 code` (not read), `u8 n`, `n` bytes message | always | message (<= 199 bytes) shown as a toast and as the launcher's refusal text; status refused, disconnect, no auto-reconnect | `laonline.c:988-1000`, `1278`, `1283` |
| `0x90` | COOP_READY | `u8 status, seed digest (32)` (body 34) | status 1 and digest == own | seed sharing active: pickups / inventory / rewards / checks / story start | `laonline_coop.c:247-248` |
| `0x91` | COOP_EVENT | `u8 sender_id, Pickup` (body 20) | READY, sender < **8** and joined, valid, seq > last seq from that sender | teammate pickup: notification, shared resources / keys / hearts granted | `laonline_coop.c:252-257`, `317-359`, `laonline.c:955-958` |
| `0x92` | COOP_SNAPSHOT | `CoopInventory` (body 68) | READY, valid | observed only (one-time "catching up" notice, never applied) | `laonline_coop.c:249-251`, `302-315` |
| `0x93` | MAP_ANCHOR | `u8 id, MapAnchor` (body 19) | id joined, valid | peer's map marker | `laonline.c:951-954` |
| `0x94` | CHAT | `u8 sender_id, u64 seq, u16 n, n bytes UTF-8` (body 12+n) | sender joined **or own id**; seq > last from that sender; valid text | chat bubble over the sender + feed line "`<name>`: text" | `laonline.c:946-949`, `laonline_chat.c:54-63` |
| `0x95` | TYPING | `u8 sender_id, u8 on` (body 3, on 0/1) | sender joined or own | "..." over the sender (never over yourself) | `laonline_chat.c:56`, `68` |
| `0x96` | CHECKS | `u8 count`, `count` x `u16 flag` (body 2+2*count) | CHECK reply and own hello sent; flags in this seed's allowlist | tracker marks those checks as team-collected (never writes game flags) | `laonline_checks.c:49-54` |
| `0x97` | REWARD | `seed digest (32), u16 item, i16 index, u16 map, u16 room, name17 finder` (body 58; **no sender id**) | REWARD reply, READY, digest == own, item/index valid | native grant if not owned yet, then an ack (0x13 kind 2) | `laonline_coop.c:141-148`, `108-140` |
| `0x98` | WORLD_RESULT | `u8 status, seed digest (32)` (body 34) | own WORLD sent, digest == own | status 1: world ready; 2: "Active session mode differs", 3: "different canonical Seed ID", 4: "world checksum differs", other: "Co-op world mismatch" | `laonline_coop.c:70-73` |
| `0x9B` | WORLD_REJECT_DETAIL | `u8 (unread), host digest (32), u8 n` (<= 95), `n` bytes host seed id (body 35+n) | - | diagnostics log only | `laonline_coop.c:65-69` |
| `0xA0` | DEATH | `u8 sender_id, u64 seq, u16 map, u16 room, u8 cause, name17` (body 32) | DEATH reply, sender < **4**, cause 0, seq != 0 and > last from that sender, name valid UTF-8 without control characters | "`<name>` was defeated ... At: `<area>`" (or "knocked out in PvP by ...") | `laonline_death.c:48-75` |
| `0xA1` | STORY | `u8 1, seed digest (32), u8 mask, u8 0` (body 36; mask bits 0..6 only) | STORY reply, digest == own | entrance-unlock flags 100-103, 105-107 set for each mask bit | `laonline_story.c:40-43`, `16-28`, `laonline_story.h:10-12` |
| `0xA2` | PVP | `u8 kind` + kind fields (5.6) | per kind | PvP | `laonline_v12.c:546-571` |
| `0x99` / `0x9A` / `0x9C` | SEED_META replies | only on the separate seed connection (section 6) | | | `laonline.c:1040-1049` |

Server types the client never parses: `0x87`, `0x88`, `0x8E`, `0x9D`-`0x9F`, `0xA3`+. Any type the client does not
know is ignored without effect (`srv_handle` matches nothing, `lao_session_message` returns 0) [code].

In Global (mode 3) everything except `0x81-0x84`, `0x89-0x8D`, `0x8F`, `0x94`, `0x95` is dropped after the V12
handler (so `0x85`, `0x86`, `0xA2` still work there) (`laonline.c:922-923`).

### 5.1 Ids, joining and leaving [code]

- The client learns its own id only from WELCOME byte 1, and needs it for chat (`laonline_chat.c:45`, `47`).
- A player is "joined" only through WELCOME's list or PLAYER_JOIN. STATE, VITALS, LOCATION, APPEARANCE, PROFILE,
  MAP_ANCHOR, CHAT and COOP_EVENT for an id that is not joined are dropped, so the join must reach the client first.
- STATE carries the sender's id at byte 1; nothing else in the body identifies the sender.
- "X joined" comes only from PLAYER_JOIN for a not-yet-joined id; players listed in WELCOME get no toast.
- Per-sender sequence numbers (chat `seq`, pickup `seq`, death `seq`) are process-lifetime counters on the sender
  and are reset on the receiver by PLAYER_LEAVE (`laonline_chat.c:64`, `laonline_coop.c:52`, `laonline_death.c:25`).

### 5.2 Minimal sequences

First player in an empty lobby, given id 0:
`09 00 | 81 00 00 | 00 00 00 00 | 00 00` (WELCOME: own id 0, no peers, no flags, no items).

Second player (Bob) gets id 1 while Alice (id 0, tunic `2f a8 3c`) is present:
to Bob `13 00 | 81 01 01 | 00 05 41 6c 69 63 65 2f a8 3c | 00 00 00 00 | 00 00`;
to Alice `09 00 | 82 01 03 42 6f 62 rr gg bb`.
Then each client's STATE `1c 00 02 <27 bytes>` goes to the other as `1d 00 84 <sender id> <27 bytes>`.

### 5.3 Co-op world gate [code]

When the setting `laonline_play_mode == 2` (`laonline_coop.c:174`), the client sends **no STATE**, no
COOP_SETUP and no V12 progress until WORLD_RESULT status 1 arrives (`laonline.c:1179`, `laonline_coop.c:63`,
`224`, `laonline_v12.c:676`): the relay must answer the WORLD query and the WORLD frame. With other settings the
WORLD frame is still sent (if the WORLD reply came) but nothing waits for the result.

### 5.4 Co-op extension details (V15) [code]

- **Seed handshake**: COOP reply -> client sends COOP_SETUP (seed digest) -> relay answers COOP_READY
  `[1][same digest]`. Everything in this list waits for that READY (`laonline_coop.c:224-243`, `247`).
- **Pickups**: non-receipt pickups (resources, small keys, hearts...) go out as PICKUP with an increasing `seq`;
  teammates' arrive as COOP_EVENT with the sender id prepended (`laonline_coop.c:206`, `252`).
- **Inventory**: INVENTORY whenever the sampled inventory changes; the relay's COOP_SNAPSHOT is observed only.
  `lao_coop_merge` (bytewise OR) suggests the relay ORs all members' inventories [inferred] (`laonline_coop_wire.h:39-42`).
- **Rewards (receipts)**: kind 1 publishes an owned unique item (`item, index, map, room`) after the local
  AddItem is read back (`laonline_coop.c:108-115`); kind 0 asks for a replay, sent when the REWARD /
  EQUIPMENT / ADVANCED reply arrives and every 300 frames (~5 s) while ready (`laonline_coop.c:85-88`, `284`);
  kind 2 acknowledges each received REWARD with status 1 = granted now, 2 = already owned, 0 = grant failed
  (`laonline_coop.c:132`). With the ADVANCED reply the client also re-publishes everything it owns
  (`laonline_coop.c:285-289`). "the relay remains the durable journal" [comment] (`laonline_coop.c:283`).
- **Checks**: CHECK hello, then one CHECK kind 1 per collected tracker flag; the relay's CHECKS frames list flags
  (`laonline_checks.c:38-53`).
- **Story**: STORY `[1][digest][mask][0]`, mask bit i = entrance flag `{100,101,102,103,105,106,107}[i]`;
  received masks are ORed in (`laonline_story.c:37`, `42`).
- **Death**: DEATH on the game's own Game Over; the relay's DEATH frame adds the sender id and its name
  (`laonline_death.c:45`, `49-51`).

### 5.5 V12 shared progress [code]

FLAG_SET flags: the 307 story flags of `laonline_v12_flags.h` (< 2048), boss / mini-boss soul flags of the seed
(extended, >= 2048, < 65536 accepted), and "world" flags (pots, bushes, pickups, chests, dungeon `KeyGet_*`;
< 8192) (`laonline_v12.c:309-314`, `786-812`). The receiver applies only flags in its own lists.

ITEM_GET keys (`laonline_v12.c:46-51`): `0x7100+bit` item bits 2..15, `0x7120+bit` upgrades 3..5, `0x7130` sensor,
`0x7140+bit` golden leaves 0..4 (value 1 each); `0x7150` trading step 1..14 (Story Co-op); `0x7160+f` level 1/2
of family f (0 bracelet, 1 shield, 2 sword); `0x7200 + f*1024 + place` value 1, place =
`(map*131 + room*7 + 1) % 1021` (`laonline_v12.c:64`). The client keeps the per-key maximum
(`laonline_v12.c:315-319`). "the relay keeps the highest value per key and replays it to late joiners in
WELCOME" [comment] (`laonline_v12.c:46-47`); "the relay ignores repeats" [comment] (`laonline_v12.c:680`).

WELCOME's tail carries the stored flags and items; the client applies them like FLAG_SET / ITEM_GET
(`laonline_v12.c:520-533`).

### 5.6 PvP (0x1C client, 0xA2 server) [code]

Client kinds (`laonline_v12.c`):

| Kind | Body after `1C` | When | Gate | Source |
|---|---|---|---|---|
| 0 state | `00, u8 on` | PvP toggled (and once after each WELCOME) | V12 reply; co-op also PVPCOOP | `195-198` |
| 1 hit | `01, u8 target` (old relay) / `01, target, u8 quarters` (PVP135) / `01, target, quarters, u8 weapon` (PVPCOOP) | a swing / item / dash reaches a player with PvP on | queued | `1174-1175`, `1195-1196`, `1209-1215` |
| 2 visual | `02, u8 weapon, u16 map, f32 x,y,z, i8 dir_x*100, i8 dir_z*100` | each attack, for others to draw | PVP135 | `1123-1126` |
| 3 knock-out | `03, u8 killer_id` | a hit took this player to 0 hearts | PVPKO | `1045-1046` |
| 5 ending | `05, u8 randomizer` | this player reached the ending (Global) | GLOBALEND | `1014-1018` |

Weapon bytes: 2 bombs, 3 bow, 4 hookshot, 5 boomerang, 6 magic rod, 8 powder, 100 sword, 101 spin, 102 bomb
arrow, 103 boots dash, 104 feather stomp (`laonline_v12.c:362-368`, `870`). Quarters are 1..8. All PvP frames go
out only in Global, or in co-op with PVPCOOP (`laonline_v12.c:195`, `202`).

Server kinds, checked in this order (`laonline_v12.c:546-571`):

| Kind | Body after `A2` (exact size) | Meaning |
|---|---|---|
| 2 | `02, u8 from, u8 weapon, u16 map, f32 x,y,z, i8 dx, i8 dz` (20) | someone's attack to draw |
| 6 | `06, u16 kills, u16 losses` (6) | your server-side totals (shown in Global) |
| 5 | `05, u8 you, u8 randomizer, u8 n, name` (>= 6; 0 < n < 32) | "`<name>` saved Koholint!" (you = 1: it was you) |
| 3 | `03, u8 quote, u16 killer_vs_victim, u16 victim_vs_killer, u8 you, u8 kn, killer, u8 vn, victim [, u8 weapon]` (>= 12; names 1..31) | kill feed; you = 1 killer, 2 victim, 0 bystander; quote seeds the shared line |
| 4 | `04, u8 attacker, u8 victim, u8 quarters` (5) | server-confirmed hit marker |
| 0 | `00, u8 id, u8 on` (4) | that player's PvP state |
| 1 | `01, u8 attacker, u8 quarters` (4) | you were hit (damage clamped 1..8; lethal only with PVPKO) |

## 6. Seed metadata side connection (0x15) [code]

A short, separate TCP connection to the same server (4 s connect, 5 s send / receive timeouts), one frame each way,
then closed (`laonline.c:1011-1055`). Only the launcher calls it (`laonline_seed_metadata*`,
`laonline_seed_descriptor*`, `laonline.c:1057-1061`), and no caller exists in this build.

Request body: `15, u8 mode, str name, str lobby, str password` and, when registering a folder,
`seed digest (32), str seed_id [, str display_name]` (`laonline.c:1036`). Lobby is always the 26-byte-capped
lobby + `.rando` (`laonline.c:1016`). Modes: 1 register seed, 4 register with display name, 2 ask for the
lobby's seed, 3 ask with display name (0 when neither; unused).

Replies the client accepts (`laonline.c:1038-1049`), first frame only, body 3..512 bytes:

- register (1/4): `99, 01, u8 n (<= 95), n bytes seed_id`, exact. Anything else = "Seed registration was refused".
- describe (2): `9A, 01, digest (32), u8 n (<= 95), seed_id, u8 total, u8 ready`, exact.
- describe (3): `9C, 01, digest (32), u8 n, seed_id, u8 total, u8 ready, u8 m (<= 90), m bytes display name`, exact.

## 6a. Session service (0x30) [code]

The rooms the menu's Online page hosts, lists and joins (`runtime/ui/laonline_lobby.mm`, from V15.2's
`runtime/ui/session_ui.mm`). Like section 6: a short separate TCP connection (3 s connect / send / receive), one
frame each way (`u16 length, 30, request`), the answer's opcode `B0`, then closed. A room's players then connect
as in section 2 with the lobby and password the answer gives (HELLO; a Co-op Randomizer room's lobby ends in
`.rando`, which the client strips and `laonline_relay_login` adds back for launch mode 2).

Request: `01, u8 action, str code, str token, str name, str build (LAO_ROOM_BUILD), u8 mode (1 co-op, 2 co-op
randomizer), str world (the seed's world checksum, hex), str seed (its ID), str title, str description, u8 ready,
u8 r, g, b (tunic), u8 r, g, b (hair), str search, u8 filter mode (0 all, 1, 2), u8 sort (0 most players, 1 fewest,
2 newest, 3 title), u16 page`. Strings are `u8 length` + bytes.

| Action | Meaning |
|---|---|
| 0 | list public rooms (8 a page, the request's filter / sort / search on the host's name) |
| 1 / 2 | host a private / public room (the request's mode, world, seed, title) |
| 3 | join the room `code` (the rejoin token from `laonline-rejoin.txt` when the token is empty: a player whose game closed takes their place back) |
| 4 | poll this player's room (every 2 s) |
| 5 | ready (the request's `ready`) |
| 6 | start (host only) |
| 7 | leave |
| 8 | a new code (host, before anyone joins) |
| 9 | rename (the request's name) |

Answer: `u8 ok, str message`; when ok also `str token (32 hex), str password, str lobby, u16 total, u16 page,
u8 count (<= 8)`, then each room: `str code, str title, str description, str host, u8 mode, u8 public, u8 started,
u8 capacity (4), str build, str world, str seed, u8 players (<= 4)`, each `str name, u8 r, g, b, u8 r, g, b, u8 ready,
u8 host`. The frame ends exactly there. Action 0 answers with the page of rooms; the others with this player's
room. The client joins only rooms of its own build; a started room is joined straight away (V13 join-in-progress).
[inferred] (the stand-in, `tools/laonline_relay.py`): 4 places, a name already in the room gets " 2", the host
leaving hands the room to the next player, an empty room is gone; lobby `room-<code>`, a random password.

## 6b. Leaderboards (0x32) and Global's player count (0x33) [code]

Both like section 6a: a connection of their own, one frame each way, the answer's opcode + 0x80
(`runtime/ui/laonline_lobby.mm`, from V15.2's `session_ui.mm`).

- **0x33** request `01`; answer `u8 ok, u16 players in Global, u16 capacity` - capacity must be 100 and players no more
  than it, the frame exactly that long; polled every 2 s while the Global tab is shown.
- **0x32 submit** `01, 01, str run id, str players (commas between co-op players), u8 mode (0 vanilla, 1 co-op, 2
  co-op randomizer), u32 clear time (s), u32 rupees, u32 deaths, u8 1`; answer `u8 ok, str message`.
- **0x32 list (V12.5)** `02, 00, u8 mode (3 all), u8 sort (0 best time, 1 highest score, 2 most rupees, 3 most
  deaths), u8 player count (0 all, 1..4), u16 page`; answer `u8 ok, str message`, when ok `u16 total, u8 count (<= 50)`,
  each run `str players, u8 mode, u8 player count, u32 seconds, u32 rupees, u32 deaths, u32 score`, the frame exactly
  that long. Not ok: the client asks the older form, **0x32 list (V12)** `01, 00, u8 mode, u8 sort (0 time, 1 rupees),
  u16 page`, answered `u8 ok, str message, u16 total, u8 count (<= 8)`, each `str players, u8 mode, u32 seconds, u32
  rupees, u32 deaths, u8 (unused)`; the score is then worked out by the client: 100,000 - seconds - 250 x deaths +
  rupees, at least 0.

## 7. Server behaviour the client expects

| # | Behaviour | Basis |
|---|---|---|
| 1 | Accepts HELLO first; answers with WELCOME (or ERROR). The client sends no STATE before an accepted WELCOME | [code] `laonline.c:1179` |
| 2 | Assigns each connection a player id 0..99, unique in the lobby, sent as WELCOME byte 1 | [code] |
| 3 | Uses low ids: the lowest free one from 0. Pickup events are only accepted from ids < 8 and death notices from ids < 4 (`laonline_coop.c:252`, `laonline_death.c:49`), and co-op lobbies are probably capped at 4 players; Global "holds up to 100 players" (`laonline_v12.c:342`) | [inferred] |
| 4 | WELCOME lists every other player already in the lobby (id, name, colour from their HELLO or latest PROFILE) | [code] format; [inferred] contents |
| 5 | Sends PLAYER_JOIN for the newcomer to everyone already there; extra JOINs for present players are harmless | [inferred]; harmlessness [code] `laonline_session.h:124-128` |
| 6 | Sends PLAYER_LEAVE to the others when a connection closes, before reusing that id | [inferred] |
| 7 | Relays each client's STATE to the other lobby members as `84 + sender id + 27 bytes`, not back to the sender (an echo is rejected anyway) | [code] format; [inferred] routing |
| 8 | Same pattern for VITALS -> `8B`, LOCATION -> `8C`, APPEARANCE -> `8D`, PROFILE -> `8A`, MAP_ANCHOR -> `93`, PICKUP -> `91`, TYPING -> `95`, FLAG_SET -> `85`, ITEM_GET -> `86`, DEATH -> `A0` (+ name17), PvP state -> `A2` kind 0 | [code] formats; [inferred] routing |
| 9 | Echoes CHAT to the sender too: the client never adds its own lines to the feed or its own bubble, and accepts `94` from its own id | [inferred, strong] `laonline_chat.c:47`, `laonline.c:947` |
| 10 | Remembers each player's last VITALS, LOCATION, APPEARANCE, MAP_ANCHOR and PvP state and sends them to newcomers: the client sends these only on change | [inferred, strong] `laonline.c:1188`, `1210`, `1224`, `1233`; `laonline_v12.c:196` |
| 11 | Answers PING with PONG. For known nonces: nonce + 1 (section 3). For keepalive stamps: anything (echo is fine) | [code] reply values; [inferred] echo |
| 12 | Answers the V12 query with the extra replies `...72`-`...75` for the features it supports | [inferred] |
| 13 | Keeps, per lobby, every FLAG_SET flag and the highest ITEM_GET value per key; replays them in WELCOME; broadcasts only new flags / higher values | [comment] `laonline_v12.c:46-47`, `680` |
| 14 | Lobby = HELLO lobby string; players with the same string share a session. Password must match the lobby's; a mismatch, a full lobby or an old client version is answered with `8F [code] [n] [message]`, then the connection is closed | [inferred]; version check [comment] `laonline.c:1112-1113`; ERROR handling [code] |
| 15 | Treats `0x31` HELLO as a Global login (lobby `V10-GLOBAL` / `V10-GLOBAL-RANDO`, no password) | [inferred] |
| 16 | Old v1 relay: an unknown client type or PvP kind gets the client dropped ("Existing Docker relay v1 rejects C_LOCATION", "an older relay would drop us for kind 5"); that is why every newer frame waits for a capability reply | [comment] `laonline.c:859`, `laonline_v12.c:1014` |
| 17 | COOP_SETUP -> COOP_READY `[1][digest]` (other status or no reply when the lobby's seeds differ) | [code] accept rule; [inferred] mismatch handling |
| 18 | WORLD -> WORLD_RESULT with status 1..4 (meanings in section 5) and, on rejection, optionally WORLD_REJECT_DETAIL with the active host's digest and seed id | [code] meanings; [inferred] when |
| 19 | REWARD kind 1: stores (lobby, digest, item, index) and sends `97` with the publisher's digest and name to the others; kind 0: replays every stored reward of that digest to the requester; kind 2: recorded or ignored | [inferred] |
| 20 | INVENTORY: merges (OR) and sends COOP_SNAPSHOT to the lobby | [inferred] |
| 21 | CHECK kind 1: stores the flag and sends `96 [1][flag]` to the lobby; CHECK hello: sends the stored list `96 [n][flags]` | [inferred] |
| 22 | STORY: ORs the mask per digest and sends `A1 [1][digest][mask][0]` to the lobby | [inferred] |
| 23 | PvP hit (kind 1) -> `A2 01 [attacker][quarters]` to the target; with PVPKO also `A2 04 [attacker][victim][quarters]` (the client then stops drawing its own markers, `laonline_v12.c:1175`); KO (kind 3) -> `A2 03 ...` to everyone with you = 1 / 2 for the two players, head-to-head counts, last hit's weapon; per-name totals as `A2 06`; ending (kind 5) -> `A2 05` to Global | [inferred] |
| 24 | Accepts PING before WELCOME (story / checks queries can arrive right after HELLO) | [code] the client does send them, `laonline.c:1243-1248` |
| 25 | Has an idle timeout longer than 15 s (the keepalive period) | [inferred] |

## 8. What a stand-in relay has to guess

1. **Id assignment** and lobby size: lowest free id from 0 is the safe choice (death notices only work for ids 0..3,
   pickups for 0..7).
2. **Echo rules**: chat must be echoed to the sender for it to see its own lines; a DEATH echo would show the
   sender its own notice, so death goes to others only; for everything else the client rejects or ignores
   echoes, so "to others only" is safe.
3. **Late-joiner replay**: which per-player values the real relay re-sends to a newcomer (vitals, location,
   appearance, anchor, PvP state). The client never re-sends them unless they change.
4. **Lobby / password rules**: who sets a lobby's password (first joiner?), whether lobbies expire when empty,
   whether progress survives an empty lobby, the maximum players per lobby, and the exact ERROR code byte and texts.
5. **Version gate**: how the relay compares `LAOV` versions (`"15.2"`) and what it says when refusing.
6. **Unknown client types**: the v1 relay drops the client; a stand-in can disconnect (to test the gating) or
   ignore. The client itself ignores unknown server types.
7. **Which PONG replies** to advertise. A stand-in for the core co-op tests can answer only CAP / MAP / CHAT, or
   none (plain echo) to emulate a v1 relay; answering COOP commits it to the seed handshake.
8. **The extra V12 replies** `...72`-`...75`: when and whether they follow the V12 reply.
9. **Seed checks**: what makes COOP_READY / WORLD_RESULT fail (first joiner's digest wins?), and whether rewards are
   journaled per lobby, per digest, or both.
10. **Inventory snapshot** contents (OR of all members, including or excluding the receiver).
11. **PvP server rules**: damage passed through or recomputed, range / PvP-on checks, `quote` byte, head-to-head and
    per-name kill counts, who receives kind 4 markers.
12. **Death notice name**: the sender's HELLO name is assumed for `name17`; whether the relay checks `cause`.
13. **WELCOME byte 2 limit**: the client accepts up to 99 listed peers; the real relay's cap is unknown.
14. **Keepalive / idle timeout** value and whether the relay ever sends its own PINGs (the client would ignore a
    server `0x07`).
15. The meaning of `0x8F` byte 1 and of `0x9B` byte 1 (both unread by the client).
