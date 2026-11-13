"""V13.5: the relay keeps running when its save folder is deleted (host log: FileNotFoundError kicked players)."""
import os, shutil, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'relay'))
import server
d = tempfile.mkdtemp()
lob = server.Lobby('V10-TEST.rando', '', d)
lob.flags.add(5); lob._dirty = True; lob.save()
ok1 = os.path.exists(lob.path)
shutil.rmtree(d)
lob.flags.add(6); lob._dirty = True
try:
    lob.save(); ok2 = os.path.exists(lob.path)
except Exception:
    ok2 = False
print(f'{"save works":<55} {"PASS" if ok1 else "FAIL"}')
print(f'{"folder deleted while running: no crash, folder made again":<55} {"PASS" if ok2 else "FAIL"}')
sys.exit(0 if ok1 and ok2 else 1)
