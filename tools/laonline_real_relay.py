#!/usr/bin/env python3
"""tools/laonline_real_relay.py <relay folder> [port] [max players]: LA-Online's own relay (the server.py + protocol.py
its developer runs in Docker; not in this repository) run for the online tests, with the stand-in's log lines added
(tools/laonline_relay.py: "join", "leave", "session host", "pvp hit", ... - what the tests read), so every online test
runs against the real relay: LAO_RELAY=<relay folder> tools/coop_tests/online_*.sh (tools/laonline_relay.py hands
over to this when LAO_RELAY is set).

Nothing in the relay is changed: its classes are wrapped to log what they did, after they did it. It keeps its saves in
build/relay_data_<port>, emptied when it starts (a fresh relay each run, as the stand-in's). Max players per lobby: 4 by
default, as the developer's Docker setup runs it (--max-players 4), or LAO_RELAY_MAX. Its own log lines are kept too,
each after "relay: ". Listens on 127.0.0.1 only. Ctrl+C stops it.
"""
import asyncio
import logging
import os
import shutil
import struct
import sys


def log(*a):
    print(*a, flush=True)


def sstr(d, o):
    n = d[o]
    return d[o + 1:o + 1 + n].decode('utf-8', 'replace'), o + 1 + n


def session_answer(out):
    """The session service's answer (frame 0xB0): (ok, message, token, lobby, [(code, members), ...])."""
    d = out[3:]  # (u16 length, 0xB0)
    if not d or d[0] != 1:
        return False, sstr(d, 1)[0] if len(d) > 1 else '', '', '', []
    msg, o = sstr(d, 1)
    token, o = sstr(d, o)
    _password, o = sstr(d, o)
    lobby, o = sstr(d, o)
    _total, _page, count = struct.unpack_from('<HHB', d, o); o += 5
    rooms = []
    for _ in range(count):
        code, o = sstr(d, o)
        for _ in range(3):  # title, description, host
            _, o = sstr(d, o)
        o += 4  # mode, public, started, places
        for _ in range(3):  # build, world, seed
            _, o = sstr(d, o)
        members = d[o]; o += 1
        for _ in range(members):
            _, o = sstr(d, o)
            o += 8  # rgb, hair, ready, host
        rooms.append((code, members))
    return True, msg, token, lobby, rooms


def wrap(relay):
    """Log lines on the relay's classes (module `relay`: its server.py)."""
    Directory, Server, Player, Leaderboards = relay.Directory, relay.Server, relay.Player, relay.Leaderboards

    request = Directory.request

    def logged_request(self, data):
        out = request(self, data)
        try:
            r = relay.Reader(data)
            _version, action = r.take('<BB')
            code, _token, name, _build = (r.string() for _ in range(4))
            mode = r.take('<B')
            _world, _seed, _title, _description = (r.string() for _ in range(4))
            ready = r.take('<B')
        except Exception:
            return out
        ok, msg, _tok, lobby, rooms = session_answer(out)
        if not ok:
            log(f'session refused (action {action}) {name}: {msg}')
        elif action in (1, 2) and rooms:
            log(f'session host {rooms[0][0]} by {name} ({"public" if action == 2 else "private"}, '
                f'{"co-op randomizer" if mode == 2 else "co-op"})')
        elif action == 3 and rooms:
            log(f'session join {rooms[0][0]} {name} ({rooms[0][1]} in)')
        elif action == 5:
            log(f'session ready {code} {name} {int(bool(ready))}')
        elif action == 6 and rooms:
            log(f'session start {code} lobby {lobby} ({rooms[0][1]} players)')
        elif action == 7:
            log(f'session leave {code} {name}')
        return out
    Directory.request = logged_request

    on_hello = Server.on_hello

    def logged_hello(self, r, writer, social=False):
        player, lobby = on_hello(self, r, writer, social)
        if player:
            at, version = r.d.find(b'LAOV'), ''
            if at >= 0 and at + 5 <= len(r.d):
                version = r.d[at + 5:at + 5 + r.d[at + 4]].decode('ascii', 'replace')
            log(f'join {player.id} {player.name} lobby {lobby.name} v{version} ({len(lobby.players)} in, '
                f'{len(lobby.flags)} flags, {len(lobby.items)} items replayed)')
        return player, lobby
    Server.on_hello = logged_hello

    send_error = Server.send_error

    def logged_error(self, writer, code, msg):
        log(f'refused ({code}): {msg}')
        return send_error(self, writer, code, msg)
    Server.send_error = logged_error

    dispatch = Server.dispatch

    def logged_dispatch(self, p, lobby, mtype, r):
        before = None
        d = r.d
        if mtype == relay.C_FLAG_SET and len(d) >= 4:
            before = struct.unpack_from('<I', d)[0] in lobby.flags
        elif mtype == relay.C_ITEM_GET and len(d) >= 4:
            k, v = struct.unpack_from('<Hh', d)
            before = lobby.items.get(k, -32768) >= v
        elif mtype == relay.C_REWARD and len(d) == 9 and d[0] == 1 and p.coop_seed:
            item, index = struct.unpack_from('<Hh', d, 1)
            before = f'{item}:{index}' in lobby.rewards.get(p.coop_seed, {})
        dispatch(self, p, lobby, mtype, r)  # (a ProtocolError goes on up: the relay logs it and drops the player)
        if mtype == relay.C_FLAG_SET and before is False:
            log(f'flag {struct.unpack_from("<I", d)[0]} from {p.id}')
        elif mtype == relay.C_ITEM_GET and before is False:
            k, v = struct.unpack_from('<Hh', d)
            log(f'item {k:#x} = {v} from {p.id}')
        elif mtype == relay.C_COOP_PICKUP and len(d) == relay.COOP_PICKUP_SIZE:
            _seq, item, count, index, _m, _room = struct.unpack(relay.COOP_PICKUP_FMT, d)
            log(f'pickup {item} x{count} (index {index}) from {p.id}')
        elif mtype == relay.C_REWARD and before is False:
            item, index = struct.unpack_from('<Hh', d, 1)
            log(f'reward {item} (index {index}) from {p.id}')
        elif mtype == relay.C_DEATH:
            log(f'death of {p.id}')
        elif mtype == relay.C_COOP_SETUP:
            log(f'coop setup {p.id}: ready')
        elif mtype == relay.C_WORLD:
            log(f'world {p.id}: {"ready" if p.world_ready else "waiting" if lobby.world is None else "differs"}')
    Server.dispatch = logged_dispatch

    send = Player.send

    def logged_send(self, data):
        if len(data) >= 6 and data[2] == relay.S_PVP and data[3] == 1:  # (a hit, sent to its target)
            log(f'pvp hit {data[4]} -> {self.id} ({data[5]} quarters)')
        return send(self, data)
    Player.send = logged_send

    board = Leaderboards.handle

    def logged_board(self, data):
        out = board(self, data)
        try:
            r = relay.Reader(data)
            _version, action = r.take('<BB')
            if action == 1 and out[3] == 1:
                _run, players = r.string(), r.string()
                mode, seconds = r.take('<BI')[:2]
                log(f'board run {players} mode {mode} {seconds}s')
        except Exception:
            pass
        return out
    Leaderboards.handle = logged_board

    seed_transfer = Server.seed_transfer

    async def logged_seed(self, reader, writer, r):
        o = r.o
        try:
            action = r.take('<B'); name = r.string(); lobby = r.string()
        except Exception:
            action, name, lobby = -1, '?', '?'
        r.o = o
        seen = []

        class Writer:  # (what it answers: 0x9D frames)
            def write(self, data):
                seen.append(bytes(data))
                return writer.write(data)

            async def drain(self):
                return await writer.drain()
        await seed_transfer(self, reader, Writer(), r)
        answers = [f for f in seen if len(f) >= 4 and f[2] == 0x9D]
        if action == 1:
            done = len(answers) >= 2 and answers[-1][3] == 1
            lob = self.lobbies.get(lobby)
            size = os.path.getsize(lob.path + '.seedpatch') if done and lob and lob.path else 0
            log(f'seed upload {lobby} by {name}: {size} bytes' if done else f'seed upload {lobby} by {name}: REJECTED')
        elif action == 0:
            if answers and answers[0][3] == 1:
                log(f'seed download {lobby} by {name}: {struct.unpack_from("<I", answers[0], 4)[0]} bytes')
            else:
                log(f'seed download {lobby} by {name}: none yet')
    Server.seed_transfer = logged_seed

    class Lines(logging.Handler):  # (its own log: leaves and PvP switches as the stand-in's lines; every line kept)
        def emit(self, rec):
            msg, args = rec.msg, rec.args if isinstance(rec.args, tuple) else ()
            if msg.startswith('#%d %r left lobby') and len(args) >= 5:
                left = 0 if args[4] == 'none' else args[4].count(',') + 1
                log(f'leave {args[0]} {args[1]} ({left} in)')
            elif msg.startswith('pvp %s player=') and len(args) >= 2:
                log(f'pvp {args[0]} {args[1]}')
            log('relay: ' + rec.getMessage())
    return Lines()


async def serve(relay, port, max_players, data):
    srv = relay.Server(min(max(max_players, 2), 8), data)
    server = await asyncio.start_server(srv.handle, '127.0.0.1', port)
    log(f'relay listening on 127.0.0.1:{port} (LA-Online\'s own relay, {max_players} players per lobby)')
    asyncio.create_task(srv.autosave())
    async with server:
        await server.serve_forever()


def main(argv):
    if not argv or not os.path.isfile(os.path.join(argv[0], 'server.py')):
        print('usage: laonline_real_relay.py <folder with LA-Online\'s server.py and protocol.py> [port] [max players]')
        return 2
    sys.path.insert(0, os.path.abspath(argv[0]))
    import server as relay
    port = int(argv[1]) if len(argv) > 1 else 47620
    max_players = int(argv[2]) if len(argv) > 2 else int(os.environ.get('LAO_RELAY_MAX', '4'))
    logger = logging.getLogger('la-online')
    logger.setLevel(logging.INFO)
    logger.addHandler(wrap(relay))
    logger.propagate = False
    data = os.path.join('build', f'relay_data_{port}')
    shutil.rmtree(data, ignore_errors=True)
    os.makedirs(data, exist_ok=True)
    try:
        asyncio.run(serve(relay, port, max_players, data))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
