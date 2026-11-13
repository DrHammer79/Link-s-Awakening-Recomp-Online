"""V13.5: Report a Bug with a screenshot / video goes through the relay to the webhook (webhook faked here)."""
import asyncio, json, os, socket, struct, sys, tempfile, threading, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'relay'))
os.environ['LAO_DISCORD_WEBHOOK'] = 'https://discord.com/api/webhooks/1/abc'
import server, report_backend as rb
sent = []
rb.deliver = lambda webhook, report, attachment=None: sent.append((report, attachment))
PORT = 47739
def run():
    async def main():
        srv = server.Server(4, tempfile.mkdtemp())
        s = await asyncio.start_server(srv.handle, '127.0.0.1', PORT)
        async with s: await s.serve_forever()
    asyncio.run(main())
threading.Thread(target=run, daemon=True).start(); time.sleep(0.8)
report = json.dumps({"project": "LA-Online Recomp", "category": "Multiplayer / Co-op", "severity": "High", "summary": "test",
                     "report_code": "LAO-ABCDE"}).encode()
def post(version, attachment=b'', kind=0):
    s = socket.create_connection(('127.0.0.1', PORT))
    head = struct.pack('<HBBI', 6, 0x34, 1, len(report)) if version == 1 else struct.pack('<HBBIBI', 11, 0x34, 2, len(report), kind, len(attachment))
    s.sendall(head + report + attachment)
    n = struct.unpack('<H', s.recv(2))[0]; d = b''
    while len(d) < n: d += s.recv(n - len(d))
    s.close(); return d[1] == 1, d[3:3 + d[2]].decode()
png = b'\x89PNG\r\n\x1a\n' + os.urandom(3 * 1024 * 1024)
mp4 = b'\0\0\0\x18ftypmp42' + os.urandom(8 * 1024 * 1024)
res = []
ok, m = post(1); res.append(('report without attachment delivered', ok and sent[-1][1] is None))
ok, m = post(2, png, 1); res.append(('report + 3 MB screenshot delivered with the picture', ok and sent[-1][1] == (1, png)))
ok, m = post(2, mp4, 3); res.append(('report + 8 MB MP4 video delivered', ok and sent[-1][1] == (3, mp4)))
n = len(sent); ok, m = post(2, b'MZ' + os.urandom(1000), 1); res.append(('a program renamed to .png is refused', not ok and len(sent) == n))
for name, good in res: print(f'{name:<58} {"PASS" if good else "FAIL"}')
sys.exit(0 if all(g for _, g in res) else 1)
