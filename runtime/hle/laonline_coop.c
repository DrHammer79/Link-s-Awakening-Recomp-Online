#include "laonline_checks.h"
#include "laonline_story.h"
#include "laonline_inventory_view.h"
#include "laonline_reward_rules.h"
#include "../settings.h"
// V15 inventory/pickup bridge. Network thread only queues values; game thread owns all guest access.
#include "../hle.h"
#include "../game_addresses.h"
#include "../rando.h"
#include "laonline_coop_wire.h"
#include "laonline_diag.h"
#include "laonline_dev_events.h"
#include "../dev_events.h"
#include "laonline_item_rules.h"
#include "laonline_session.h"
#include "laonline_location.h"
#include "laonline_v12.h"
#include <pthread.h>
#include <stdio.h>
extern int game_in_play(void),laonline_level_ready(void);
extern bool laonline_ui_paused(void);
extern bool laonline_ui_settled(unsigned);
/* V13: remote grants start 45 frames (~0.75 s) after a menu closes, at most 4 per frame (the rest wait their turn) */
#define LAO_SETTLE_FRAMES 45
#define LAO_GRANTS_PER_FRAME 4
extern void laonline_notify(const char *);
extern void laonline_notify_item(const char *,int);
extern int laonline_seed_identity(unsigned char out[32]);
extern bool flags_ext_get(int);
extern void flags_put(int,bool);
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int capability,ready,seed_ok,setup_sent,initialized,applying;
static int world_cap,world_sent,world_ready,world_required,world_rejected;
static char world_seed_id[96];
static unsigned char seed[32];
static LaoCoopInventory remote,local,last_sent;
static int inventory_due,have_sent,enabled;
static int reward_owned(int,int);
static LaoInventoryView inventory_view;
int laonline_inventory_view(LaoInventoryView *out){pthread_mutex_lock(&lock);*out=inventory_view;pthread_mutex_unlock(&lock);return out->valid;}
static void inventory_view_sample(void){
 extern int silent_seashell_total(void);
 LaoInventoryView view={.valid=1};view.shell_total=silent_seashell_total();uint64_t save=GA(LIVE_SAVE);uint32_t keys=LD32(save+0x556c);unsigned songs=LD8(save+0x555b);
 for(int i=0;i<LAO_INVENTORY_ICONS;i++){int item=lao_inventory_icons[i];view.owned[i]=item>=120?!!(songs&(1u<<(item-120))):item>=50?!!(keys&(1u<<(item-43))):reward_owned(item,0);}
 view.shells=__builtin_popcountll(LD64(save+0x5548)&((1ull<<50)-1));view.pieces=__builtin_popcountll(LD64(save+0x5550)&0xffffffffull);view.containers=__builtin_popcount(LD16(save+0x5558)&0x1ff);
 pthread_mutex_lock(&lock);inventory_view=view;pthread_mutex_unlock(&lock);
}

static int snapshot_seen,progress_warning;
static uint64_t sequence,last_sample,last_reward_replay;
static uint64_t received_sequence[8];
void laonline_coop_peer_left(int id){if(id<0||id>=8)return;pthread_mutex_lock(&lock);received_sequence[id]=0;pthread_mutex_unlock(&lock);}
static uint16_t map_id=0xffff,room_id=0xffff;
static int dungeon_index=-1;
static uint16_t map_snapshot=0xffff;static int dungeon_map_visible;
int laonline_coop_dungeon_map(uint16_t map){pthread_mutex_lock(&lock);int ok=dungeon_map_visible && map==map_snapshot;pthread_mutex_unlock(&lock);return ok;}

#define QUEUE 256
static LaoPickup outbound[QUEUE]; static unsigned out_n;
typedef struct {LaoPickup pickup;char name[17];} Incoming;
static Incoming incoming[QUEUE];static unsigned in_n;
void laonline_coop_world_capability(void){pthread_mutex_lock(&lock);world_cap=1;pthread_mutex_unlock(&lock);}
int laonline_coop_world_ready(void){pthread_mutex_lock(&lock);int ok=!world_required||world_ready;pthread_mutex_unlock(&lock);return ok;}
void laonline_coop_world_message(const uint8_t *b,size_t n){
 if(n>=35&&b[0]==0x9b&&b[34]<=95&&n==35+b[34]){
  char host[96]={0},hh[65],ch[65];snprintf(host,sizeof host,"%.*s",b[34],b+35);
  const char *hex="0123456789abcdef";for(int i=0;i<32;i++){hh[2*i]=hex[b[i+2]>>4];hh[2*i+1]=hex[b[i+2]&15];ch[2*i]=hex[seed[i]>>4];ch[2*i+1]=hex[seed[i]&15];}hh[64]=ch[64]=0;
  laonline_diag_seed("relay_rejected",host,world_seed_id,hh,ch,rando_seed_dir(),strcmp(host,world_seed_id)?"canonical_id":"world_sha256_or_mode");return;
 }
 if(n!=34 || b[0]!=0x98)return;pthread_mutex_lock(&lock);
 if(world_sent && !memcmp(b+2,seed,32)){world_ready=b[1]==1;world_rejected=!world_ready;}
 int rejected=world_rejected;pthread_mutex_unlock(&lock);
 if(rejected)laonline_notify(b[1]==2?"Active session mode differs from your selected mode.":b[1]==3?"Active host uses a different canonical Seed ID. Import their current download.":b[1]==4?"Active host world checksum differs. Full hashes are in Report A Bug diagnostics.":"Co-op world mismatch. See seed diagnostics.");
}

static char local_name[17]="Player";
typedef struct {uint16_t item;int16_t index;uint16_t map,room;char name[17];unsigned epoch;int publish;} Reward;
static Reward reward_local[QUEUE],reward_send[QUEUE],reward_in[QUEUE];
static unsigned reward_local_n,reward_send_n,reward_in_n;
static unsigned char dev_seen_reward[128][64];
static struct {uint16_t item;int16_t index;uint8_t status;} reward_ack[QUEUE];
static unsigned reward_ack_n;
static int equipment_cap,advanced_cap,bootstrap_due;
static int reward_cap,reward_request;static unsigned reward_epoch;
void laonline_coop_advanced_capability(void){pthread_mutex_lock(&lock);advanced_cap=1;bootstrap_due=1;reward_request=1;pthread_mutex_unlock(&lock);}

void laonline_coop_equipment_capability(void){pthread_mutex_lock(&lock);equipment_cap=1;reward_request=1;pthread_mutex_unlock(&lock);}
void laonline_coop_rewards_capability(void){pthread_mutex_lock(&lock);reward_cap=1;reward_request=1;pthread_mutex_unlock(&lock);}
static uint64_t reward_bits(int item,int index){
 uint64_t save=GA(LIVE_SAVE);
 switch(item){case 22:return LD64(save+0x5548)&((1ull<<50)-1);
 case 58:return LD64(save+0x5550)&0xffffffffull;case 59:return LD16(save+0x5558)&0x1ff;
 default:return LD8(save+0xe0+(index==9?18:2*index)*0x10+1);}
}
static int reward_owned(int item,int index){
 if(lao_advanced_reward(item))return item>=120?!!(LD8(GA(LIVE_SAVE)+0x555b)&(1u<<(item-120))):!!(LD32(GA(LIVE_SAVE)+0x556c)&(1u<<(item-43)));
 if(lao_equipment(item)){
  uint64_t save=GA(LIVE_SAVE);int bit=lao_equipment_bit(item),key=lao_key_equipment_bit(item);
  return bit>=0?!!(LD32(save+0x5568)&(1u<<bit)) &&
    (!lao_equipment_upgrade(item)||(LD8(save+0x555c)&lao_equipment_upgrade(item))):!!(LD32(save+0x556c)&(1u<<key));
 }
 return lao_reward_dungeon_bit(item)?!!(reward_bits(item,index)&lao_reward_dungeon_bit(item)):!!(reward_bits(item,index)&(1ull<<index));}
static void reward_progress(char *out,size_t cap,int item,int index){
 out[0]=0;if(!g_settings.laonline_collection_counters)return;
 if(item==22 || item==58 || item==59)snprintf(out,cap,"\n%d/%d",__builtin_popcountll(reward_bits(item,index)),lao_reward_limit(item));
}
/* Game thread, after native AddItem has returned. No success notice before readback. */
static void rewards_tick(Cpu *c){
 for(unsigned i=0;i<reward_local_n;i++){
  Reward *r=&reward_local[i];if(!reward_owned(r->item,r->index)){laonline_diag("reward.local_unconfirmed",r->map,r->room,r->item,r->index,0);continue;}
  char text[160],where[33],progress[24];lao_location_ids(r->map,r->room,where);reward_progress(progress,sizeof progress,r->item,r->index);
  snprintf(text,sizeof text,"%s obtained!\n%s%s\nFrom: %s",local_name,lao_pickup_name(r->item),progress,where);
  if(g_settings.laonline_pickup_notifications)laonline_notify_item(text,r->item);
  pthread_mutex_lock(&lock);if(r->publish && (!lao_equipment(r->item)||equipment_cap) && r->epoch==reward_epoch && ready && reward_cap && reward_send_n<QUEUE)reward_send[reward_send_n++]=*r;pthread_mutex_unlock(&lock);
 }reward_local_n=0;
 Reward events[QUEUE];unsigned count;pthread_mutex_lock(&lock);count=reward_in_n;memcpy(events,reward_in,count*sizeof *events);reward_in_n=0;pthread_mutex_unlock(&lock);
 applying=1;
 for(unsigned i=0;i<count;i++){
  Reward *r=&events[i];
  int before=reward_owned(r->item,r->index);
  int first=!dev_seen_reward[r->item][r->index];dev_seen_reward[r->item][r->index]=1;
  if(first||!before)dev_events_emit("NETWORK","REMOTE_REWARD_RECEIVED",r->name,r->item,r->index,before,before,"NATIVE_GRANT_NOT_YET_CONFIRMED","REMOTE",0);
  if(!g_settings.laonline_shared_items){if(first)dev_events_emit("ITEM","ITEM_GRANT_REJECTED",lao_pickup_name(r->item),r->item,r->index,before,before,"SHARED_ITEMS_DISABLED","REMOTE",0);continue;}

  if(!before){Cpu saved=*c;c->x[0]=r->item;c->x[1]=1;c->x[2]=(uint64_t)(int64_t)((lao_equipment(r->item)||lao_advanced_reward(r->item))?-1:r->index);c->x[3]=0;lao_dev_remote(1);guest_call(c,GA(ADD_ITEM));lao_dev_remote(0);*c=saved;}
  int applied=reward_owned(r->item,r->index);
  if(!before)dev_events_emit("ITEM",applied?"ITEM_GRANT_APPLIED":"ITEM_GRANT_FAILED",r->name,r->item,r->index,before,applied,applied?"NATIVE_OWNERSHIP_CONFIRMED_SAVE_PENDING":"NATIVE_OWNERSHIP_MISSING","REMOTE",0);
  // Items.gsheet gettingFlag for native instruments (verified in base and supplied world).
  // A native instrument receipt is independent of the location where Randomizer placed it.
  if(applied&&r->item>=50&&r->item<=57){int flag=349+r->item-50;if(!flags_ext_get(flag)){flags_put(flag,true);laonline_diag(flags_ext_get(flag)?"instrument.getting_flag_verified":"instrument.getting_flag_failed",r->map,r->room,flag,r->item,flags_ext_get(flag));}}

  pthread_mutex_lock(&lock);if(reward_ack_n<QUEUE){reward_ack[reward_ack_n].item=r->item;reward_ack[reward_ack_n].index=r->index;reward_ack[reward_ack_n++].status=applied?(before?2:1):0;}pthread_mutex_unlock(&lock);
  if(!before||!applied)laonline_diag(applied?"reward.inventory_verified_save_pending":"reward.grant_failed",r->map,r->room,r->item,r->index,before);
  if(applied && !before && g_settings.laonline_pickup_notifications){
   char text[160],where[33],progress[24];lao_location_ids(r->map,r->room,where);reward_progress(progress,sizeof progress,r->item,r->index);
   snprintf(text,sizeof text,"%s found\n%s%s\nFrom: %s\nYou got it too!",r->name,lao_pickup_name(r->item),progress,where);laonline_notify_item(text,r->item);
  }
 }
 applying=0;
}
void laonline_coop_rewards_message(const uint8_t *b,size_t n){
 // Seed is included in every delivery; an old transport can never grant a different seed's reward.
 if(!b || n!=58 || b[0]!=LAO_REWARD_RECEIVE){dev_events_emit("NETWORK","ITEM_GRANT_REJECTED","Malformed reward frame",0,0,0,0,"INVALID_FRAME","REMOTE",0);return;}
 Reward r={0};memcpy(&r.item,b+33,2);memcpy(&r.index,b+35,2);memcpy(&r.map,b+37,2);memcpy(&r.room,b+39,2);memcpy(r.name,b+41,17);r.name[16]=0;
 if(!lao_reward_valid(r.item,r.index)){dev_events_emit("NETWORK","ITEM_GRANT_REJECTED","Unsupported reward identity",r.item,r.index,0,0,"INVALID_ITEM_OR_INDEX","REMOTE",0);return;}
 if(memcmp(b+1,seed,32)){dev_events_emit("NETWORK","ITEM_GRANT_REJECTED","Reward from another world",r.item,r.index,0,0,"WORLD_MISMATCH","REMOTE",0);return;}
 pthread_mutex_lock(&lock);if(reward_cap && ready && !memcmp(b+1,seed,32) && lao_reward_valid(r.item,r.index) && (!lao_equipment(r.item)||equipment_cap) && (!lao_advanced_reward(r.item)||advanced_cap) && reward_in_n<QUEUE)reward_in[reward_in_n++]=r;pthread_mutex_unlock(&lock);
}

static int pickup_popup(int item){
 switch(item){case 101:case 102:case 104:case 105:case 106:case 108:return 0;default:return 1;}
}
void laonline_coop_transport(int on) {
 extern void laonline_death_transport(int);laonline_death_transport(on);
 laonline_checks_transport(on);laonline_story_transport();
 pthread_mutex_lock(&lock);
 memset(dev_seen_reward,0,sizeof dev_seen_reward);world_cap=world_sent=world_ready=world_rejected=0;reward_epoch++;capability=on;ready=setup_sent=have_sent=0;dungeon_map_visible=0;advanced_cap=bootstrap_due=0;equipment_cap=0;reward_cap=reward_request=0;reward_send_n=reward_in_n=reward_ack_n=0;snapshot_seen=progress_warning=0;out_n=in_n=0;
 memset(received_sequence,0,sizeof received_sequence);
 memset(&remote,0,sizeof remote);memset(&last_sent,0,sizeof last_sent);
 pthread_mutex_unlock(&lock);
}
RT_ON_RESET(laonline_coop_reset) {
 pthread_mutex_lock(&lock);
 memset(&inventory_view,0,sizeof inventory_view);
 initialized=seed_ok=ready=setup_sent=have_sent=inventory_due=enabled=0;world_cap=world_sent=world_ready=world_required=world_rejected=0;snapshot_seen=progress_warning=0;
 out_n=in_n=0;memset(&remote,0,sizeof remote);memset(&local,0,sizeof local);
 pthread_mutex_unlock(&lock);
 applying=0;last_sample=0;dungeon_map_visible=0;map_snapshot=0xffff;reward_local_n=reward_send_n=reward_in_n=reward_ack_n=0;advanced_cap=bootstrap_due=0;equipment_cap=0;reward_cap=reward_request=0;
}
void laonline_coop_initialize(int on,const char *name) {
 if(initialized)return; initialized=1;enabled=on;
 if(!on)return;
 laonline_checks_load();
 world_required=g_settings.laonline_play_mode==2;world_seed_id[0]=0;
 const char *dir=rando_seed_dir();if(dir){char path[1024];snprintf(path,sizeof path,"%s/seed_id.txt",dir);FILE *f=fopen(path,"rb");if(f){fgets(world_seed_id,sizeof world_seed_id,f);fclose(f);world_seed_id[strcspn(world_seed_id,"\r\n")]=0;for(char *p=world_seed_id;*p;p++)if(*p>='a'&&*p<='z')*p-=32;}}
 unsigned char identity[32];int ok=laonline_seed_identity(identity);
 pthread_mutex_lock(&lock);seed_ok=ok;if(ok)memcpy(seed,identity,32);pthread_mutex_unlock(&lock);
 snprintf(local_name,sizeof local_name,"%.16s",name);
 {char hash[65];const char *hex="0123456789abcdef";for(int i=0;i<32;i++){hash[i*2]=hex[identity[i]>>4];hash[i*2+1]=hex[identity[i]&15];}hash[64]=0;if(ok)dev_events_emit("WORLD","WORLD_IDENTITY",hash,0,0,0,1,"EXACT_CONTENT_DIGEST","LOCAL",0);}

 LOG("[laonline] V15 co-op identity %s; exact matching game/seed content required\n",seed_ok?"ready":"unavailable (sharing blocked)");
}
static int current_dungeon(const char *level) {
 int n=0; if(sscanf(level,"Lv%d",&n)!=1)return -1;
 return n>=1&&n<=8?n-1:n==10?9:-1;
}
void laonline_coop_context(uint16_t map,uint16_t room,const char *level) {
 if(map!=map_id || room!=room_id)laonline_diag("minimap.context_changed",map,room,0,0,current_dungeon(level));
 map_id=map;room_id=room;dungeon_index=current_dungeon(level);
}
/* LAS Recomp Co-Op's port: a grant made for the team elsewhere (laonline_v12.c native_grant: shared items, levels,
 * rupees, ammo) is a teammate's, not this player's pickup - not reported back to the team, as this file's own
 * grants while applying. */
void laonline_coop_remote_grant(int on) {
 static int was;
 if(on) was=applying, applying=1;
 else applying=was;
}
void laonline_coop_pickup(int item,int count,int index) {
 if(!enabled || applying || !g_ga || !game_version() || !game_in_play())return;
 const char *name=lao_pickup_name(item);if(!name || count<1 || count>999)return;
 // V13: sword / shield / Power Bracelet pickups are counted per place for the whole team (Lv1 -> Lv2, laonline_v12.c)
 if(item<=3||item==13||item==14){extern void laonline_v12_progressive_found(int,uint16_t,uint16_t);laonline_v12_progressive_found(item,map_id,room_id);}
 // Silent index is already decoded by rt_silent_add. Keep randomizer's explicit dungeon index.
 if((item==48 || lao_reward_dungeon_bit(item)) && index<0)index=dungeon_index;
 // Special randomizer indices (pets/triforce/refills) must not be reinterpreted as equipment.
 if(lao_equipment(item) && index==-1 && count==1 && equipment_cap)index=0;
 if(lao_advanced_reward(item) && index==-1 && count==1 && advanced_cap)index=0;
 if(lao_reward_valid(item,index) && count==1 && (!lao_equipment(item)||equipment_cap) && (!lao_advanced_reward(item)||advanced_cap)){
  if(reward_local_n<QUEUE){Reward *r=&reward_local[reward_local_n++];*r=(Reward){(uint16_t)item,(int16_t)index,map_id,room_id,{0}};pthread_mutex_lock(&lock);r->epoch=reward_epoch;r->publish=ready&&reward_cap;pthread_mutex_unlock(&lock);}
  return;
 }
 if(index==80 && (item==4||item==11||item==60))index=-1;
 LaoPickup event={++sequence,(uint16_t)item,(uint16_t)count,(int16_t)index,map_id,room_id};
 if(!lao_pickup_valid(&event))return;
 laonline_diag("pickup.detected",map_id,room_id,item,event.seq,count);
 {extern void laonline_world_log(const char *,...);char at[33];lao_location_ids(map_id,room_id,at);laonline_world_log("YOU   picked up %s x%d at %s",name,count,at);}
 char text[160],where[33];lao_location_ids(map_id,room_id,where);
 snprintf(text,sizeof text,"%s obtained!\n%s x%d\nFrom: %s",local_name,name,count,where);
 if(g_settings.laonline_pickup_notifications && pickup_popup(item))laonline_notify_item(text,item);
 pthread_mutex_lock(&lock);
 if(ready && out_n<QUEUE)outbound[out_n++]=event;
 else if(ready)LOG("[laonline] V15 pickup queue full; event not sent\n");
 pthread_mutex_unlock(&lock);
 LOG("[laonline] V15 local pickup seq=%llu item=%d count=%d index=%d\n",(unsigned long long)event.seq,item,count,index);
}
// Returns one complete TCP frame; no guest memory is touched here.
int laonline_coop_packet(uint8_t *out,int capacity) {
 int n=0;pthread_mutex_lock(&lock);
 if(world_cap && seed_ok && !world_sent && capacity>=132){
  size_t len=strlen(world_seed_id);out[0]=(uint8_t)(35+len);out[1]=0;out[2]=0x14;out[3]=world_required?1:0;memcpy(out+4,seed,32);out[36]=(uint8_t)len;memcpy(out+37,world_seed_id,len);n=37+(int)len;world_sent=1;
 } else if(capability && seed_ok && !setup_sent && (!world_required||world_ready) && capacity>=35) {
  out[0]=33;out[1]=0;out[2]=LAO_COOP_SETUP;memcpy(out+3,seed,32);setup_sent=1;n=35;
 } else if(ready && reward_cap && reward_request && capacity>=4){
  out[0]=2;out[1]=0;out[2]=LAO_REWARD_SEND;out[3]=0;reward_request=0;n=4;
 } else if(ready && reward_cap && reward_ack_n && capacity>=9){
  out[0]=7;out[1]=0;out[2]=LAO_REWARD_SEND;out[3]=2;
  memcpy(out+4,&reward_ack[0].item,2);memcpy(out+6,&reward_ack[0].index,2);out[8]=reward_ack[0].status;
  memmove(reward_ack,reward_ack+1,--reward_ack_n*sizeof *reward_ack);n=9;
 } else if(ready && reward_cap && reward_send_n && capacity>=12){
  Reward *r=&reward_send[0];out[0]=10;out[1]=0;out[2]=LAO_REWARD_SEND;out[3]=1;
  memcpy(out+4,r,8);memmove(reward_send,reward_send+1,--reward_send_n*sizeof *reward_send);n=12;
 } else if(ready && out_n && capacity>=(int)(3+sizeof(LaoPickup))) {
  n=3+sizeof(LaoPickup);out[0]=n-2;out[1]=0;out[2]=LAO_COOP_PICKUP;
  memcpy(out+3,&outbound[0],sizeof(LaoPickup));memmove(outbound,outbound+1,sizeof outbound[0]*--out_n);
 } else if(ready && inventory_due && (!have_sent || memcmp(&local,&last_sent,sizeof local)) && capacity>=(int)(3+sizeof local)) {
  n=3+sizeof local;out[0]=n-2;out[1]=0;out[2]=LAO_COOP_INVENTORY;memcpy(out+3,&local,sizeof local);
  last_sent=local;have_sent=1;inventory_due=0;
 }
 pthread_mutex_unlock(&lock);if(!n)n=laonline_checks_packet(out,capacity,ready);if(!n)n=laonline_story_packet(out,capacity,ready,seed);return n;
}
void laonline_coop_message(const uint8_t *b,size_t n,const char *name) {
 if(!b || !n)return;
 pthread_mutex_lock(&lock);
 if(b[0]==LAO_COOP_READY && n==34 && b[1]==1 && seed_ok && !memcmp(b+2,seed,32)) {
  ready=1;have_sent=0;LOG("[laonline] V15 verified seed sharing active\n");
 } else if(ready && b[0]==LAO_COOP_SNAPSHOT && n==1+sizeof(LaoCoopInventory)) {
  LaoCoopInventory inv;memcpy(&inv,b+1,sizeof inv);
  if(lao_coop_inventory_valid(&inv)){remote=inv;snapshot_seen=1;}
 } else if(ready && b[0]==LAO_COOP_EVENT && n==2+sizeof(LaoPickup) && b[1]<8 && name && name[0]) {
  LaoPickup p;memcpy(&p,b+2,sizeof p);
  if(lao_pickup_valid(&p) && lao_pickup_name(p.item) && p.seq>received_sequence[b[1]] && in_n<QUEUE) {
   received_sequence[b[1]]=p.seq;
   incoming[in_n].pickup=p;snprintf(incoming[in_n++].name,17,"%.16s",name);
  } else LOG("[laonline] V15 invalid or overflowing pickup discarded\n");
 }
 pthread_mutex_unlock(&lock);
}
static LaoCoopInventory read_inventory(void) {
 uint64_t save=GA(LIVE_SAVE);LaoCoopInventory s={0};
 s.items=LD32(save+0x5568)&0xffffu;s.keys=LD32(save+0x556c)&0x17ffdu;
 s.shells=LD64(save+0x5548)&((1ull<<50)-1);s.pieces=(uint32_t)LD64(save+0x5550);
 s.containers=LD16(save+0x5558)&0x1ff;s.songs=LD8(save+0x555b)&7;
 s.upgrades=LD8(save+0x555c)&0x3f;s.sensor=LD8(save+0x556e)&1;s.leaves=LD8(save+0x5582)&31;
 for(int i=0;i<9;i++)s.dungeon[i]=LD8(save+0xe0+(i==8?18:2*i)*0x10+1)&15;
 for(unsigned i=0;i<sizeof lao_progress_flags/sizeof *lao_progress_flags;i++)
  if(flags_ext_get(lao_progress_flags[i]))s.flags[i>>3]|=1u<<(i&7);
 return s;
}
/* Legacy snapshots combine all peers and have no host identity, sequence, or
 * semantic quest transition. They are observation-only until a verified native
 * adapter exists. Never write their flags, inventory or dungeon bits to a save. */
void laonline_coop_tick(Cpu *c,uint64_t frame) {
 extern void laonline_heart_bridge_install(void);
 if(enabled)laonline_heart_bridge_install();

 if(!enabled || !seed_ok || (world_required&&!laonline_coop_world_ready()) || !g_ga || !game_version() || !game_in_play() ||
 !laonline_level_ready() || laonline_ui_paused() || LD32(GA(LIVE_SAVE))!=0xe)return;
 laonline_checks_tick();laonline_story_tick();
 // Bounded replay also repairs an unsaved receipt after loading an older local save.
 // Ownership readback prevents duplicate native grants; relay remains the durable journal.
 pthread_mutex_lock(&lock);if(ready&&reward_cap&&frame-last_reward_replay>=300){reward_request=1;last_reward_replay=frame;}pthread_mutex_unlock(&lock);
 if(advanced_cap&&bootstrap_due&&ready&&reward_cap&&g_settings.laonline_shared_items){
  // Read-back verified monotonic ownership can repair a missed acquisition after reconnect.
  static const int items[]={0,1,2,3,5,6,7,8,9,12,13,14,15,16,20,24,25,26,27,28,50,51,52,53,54,55,56,57,120,121,122};
  pthread_mutex_lock(&lock);bootstrap_due=0;for(unsigned i=0;i<sizeof items/sizeof *items;i++)if(reward_owned(items[i],0)&&reward_send_n<QUEUE){Reward *r=&reward_send[reward_send_n++];*r=(Reward){(uint16_t)items[i],0,map_id,room_id,{0},reward_epoch,1};}pthread_mutex_unlock(&lock);
 }
 if(laonline_ui_settled(LAO_SETTLE_FRAMES))rewards_tick(c);
 pthread_mutex_lock(&lock);map_snapshot=map_id;
 dungeon_map_visible=dungeon_index>=0 && dungeon_index!=8 && dungeon_index<=9 &&
   (LD8(GA(LIVE_SAVE)+0xe0+(dungeon_index==9?18:2*dungeon_index)*0x10+1)&2);
 pthread_mutex_unlock(&lock);

 LaoCoopInventory merge;Incoming events[QUEUE];unsigned count;int active,seen;
 int settled=laonline_ui_settled(LAO_SETTLE_FRAMES);
 pthread_mutex_lock(&lock);active=ready;seen=snapshot_seen;merge=remote;
 count=!settled?0:in_n<LAO_GRANTS_PER_FRAME?in_n:LAO_GRANTS_PER_FRAME;memcpy(events,incoming,count*sizeof *events);
 if(count){memmove(incoming,incoming+count,(in_n-count)*sizeof *incoming);in_n-=count;}
 pthread_mutex_unlock(&lock);
 if(active && seen && !progress_warning) {
  LaoCoopInventory before=read_inventory();
  if(memcmp(&before,&merge,sizeof before)) {
   progress_warning=1;
   unsigned differences=0;
   for(unsigned j=0;j<LAO_PROGRESS_FLAG_COUNT;j++)if(((before.flags[j>>3]^merge.flags[j>>3])>>(j&7))&1) {
    if(differences++<16)LOG("[laonline] M4 progression.diff flag=%d local=%u proposed=%u applied=0\n",lao_progress_flags[j],(before.flags[j>>3]>>(j&7))&1,(merge.flags[j>>3]>>(j&7))&1);
   }
   LOG("[laonline] M4 progression.audit differences=%u local_items=%x proposed_items=%x local_keys=%x proposed_keys=%x native_control_lock=unobserved pause_eligibility=unobserved map_eligibility=unobserved\n",differences,before.items,merge.items,before.keys,merge.keys);
   laonline_notify("Catching up with your team's progress...");
   LOG("[laonline] M4 progression.rejected reason=legacy-unsequenced-snapshot native_writes=0 save_commit=none; pause/map locks untouched\n");
   laonline_diag("progression.rejected",map_id,room_id,0,0,0);
  }
 }
 applying=1;
 for(unsigned i=0;i<count;i++) {
  int recovery_result=0;LaoPickup *p=&events[i].pickup;int share=(lao_coop_resource(p->item) && g_settings.laonline_sync_supplies) ||
   /* V13 (Mr. Cursey: "it ate multiple small keys"): the room's key-pop / key-door flags are shared, so a key a
    * teammate picked up was gone for you too, while keys themselves were not shared. A teammate's Small Key now
    * reaches you (same dungeon index, native AddItem; each pickup event is delivered once by its sequence number).
    * Keys found while you were offline are not replayed - doors they opened still are. */
   (p->item==48 && p->index>=0 && p->index<=9 && p->count<=9 && g_settings.laonline_sync_keys);
  // Refill items must not unlock a bag/bow the recipient has not found.
  uint32_t inv=LD32(GA(LIVE_SAVE)+0x5568);
  if((p->item==4&&!(inv&4)) || (p->item==11&&!(inv&256)) || (p->item==60&&!(inv&8)))share=0;
  if((p->item==110 || p->item==61 || p->item==62) && (int16_t)LD16(GA(LIVE_SAVE)+0x5544)<=0)share=0;
  if(share && p->item==100) {
   // Same verified quarter-heart field and max-health formula used by
   // silent_pickups.c. Recovery hearts are repeatable; never change capacity.
   uint64_t status=GA(PLAYER_STATUS_PTR)?LD64(GA(PLAYER_STATUS_PTR)):0;
   int valid=(status>=MAIN_BASE&&status+0x100<SDK_BASE)||(status>=HEAP_BASE&&status+0x100<HEAP_BASE+HEAP_MAX);
   if(!valid){laonline_diag("recovery.rejected",p->map,p->room,p->item,p->seq,0);continue;}
   // Verified 1.0.0 AddItem case 100 (main+8d0fd4) dereferences
   // PLAYER_STATUS_PTR and writes +84, rather than assuming a save alias.
   int hp=(int16_t)LD16(status+0x84);
   int max=12+4*(__builtin_popcount(LD16(status+0x98)&0x1ff)+__builtin_popcountll(LD64(status+0x90)&0xffffffffull)/4);
   // LAS Recomp Co-Op's port: through that same AddItem case 100 (hearts + count * 4, up to the maximum), not a write
   // of +84 (LAS Recomp's review: no direct save writes); the result read back as before.
   if(hp>0 && hp<max){int healed=hp+4*(int)p->count;if(healed>max)healed=max;
    {Cpu saved=*c;c->x[0]=100;c->x[1]=p->count;c->x[2]=(uint64_t)(int64_t)-1;c->x[3]=0;lao_dev_remote(1);guest_call(c,GA(ADD_ITEM));lao_dev_remote(0);*c=saved;}
    recovery_result=LD16(status+0x84)==healed?1:0;
    laonline_diag("recovery.applied",p->map,p->room,p->item,p->seq,healed-hp);
    LOG("[laonline] M6 recovery.applied peer=%s seq=%llu before=%d after=%d max=%d\n",events[i].name,(unsigned long long)p->seq,hp,healed,max);}
   else {recovery_result=hp>=max?2:3;LOG("[laonline] M6 recovery.noop peer=%s seq=%llu hp=%d max=%d\n",events[i].name,(unsigned long long)p->seq,hp,max);}
  } else if(share && !laonline_v12_resource(c,p->item,(int)p->count)) {  /* V12: rupees / ammo there (AddItem); others here */
   Cpu saved=*c;c->x[0]=p->item;c->x[1]=p->count;c->x[2]=(uint64_t)(int64_t)(p->item==48?p->index:-1);c->x[3]=0;
   lao_dev_remote(1);guest_call(c,GA(ADD_ITEM));lao_dev_remote(0);*c=saved;
  }
  // This observer has no per-item native grant acknowledgment yet. Never claim
  // "you got it too" solely because a notification or a grant request exists.
  char text[160],where[33];lao_location_ids(p->map,p->room,where);
  snprintf(text,sizeof text,"%s found\n%s x%u\nFrom: %s",events[i].name,lao_pickup_name(p->item),p->count,where);
  if(p->item==100 && share){
   const char *result=recovery_result==1?"You got it too!":recovery_result==2?"Already at full health":recovery_result==3?"Not applied while defeated":"Healing could not be confirmed";
   snprintf(text,sizeof text,"%s found\nRecovery Heart\n%s\nFrom: %s",events[i].name,result,where);
  }
  if(g_settings.laonline_pickup_notifications && pickup_popup(p->item))laonline_notify_item(text,p->item);
  laonline_diag(share?"pickup.grant_requested":"pickup.received",p->map,p->room,p->item,p->seq,p->count);
  {extern void laonline_world_log(const char *,...);laonline_world_log("TEAM  %s picked up %s x%u at %s%s",events[i].name,lao_pickup_name(p->item),p->count,where,share?" (you got it too)":"");}
  LOG("[laonline] V15 remote pickup player=%s seq=%llu item=%u count=%u index=%d %s\n",events[i].name,
   (unsigned long long)p->seq,p->item,p->count,p->index,share?"native grant":"inventory snapshot/notification");
 }
 applying=0;
 if(frame-last_sample>=30 || !last_sample) {
  inventory_view_sample();LaoCoopInventory s=read_inventory();pthread_mutex_lock(&lock);local=s;inventory_due=1;pthread_mutex_unlock(&lock);last_sample=frame;
 }
}

