"""V15.2: Co-op Randomizer seed hand-off (LAO-7VVLF / LAO-XADXC "lobbies not sending seed").
Real relay (Server.handle) + the real seed helper's protocol code (tools/rando/lao_seed_transfer.py); a small stand-in
world package instead of a real seed (no game files)."""
import asyncio, os, sys, tempfile, threading, time, io, zipfile, json, hashlib, struct
here=os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0,os.path.join(here,'..','relay'));sys.path.insert(0,os.path.join(here,'..','tools','rando'))
import server as S
import lao_seed_transfer as T
ok=True
def check(n,c):
    global ok;ok&=bool(c);print(f"{n:<66} {'PASS' if c else 'FAIL'}")
def start(data):
    srv=S.Server(4,data);loop=asyncio.new_event_loop();port=[]
    def run():
        asyncio.set_event_loop(loop);s=loop.run_until_complete(asyncio.start_server(srv.handle,'127.0.0.1',0));port.append(s.sockets[0].getsockname()[1]);loop.run_forever()
    threading.Thread(target=run,daemon=True).start()
    while not port:time.sleep(.05)
    return port[0]
d=tempfile.mkdtemp();world='4e7da78467497eb0a58e871659c0d45f08da47c7e7b332e526b5933a6300f8e1'
b=io.BytesIO()
with zipfile.ZipFile(b,'w') as z:z.writestr('manifest.json',json.dumps({'format':'LAO-WORLD-PATCH-1','baseline':'beta_5.4','seed_id':'5ZX05-BWRR6-40004-6CSAX-GE000-0003F','world_sha256':world,'name':'MightyBowWow115'}));z.writestr('patch','y'*50000)
data=b.getvalue();cfg=os.path.join(d,'room.ini')
def ini(port,name):open(cfg,'w').write(f'server = 127.0.0.1:{port}\nname = {name}\nlobby = V10-XAJDLT\npassword = \n')
def download():
    sock,cred=T.connection(cfg)
    with sock:T.send(sock,0x16,b'\0'+cred);return T.receive(sock)
p1=start(d);ini(p1,'kit')
check('before the host uploads: joiner told to wait',download()[:2]==b'\x9d\x00')
ini(p1,'Velvet');sock,cred=T.connection(cfg)
with sock:
    T.send(sock,0x16,b'\1'+cred+bytes.fromhex(world)+struct.pack('<I',len(data))+hashlib.sha256(data).digest());a=T.receive(sock)
    for o in range(0,len(data),4000):T.send(sock,0x17,data[o:o+4000])
    T.send(sock,0x18);done=T.receive(sock)
check('host uploads the seed',a==b'\x9d\x01' and done==b'\x9d\x01')
ini(p1,'kit');r=download();check('joiner gets it',len(r)==38 and r[:2]==b'\x9d\x01')
p2=start(d);ini(p2,'kit');r=download()
check('V15.2: after a SERVER RESTART (update) the joiner still gets it',len(r)==38 and r[:2]==b'\x9d\x01')
sys.exit(0 if ok else 1)
