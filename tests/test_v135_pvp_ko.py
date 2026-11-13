# V13.5 relay: PvP knock-outs -> kill feed to everyone in Global + pvp_kills.json counter.  python tests/test_v135_pvp_ko.py
import os,sys,struct,tempfile,json
sys.path.insert(0,os.path.join(os.path.dirname(__file__),'..','relay'))
import server as S
from protocol import *
class W:
    def __init__(s):s.out=[];s.transport=s
    def is_closing(s):return False
    def get_write_buffer_size(s):return 0
    def write(s,b):s.out.append(b)
def P(pid,name):
    w=W();p=S.Player(pid,name,(1,2,3),w);p.v12_capable=True;p.pvp=True;return p,w
def frames(w):
    r=[]
    for b in w.out:
        n=struct.unpack_from('<H',b)[0];r.append(b[2:2+n])
    w.out.clear();return r
d=tempfile.mkdtemp();srv=S.Server(4,d);ok=True
def T(n,c):
    global ok;print('%-60s %s'%(n,'PASS' if c else 'FAIL'));ok&=bool(c)
lob=srv.get_lobby('global','');lob.social=True
lob2=srv.get_lobby('global-2','');lob2.social=True
sanic,ws=P(0,'Sanic');velvet,wv=P(1,'Velvet');kit,wk=P(0,'Kit')
lob.players={0:sanic,1:velvet};lob2.players={0:kit}
def send(p,lobby,data):srv.dispatch(p,lobby,C_PVP,Reader(bytes(data)))
def _fresh():  # (V15.1: each test step starts with nobody just hit)
    for x in (sanic,velvet,kit):x.pvp_hurt_at=-99.0;x.pvp_weapon_times={}
    sanic.ko_at=-1e9  # (Sanic's own KO in the revenge step is long ago in these steps)
send(velvet,lob,[3,0]);T('KO without a recent hit by that player is ignored',not any(f[0]==S_PVP for f in frames(wk)))
send(sanic,lob,[1,1,8]);frames(wv)
send(velvet,lob,[3,0])
fv,fs,fk=frames(wv),frames(ws),frames(wk)
feed=lambda fl:[f for f in fl if f[0]==S_PVP and f[1]==3]
T('everyone in Global gets the kill feed (other lobby too)',len(feed(fv))==1 and len(feed(fs))==1 and len(feed(fk))==1)
f=feed(fs)[0];kv,vk,you=struct.unpack_from('<HHB',f,3);kl=f[8];k=f[9:9+kl].decode();vl=f[9+kl];v=f[10+kl:10+kl+vl].decode()
T('frame: Sanic 1 - Velvet 0, names right',(kv,vk,k,v)==(1,0,'Sanic','Velvet'))
T('V15: the weapon rides along (hit with 8 quarters, old 3-byte frame -> unknown 0)',f[10+kl+vl]==0)
T('"you" byte: killer 1, victim 2, others 0',you==1 and feed(fv)[0][7]==2 and feed(fk)[0][7]==0)
T('same funny line for everybody',feed(fs)[0][2]==feed(fv)[0][2]==feed(fk)[0][2])
send(velvet,lob,[3,0]);T('the same hit cannot be counted twice',not feed(frames(wk)))
j=json.load(open(os.path.join(d,'pvp_kills.json')))
T('pvp_kills.json: Sanic 1 kill, Velvet 1 fall',j['players']['sanic']['kills']==1 and j['players']['velvet']['falls']==1)
import time
velvet.ko_at-=5;sanic.pvp_hit_at-=5;sanic.pvp_hit_times={};_fresh();velvet.pvp_hit_at-=5;velvet.pvp_hit_times={}
send(velvet,lob,[1,0,8]);send(sanic,lob,[3,1]);f=feed(frames(wk))[0];kv,vk,_=struct.unpack_from('<HHB',f,3)
T('Velvet gets one back: Velvet 1 - Sanic 1',(kv,vk)==(1,1) and f[9:9+f[8]].decode()=='Velvet')
srv2=S.Server(4,d);T('the counter survives a server restart',srv2.pvp_kos.data['players']['sanic']['falls']==1)
velvet.pvp=False;velvet.ko_at-=5;send(sanic,lob,[1,1,8]);send(velvet,lob,[3,0]);T('PvP off: no KO counted',not feed(frames(wk)))
velvet.pvp=True;sanic.pvp_hit_at-=5;sanic.pvp_hit_times={};_fresh();frames(ws);frames(wv);send(sanic,lob,[1,1,4])
hm=lambda fl:[f for f in fl if f[0]==S_PVP and f[1]==4]
a,b=hm(frames(ws)),hm(frames(wv))
T('V14: both players get the hit marker (attacker, victim, damage)',len(a)==1 and len(b)==1 and tuple(a[0][2:5])==(0,1,4))
T('V14: the other Global lobby does not get the marker',not hm(frames(wk)))
lock=S.Player(2,'Locus',(1,1,1),W());lock.v12_capable=True;lock.pvp=True;lob.players[2]=lock
sanic.pvp_hit_times={};_fresh();frames(ws);send(sanic,lob,[1,1,4]);send(sanic,lob,[1,2,4])
T('V14: one blast can hit two players at once',len(hm(frames(ws)))==2)
send(sanic,lob,[1,1,4]);T('V14: the same player is not hit again within 0.45 s',not hm(frames(ws)))
for w_ in (ws,wv,wk):frames(w_)
send(velvet,lob,[5,1])
en=lambda fl:[f for f in fl if f[0]==S_PVP and f[1]==5]
a,b,c=en(frames(ws)),en(frames(wv)),en(frames(wk))
T('V14: "Velvet saved Koholint" goes to everyone in Global (other lobby too)',len(a)==1 and len(b)==1 and len(c)==1 and a[0][5:5+a[0][4]].decode()=='Velvet' and a[0][3]==1)
T('V14: Velvet herself gets the "you" version',b[0][2]==1 and a[0][2]==0)
send(velvet,lob,[5,1]);T('V14: not repeated within 2 minutes',not en(frames(ws)))
# V15: your Kills / Losses after a KO (Global) - kind 6
for w_ in (ws,wv,wk,lock.writer):frames(w_)
velvet.pvp=True;sanic.pvp=True;velvet.ko_at=-1e9;sanic.pvp_hit_times={};_fresh();send(sanic,lob,[1,1,8]);send(velvet,lob,[3,0])
tot=lambda fl:[struct.unpack_from('<HH',f,2) for f in fl if f[0]==S_PVP and f[1]==6]
a,b=tot(frames(ws)),tot(frames(wv))
T('V15: killer and victim get their Kills / Losses',len(a)==1 and len(b)==1 and a[0][0]>=1 and b[0][1]>=1);print('    Sanic',a,'Velvet',b)
# V15: PvP in a Co-op lobby (not Global): states, hits and visuals pass, no kill feed / counter
co=srv.get_lobby('LASOnline','');co.social=False
s1,w1=P(0,'Kit');s2,w2=P(1,'Locus');co.players={0:s1,1:s2}
send(s1,co,[0,1]);send(s2,co,[0,1]);frames(w1);frames(w2)
send(s1,co,[1,1,4]);T('V15 co-op: the hit reaches the other player',any(f[0]==S_PVP and f[1]==1 for f in frames(w2)))
send(s1,co,[2,3]+[0]*16);T('V15 co-op: others SEE the attack',any(f[0]==S_PVP and f[1]==2 for f in frames(w2)))
before=json.load(open(os.path.join(d,'pvp_kills.json')))
send(s2,co,[3,0]);send(s2,co,[5,0])
T('V15 co-op: a KO is not counted or announced (Global only), no error',not any(f[0]==S_PVP and f[1] in (3,5,6) for f in frames(w1)+frames(w2)) and json.load(open(os.path.join(d,'pvp_kills.json')))==before)
# V15: the weapon of the last hit is in the kill feed (4-byte hit frame)
velvet.ko_at=-1e9;sanic.pvp_hit_times={};_fresh();velvet.pvp=True;sanic.pvp=True;frames(wk)
send(sanic,lob,[1,1,8,2]);send(velvet,lob,[3,0])
f=[x for x in frames(wk) if x[0]==S_PVP and x[1]==3][0];kl=f[8];vl=f[9+kl]
T('V15: kill feed says the weapon (2 = Bombs)',f[10+kl+vl]==2)
# V15.1: boomerang cooldown, a player just hit is safe for a moment, no hits while knocked out
def hits_to(w):return [f for f in frames(w) if f[0]==S_PVP and f[1]==1]
velvet.ko_at=-1e9;sanic.ko_at=-1e9;sanic.pvp_hit_times={};_fresh();velvet.pvp=True;sanic.pvp=True;frames(wv)
send(sanic,lob,[1,1,2,5]);sanic.pvp_hit_times={};velvet.pvp_hurt_at=-99.0;send(sanic,lob,[1,1,2,5])
T('V15.1: boomerang twice in a row: only the first one hits',len(hits_to(wv))==1)
sanic.pvp_hit_times={};_fresh();send(sanic,lob,[1,1,2,3]);sanic.pvp_hit_times={};send(sanic,lob,[1,1,2,6])
T('V15.1: a player who was just hit is safe for a moment',len(hits_to(wv))==1)
sanic.pvp_hit_times={};_fresh();sanic.ko_at=__import__('time').monotonic();send(sanic,lob,[1,1,2,100])
T('V15.1: knocked out: your hits reach no one',len(hits_to(wv))==0)
sanic.ko_at=-1e9;sanic.pvp_hit_times={};_fresh();send(sanic,lob,[1,1,2,100])
T('V15.1: back up: hits work again',len(hits_to(wv))==1)
sys.exit(0 if ok else 1)
