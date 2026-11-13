"""V13: the relay must not keep leaderboards.sqlite3 open between requests (Windows: file could not be deleted)."""
import os, struct, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'relay'))
import server
from protocol import sstr

def open_handles(path):
    n = 0
    for fd in os.listdir('/proc/self/fd'):
        try: n += os.path.realpath('/proc/self/fd/' + fd) == os.path.realpath(path)
        except OSError: pass
    return n

d = tempfile.mkdtemp(); f = os.path.join(d, 'leaderboards.sqlite3')
lb = server.Leaderboards(d)
ok = []
ok.append(('file not held open after start', open_handles(f) == 0))
sub = bytes([2, 1]) + sstr('run-abcdefgh') + sstr('Velvet,Sanic') + struct.pack('<BIIIB', 1, 3600, 50, 2, 1)
r = lb.handle(sub); ok.append(('run recorded', r[3] == 1))
ok.append(('file not held open after a submit', open_handles(f) == 0))
q = bytes([2, 0]) + struct.pack('<BBBH', 3, 0, 0, 0)
r = lb.handle(q); total = struct.unpack_from('<H', r, 3 + 1 + 1 + len('Player-reported completed runs; not independently verified.'))[0]
ok.append(('run listed', total == 1))
ok.append(('file not held open after a query', open_handles(f) == 0))
os.remove(f)
r = lb.handle(q); ok.append(('file deleted while relay runs: board answers (empty)', r[3] == 1 and open_handles(f) == 0))
for name, good in ok: print(f'{name:<60} {"PASS" if good else "FAIL"}')
sys.exit(0 if all(g for _, g in ok) else 1)
