#include <windows.h>
#include <assert.h>
#include "../runtime/hle/laonline_coop.c"
void laonline_heart_bridge_install(void){}
const char *rando_seed_dir(void){return "validated-randomizer-fixture";}
const GameAddrs *g_ga;static GameVersion version={.key="v1_0_0"};
const GameVersion *game_version(void){return &version;}
Settings g_settings={.laonline_shared_items=true,.laonline_pickup_notifications=true,.laonline_collection_counters=true};
static int paused,notices,grants,last_item,last_index;static bool flags[2048];
int game_in_play(void){return 1;}int laonline_level_ready(void){return 1;}
bool laonline_ui_paused(void){return paused;}
void laonline_notify(const char *s){assert(strstr(s,"obtained!")||strstr(s,"found")||strstr(s,"Story sync blocked"));notices++;}
int laonline_seed_identity(unsigned char *out){memset(out,7,32);return 1;}
bool flags_ext_get(int i){return flags[i];}void flags_put(int i,bool b){flags[i]=b;}
void guest_call(Cpu *c,uint64_t address) {
 assert(address==GA(ADD_ITEM));grants++;last_item=c->x[0];last_index=(int)c->x[2];
 int item=(int)c->x[0],index=(int)c->x[2];
 if(lao_advanced_reward(item)){if(item>=120)ST8(GA(LIVE_SAVE)+0x555b,LD8(GA(LIVE_SAVE)+0x555b)|(1u<<(item-120)));else ST32(GA(LIVE_SAVE)+0x556c,LD32(GA(LIVE_SAVE)+0x556c)|(1u<<(item-43)));}
 else if(lao_equipment(item)){
 uint64_t save=GA(LIVE_SAVE);int bit=lao_equipment_bit(item),key=lao_key_equipment_bit(item);
 if(bit>=0){ST32(save+0x5568,LD32(save+0x5568)|(1u<<bit));ST8(save+0x555c,LD8(save+0x555c)|lao_equipment_upgrade(item));}
 else ST32(save+0x556c,LD32(save+0x556c)|(1u<<key));
 }else if(lao_reward_valid(item,index)){
  uint64_t save=GA(LIVE_SAVE);if(item==22)ST64(save+0x5548,LD64(save+0x5548)|(1ull<<index));
  else if(item==58)ST64(save+0x5550,LD64(save+0x5550)|(1ull<<index));
  else if(item==59)ST16(save+0x5558,LD16(save+0x5558)|(1u<<index));
  else {uint64_t p=save+0xe0+(index==9?18:2*index)*0x10+1;ST8(p,LD8(p)|lao_reward_dungeon_bit(item));}
 }
 laonline_coop_pickup(item,c->x[1],index);memset(c,0,sizeof *c);
}
int old_resource_main(void){
 for(int i=0;i<127;i++)assert(pickup_popup(i)==!(i==101||i==102||i==104||i==105||i==106||i==108));
 assert(sizeof(LaoCoopInventory)==67 && sizeof(LaoPickup)==18);
 assert(VirtualAlloc((void *)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220,.ADD_ITEM=MAIN_BASE+0x7e18b0,.PLAYER_STATUS_PTR=MAIN_BASE+0x1000};g_ga=&ga;
 ST64(GA(PLAYER_STATUS_PTR),GA(LIVE_SAVE)+0x54c0);ST32(GA(LIVE_SAVE),0xe);uint8_t packet[256];Cpu cpu;memset(&cpu,0x5a,sizeof cpu);Cpu saved=cpu;
 laonline_coop_initialize(1,"Dad");laonline_coop_transport(1);assert(laonline_coop_packet(packet,sizeof packet)==35 && packet[2]==LAO_COOP_SETUP);
 uint8_t ack[34]={LAO_COOP_READY,1};memset(ack+2,7,32);laonline_coop_message(ack,sizeof ack,NULL);assert(ready);
 laonline_coop_context(1,2,"Lv03KeyCavern");laonline_coop_pickup(48,1,-1);assert(out_n==1&&outbound[0].index==2);
 assert(laonline_coop_packet(packet,sizeof packet)==21 && packet[2]==LAO_COOP_PICKUP);
 LaoCoopInventory inv={.items=5,.keys=4,.shells=2,.pieces=4,.containers=1,.songs=1,.upgrades=1,.sensor=1,.leaves=1,.flags={1}};inv.dungeon[2]=8;
 uint8_t snap[68]={LAO_COOP_SNAPSHOT};memcpy(snap+1,&inv,sizeof inv);laonline_coop_message(snap,sizeof snap,NULL);
 ST32(GA(LIVE_SAVE)+0x5568,2);ST8(GA(LIVE_SAVE)+0x555c,0xc0);ST16(GA(LIVE_SAVE)+0x5544,7);ST32(GA(LIVE_SAVE)+0x5540,123);
 unsigned char untouched[24288];memcpy(untouched,(void*)(uintptr_t)GA(LIVE_SAVE),sizeof untouched);
 paused=1;laonline_coop_tick(&cpu,30);assert(LD32(GA(LIVE_SAVE)+0x5568)==2);
 paused=0;laonline_coop_tick(&cpu,31);assert(LD32(GA(LIVE_SAVE)+0x5568)==2 && LD8(GA(LIVE_SAVE)+0x555c)==0xc0);
 assert(!memcmp(untouched,(void*)(uintptr_t)GA(LIVE_SAVE),sizeof untouched));
 assert(LD16(GA(LIVE_SAVE)+0x5544)==7 && LD32(GA(LIVE_SAVE)+0x5540)==123 && !flags[lao_progress_flags[0]]);
 assert(LD8(GA(LIVE_SAVE)+0xe0+4*0x10+1)==0);assert(!memcmp(&cpu,&saved,sizeof cpu));
 LaoCoopInventory invalid=inv;invalid.flags[31]=0xff;memcpy(snap+1,&invalid,sizeof invalid);laonline_coop_message(snap,sizeof snap,NULL);assert(remote.flags[0]==1);
 uint8_t event[20]={LAO_COOP_EVENT,1};LaoPickup pick={1,101,5,-1,1,2};memcpy(event+2,&pick,sizeof pick);laonline_coop_message(event,sizeof event,"Son");
 laonline_coop_tick(&cpu,32);assert(grants==1&&last_item==101&&last_index==-1&&out_n==0&&notices==2);assert(!memcmp(&cpu,&saved,sizeof cpu));
 pick.seq=2;pick.item=48;pick.index=9;memcpy(event+2,&pick,sizeof pick);laonline_coop_message(event,sizeof event,"Son");laonline_coop_tick(&cpu,33);assert(grants==1&&last_index==-1);
 pick.seq=3;pick.item=60;pick.index=-1;memcpy(event+2,&pick,sizeof pick);laonline_coop_message(event,sizeof event,"Son");laonline_coop_tick(&cpu,34);assert(grants==1);
 // Heart event is shared independently of the picker and cannot echo.
 pick.seq=4;pick.item=100;pick.count=1;memcpy(event+2,&pick,sizeof pick);
 laonline_coop_message(event,sizeof event,"Friend");laonline_coop_tick(&cpu,35);assert(LD16(GA(LIVE_SAVE)+0x5544)==11&&grants==1&&out_n==0);
 laonline_coop_message(event,sizeof event,"Friend");laonline_coop_tick(&cpu,36);assert(LD16(GA(LIVE_SAVE)+0x5544)==11);
 pick.seq=5;memcpy(event+2,&pick,sizeof pick);laonline_coop_message(event,sizeof event,"Friend");laonline_coop_tick(&cpu,37);assert(LD16(GA(LIVE_SAVE)+0x5544)==12);
 ST16(GA(LIVE_SAVE)+0x5544,0);pick.seq=6;memcpy(event+2,&pick,sizeof pick);laonline_coop_message(event,sizeof event,"Friend");laonline_coop_tick(&cpu,38);assert(!LD16(GA(LIVE_SAVE)+0x5544));
 assert(!memcmp(&cpu,&saved,sizeof cpu));assert(LD32(GA(LIVE_SAVE)+0x5540)==123);
 // Active player status may differ from the saved-data alias.
 uint64_t active=MAIN_BASE+0x1800000;ST64(GA(PLAYER_STATUS_PTR),active);ST16(active+0x84,5);
 pick.seq=7;memcpy(event+2,&pick,sizeof pick);laonline_coop_message(event,sizeof event,"Friend");laonline_coop_tick(&cpu,39);
 assert(LD16(active+0x84)==9 && LD16(GA(LIVE_SAVE)+0x5544)==0);assert(!memcmp(&cpu,&saved,sizeof cpu));
 laonline_coop_transport(0);assert(!ready&&!in_n&&!out_n);assert(!laonline_coop_packet(packet,sizeof packet));
 version.key="v1_0_1";laonline_coop_pickup(101,1,-1);assert(notices>=4);
 puts("PASS: production snapshot rejection/native resource grant, registers, echo suppression, pause/version guards, existing HP/wallet/tunic preservation, missing-bag guard and disconnect cleanup");
}

void laonline_notify_item(const char *s,int item){(void)item;laonline_notify(s);}

int main(void){
 assert(VirtualAlloc((void *)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220,.ADD_ITEM=MAIN_BASE+0x7e18b0};g_ga=&ga;ST32(GA(LIVE_SAVE),0xe);
 Cpu cpu;memset(&cpu,0x5a,sizeof cpu);Cpu saved=cpu;
 laonline_coop_initialize(1,"Finder");laonline_coop_transport(1);uint8_t readymsg[34]={LAO_COOP_READY,1};memset(readymsg+2,7,32);laonline_coop_message(readymsg,sizeof readymsg,NULL);laonline_coop_rewards_capability();
 uint8_t b[58]={LAO_REWARD_RECEIVE};memset(b+1,7,32);uint16_t item=22;int16_t index=4;memcpy(b+33,&item,2);memcpy(b+35,&index,2);memcpy(b+41,"Friend",7);
 laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,1);assert(reward_owned(22,4)&&grants==1&&notices==1&&reward_ack_n==1);assert(!memcmp(&cpu,&saved,sizeof cpu));
 laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,2);assert(grants==1&&notices==1);assert(reward_ack[1].status==2);
 b[1]=8;laonline_coop_rewards_message(b,sizeof b);assert(!reward_in_n);b[1]=7;
 index=50;memcpy(b+35,&index,2);laonline_coop_rewards_message(b,sizeof b);assert(!reward_in_n);
 // Finder notification is delayed until native inventory confirms the reward.
 laonline_coop_pickup(58,1,3);assert(notices==1);ST64(GA(LIVE_SAVE)+0x5550,8);laonline_coop_tick(&cpu,3);assert(notices==2 && reward_send_n==1);
 // Receiving preference blocks grants; legacy snapshot quest flags remain untouched.
 item=59;index=1;memcpy(b+33,&item,2);memcpy(b+35,&index,2);g_settings.laonline_shared_items=false;
 laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,4);assert(!reward_owned(59,1)&&grants==1);
 g_settings.laonline_shared_items=true;
 // New equipment is forbidden before its separate negotiated capability.
 item=2;index=0;memcpy(b+33,&item,2);memcpy(b+35,&index,2);
 laonline_coop_rewards_message(b,sizeof b);assert(!reward_in_n);
 laonline_coop_equipment_capability();
 ST32(GA(LIVE_SAVE)+0x5540,77);ST16(GA(LIVE_SAVE)+0x5544,5);
 laonline_coop_rewards_message(b,sizeof b);paused=1;laonline_coop_tick(&cpu,5);assert(!reward_owned(2,0)&&reward_in_n==1);
 paused=0;laonline_coop_tick(&cpu,6);assert(reward_owned(2,0)&&last_item==2&&last_index==-1);
 int count=grants;laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,7);assert(grants==count);
 // Upgrade then lower tier replay: no downgrade or duplicate native call.
 item=3;memcpy(b+33,&item,2);laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,8);assert(reward_owned(3,0));
 item=2;memcpy(b+33,&item,2);count=grants;laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,9);assert(grants==count&&reward_owned(3,0));
 assert(LD32(GA(LIVE_SAVE)+0x5540)==77&&LD16(GA(LIVE_SAVE)+0x5544)==5&&!memcmp(&cpu,&saved,sizeof cpu));
 // Unsaved local rollback: durable server replay grants it again only if absent.
 ST32(GA(LIVE_SAVE)+0x5568,0);ST8(GA(LIVE_SAVE)+0x555c,0);laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,10);assert(reward_owned(2,0));
 // Source native callback only publishes after readback; do not echo receiver calls.
 item=6;laonline_coop_pickup(item,1,-1);assert(reward_local_n==1);ST32(GA(LIVE_SAVE)+0x5568,LD32(GA(LIVE_SAVE)+0x5568)|16);laonline_coop_tick(&cpu,11);assert(reward_send_n==2);

 // Map ownership unlocks only its destination dungeon, including Randomizer and same-tick shared grants.
 laonline_coop_context(3,1,"Lv03KeyCavern");
 ST8(GA(LIVE_SAVE)+0xe0+4*0x10+1,0);laonline_coop_tick(&cpu,12);assert(!laonline_coop_dungeon_map(3));
 item=44;index=2;memcpy(b+33,&item,2);memcpy(b+35,&index,2);laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,13);
 assert(reward_owned(44,2)&&!reward_owned(45,2)&&!laonline_coop_dungeon_map(3));
 item=45;index=0;memcpy(b+33,&item,2);memcpy(b+35,&index,2);laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,14);
 assert(reward_owned(45,0)&&!reward_owned(45,2)&&!laonline_coop_dungeon_map(3));
 index=2;memcpy(b+35,&index,2);laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,15);
 assert(reward_owned(45,2)&&laonline_coop_dungeon_map(3));
 ST8(GA(LIVE_SAVE)+0xe0+4*0x10+1,2);laonline_coop_tick(&cpu,16);assert(!reward_owned(44,2)&&laonline_coop_dungeon_map(3));
 laonline_coop_context(0,1,"Field");laonline_coop_tick(&cpu,17);assert(!laonline_coop_dungeon_map(0));
 laonline_coop_context(4,1,"House");laonline_coop_tick(&cpu,18);assert(!laonline_coop_dungeon_map(4));
 item=50;index=0;memcpy(b+33,&item,2);memcpy(b+35,&index,2);laonline_coop_rewards_message(b,sizeof b);assert(!reward_in_n);
 laonline_coop_advanced_capability();bootstrap_due=0;
 int prior=grants;laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,19);assert(reward_owned(50,0)&&grants==prior+1&&last_index==-1&&flags[349]&&!flags[350]);
 laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,20);assert(grants==prior+1);
 item=120;memcpy(b+33,&item,2);laonline_coop_rewards_message(b,sizeof b);laonline_coop_tick(&cpu,21);assert(reward_owned(120,0));
 assert(LD32(GA(LIVE_SAVE)+0x5540)==77&&LD16(GA(LIVE_SAVE)+0x5544)==5&&!memcmp(&cpu,&saved,sizeof cpu));
 puts("PASS production advanced capability, native adapter readback, duplicate suppression and HP/wallet preservation");
 puts("PASS Randomizer map visibility: hidden without Map, Compass alone insufficient, exact destination, same-tick shared Map, Map without Compass, field and house");
 assert(!out_n && !flags[lao_progress_flags[0]]);puts("PASS production indexed native grants/readback, duplicate suppression, exact seed/index rejection, local post-commit notice, receive preference and register preservation");
}

int silent_seashell_total(void){return 50;}

void lao_dev_remote(int on){}
void laonline_death_transport(int on){}
void laonline_checks_transport(int on){}
void laonline_checks_load(void){}
void laonline_checks_tick(void){}
int laonline_checks_packet(uint8_t *p,int n,int r){return 0;}
void laonline_diag(const char *e,uint16_t m,uint16_t r,uint64_t o,uint64_t q,int v){}
void laonline_diag_seed(const char *a,const char *b,const char *c,const char *d,const char *e,const char *f,const char *g){}

void dev_events_emit(const char *a,const char *b,const char *c,int d,int e,int64_t f,int64_t g,const char *h,const char *i,uint64_t j){}
