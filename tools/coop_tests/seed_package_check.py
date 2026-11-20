#!/usr/bin/env python3
"""tools/coop_tests/seed_package_check.py <package> <seed folder>: a Co-op Randomizer seed package (runtime/
laonline_seed_transfer.cpp) holds the seed folder's own files only - its manifest, then each file under seed/, every
one a file of that folder, none of its saves or old game files (romfs/) - and the seed's record as it is. Exit 0 if so
(online_seed.sh)."""
import json
import os
import sys
import zipfile

z = zipfile.ZipFile(sys.argv[1])
seed = sys.argv[2]
m = json.loads(z.read('manifest.json'))
names = [n for n in z.namelist() if n != 'manifest.json']
own = {os.path.relpath(os.path.join(d, f), seed).replace(os.sep, '/') for d, _, fs in os.walk(seed) for f in fs}
problems = []
if m.get('format') != 'LAO-WORLD-PATCH-1' or m.get('seed_format') != 'lasrecomp-seed-1':
    problems.append(f'manifest format: {m.get("format")} / {m.get("seed_format")}')
if not names:
    problems.append('no files')
for n in names:
    rel = n[5:] if n.startswith('seed/') else None
    if rel is None or rel not in own or rel.startswith(('romfs/', 'save/')):
        problems.append(f'not the seed\'s own file: {n}')
if z.read('seed/seed.json') != open(os.path.join(seed, 'seed.json'), 'rb').read():
    problems.append('seed.json differs')
for p in problems:
    print(p)
sys.exit(1 if problems else 0)
