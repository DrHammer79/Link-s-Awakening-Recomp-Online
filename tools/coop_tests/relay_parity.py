#!/usr/bin/env python3
"""tools/coop_tests/relay_parity.py [--native <command>]: the game's own relay (runtime/laonline_host.cpp: `lasrecomp
--relay`, the one the Online page's Relay tab hosts) against the stand-in (tools/laonline_relay.py, LA-Online's relay's
rules), frame by frame. The same clients' requests go to each - pings and their capability answers, the session
service (host, join, ready, start, list, a new code, a name change, leave), HELLO and the roster, state / vitals /
look / marker passed on and replayed to a late joiner, shared flags and items, chat, typing, pickups, checks, rewards,
story, inventory, death notices, PvP (before the V12 query: dropped; hits, their rate limits), the refusals (a room's
lobby before it started, not a member, a wrong password, a name taken, a full lobby, Global without its handshake),
Global's count, the leaderboards and a Co-op Randomizer seed's upload and download - and every frame each client gets
back is compared (the room codes, tokens and passwords, random, are named in the order they appear). Prints PASS /
FAIL per step. Python 3.8+, standard library only.

--native: the command that runs the game's relay, "{port}" for its port (default: build/lasrecomp.exe --relay {port},
run as tools/laonline_relay.py runs it: LAO_NATIVE_RELAY).
"""
import hashlib
import os
import shlex
import socket
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def frame(body):
    return struct.pack('<H', len(body)) + body


def s8(b):
    return bytes([len(b)]) + b


class Client:
    def __init__(self, port):
        self.s = socket.create_connection(('127.0.0.1', port), timeout=3)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b''
        self.closed = False

    def send(self, body):
        try:
            self.s.sendall(frame(body))
        except OSError:
            self.closed = True

    def frames(self, quiet=0.25):
        """every frame until none comes for <quiet> s (or the connection closes)"""
        out = []
        self.s.settimeout(quiet)
        while True:
            while len(self.buf) >= 2:
                (n,) = struct.unpack_from('<H', self.buf)
                if len(self.buf) < 2 + n:
                    break
                out.append(self.buf[2:2 + n])
                self.buf = self.buf[2 + n:]
            try:
                got = self.s.recv(65536)
            except socket.timeout:
                return out
            except OSError:
                self.closed = True
                return out
            if not got:
                self.closed = True
                return out
            self.buf += got

    def close(self):
        self.s.close()


def one_shot(port, body):
    c = Client(port)
    c.send(body)
    f = c.frames(0.5)
    c.close()
    return f


def session(port, action, code=b'', token=b'', name=b'Alice', mode=1, ready=False, title=b'', search=b'', page=0,
            build=b'16.0'):
    body = bytes([0x30, 1, action]) + s8(code) + s8(token) + s8(name) + s8(build) + bytes([mode])
    body += s8(b'') + s8(b'') + s8(title) + s8(b'') + bytes([int(ready)]) + b'\x10\x20\x30' + b'\x40\x50\x60'
    body += s8(search) + bytes([0, 0]) + struct.pack('<H', page)
    return one_shot(port, body)


def parse_session(f):
    """(ok, message, token, password, lobby) of a session answer"""
    b = f[0]
    o = 2
    out = [b[1]]
    for _ in range(4):
        n = b[o]
        out.append(b[o + 1:o + 1 + n])
        o += 1 + n
    return out


def hello(port, name, lobby, password=b'', t=0x01, version=b'16.0'):
    c = Client(port)
    c.send(bytes([t]) + struct.pack('<H', 1) + s8(name) + b'\x11\x22\x33' + s8(lobby) + s8(password) + b'LAOV' + s8(version))
    return c


def scenario(port, log):
    """the requests; log(step, client, frames) each answer"""
    def got(step, **clients):
        for k, c in clients.items():
            log(step, k, c.frames())

    # pings: a capability query answered nonce + 1, the V12 query with its extras, any other echoed
    p = Client(port)
    for v in (0x4c414f51, 0x4c414f70, 1234):
        p.send(b'\x07' + struct.pack('<I', v))
    got('pings', p=p)
    p.close()

    # sessions
    alice = session(port, 1, name=b'Alice')
    log('host a private session', 'svc', alice)
    _, _, a_tok, a_pw, a_lobby = parse_session(alice)
    code = a_lobby[4:10]
    log('join with a different build', 'svc', session(port, 3, code, name=b'Bob', build=b'15.0'))
    log('join a room that is not there', 'svc', session(port, 3, b'ZZZZZZ', name=b'Bob'))
    bob = session(port, 3, code, name=b'Bob')
    log('join by code', 'svc', bob)
    _, _, b_tok, _, _ = parse_session(bob)
    log('a name taken', 'svc', session(port, 3, code, name=b'bob'))
    log('back with the token', 'svc', session(port, 3, code, token=b_tok, name=b'Bob'))
    log('start before ready', 'svc', session(port, 6, code, token=a_tok, name=b'Alice'))
    log('not the host starts', 'svc', session(port, 6, code, token=b_tok, name=b'Bob'))
    log('rename while not ready', 'svc', session(port, 9, code, token=b_tok, name=b'Bobby'))
    log('rename to a name taken', 'svc', session(port, 9, code, token=b_tok, name=b'Alice'))
    log('a new code with two in', 'svc', session(port, 8, code, token=a_tok, name=b'Alice'))
    log('lobby before the start', 'hello', hello(port, b'Alice', a_lobby, a_pw).frames())
    log('ready', 'svc', session(port, 5, code, token=a_tok, name=b'Alice', ready=True))
    log('ready', 'svc', session(port, 5, code, token=b_tok, name=b'Bobby', ready=True))
    log('start', 'svc', session(port, 6, code, token=a_tok, name=b'Alice'))
    log('poll', 'svc', session(port, 4, code, token=b_tok, name=b'Bobby'))
    log('wrong mode', 'svc', session(port, 0, mode=0))
    pub = session(port, 2, name=b'Carol', title=b'Open World')
    log('host a public session', 'svc', pub)
    _, _, c_tok, _, _ = parse_session(pub)
    log('list', 'svc', session(port, 0, search=b'open'))
    log('host twice', 'svc', session(port, 2, token=c_tok, name=b'Carol'))
    c_code = parse_session(pub)[4][4:10]
    renewed = session(port, 8, c_code, token=c_tok, name=b'Carol')
    log('a new code alone', 'svc', renewed)
    log('leave', 'svc', session(port, 7, parse_session(renewed)[4][4:10], token=c_tok, name=b'Carol'))
    log('expired membership', 'svc', session(port, 4, b'ZZZZZZ', token=b'x', name=b'Carol'))

    # the room's lobby
    log('not a member', 'hello', hello(port, b'Mallory', a_lobby, a_pw).frames())
    log('wrong password', 'hello', hello(port, b'Alice', a_lobby, b'nope').frames())
    a = hello(port, b'Alice', a_lobby, a_pw)
    got('alice joins', a=a)
    a.send(b'\x02' + bytes(range(27)))                 # state
    a.send(b'\x09\x0c\x10')                             # vitals
    a.send(b'\x0a' + b'\x01\x02\x03\x04mapname')       # location
    a.send(b'\x0b' + bytes(range(18)))                  # appearance
    a.send(b'\x0f' + bytes(range(17)))                  # marker
    a.send(b'\x03' + struct.pack('<I', 77))             # a flag
    a.send(b'\x04' + struct.pack('<Hh', 5, 2))          # an item
    a.send(b'\x0c' + bytes(32))                         # co-op seed
    got('alice alone', a=a)
    b = hello(port, b'Bobby', a_lobby, a_pw)
    got('bobby joins (replays)', a=a, b=b)
    log('a name taken in the lobby', 'hello', hello(port, b'BOBBY', a_lobby, a_pw).frames())
    b.send(b'\x03' + struct.pack('<I', 77)); b.send(b'\x03' + struct.pack('<I', 78))
    b.send(b'\x04' + struct.pack('<Hh', 5, 1)); b.send(b'\x04' + struct.pack('<Hh', 5, 3))
    b.send(b'\x10' + s8(b'hello there')); b.send(b'\x11\x01')
    b.send(b'\x0d' + bytes(8) + struct.pack('<HHh', 0x2a, 1, -1) + bytes(3))
    b.send(b'\x0c' + bytes(32)); b.send(b'\x0c' + bytes(31) + b'\x01')
    got('progress, chat, pickup, seed', a=a, b=b)
    gap = lambda: time.sleep(0.15)  # (one client's frames, then the other's: in one order on any relay)
    b.send(b'\x0c' + bytes(32)); gap()
    a.send(b'\x13\x01' + struct.pack('<HhHH', 9, 3, 1, 2)); a.send(b'\x13\x01' + struct.pack('<HhHH', 9, 3, 1, 2)); gap()
    b.send(b'\x13\x00'); gap()
    a.send(b'\x12\x01' + struct.pack('<H', 300)); a.send(b'\x12\x01' + struct.pack('<H', 300)); gap()
    b.send(b'\x12\x00'); gap()
    a.send(b'\x0e' + bytes([1] * 10 + [0] * 57)); gap()
    b.send(b'\x0e' + bytes([2] * 67)); gap()
    a.send(b'\x1a\x00' + bytes(32) + b'\x05'); gap()
    b.send(b'\x1a\x00' + bytes(32) + b'\x02')
    b.send(b'\x14\x00' + bytes(32))
    b.send(b'\x19' + bytes(13))
    got('rewards, checks, inventory, story, death', a=a, b=b)
    a.send(b'\x07' + struct.pack('<I', 0x4c414f70)); a.send(b'\x1c\x00\x01'); gap()
    b.send(b'\x07' + struct.pack('<I', 0x4c414f70)); b.send(b'\x1c\x00\x01')
    got('pvp on', a=a, b=b)
    a.send(b'\x1c\x01\x01\x03'); a.send(b'\x1c\x01\x01\x03')  # (the second: too soon)
    a.send(b'\x1c\x02' + bytes(17)); a.send(b'\x1c\x03\x01')
    got('pvp hits', a=a, b=b)
    time.sleep(0.9)
    a.send(b'\x1c\x01\x01')
    got('pvp hit later', a=a, b=b)
    c3 = hello(port, b'Lone', b'pvp-lobby')
    c3.send(b'\x1c\x00\x01')
    log('pvp before the V12 query', 'c', c3.frames())
    log('pvp before: closed', 'c', [b'closed' if c3.closed else b'open'])
    b.close()
    got('bobby leaves', a=a)
    a.close()

    # a full lobby, Global
    full = [hello(port, b'P%d' % i, b'full') for i in range(4)]
    for c in full:
        c.frames()
    log('full', 'hello', hello(port, b'P9', b'full').frames())
    log('global without its handshake', 'hello', hello(port, b'G', b'V10-GLOBAL').frames())
    g = hello(port, b'G', b'whatever', b'pw', t=0x31)
    log('global', 'g', g.frames())
    log('global count', 'svc', one_shot(port, b'\x33'))
    g.close()
    time.sleep(0.3)  # (its leave first: the log lines in one order)
    for c in full:
        c.close()

    # leaderboards
    run = b'\x32\x01\x01' + s8(b'id1') + s8(b'Alice,Bobby') + b'\x01' + struct.pack('<III', 3600, 50, 2)
    log('board run', 'svc', one_shot(port, run))
    log('board run', 'svc', one_shot(port, b'\x32\x01\x01' + s8(b'id2') + s8(b'Carol') + b'\x02' + struct.pack('<III', 5000, 0, 0)))
    log('board page', 'svc', one_shot(port, b'\x32\x02\x00\x03\x01\x00\x00\x00'))
    log('board page cut short (dropped)', 'svc', one_shot(port, b'\x32\x02\x00\x03\x01\x00\x00'))
    log('board page (one player)', 'svc', one_shot(port, b'\x32\x02\x00\x03\x01\x01\x00\x00'))

    # a seed
    data = bytes((i * 7) & 255 for i in range(10000))
    up = Client(port)
    up.send(b'\x16\x01' + s8(b'Alice') + s8(b'L.rando') + s8(b'pw') + bytes(32) + struct.pack('<I', len(data)) +
            hashlib.sha256(data).digest())
    log('seed upload starts', 'up', up.frames())
    for i in range(0, len(data), 3000):
        up.send(b'\x17' + data[i:i + 3000])
    up.send(b'\x18')
    log('seed upload', 'up', up.frames())
    log('seed download, wrong password', 'down', one_shot(port, b'\x16\x00' + s8(b'Bob') + s8(b'L.rando') + s8(b'no')))
    log('seed download', 'down', one_shot(port, b'\x16\x00' + s8(b'Bob') + s8(b'L.rando') + s8(b'pw')))


def record(cmd, port):
    env = dict(os.environ)
    ucrt = 'C:/msys64/ucrt64/bin'  # (the dev build's DLLs, as tools/coop_tests/run_test.sh puts them on PATH)
    if os.name == 'nt' and os.path.isdir(ucrt):
        env['PATH'] = ucrt + os.pathsep + env.get('PATH', '')
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    for _ in range(100):  # (listening)
        try:
            socket.create_connection(('127.0.0.1', port), timeout=0.2).close()
            break
        except OSError:
            time.sleep(0.1)
    steps = []
    names = {}

    def name_randoms(b):  # (codes, tokens, passwords: named in the order they appear)
        if b and b[0] == 0xB0 and len(b) > 2 and b[1] == 1:
            o = 2
            n = b[o]; o += 1 + n
            for _ in range(3):
                n = b[o]
                v = b[o + 1:o + 1 + n]
                o += 1 + n
                if v.startswith(b'V10-'):
                    v = v[4:10]
                if v and v not in names:
                    names[v] = b'<%d>' % len(names)
        for k in sorted(names, key=len, reverse=True):
            b = b.replace(k, names[k])
        return b

    try:
        scenario(port, lambda step, who, frames: steps.append((step, who, [name_randoms(f) for f in frames])))
    finally:
        proc.kill()
        out = proc.communicate()[0]
    for k in sorted(names, key=len, reverse=True):
        out = out.replace(k, names[k])
    return steps, out.decode(errors='replace').replace('\r', '')


def main():
    native = None
    if '--native' in sys.argv:
        native = shlex.split(sys.argv[sys.argv.index('--native') + 1])
    else:
        native = [os.path.join(ROOT, 'build', 'lasrecomp.exe'), '--relay', '{port}']
    a, a_log = record([sys.executable, '-u', os.path.join(ROOT, 'tools', 'laonline_relay.py'), '47651'], 47651)
    os.environ.setdefault('LAONLINE_HOST_BIND', '127.0.0.1')  # (this PC only: no firewall asks)
    b, b_log = record([x.replace('{port}', '47652') for x in native], 47652)
    fail = 0
    for (step, who, fa), (_, _, fb) in zip(a, b):
        same = fa == fb
        fail |= not same
        print(f'{"PASS" if same else "FAIL"} {step} ({who}: {len(fa)} frame(s))')
        if not same:
            print('   stand-in:', [f.hex() for f in fa][:6])
            print('   game:    ', [f.hex() for f in fb][:6])
    if len(a) != len(b):
        print('FAIL steps differ', len(a), len(b))
        fail = 1
    la = [l for l in a_log.splitlines() if not l.startswith('relay listening')]
    lb = [l for l in b_log.splitlines() if not l.startswith('relay listening')]
    same = la == lb
    print(f'{"PASS" if same else "FAIL"} the same log lines ({len(la)})')
    if not same:
        import difflib
        print('\n'.join(list(difflib.unified_diff(la, lb, 'stand-in', 'game', lineterm=''))[:40]))
        fail = 1
    return fail


if __name__ == '__main__':
    sys.exit(main())
