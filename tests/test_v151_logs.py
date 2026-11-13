"""V15.1 SEND LOGS TO DEVELOPER: the game's zip of every log goes through the relay (0x35) to the logs webhook.
Runs the real relay (Server.handle) with Discord replaced by a recorder, and the real game-side sender
(runtime/laonline_report_send.cpp, built as tests/../build-test/logs_client) as a separate program.
Usage: python3 test_v151_logs.py <path to logs_client binary>"""
import asyncio, io, os, sys, tempfile, threading, zipfile, subprocess, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'relay'))
import report_backend as rb
import server as S
ok = True
def T(n, c):
    global ok; ok &= bool(c); print(f"{n:<66} {'PASS' if c else 'FAIL'}")
sent = []
def fake_deliver(webhook, name, code, game, data):
    if 'fail' in webhook: raise ValueError('discord says no')
    sent.append((webhook, name, code, game, data))
rb.deliver_logs = fake_deliver
data_dir = tempfile.mkdtemp()
srv = S.Server(4, data_dir)
loop = asyncio.new_event_loop(); port = []
def run():
    asyncio.set_event_loop(loop)
    s = loop.run_until_complete(asyncio.start_server(srv.handle, '127.0.0.1', 0))
    port.append(s.sockets[0].getsockname()[1]); loop.run_forever()
threading.Thread(target=run, daemon=True).start()
while not port: time.sleep(.05)
work = tempfile.mkdtemp()
open(os.path.join(work, 'laonline.ini'), 'w').write(f'server = 127.0.0.1:{port[0]}\n')
zpath = os.path.join(work, 'LA-Online-Logs-Velvet.zip')
with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
    for n in ('lasrecomp.log', 'laonline-world-log.txt', 'laonline-rejoin.txt', 'laonline-pvp-research.log', 'dev-events-log.txt'):
        z.writestr(n, ('log line\n' * 5000))
def client(path):
    out = subprocess.run([sys.argv[1], path], cwd=work, capture_output=True, text=True, timeout=60).stdout.strip()
    r, _, msg = out.partition('|'); return int(r), msg
for k in ('LAO_DISCORD_LOGS_WEBHOOK', 'LAO_DISCORD_WEBHOOK'): os.environ.pop(k, None)
r, m = client(zpath); T('no webhook on the server: not sent, told to drag the zip', r == 0 and 'drag it into Discord' in m); print('   ', m)
open(os.path.join(data_dir, 'logs-webhook.txt'), 'w').write('https://discord.com/api/webhooks/1/abc\n')
r, m = client(zpath); T('logs webhook in the saves folder (logs-webhook.txt): sent', r == 1 and len(sent) == 1); print('   ', m)
w, name, code, game, data = sent[-1]
T('...to the logs webhook, with name / log code / version', w.endswith('/1/abc') and name == 'Velvet_Charm' and code == 'LAO-ABCDE' and game.startswith('V15'))
T('...the whole zip arrives intact (rejoin + pvp + world logs inside)', data == open(zpath, 'rb').read() and 'laonline-rejoin.txt' in zipfile.ZipFile(io.BytesIO(data)).namelist())
os.environ['LAO_DISCORD_LOGS_WEBHOOK'] = 'https://discord.com/api/webhooks/2/env'
r, m = client(zpath); T('LAO_DISCORD_LOGS_WEBHOOK (server .env) wins', r == 1 and sent[-1][0].endswith('/2/env'))
client(zpath)
r, m = client(zpath); T('a 4th send within 10 minutes from the same PC: refused politely', r == 0 and '3 times' in m); print('   ', m)
srv.logs_times = {}
bad = os.path.join(work, 'not-a-zip.zip'); open(bad, 'wb').write(b'hello' * 100)
r, m = client(bad); T('not a zip: rejected', r == 0 and 'rejected' in m)
srv.logs_times = {}; os.environ['LAO_DISCORD_LOGS_WEBHOOK'] = 'https://discord.com/api/webhooks/3/fail'
r, m = client(zpath); T('Discord refuses: the player is told to drag the zip in', r == 0 and 'drag it into Discord' in m)
os.environ.pop('LAO_DISCORD_LOGS_WEBHOOK')
open(os.path.join(work, 'laonline.ini'), 'w').write('server = 127.0.0.1:1\n')
r, m = client(zpath); T('server unreachable: -1 (the game shows the zip instead)', r == -1)
sys.exit(0 if ok else 1)
