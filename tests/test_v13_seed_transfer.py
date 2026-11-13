import socket,struct,sys,io,json,zipfile,hashlib,tempfile,os,subprocess,time
# Start a relay first: python relay/server.py --host 127.0.0.1 --port 47731 --data <empty folder>
_h=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,os.path.join(_h,'..','relay'));sys.path.insert(0,os.path.join(_h,'..','tools','rando'))
from protocol import sstr
import lao_seed_transfer as T
PORT=47731
def x(op,body):
    s=socket.create_connection(('127.0.0.1',PORT));s.sendall(struct.pack('<H',len(body)+1)+bytes([op])+body);h=b''
    while len(h)<3:h+=s.recv(3-len(h))
    n=h[0]|h[1]<<8;d=b''
    while len(d)<n-1:d+=s.recv(n-1-len(d))
    s.close();return d
def room(action,code='',token='',name='A',mode=1,world='',seed='',ready=0):
    b=bytes([1,action])+sstr(code)+sstr(token)+sstr(name)+sstr('13')+bytes([mode])+sstr(world)+sstr(seed)+sstr('')+sstr('')+bytes([ready])+bytes(6)+sstr('')+struct.pack('<BBH',0,0,0)
    d=x(0x30,b);ok=d[0];o=1;l=d[o];msg=d[o+1:o+1+l].decode();o+=1+l
    if not ok:return ok,msg,'','',''
    out=[]
    for _ in range(3):l=d[o];out.append(d[o+1:o+1+l].decode());o+=1+l
    return ok,msg,*out
def check(n,c):print(f'{n:62} {"PASS" if c else "FAIL"}')
W=hashlib.sha256(b'world').hexdigest();SID='5152R-FK9R6-50002-6CSAZ-G7000-00033'
ok,_,host,pw,lobby=room(2,name='Sanic',mode=2,world=W,seed=SID);code=lobby[4:10]
room(5,code,host,'Sanic',2,W,SID,1);ok,*_=room(6,code,host,'Sanic',2,W,SID);check('host created and started a Co-op Randomizer room',ok)
# host publishes the world package to the ROOM lobby (what the launcher's coop_room_ini + --lao-publish does)
def ini(name,lobby,pw):
    p=tempfile.mktemp(suffix='.ini');open(p,'w').write(f'server = 127.0.0.1:{PORT}\nname = {name}\nlobby = {lobby[:-6]}\npassword = {pw}\n');return p
pkg=io.BytesIO()
with zipfile.ZipFile(pkg,'w') as z:z.writestr('manifest.json',json.dumps({'format':'LAO-WORLD-PATCH-1','baseline':'beta_5.4','seed_id':SID,'world_sha256':W,'name':'WildOwl793'}));z.writestr('patch.bin',os.urandom(int(os.environ.get('LAO_TEST_SEED_BYTES','20000'))))
data=pkg.getvalue()
sock,cred=T.connection(ini('Sanic',lobby,pw))
with sock:
    T.send(sock,0x16,b'\1'+cred+bytes.fromhex(W)+struct.pack('<I',len(data))+bytes.fromhex(T.sha(data)))
    r1=T.receive(sock)
    for off in range(0,len(data),4000):T.send(sock,0x17,data[off:off+4000])
    T.send(sock,0x18);r2=T.receive(sock)
check('host world package uploaded to the room (auto-publish)',r1==b'\x9d\1' and r2==b'\x9d\1')
# late joiner without the seed
ok,msg,tok,jpw,jlobby=room(3,code,'','Velvet',mode=1);check('Velvet joins the running room without the seed',ok and jlobby==lobby and jpw==pw)
sock,cred=T.connection(ini('Velvet',jlobby,jpw))
with sock:
    T.send(sock,0x16,b'\0'+cred);rep=T.receive(sock);size=struct.unpack('<I',rep[2:6])[0];got=b''
    while len(got)<size:c=T.receive(sock);got+=c[1:]
    tail=T.receive(sock)
check('Velvet downloads the exact package (size + SHA-256 match)',got==data and rep[6:38]==hashlib.sha256(data).digest() and tail==b'\x9f')
# wrong password cannot fetch a private room's seed
sock,cred=T.connection(ini('Mallory',jlobby,'x'*48))
with sock:T.send(sock,0x16,b'\0'+cred);rep=T.receive(sock)
check('wrong room password: seed download refused',rep==b'\x9d\0')
# V13.5: a big seed (over the old 16 MB limit) goes through too - run with LAO_TEST_SEED_BYTES=40000000
check(f'package size tested: {len(data)/1048576:.1f} MB', True)
