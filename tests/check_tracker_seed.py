# V13.5: tracker sanity check for a seed: python tests/check_tracker_seed.py rando/<seed>/tracker.tsv [alias]
# Gives every item and reports checks that still can never be reached ("alias" = with the V13.5 Face Shrine fix).
import re,sys
rows=[l.rstrip('\r\n').split('\t') for l in open(sys.argv[1],encoding='utf-8')]
alias=len(sys.argv)>2
S=set(r[1] for r in rows if r[0]=='S')
nodes=[r for r in rows if r[0]=='N' and len(r)>=7]
checks=[r for r in rows if r[0]=='C']
if alias:
  names={n[2] for n in nodes}
  for k,v in (('D6-4D','D6-5D'),('D6-2A','D6-4D')):
    if k not in names: nodes.append(['N','room',k,'-',v,'-','-'])
inv='sword:2 shield:2 bracelet:2 bomb bow hookshot boomerang rod shovel powder feather boots ocarina bottle:3 flippers medicine tail-key slime-key angler-key face-key bird-key full-moon-cello conch-horn sea-lilys-bell surf-harp wind-marimba coral-triangle evening-calm-organ thunder-drum mushroom shell-sensor song-ballad song-mambo song-soul red-tunic blue-tunic powder-capacity bomb-capacity arrow-capacity golden-leaf:5 seashell:50 yoshi-doll ribbon dog-food bananas stick honeycomb pineapple hibiscus letter broom fishing-hook necklace mermaid-scale magnifying-lens rooster marin-companion ghost'.split()
acc={}
for t in inv:
  n,_,c=t.partition(':');acc[n]=int(c or 1)
for d in range(9): acc['key-D%d'%d]=9;acc['nightmare-key-D%d'%d]=1
for s in S: acc[s]=1
import os  # boss / mini-boss souls: the runtime's tokens (souls.c souls_at -> tracker_overlay.mm inventory)
sp=os.path.join(os.path.dirname(sys.argv[1]),'souls.tsv')
if os.path.exists(sp):
  for l in open(sp,encoding='utf-8'):
    c=l.rstrip('\r\n').split('\t')
    if len(c)>=4 and c[1].startswith('0x') and (c[0].isdigit() or c[0]=='mini'): acc['soul-'+c[3].lower().replace(' ','-')]=1
def ev(c):
  if c in ('','-'):return True
  e=re.sub(r'([A-Za-z0-9\-\[\]]+)(?::(\d+))?',lambda m:'(acc.get(%r,0)>=%s)'%(m.group(1),m.group(2) or 1),c)
  e=e.replace('&',' and ').replace('|',' or ').replace('!',' not ')
  return eval(e)
done=set();grew=True
while grew:
  grew=False
  for n in nodes:
    t,name,reg,c=n[1],n[2],n[3],n[4]
    if name in done:continue
    if t=='enemy':
      if ev(c):done.add(name);acc[name]=1;acc[name+'[pit]']=1;acc[name+'[heavy]']=1;grew=True
      else:
        for i,suf in ((5,'[pit]'),(6,'[heavy]')):
          if n[i] not in ('','-') and not acc.get(name+suf) and ev(n[i]):acc[name+suf]=1;grew=True
      continue
    ok=(t not in('item','follower')) or reg=='-' or acc.get(reg,0)>0
    if ok and ev(c):
      done.add(name);acc[name]=1;grew=True
      if t=='follower' or name=='moblin-cave':acc['bow-wow']=1
miss=[c[1] for c in checks if c[1] not in done]
print('checks',len(checks),'unreachable with everything:',len(miss));print(miss[:40])
