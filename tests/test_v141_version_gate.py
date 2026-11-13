# V14.1: older LA-Online versions can't play on a V14.1 server (HELLO version), rooms need the same version.
import os,sys,struct
sys.path.insert(0,os.path.join(os.path.dirname(__file__),'..','relay'))
import server as S
from protocol import *
class W:
    def __init__(s):s.out=[];s.transport=s
    def is_closing(s):return False
    def get_write_buffer_size(s):return 0
    def write(s,b):s.out.append(b)
ok=True
def T(n,c):
    global ok;print('%-62s %s'%(n,'PASS' if c else 'FAIL'));ok&=bool(c)
srv=S.Server(4,'')
def hello(name,ver=None,social=False,lobby='LASOnline'):
    b=struct.pack('<H',1)+sstr(name)+bytes([1,2,3])+sstr(lobby)+sstr('')
    if ver is not None:b+=b'LAOV'+sstr(ver)
    w=W();p,l=srv.on_hello(Reader(b),w,social=social)
    err=[x for x in w.out if x[2]==S_ERROR]
    return p,(err[0][5:5+err[0][4]].decode() if err else '')
p,e=hello('Old13');T('V13 / V14 game (no version sent): refused',p is None and 'UPDATE NOW' in e);print('   ',e)
p,e=hello('Old14',ver='14.1');T('V14.1 game: refused by a V15 server',p is None and 'V15' in e)
p,e=hello('V151',ver='15.1');T('V15.2 server: a V15.1 game is told to update',p is None and 'V15.2' in e and 'UPDATE NOW' in e);print('   ',e)
p,e=hello('New',ver='15.2');T('V15.2 game: plays',p is not None)
p,e=hello('Newer',ver='15.3');T('a newer game (V15.3): plays',p is not None)
p,e=hello('GlobalOld',social=True);T('Global: older version refused too',p is None and 'UPDATE NOW' in e)
p,e=hello('GlobalNew',ver='15.2',social=True);T('Global: V15.2 plays',p is not None)
os.environ['LAO_MIN_CLIENT_VERSION']='15.3'
p,e=hello('Bumped',ver='15.2');T('server set to V15.3: V15.2 refused (next release)',p is None and 'V15.3' in e)
del os.environ['LAO_MIN_CLIENT_VERSION']
T('version numbers: 15.2 > 15.1 > 15 > 14.1 > 14 > 13.1',version_rank('15.2')>version_rank('15.1')>version_rank('15')>version_rank('14.1')>version_rank('14')>version_rank('13.1')>0 and version_rank('x')<0)
sys.exit(0 if ok else 1)
