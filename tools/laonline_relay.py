#!/usr/bin/env python3
"""tools/laonline_relay.py [port] [max players]: a stand-in for LA-Online's relay server (server.py, its developer's,
not public), for tests on one PC. Written from what the client sends and accepts (docs/laonline-protocol.md): lobbies
by name, ids, the roster, every player's state / hearts / look / map marker passed to the others, shared progress
(each flag kept, the highest value per item key, both replayed to late joiners), pickups, death notices, chat, the
co-op seed handshake and its rewards, checks, story and inventory, PvP's state and hits. Its rules are the real
relay's (V15.2's server.py, read 2026-11-18) where they matter to the client: a session room's lobby ("V10-<code>")
admits only the room's members, by name, once it started; 4 players per lobby (the developer's Docker setup; 100 in
Global, whose lobbies take only Global's HELLO); a name once per lobby; PvP frames only after the V12 query (else the
connection is dropped), hits only between players who both have PvP on, half a heart unless it says, at most one per
0.45 s per target and none on a player hit in the last 0.8 s; rooms of 4, a name once per room, a member not seen for
10 s not Ready and for 2 minutes gone. Elsewhere the safe choice the spec lists (section 8): the lowest free id; chat
echoed to its sender, death notices not; each player's last hearts / location / look / marker / PvP state replayed
to newcomers; the first joiner sets a lobby's password and seed. A test tool, not a server for play.

Clients: laonline.ini `mode = server`, `server = 127.0.0.1:<port>` (default port 47620). Logs one line per event on
stdout ("join", "leave", "flag", "item", "pickup", "reward", ...), which the tests read. Python 3.8+, standard
library only. Ctrl+C stops it. With LAO_RELAY set to the folder of LA-Online's own relay it runs that one instead,
with these log lines (tools/laonline_real_relay.py).
"""
import asyncio
import hashlib
import os
import secrets
import struct
import sys

CAP_REPLIES = {0x4c414f51, 0x4c414f53, 0x4c414f55, 0x4c414f57, 0x4c414f59, 0x4c414f5b, 0x4c414f5d, 0x4c414f5f,
               0x4c414f61, 0x4c414f63, 0x4c414f67, 0x4c414f70}  # each answered with nonce + 1 (spec section 3)
V12_EXTRAS = (0x4c414f72, 0x4c414f73, 0x4c414f74, 0x4c414f75)  # sent unprompted with the V12 reply
INV_SIZE = 67
MAX_PLAYERS = int(sys.argv[2]) if __name__ == '__main__' and len(sys.argv) > 2 else 4  # (per lobby; Global: 100)


def log(*a):
    print(*a, flush=True)


def frame(body):
    return struct.pack('<H', len(body)) + body


def put_str(s):
    s = s[:255]
    return bytes([len(s)]) + s


def name17(n):
    return n[:16].ljust(17, b'\0')


class Lobby:
    def __init__(self, name, password):
        self.name, self.password = name, password
        self.players = {}         # id -> Player
        self.flags = set()        # FLAG_SET flags
        self.items = {}           # ITEM_GET key -> highest value
        self.digest = None        # the co-op seed digest (first COOP_SETUP / WORLD)
        self.rewards = {}         # (digest, item, index) -> (map, room, finder name)
        self.checks = set()
        self.story = {}           # digest -> mask
        self.inventory = bytearray(INV_SIZE)


class Player:
    def __init__(self, writer):
        self.w, self.id, self.lobby, self.name, self.rgb = writer, None, None, b'', b'\0\0\0'
        self.last = {}            # 0x8B / 0x8C / 0x8D / 0x93 / 0xA2 -> the last body passed on (late joiners)
        self.digest = None
        self.v12, self.pvp = False, False   # (the V12 query seen; PvP on)
        self.hit_at, self.hurt_at = {}, -99.0  # (target id -> its last hit from him; his last hit taken)

    def send(self, body):
        try:
            self.w.write(frame(body))
        except Exception:
            pass


lobbies = {}


def others(p):
    return [q for q in p.lobby.players.values() if q is not p]


def to_others(p, body, remember=None):
    if remember is not None:
        p.last[remember] = body
    for q in others(p):
        q.send(body)


def to_all(p, body):
    for q in p.lobby.players.values():
        q.send(body)


def hello(p, t, b):
    o = 1
    (ver,) = struct.unpack_from('<H', b, o); o += 2
    n = b[o]; name = bytes(b[o + 1:o + 1 + n]); o += 1 + n
    rgb = bytes(b[o:o + 3]); o += 3
    n = b[o]; lobby = bytes(b[o + 1:o + 1 + n]); o += 1 + n
    n = b[o]; password = bytes(b[o + 1:o + 1 + n]); o += 1 + n
    version = b''
    if b[o:o + 4] == b'LAOV':
        n = b[o + 4]; version = bytes(b[o + 5:o + 5 + n])
    def refuse(code, msg):
        p.send(bytes([0x8F, code]) + put_str(msg.encode()))
        log(f'refused {name.decode(errors="replace")}: {msg} (lobby {lobby.decode(errors="replace")})')
        return False
    if t == 0x31:  # (Global: its own two lobbies, no password)
        password = b''
        lobby = b'V10-GLOBAL-RANDO' if lobby == b'V10-GLOBAL-RANDO' else b'V10-GLOBAL'
    elif lobby.startswith(b'V10-GLOBAL'):
        return refuse(6, 'Global requires the social-only client handshake.')
    for r in rooms.values():  # (a session room's lobby: its members only, once it started)
        if r.lobby() == lobby:
            prune()
            if not r.started:
                return refuse(5, 'The host has not started this session.')
            if password != r.password or name not in [m.name for m in r.members]:
                return refuse(5, 'Join this session through its room code or public listing first.')
    L = lobbies.setdefault(lobby, Lobby(lobby, password))
    if L.password != password:
        return refuse(5, 'Wrong lobby password.')
    if any(q.name.lower() == name.lower() for q in L.players.values()):
        return refuse(3, f'Someone in this lobby is already called {name.decode(errors="replace")}.')
    pid = next((i for i in range(100 if t == 0x31 else MAX_PLAYERS) if i not in L.players), None)
    if pid is None:
        return refuse(4, 'Lobby is full.')
    p.id, p.lobby, p.name, p.rgb = pid, L, name[:16] or b'Player', rgb
    roster = b''.join(bytes([q.id, len(q.name)]) + q.name + q.rgb for q in L.players.values())
    flags = b''.join(struct.pack('<I', f) for f in sorted(L.flags))
    items = b''.join(struct.pack('<Hh', k, v) for k, v in sorted(L.items.items()))
    p.send(bytes([0x81, pid, len(L.players)]) + roster + struct.pack('<I', len(L.flags)) + flags +
           struct.pack('<H', len(L.items)) + items)
    for q in L.players.values():
        q.send(bytes([0x82, pid, len(p.name)]) + p.name + p.rgb)
        for body in q.last.values():  # (what the newcomer would otherwise never get: sent only on change)
            p.send(body)
    L.players[pid] = p
    log(f'join {pid} {p.name.decode(errors="replace")} lobby {lobby.decode(errors="replace")} '
        f'v{version.decode(errors="replace")} ({len(L.players)} in, {len(L.flags)} flags, {len(L.items)} items replayed)')
    return True


def handle(p, b):
    t = b[0]
    L = p.lobby
    if t == 0x07:  # PING
        (v,) = struct.unpack_from('<I', b, 1)
        p.send(bytes([0x89]) + struct.pack('<I', v + 1 if v in CAP_REPLIES else v))
        if v == 0x4c414f70:
            p.v12 = True
            for x in V12_EXTRAS:
                p.send(bytes([0x89]) + struct.pack('<I', x))
        return
    if L is None:
        return
    pid = bytes([p.id])
    if t == 0x02:     # STATE
        to_others(p, bytes([0x84]) + pid + b[1:28])
    elif t == 0x08:   # PROFILE
        p.rgb = bytes(b[1:4])
        to_others(p, bytes([0x8A]) + pid + p.rgb, 0x8A)
    elif t == 0x09:   # VITALS
        to_others(p, bytes([0x8B]) + pid + b[1:3], 0x8B)
    elif t == 0x0A:   # LOCATION
        to_others(p, bytes([0x8C]) + pid + b[1:], 0x8C)
    elif t == 0x0B:   # APPEARANCE
        to_others(p, bytes([0x8D]) + pid + b[1:19], 0x8D)
    elif t == 0x0F:   # MAP_ANCHOR
        to_others(p, bytes([0x93]) + pid + b[1:18], 0x93)
    elif t == 0x03:   # FLAG_SET: kept, new ones passed on
        (f,) = struct.unpack_from('<I', b, 1)
        if f not in L.flags:
            L.flags.add(f)
            log(f'flag {f} from {p.id}')
            to_others(p, bytes([0x85]) + pid + struct.pack('<I', f))
    elif t == 0x04:   # ITEM_GET: the highest value per key kept, a higher one passed on
        k, v = struct.unpack_from('<Hh', b, 1)
        if v > L.items.get(k, 0):
            L.items[k] = v
            log(f'item {k:#x} = {v} from {p.id}')
            to_others(p, bytes([0x86]) + pid + struct.pack('<Hh', k, v))
    elif t == 0x0C:   # COOP_SETUP -> COOP_READY
        d = bytes(b[1:33]); p.digest = d
        if L.digest is None:
            L.digest = d
        ok = d == L.digest
        log(f'coop setup {p.id}: {"ready" if ok else "seed differs"}')
        p.send(bytes([0x90, 1 if ok else 0]) + d)
    elif t == 0x14:   # WORLD -> WORLD_RESULT
        d = bytes(b[2:34]); p.digest = d
        if L.digest is None:
            L.digest = d
        p.send(bytes([0x98, 1 if d == L.digest else 4]) + d)
    elif t == 0x0D:   # PICKUP
        item, count, index = struct.unpack_from('<HHh', b, 9)
        log(f'pickup {item} x{count} (index {index}) from {p.id}')
        to_others(p, bytes([0x91]) + pid + b[1:19])
    elif t == 0x0E:   # INVENTORY: ORed, the lobby's snapshot to everyone
        for i in range(INV_SIZE):
            L.inventory[i] |= b[1 + i]
        to_all(p, bytes([0x92]) + bytes(L.inventory))
    elif t == 0x10:   # CHAT: to everyone, the sender too
        to_all(p, bytes([0x94]) + pid + b[1:])
    elif t == 0x11:   # TYPING
        to_others(p, bytes([0x95]) + pid + b[1:2])
    elif t == 0x12:   # CHECK
        if b[1] == 0:
            fl = sorted(L.checks)
            for i in range(0, max(len(fl), 1), 255):
                part = fl[i:i + 255]
                p.send(bytes([0x96, len(part)]) + b''.join(struct.pack('<H', f) for f in part))
        elif b[1] == 1:
            (f,) = struct.unpack_from('<H', b, 2)
            if f not in L.checks:
                L.checks.add(f)
                to_others(p, bytes([0x96, 1]) + struct.pack('<H', f))
    elif t == 0x13:   # REWARD
        if b[1] == 1 and p.digest:
            item, index, m, r = struct.unpack_from('<HhHH', b, 2)
            key = (p.digest, item, index)
            if key not in L.rewards:
                L.rewards[key] = (m, r, p.name)
                log(f'reward {item} (index {index}) from {p.id}')
            for q in others(p):
                q.send(bytes([0x97]) + p.digest + struct.pack('<HhHH', item, index, m, r) + name17(p.name))
        elif b[1] == 0 and p.digest:
            for (d, item, index), (m, r, who) in L.rewards.items():
                if d == p.digest:
                    p.send(bytes([0x97]) + d + struct.pack('<HhHH', item, index, m, r) + name17(who))
    elif t == 0x1A:   # STORY: ORed per seed, to everyone
        d, mask = bytes(b[2:34]), b[34]
        L.story[d] = L.story.get(d, 0) | mask
        to_all(p, bytes([0xA1, 1]) + d + bytes([L.story[d], 0]))
    elif t == 0x19:   # DEATH: to the others (an echo would show the sender its own notice)
        log(f'death of {p.id}')
        to_others(p, bytes([0xA0]) + pid + b[1:14] + name17(p.name))
    elif t == 0x1C:   # PVP (only after the V12 query: the real relay drops a connection that sends it before)
        if not p.v12:
            log(f'protocol error from {p.id}: PvP before the V12 query (dropped)')
            return False
        kind = b[1]
        if kind == 0:
            p.pvp = len(b) > 2 and b[2] != 0
            to_others(p, bytes([0xA2, 0]) + pid + bytes([int(p.pvp)]), 0xA2)
            log(f'pvp {"on" if p.pvp else "off"} {p.id}')
        elif kind == 1 and len(b) >= 3:
            target, quarters = b[2], min(8, max(1, b[3])) if len(b) > 3 else 2  # (half a heart unless it says)
            q = L.players.get(target)
            now = asyncio.get_running_loop().time()
            if q and q is not p and p.pvp and q.pvp and now - p.hit_at.get(target, -99.0) >= 0.45 and now - q.hurt_at >= 0.8:
                p.hit_at[target], q.hurt_at = now, now
                q.send(bytes([0xA2, 1]) + pid + bytes([quarters]))
                for r in L.players.values():  # (the hit confirmed to everyone: their hit markers - V14's kind 4)
                    r.send(bytes([0xA2, 4]) + pid + bytes([target, quarters]))
                log(f'pvp hit {p.id} -> {target} ({quarters} quarters)')
        elif kind == 2 and len(b) == 19:  # an attack to draw: (attacker,) item, map, position, facing
            to_others(p, bytes([0xA2, 2]) + pid + b[2:19])
        elif kind == 3 and len(b) >= 3:  # knocked out by player b[2] (the real relay's kill feed: not here)
            log(f'pvp knockout {p.id} by {b[2]}')
    else:
        log(f'ignored type {t:#x} from {p.id}')


# ---------------------------------------------------------------- the session service (opcode 0x30): the rooms the
# menu's Online page hosts, lists and joins (runtime/ui/laonline_lobby.mm; LA-Online V15.2's session_ui.mm). One request
# per connection, answered with opcode 0xB0. A room's members then meet in its relay lobby ("V10-<code>", ".rando" for
# a Co-op Randomizer room) with its password - only its members, by name. Its rules (the real relay's): 4 places; a
# name once per room; the host starts once every member is Ready; a member not seen (a request of his, or his name
# playing in the room's lobby) for 10 s isn't Ready, for 2 minutes is gone; the host leaving hands the room to the
# next member; an empty room is gone.
class Member:
    def __init__(self, token, name, rgb, hair):
        self.token, self.name, self.rgb, self.hair, self.ready, self.host = token, name, rgb, hair, False, False
        self.seen = asyncio.get_running_loop().time()


def prune():
    now = asyncio.get_running_loop().time()
    for code, r in list(rooms.items()):
        L = lobbies.get(r.lobby())
        playing = {q.name for q in L.players.values()} if L else set()
        for m in list(r.members):
            if m.name in playing:
                m.seen = now
            elif now - m.seen > 120:
                r.members.remove(m)
                log(f'session gone {r.code.decode()} {m.name.decode(errors="replace")} (not seen for 2 minutes)')
            elif now - m.seen > 10:
                m.ready = False
        if not r.members:
            del rooms[code]
        elif not any(m.host for m in r.members):
            r.members[0].host = True
            r.host = r.members[0].name


class Room:
    serial = 0

    def __init__(self, code, title, host, mode, public, build, world, seed):
        Room.serial += 1
        self.code, self.title, self.mode, self.public, self.build = code, title, mode, public, build
        self.world, self.seed, self.started, self.members, self.created = world, seed, False, [], Room.serial
        self.password = secrets.token_hex(8).encode()
        self.host = host

    def lobby(self):
        return b'V10-' + self.code + (b'.rando' if self.mode == 2 else b'')

    def wire(self):
        b = put_str(self.code) + put_str(self.title) + put_str(b'') + put_str(self.host)
        b += bytes([self.mode, int(self.public), int(self.started), 4]) + put_str(self.build) + put_str(self.world)
        b += put_str(self.seed) + bytes([len(self.members)])
        for m in self.members:
            b += put_str(m.name) + m.rgb + m.hair + bytes([int(m.ready), int(m.host)])
        return b


rooms = {}  # code -> Room
CODE_LETTERS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'


def session(b):
    o = 2  # (version 1, action)
    action = b[1]

    def s():
        nonlocal o
        n = b[o]; v = bytes(b[o + 1:o + 1 + n]); o += 1 + n
        return v
    code, token, name, build = s(), s(), s(), s()
    mode = b[o]; o += 1
    world, seed, title, _description = s(), s(), s(), s()
    ready = b[o] != 0; rgb = bytes(b[o + 1:o + 4]); hair = bytes(b[o + 4:o + 7]); o += 7
    search = s()
    filt, sort = b[o], b[o + 1]
    (page,) = struct.unpack_from('<H', b, o + 2)
    name = name[:16] or b'Player'

    def fail(msg):
        return bytes([0]) + put_str(msg.encode())

    def ok(msg, tok=b'', room=None, listed=None, total=0, pg=0):
        out = bytes([1]) + put_str(msg.encode()) + put_str(tok)
        out += put_str(room.password if room else b'') + put_str(room.lobby() if room else b'')
        shown = listed if listed is not None else ([room] if room else [])
        out += struct.pack('<HH', total, pg) + bytes([len(shown)])
        return out + b''.join(r.wire() for r in shown)

    def drop(r, m):
        r.members.remove(m)
        log(f'session leave {r.code.decode()} {m.name.decode(errors="replace")}')
        if not r.members:
            del rooms[r.code]
        elif m.host:
            r.members[0].host = True
            r.host = r.members[0].name

    def new_code():
        while True:
            c = ''.join(secrets.choice(CODE_LETTERS) for _ in range(6)).encode()
            if c not in rooms:
                return c

    prune()
    if mode not in (1, 2):
        return fail('This room needs a compatible client and game mode.')
    if action == 0:
        listed = [r for r in rooms.values() if r.public and (filt == 0 or r.mode == filt)
                  and (search.lower() in r.title.lower() or search.lower() in r.host.lower())]
        keys = [lambda r: -len(r.members), lambda r: len(r.members), lambda r: -r.created, lambda r: r.title.lower()]
        listed.sort(key=keys[sort if sort < 4 else 0])
        return ok('', listed=listed[page * 8:page * 8 + 8], total=len(listed), pg=page)
    if action in (1, 2):
        if token and any(m.token == token for r in rooms.values() for m in r.members):
            return fail('Leave your current session before creating another.')
        r = Room(new_code(), title or name + b"'s adventure", name, mode, action == 2, build, world, seed)
        m = Member(secrets.token_hex(16).encode(), name, rgb, hair)
        m.host = True
        r.members.append(m)
        rooms[r.code] = r
        log(f'session host {r.code.decode()} by {name.decode(errors="replace")} '
            f'({"public" if r.public else "private"}, {"co-op randomizer" if r.mode == 2 else "co-op"})')
        return ok('Session created.', m.token, r)
    r = rooms.get(code)
    if action == 3:
        if not r:
            return fail('Room not found or expired. Check the six-character code.')
        if r.build != build:
            return fail('This adventure needs a different version of LA-Online. Update the game to join.')
        m = next((x for x in r.members if token and x.token == token), None)
        if m:  # (his place again: the token he kept)
            m.name, m.rgb, m.hair, m.seen = name, rgb, hair, asyncio.get_running_loop().time()
            return ok('Welcome back.', m.token, r)
        if len(r.members) >= 4:
            return fail('Session full: all four player slots are taken.')
        if any(x.name.lower() == name.lower() for x in r.members):
            return fail('That player name is already in this adventure. If it is you reconnecting, wait up to 2 '
                        'minutes or pick another name.')
        m = Member(secrets.token_hex(16).encode(), name, rgb, hair)
        m.ready = r.started
        r.members.append(m)
        log(f'session join {r.code.decode()} {m.name.decode(errors="replace")} ({len(r.members)} in)')
        return ok('Joined the session.', m.token, r)
    m = next((x for x in r.members if token and x.token == token), None) if r else None
    if not m:
        return fail('Your room membership expired. Join again.')
    m.seen = asyncio.get_running_loop().time()
    if action == 4:
        return ok('', m.token, r)
    if action == 5:
        if r.started:
            return fail('The adventure has already started.')
        m.ready = ready
        log(f'session ready {r.code.decode()} {m.name.decode(errors="replace")} {int(ready)}')
        return ok('', m.token, r)
    if action == 6:
        if not m.host:
            return fail('Only the host can start the adventure.')
        if not all(x.ready for x in r.members):
            return fail('Every player must be ready before starting.')
        r.started = True
        log(f'session start {r.code.decode()} lobby {r.lobby().decode()} ({len(r.members)} players)')
        return ok('Session started.', m.token, r)
    if action == 7:
        drop(r, m)
        return ok('Left the session.')
    if action == 8:
        if not m.host or len(r.members) != 1 or r.started:
            return fail('Only the host can change the code, before another player joins.')
        del rooms[r.code]
        r.code = new_code()
        r.password = secrets.token_hex(8).encode()
        rooms[r.code] = r
        return ok('New room code.', m.token, r)
    if action == 9:
        if m.ready or r.started:
            return fail('Mark Not Ready before changing your name.')
        if any(x is not m and x.name.lower() == name.lower() for x in r.members):
            return fail('That player name is already in this room.')
        m.name = name
        if m.host:
            r.host = m.name
        return ok('Name changed.', m.token, r)
    return fail('Unknown request.')


# ---------------------------------------------------------------- Global's player count (0x33) and the leaderboards
# (0x32) - the Online page's Global and Leaderboards tabs. One request per connection, as the session service.
def global_count():
    n = sum(len(L.players) for name, L in lobbies.items() if name.startswith(b'V10-GLOBAL'))
    return bytes([1]) + struct.pack('<HH', min(n, 100), 100)


runs = []  # (players, mode, count, seconds, rupees, deaths, score)


def board(b):
    version, action = b[0], b[1]
    if version == 1 and action == 1:  # a completed run: str id, str players, u8 mode, u32 seconds / rupees / deaths, u8 1
        o = 2
        n = b[o]; o += 1 + n
        n = b[o]; players = bytes(b[o + 1:o + 1 + n]); o += 1 + n
        mode = b[o]; o += 1
        seconds, rupees, deaths = struct.unpack_from('<III', b, o)
        count = min(4, players.count(b',') + 1)
        score = max(0, 100000 - seconds - 250 * deaths + rupees)
        runs.append((players, mode, count, seconds, rupees, deaths, score))
        log(f'board run {players.decode(errors="replace")} mode {mode} {seconds}s')
        return bytes([1]) + put_str(b'Thank you.')
    if version == 2 and action == 0:  # a page: u8 mode (3 all), u8 sort, u8 player count (0 all), u16 page
        mode, sort, count = b[2], b[3], b[4]
        (page,) = struct.unpack_from('<H', b, 5)
        rows = [r for r in runs if (mode == 3 or r[1] == mode) and (count == 0 or r[2] == count)]
        key = [lambda r: r[3], lambda r: -r[6], lambda r: -r[4], lambda r: -r[5]][sort if sort < 4 else 0]
        rows.sort(key=key)
        shown = rows[page * 50:page * 50 + 50]
        out = bytes([1]) + put_str(b'') + struct.pack('<H', len(rows)) + bytes([len(shown)])
        for r in shown:
            out += put_str(r[0]) + bytes([r[1], r[2]]) + struct.pack('<IIII', r[3], r[4], r[5], r[6])
        return out
    return bytes([0]) + put_str(b'Unknown request.')


# ---------------------------------------------------------------- a Co-op Randomizer room's seed (0x16 / 0x17 / 0x18):
# the host's game uploads it, a joiner's downloads it (tools/rando/lao_seed_transfer.py: verified patches against each
# player's own game files). Kept per lobby ("<room lobby>.rando") with the uploader's password; given only with it.
seeds = {}  # lobby -> (password, package, sha256)


async def read_frame(reader):
    (n,) = struct.unpack('<H', await reader.readexactly(2))
    return await reader.readexactly(n)


async def seed_transfer(reader, writer, b):
    o = 2
    fields = []
    for _ in range(3):  # name, lobby, password
        n = b[o]; fields.append(bytes(b[o + 1:o + 1 + n])); o += 1 + n
    name, lobby, password = fields
    if b[1] == 1:  # upload: world checksum, u32 size, sha256; then chunks (0x17), then the end (0x18)
        o += 32
        (size,) = struct.unpack_from('<I', b, o)
        digest = bytes(b[o + 4:o + 36])
        writer.write(frame(b'\x9d\x01'))
        await writer.drain()
        data = bytearray()
        while True:
            f = await read_frame(reader)
            if f[0] == 0x17:
                data += f[1:]
            elif f[0] == 0x18:
                break
        ok = len(data) == size and hashlib.sha256(data).digest() == digest
        if ok:
            seeds[lobby] = (password, bytes(data), digest)
        log(f'seed upload {lobby.decode(errors="replace")} by {name.decode(errors="replace")}: {size} bytes'
            f'{"" if ok else " REJECTED"}')
        writer.write(frame(b'\x9d' + bytes([1 if ok else 0])))
    else:  # download: u32 size + sha256, the package in chunks (0x9E), the end (0x9F)
        got = seeds.get(lobby)
        if not got or got[0] != password:
            writer.write(frame(b'\x9d\x00'))
            log(f'seed download {lobby.decode(errors="replace")} by {name.decode(errors="replace")}: none yet')
        else:
            data = got[1]
            writer.write(frame(b'\x9d\x01' + struct.pack('<I', len(data)) + got[2]))
            for i in range(0, len(data), 4000):
                writer.write(frame(b'\x9e' + data[i:i + 4000]))
            writer.write(frame(b'\x9f'))
            log(f'seed download {lobby.decode(errors="replace")} by {name.decode(errors="replace")}: {len(data)} bytes')
    await writer.drain()


async def client(reader, writer):
    p = Player(writer)
    try:
        while True:
            head = await reader.readexactly(2)
            (n,) = struct.unpack('<H', head)
            if n == 0:
                continue
            b = await reader.readexactly(n)
            if p.lobby is None and b[0] == 0x16:  # (a seed transfer: its own connection)
                await seed_transfer(reader, writer, b)
                break
            if p.lobby is None and b[0] in (0x30, 0x32, 0x33):  # (one answer, then the connection closes)
                body = bytes([b[0] + 0x80]) + (session(b[1:]) if b[0] == 0x30 else board(b[1:]) if b[0] == 0x32
                                               else global_count())
                writer.write(frame(body))
                await writer.drain()
                break
            if p.lobby is None and b[0] in (0x01, 0x31):
                if not hello(p, b[0], b):
                    break
            elif handle(p, b) is False:  # (a protocol error: the connection dropped, as the real relay does)
                await writer.drain()
                break
            await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError, struct.error, IndexError):
        pass
    finally:
        if p.lobby is not None and p.lobby.players.get(p.id) is p:
            del p.lobby.players[p.id]
            for q in p.lobby.players.values():
                q.send(bytes([0x83, p.id]))
            log(f'leave {p.id} {p.name.decode(errors="replace")} ({len(p.lobby.players)} in)')
        writer.close()


async def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 47620
    server = await asyncio.start_server(client, '127.0.0.1', port)
    log(f'relay listening on 127.0.0.1:{port}')
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    if os.environ.get('LAO_RELAY'):  # (LA-Online's own relay instead, with these log lines: tools/laonline_real_relay.py)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import laonline_real_relay
        sys.exit(laonline_real_relay.main([os.environ['LAO_RELAY']] + sys.argv[1:]))
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
