import struct,sys
import os;sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','relay'))
from server import Server
from protocol import sstr
srv=Server(4,None)
D=srv.directory
def req(action,code='',token='',name='A',build='15.2',mode=1,world='',seed='',ready=0,page=0):
    b=bytes([1,action])+sstr(code)+sstr(token)+sstr(name)+sstr(build)+bytes([mode])+sstr(world)+sstr(seed)+sstr('')+sstr('')+bytes([ready])+bytes(6)+sstr('')+struct.pack('<BBH',0,0,page)
    f=D.request(b)[3:];ok=f[0];o=1;l=f[o];msg=f[o+1:o+1+l].decode();o+=1+l
    if not ok:return ok,msg,''
    l=f[o];tok=f[o+1:o+1+l].decode();return ok,msg,tok
W='a'*64;S='SEED-ID-AAAAA-BBBBB-CCCCC'
def check(name,cond):print(f'{name:55} {"PASS" if cond else "FAIL"}')
ok,msg,host=req(2,name='Sanic',mode=2,world=W,seed=S);code=list(D.rooms)[0]
req(5,code,host,'Sanic',mode=2,world=W,seed=S,ready=1);ok,_,_=req(6,code,host,'Sanic',mode=2,world=W,seed=S);check('host starts adventure',ok and D.rooms[code]['started'])
ok,m,tv=req(3,code,'','Velvet',mode=1);check('late join, running room, no seed yet',ok)
ok,m,_=req(3,code,'','Old',build='12');check('older version refused with clear message',not ok and ('version' in m or 'UPDATE NOW' in m));print('   ',m)
ok,m,_=req(3,code,'','velvet');check('same name refused',not ok)
ok,m,tk=req(3,code,'','Kit');check('third player joins',ok)
ok,m,_=req(3,code,'','Locus');check('fourth player joins (4/4)',ok and len(D.rooms[code]['members'])==4)
ok,m,_=req(3,code,'','Lucas');check('fifth refused: session full',not ok and 'full' in m.lower());print('   ',m)
ok,m,t=req(3,code,tk,'Kit');check('reconnect with own token takes slot back',ok and t==tk and len(D.rooms[code]['members'])==4)
ok,m,_=req(7,code,tk,'Kit');check('player leaves -> 3/4',ok and len(D.rooms[code]['members'])==3)
ok,m,_=req(3,code,'','Lucas');check('new player takes freed slot',ok and len(D.rooms[code]['members'])==4)
ok,m,_=req(3,code,'','Bad',mode=2,world='zz',seed='x');check('malformed world refused',not ok)
ok,m,_=req(4,code,tv,'Velvet',mode=2);check('poll without seed accepted',ok)
ok,m,_=req(7,code,host,'Sanic',mode=2,world=W,seed=S);check('host leaves: room stays, new host',ok and code in D.rooms and D.rooms[code]['host']!=host)
