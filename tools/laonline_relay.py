#!/usr/bin/env python3
"""tools/laonline_relay.py [port]: a stand-in for LA-Online's relay server (its "Docker relay v1", server.py, isn't
public), for tests on one PC. Written from what the client sends and accepts (docs/laonline-protocol.md): lobbies by
name, ids, the roster, every player's state / hearts / look / map marker passed to the others, shared progress (each
flag kept, the highest value per item key, both replayed to late joiners), pickups, death notices, chat, the co-op
seed handshake and its rewards, checks, story and inventory, PvP's state and hits. Where the real relay's rules aren't
known it takes the safe choice the spec lists (section 8): the lowest free id; chat echoed to its sender, death
notices not; each player's last hearts / location / look / marker / PvP state replayed to newcomers; the first
joiner sets a lobby's password and seed. A test tool, not a server for play.

Clients: laonline.ini `mode = server`, `server = 127.0.0.1:<port>` (default port 47620). Logs one line per event on
stdout ("join", "leave", "flag", "item", "pickup", "reward", ...), which the tests read. Python 3.8+, standard
library only. Ctrl+C stops it.
"""
import asyncio
import struct
import sys

CAP_REPLIES = {0x4c414f51, 0x4c414f53, 0x4c414f55, 0x4c414f57, 0x4c414f59, 0x4c414f5b, 0x4c414f5d, 0x4c414f5f,
               0x4c414f61, 0x4c414f63, 0x4c414f67, 0x4c414f70}  # each answered with nonce + 1 (spec section 3)
V12_EXTRAS = (0x4c414f72, 0x4c414f73, 0x4c414f74, 0x4c414f75)  # sent unprompted with the V12 reply
INV_SIZE = 67


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
    if t == 0x31:
        password = b''
    L = lobbies.setdefault(lobby, Lobby(lobby, password))
    if L.password != password:
        msg = b'Wrong lobby password'
        p.send(bytes([0x8F, 1, len(msg)]) + msg)
        log(f'refused {name.decode(errors="replace")}: password (lobby {lobby.decode(errors="replace")})')
        return False
    pid = next(i for i in range(100) if i not in L.players)
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
    elif t == 0x1C:   # PVP
        kind = b[1]
        if kind == 0:
            to_others(p, bytes([0xA2, 0]) + pid + b[2:3], 0xA2)
        elif kind == 1 and len(b) >= 3:
            target, quarters = b[2], b[3] if len(b) > 3 else 1
            q = L.players.get(target)
            if q:
                q.send(bytes([0xA2, 1]) + pid + bytes([quarters]))
                log(f'pvp hit {p.id} -> {target} ({quarters} quarters)')
    else:
        log(f'ignored type {t:#x} from {p.id}')


async def client(reader, writer):
    p = Player(writer)
    try:
        while True:
            head = await reader.readexactly(2)
            (n,) = struct.unpack('<H', head)
            if n == 0:
                continue
            b = await reader.readexactly(n)
            if p.lobby is None and b[0] in (0x01, 0x31):
                if not hello(p, b[0], b):
                    break
            else:
                handle(p, b)
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
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
