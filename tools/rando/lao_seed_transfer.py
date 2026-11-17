"""Verified seed transport. Import applies patches; it never invokes the randomizer.

ROMFS entries are XOR patches bound to SHA-256 of a file in the recipient's own
game installation. No original ROMFS files or saves are archived.
"""
from pathlib import Path, PurePosixPath
import hashlib, json, os, re, struct, sys, zipfile, zlib
import socket, tempfile

GENERATORS = {'beta_5.3':'LASRecomp-beta_5.3+LAO-community3', 'beta_5.4':'LASRecomp-beta_5.4+LAO-community3'}
GENERATOR = GENERATORS['beta_5.4']
def generation(root):
    marker=Path(root)/'generator.version'
    if not marker.is_file():return 'beta_5.3',GENERATORS['beta_5.3']
    lines=marker.read_text(encoding='utf-8').splitlines()
    if len(lines)!=2 or GENERATORS.get(lines[0])!=lines[1]:raise ValueError('Unsupported generator provenance')
    return lines[0],lines[1]
TOP = {'generator.version','settings.json','request.json','seed_id.txt','flags_ext','logic.json','tracker.json',
       'hint_steps.json','spheres.json','hints.txt','spoiler.txt','spoiler_view.txt'}
HASH_TOP = {'flags_ext','logic.json','tracker.json','hint_steps.json','spheres.json'}
LIMIT = 1024 * 1024 * 1024  # V13.5: per file (was 64 MB: big seeds failed with 'Seed file exceeds transfer limit')

def sha(data): return hashlib.sha256(data).hexdigest()
def safe_name(name):
    return isinstance(name,str) and 0 < len(name) <= 90 and name not in ('.','..') and re.fullmatch(r'[A-Za-z0-9 _()\-]+',name)
def identity(root):
    root=Path(root); names=[]
    for sub in ('romfs','exefs'):
        if not (root/sub).is_dir(): raise ValueError('Incomplete world: '+sub+' missing')
        names += [p.relative_to(root).as_posix() for p in (root/sub).rglob('*') if p.is_file()]
    if not names or not (root/'settings.json').is_file(): raise ValueError('Incomplete settings/world')
    names += ['settings.json']
    names += [p.name for p in root.iterdir() if p.is_file() and (p.suffix=='.tsv' or p.name in HASH_TOP)]
    h=hashlib.sha256(b'LAO15-1.0.0-inventory-v1\0')
    for name in sorted(names):
        data=(root/name).read_bytes();h.update(name.encode()+b'\0');h.update(struct.pack('<Q',len(data)));h.update(data)
    return h.hexdigest()
def label(root):
    file=Path(root)/'seed_display_name.txt'
    name=file.read_text().strip() if file.is_file() else Path(root).name
    if not safe_name(name): raise ValueError('Seed display name is unsupported')
    return name
def permitted(rel):
    p=PurePosixPath(rel)
    if '\\' in rel or ':' in rel or p.is_absolute() or '..' in p.parts or any(x.startswith('.') for x in p.parts): return False
    if rel.startswith('romfs/'): return len(p.parts)>1
    if rel in ('exefs/1.0.0.pchtxt','exefs/1.0.1.pchtxt','exefs/1.1.0.pchtxt'): return True
    return len(p.parts)==1 and (rel in TOP or p.suffix=='.tsv')
def xor(data, base):
    # V13.5: same result as the old per-byte loop (base shorter -> 0 past its end), but fast on big files
    base=base[:len(data)].ljust(len(data),b'\0')
    return (int.from_bytes(data,'little')^int.from_bytes(base,'little')).to_bytes(len(data),'little')
def checked_file(root, rel):
    if not permitted(rel): raise ValueError('Unsupported package path: '+rel)
    root=Path(root).resolve();path=(root/rel)
    if not path.resolve().is_relative_to(root) or path.is_symlink(): raise ValueError('Unsafe seed path')
    data=path.read_bytes()
    if len(data)>LIMIT: raise ValueError('Seed file exceeds transfer limit')
    return data
def compatible(root, seed_id):
    settings=json.loads((root/'settings.json').read_text())
    if settings.get('seed')!=seed_id: raise ValueError('Canonical ID disagrees with settings.json')
    for name in ('tracker.tsv','logic.json','flags.tsv','check_flags.tsv'):
        if not (root/name).is_file(): raise ValueError('Incomplete seed: '+name+' missing')
    if not re.fullmatch(r'[0-9A-HJKMNP-TV-Z-]{20,95}',seed_id): raise ValueError('Invalid canonical ID')
def save_proof(root,m):
    proof='LAO-SEED-2\nbaseline='+m['baseline']+'\ngame=1.0.0\ngenerator='+m['generator']+'\nseed_id='+m['seed_id']+'\nworld_sha256='+m['world_sha256']+'\ndisplay_name='+m['name']+'\n'
    (root/'seed_identity.laoseed').write_text(proof,encoding='utf-8')

def export_seed(root, base, destination, replace=False):
    root=Path(root).resolve();base=Path(base).resolve();destination=Path(destination)
    seed_id=(root/'seed_id.txt').read_text().strip();compatible(root,seed_id)
    name=label(root);full_hash=identity(root)
    entries=[];blobs=[];total=0;alternatives={}
    for path in sorted(root.rglob('*')):
        rel=path.relative_to(root).as_posix()
        if path.is_symlink(): raise ValueError('Seed symlinks cannot be exported')
        if not path.is_file() or not permitted(rel): continue
        data=checked_file(root,rel);total+=len(data)
        if len(entries)>=30000 or total>1024**3: raise ValueError('Seed package too large')
        entry={'path':rel,'sha256':sha(data),'size':len(data),'blob':f'data/{len(entries):05d}'}
        if rel.startswith('romfs/'):
            original=base/rel[6:]
            if not original.is_file():
                ext=path.suffix
                if ext not in alternatives: alternatives[ext]=sorted(p for p in base.rglob('*'+ext) if p.is_file() and not p.is_symlink())
                if not alternatives[ext]: raise ValueError('Own game lacks a patch base for '+rel)
                original=alternatives[ext][0]
            original=original.resolve()
            if not original.is_relative_to(base) or original.stat().st_size>LIMIT: raise ValueError('Invalid game patch base')
            before=original.read_bytes()
            # Only changed bytes are meaningful; unchanged bytes become zero.
            delta=xor(data,before)
            entry.update(kind='xor',base=original.relative_to(base).as_posix(),base_sha256=sha(before))
            blobs.append(zlib.compress(delta,9))
        else: entry['kind']='metadata';blobs.append(data)
        entries.append(entry)
    baseline,generator=generation(root)
    manifest={'format':'LAO-WORLD-PATCH-1','baseline':baseline,'game':'1.0.0','generator':generator,
              'name':name,'seed_id':seed_id,'world_sha256':full_hash,'settings_sha256':sha((root/'settings.json').read_bytes()),'entries':entries}
    if not replace and destination.exists():raise FileExistsError('Export already exists; original preserved')
    fd,temporary=tempfile.mkstemp(prefix='.lao-export-',dir=destination.parent)
    try:
        with os.fdopen(fd,'w+b') as output:
            with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,6) as z:
                z.writestr('manifest.json',json.dumps(manifest,sort_keys=True,separators=(',',':')))
                for entry,blob in zip(entries,blobs): z.writestr(entry['blob'],blob)
            output.flush();os.fsync(output.fileno())
        with zipfile.ZipFile(temporary) as z:
            if z.testzip() is not None: raise ValueError('Export verification failed')
        if identity(root)!=full_hash: raise ValueError('Source world changed during export; original download preserved')
        if replace:os.replace(temporary,destination)
        else:os.link(temporary,destination)
    finally:
        if os.path.exists(temporary):os.unlink(temporary)
    print('Exported exact named world '+name+' '+seed_id+' '+full_hash,flush=True)

def import_seed(package,base,rando):
    base=Path(base).resolve();rando=Path(rando).resolve();rando.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(package) as z:
        infos=z.infolist()
        if len(infos)>30001 or sum(i.file_size for i in infos)>1024**3 or len({i.filename for i in infos})!=len(infos): raise ValueError('Oversized or duplicate ZIP entries')
        if z.getinfo('manifest.json').file_size>8*1024*1024: raise ValueError('Oversized manifest')
        m=json.loads(z.read('manifest.json'))
        if m.get('format')!='LAO-WORLD-PATCH-1' or m.get('baseline') not in GENERATORS or m.get('game')!='1.0.0': raise ValueError('Game/package version mismatch')
        if m.get('generator')!=GENERATORS[m['baseline']]: raise ValueError('Generator compatibility mismatch')
        if not safe_name(m.get('name')) or not re.fullmatch('[0-9a-f]{64}',m.get('world_sha256','')): raise ValueError('Invalid seed identity metadata')
        entries=m.get('entries');seen=set()
        if not isinstance(entries,list) or not entries or len(entries)>30000: raise ValueError('Invalid package entries')
        for e in entries:
            rel=e['path']
            if not permitted(rel) or rel in seen or type(e['size'])!=int or not 0<=e['size']<=LIMIT or not re.fullmatch('[0-9a-f]{64}',e['sha256']): raise ValueError('Invalid seed file metadata')
            seen.add(rel)
        # Repeated imports resolve the same world, independently of its local alias.
        for existing in sorted(rando.iterdir()):
            if existing.name.startswith('.') or not existing.is_dir() or existing.is_symlink(): continue
            try:
                if (existing/'seed_id.txt').read_text().strip()==m['seed_id'] and identity(existing)==m['world_sha256']:
                    compatible(existing,m['seed_id']);(existing/'seed_display_name.txt').write_text(m['name']+'\n',encoding='utf-8');save_proof(existing,m)
                    print(json.dumps({'event':'seed.patch_import','host_id':m['seed_id'],'client_id':m['seed_id'],'host_world_sha256':m['world_sha256'],'client_world_sha256':identity(existing),'host_settings_sha256':m['settings_sha256'],'client_settings_sha256':sha((existing/'settings.json').read_bytes()),'generator':m['generator'],'prepared_name':m['name'],'active_folder':existing.name,'result':'existing_exact_world_reused'},sort_keys=True),flush=True)
                    return existing.name
            except (OSError,ValueError): pass
        stage=rando/('.i-'+m['world_sha256'][:12])
        if stage.exists(): raise ValueError('A previous incomplete import is retained safely. No new folder was created; see transfer log.')
        stage.mkdir()
        try:
            for e in entries:
                blob=z.read(e['blob'])
                if e['kind']=='xor' and e['path'].startswith('romfs/'):
                    rel=PurePosixPath(e['base'])
                    if rel.is_absolute() or '..' in rel.parts or '\\' in str(rel) or ':' in str(rel): raise ValueError('Unsafe game base path')
                    original=(base/str(rel)).resolve()
                    if not original.is_relative_to(base) or original.stat().st_size>LIMIT: raise ValueError('Invalid game base')
                    before=original.read_bytes()
                    if sha(before)!=e['base_sha256']: raise ValueError('Game-data version/content mismatch for '+e['path'])
                    decoder=zlib.decompressobj();delta=decoder.decompress(blob,e['size']+1)
                    if len(delta)!=e['size'] or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail: raise ValueError('Invalid bounded patch')
                    data=xor(delta,before)
                elif e['kind']=='metadata' and not e['path'].startswith('romfs/'): data=blob
                else: raise ValueError('Package contains an unsupported raw asset')
                if len(data)!=e['size'] or sha(data)!=e['sha256']: raise ValueError('World-data checksum mismatch at '+e['path'])
                target=stage/e['path'];target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
            compatible(stage,m['seed_id'])
            if identity(stage)!=m['world_sha256']: raise ValueError('Full world checksum mismatch')
            if sha((stage/'settings.json').read_bytes())!=m['settings_sha256']: raise ValueError('Settings checksum mismatch')
            (stage/'seed_display_name.txt').write_text(m['name']+'\n',encoding='utf-8')
            save_proof(stage,m)
            (stage/'generator.version').write_text(m['baseline']+'\n'+m['generator']+'\n',encoding='utf-8')
            (stage/'lao_source_identity.json').write_text(json.dumps({k:m[k] for k in ('name','seed_id','world_sha256','generator')}),encoding='utf-8')
            destination=rando/m['name'];suffix=2
            while destination.exists(): destination=rando/(m['name']+' ('+str(suffix)+')');suffix+=1
            stage.rename(destination)
            print(json.dumps({'event':'seed.patch_import','host_id':m['seed_id'],'client_id':m['seed_id'],'host_world_sha256':m['world_sha256'],'client_world_sha256':identity(destination),'host_settings_sha256':m['settings_sha256'],'client_settings_sha256':sha((destination/'settings.json').read_bytes()),'generator':m['generator'],'prepared_name':m['name'],'active_folder':destination.name,'result':'verified_without_generation'},sort_keys=True),flush=True)
            return destination.name
        except Exception:
            # Delete only this newly created, contained, unactivated staging tree.
            import shutil
            if stage.parent==rando and stage.name=='.i-'+m['world_sha256'][:12] and not stage.is_symlink(): shutil.rmtree(stage)
            raise

def connection(config):
    cfg={}
    for line in Path(config).read_text(encoding='utf-8-sig').splitlines():
        line=line.split(';',1)[0]
        if '=' in line:
            k,v=line.split('=',1);cfg[k.strip()]=v.strip()
    endpoint=cfg.get('server','127.0.0.1:47620');host,sep,port=endpoint.rpartition(':')
    if not sep:host,port=endpoint,'47620'
    sock=socket.create_connection((host,int(port)),timeout=8);sock.settimeout(30)
    def string(text):
        data=text.encode('utf-8')
        if len(data)>255:raise ValueError('Configuration field too long')
        return bytes([len(data)])+data
    credentials=string(cfg.get('name','Player'))+string(cfg.get('lobby','default')[:26]+'.rando')+string(cfg.get('password',''))
    return sock,credentials
def send(sock,opcode,data=b''):
    body=bytes([opcode])+data;sock.sendall(struct.pack('<H',len(body))+body)
def receive(sock):
    def take(n):
        data=b''
        while len(data)<n:
            part=sock.recv(n-len(data))
            if not part:raise ValueError('Relay closed seed transfer')
            data+=part
        return data
    n=struct.unpack('<H',take(2))[0]
    if not 1<=n<=4096:raise ValueError('Invalid relay frame')
    return take(n)
def publish_seed(root,base,config):
    root=Path(root);world=identity(root)
    with tempfile.TemporaryDirectory(prefix='lao-publish-') as tmp:
        file=Path(tmp)/'world.laoseed';export_seed(root,base,file);data=file.read_bytes()
        if len(data)>256*1024*1024:raise ValueError('Seed is over 256 MB - too big to send through the server; use Download Seed instead')
        sock,credentials=connection(config)
        with sock:
            send(sock,0x16,b'\1'+credentials+bytes.fromhex(world)+struct.pack('<I',len(data))+bytes.fromhex(sha(data)))
            if receive(sock)!=b'\x9d\1':raise ValueError('Relay refused seed upload; update relay and check active host world')
            for offset in range(0,len(data),4000):send(sock,0x17,data[offset:offset+4000])
            send(sock,0x18)
            if receive(sock)!=b'\x9d\1':raise ValueError('Relay rejected package identity or checksum')
        print('Seed uploaded: exact named world '+label(root)+' '+world,flush=True)
def sync_seed(config,base,rando):
    print('Connecting to Host Seed...',flush=True);sock,credentials=connection(config)
    with tempfile.TemporaryDirectory(prefix='lao-sync-') as tmp:
        file=Path(tmp)/'host.laoseed'
        with sock:
            send(sock,0x16,b'\0'+credentials);reply=receive(sock)
            if len(reply)!=38 or reply[:2]!=b'\x9d\1':raise ValueError("The host's seed is not on the server yet. The host's game uploads it automatically - trying again shortly.")
            size=struct.unpack('<I',reply[2:6])[0];expected=reply[6:38].hex()
            if not 0<size<=256*1024*1024:raise ValueError('Invalid host package size')
            print('Synchronizing World Data...',flush=True)
            with file.open('xb') as output:
                received=0
                while received<size:
                    chunk=receive(sock)
                    if chunk[0]!=0x9e or len(chunk)<2 or received+len(chunk)-1>size:raise ValueError('Invalid seed transfer chunk')
                    output.write(chunk[1:]);received+=len(chunk)-1
                if receive(sock)!=b'\x9f':raise ValueError('Incomplete seed transfer')
        if sha(file.read_bytes())!=expected:raise ValueError('Downloaded package checksum mismatch')
        print('Checking Randomizer Settings / Verifying World Checksum...',flush=True)
        return import_seed(file,base,rando)

def main(argv):
    try:
        if argv[0]=='--lao-export': export_seed(*argv[1:4],replace=len(argv)>4 and argv[4]=='replace')
        elif argv[0]=='--lao-publish': publish_seed(*argv[1:4])
        elif argv[0]=='--lao-sync':
            name=sync_seed(*argv[1:4]);Path(argv[4]).write_text(name+'\n',encoding='utf-8');print('Seed Synchronized! '+name,flush=True)
        elif argv[0]=='--lao-import':
            name=import_seed(*argv[1:4]);Path(argv[4]).write_text(name+'\n',encoding='utf-8');print('Seed synchronized: '+name,flush=True)
        else: raise ValueError('Invalid seed transfer operation')
        return 0
    except Exception as e:
        print('Seed transfer failed: '+str(e),flush=True);return 1
