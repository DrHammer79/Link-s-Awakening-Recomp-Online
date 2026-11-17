// Game-thread, read-only instrumentation of existing verified save structures.
#include "laonline_dev_events.h"
#include "../dev_events.h"
#include "../hle.h"
#include "../game_addresses.h"
#include "../settings.h"
#include "../rando.h"
#include "laonline_item_rules.h"
#include "laonline_reward_rules.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
extern int game_in_play(void);
extern bool flags_ext_get(int);
static unsigned char old_flags[5632],old_dungeons[19*16],old_inventory[0x80];
static int sampled,remote_depth,suspended,waiting_logged;
static uint64_t counter;
static struct{int id;char name[80];} seed_names[2048];static unsigned nseed_names;
typedef struct {int item,index,count;uint64_t id;int remote,before;} Pending;
static Pending observations[256];
static Pending pending[256],awaiting_save[512];static unsigned npending,nsave;
static pthread_mutex_t save_mx=PTHREAD_MUTEX_INITIALIZER;
static unsigned char expected_save[0x5ee0];static int expected_valid;
static const char *flag_name(int i){for(unsigned n=0;n<nseed_names;n++)if(seed_names[n].id==i)return seed_names[n].name;switch(i){
#include "dev_flag_names.inc"
default:return "UNKNOWN_FLAG";}}
static int usable(void){return g_ga&&game_version()&&game_in_play()&&LD32(GA(LIVE_SAVE))==0xe;}
static int owned(const unsigned char *s,int item,int index){int b=lao_equipment_bit(item),k=lao_key_equipment_bit(item);uint32_t bits=0;if(b>=0){memcpy(&bits,s+0x5568,4);return !!(bits&(1u<<b))&&(!lao_equipment_upgrade(item)||(s[0x555c]&lao_equipment_upgrade(item)));}if(k>=0){memcpy(&bits,s+0x556c,4);return !!(bits&(1u<<k));}
 if(item>=50&&item<=57){memcpy(&bits,s+0x556c,4);return !!(bits&(1u<<(item-43)));}if(item>=120&&item<=122)return !!(s[0x555b]&(1u<<(item-120)));
 int db=lao_reward_dungeon_bit(item);if(db&&index>=0&&index<=9&&index!=8)return !!(s[0xe1+(index==9?18:index*2)*16]&db);
 if((item==22&&index>=0&&index<50)||(item==58&&index>=0&&index<32)||(item==59&&index>=0&&index<9)){uint64_t bits64=0;memcpy(&bits64,s+(item==22?0x5548:item==58?0x5550:0x5558),item==59?2:8);return !!(bits64&(1ull<<index));}return -1;}
void lao_dev_remote(int active){remote_depth+=active?1:-1;if(remote_depth<0)remote_depth=0;}
uint64_t lao_dev_item_begin(int item,int count,int index){
 if(!usable())return 0;if(!sampled)lao_dev_tick(0);uint64_t id=++counter;const char *name=lao_pickup_name(item);const char *source=remote_depth?"REMOTE_NATIVE_GRANT":"LOCAL_NATIVE_ADD";
 Pending *o=&observations[id%256];*o=(Pending){item,index,count,id,remote_depth!=0,owned((const unsigned char *)(uintptr_t)GA(LIVE_SAVE),item,index)};
 dev_events_emit("ITEM","ITEM_PICKUP_OBSERVED",name?name:"UNKNOWN_ITEM_ID",item,index,o->before,count,"NATIVE_ADD_ENTERED_NOT_YET_VERIFIED",source,id);
 dev_events_emit("ITEM","ITEM_GRANT_REQUESTED",name?name:"UNKNOWN_ITEM_ID",item,index,o->before,count,"SAVE_PENDING",source,id);return id;
}
void lao_dev_item_end(uint64_t id,int item,int count,int index,int handled){
 if(!id||!usable())return;Pending *o=&observations[id%256];int valid=o->id==id;int before=valid&&o->item==item&&o->index==index?o->before:-1;
 if(valid&&(o->item!=item||o->index!=index))dev_events_emit("ITEM","ITEM_REQUEST_NORMALIZED",lao_pickup_name(item),item,index,o->item,item,"NATIVE_POLICY_NORMALIZATION","LOCAL_OR_REMOTE",id);
 if(npending<256)pending[npending++]=(Pending){item,index,count,id,valid?o->remote:remote_depth!=0,before};
 else dev_events_emit("ITEM","ITEM_VERIFICATION_OVERFLOW",lao_pickup_name(item),item,index,0,handled,"UNVERIFIED","LOCAL_LOG",id);
}
void lao_dev_item(int item,int count,int index,int handled){uint64_t id=lao_dev_item_begin(item,count,index);lao_dev_item_end(id,item,count,index,handled);}
void lao_dev_tick(uint64_t frame){if(!usable()){if(!waiting_logged){const char *reason=!g_ga?"Game addresses not initialized":!game_version()?"No game version selected":!game_in_play()?"Waiting for active gameplay":"Waiting for loaded native save";dev_events_emit("LOGGER","GAMEPLAY_OBSERVER_WAITING",reason,0,0,0,0,"NO_GAMEPLAY_OWNERSHIP_CLAIM","LOCAL",0);waiting_logged=1;}if(sampled&&!suspended){dev_events_emit("SCENE","GAMEPLAY_SUSPENDED","Player absent or transition",0,0,1,0,"OBSERVED","LOCAL",0);suspended=1;}return;}
 waiting_logged=0;
 if(frame&&frame%3600==0)dev_events_emit("LOGGER","GAMEPLAY_OBSERVER_ACTIVE","Read-only gameplay observation running",0,0,0,0,"RECORDING","LOCAL",0);
 if(suspended){dev_events_emit("SCENE","GAMEPLAY_RESUMED","State observation resumes",0,0,0,1,"OBSERVED","LOCAL",0);suspended=0;}
 if(frame%6&&sampled&&!npending)return;unsigned char *s=(unsigned char *)(uintptr_t)GA(LIVE_SAVE);
 if(!sampled){char name[80]="Player",seed[180]="none";FILE *f=fopen("laonline.ini","rb");if(f){char line[256];while(fgets(line,sizeof line,f))if(sscanf(line,"name = %16[^;\r\n]",name)==1)break;fclose(f);}const char *dir=rando_seed_dir();nseed_names=0;if(dir){char p[1200];snprintf(p,sizeof p,"%s/flags.tsv",dir);f=fopen(p,"rb");if(f){char line[256];while(nseed_names<2048&&fgets(line,sizeof line,f)){int id;char label[80];if(sscanf(line,"%79[^\t]\t%d",label,&id)==2&&id>=0&&id<45056){seed_names[nseed_names].id=id;snprintf(seed_names[nseed_names++].name,80,"%s",label);}}fclose(f);}snprintf(p,sizeof p,"%s/seed_id.txt",dir);f=fopen(p,"rb");if(f){fgets(seed,sizeof seed,f);seed[strcspn(seed,"\r\n")]=0;fclose(f);}}
 const char *mode=g_settings.laonline_play_mode==0?"VANILLA":g_settings.laonline_play_mode==2?"CO-OP RANDOMIZER":g_settings.laonline_play_mode==3?"GLOBAL ONLINE":"STORY CO-OP";dev_events_context(mode,name,seed);dev_events_emit("SCENE","GAMEPLAY_STATE_BASELINE","Loaded save; baseline is not a new grant",0,0,0,0,"OBSERVED","LOCAL",0);
 memset(old_flags,0,sizeof old_flags);for(int i=0;i<45056;i++)if(flags_ext_get(i))old_flags[i>>3]|=1u<<(i&7);memcpy(old_dungeons,s+0xe0,sizeof old_dungeons);memcpy(old_inventory,s+0x5540,sizeof old_inventory);sampled=1;
 }else{
 for(int i=0;i<45056;i++){int before=!!(old_flags[i>>3]&(1u<<(i&7))),after=flags_ext_get(i);if(before!=after){dev_events_emit("STORY_FLAG","FLAG_CHANGED_OBSERVED",flag_name(i),i,0,before,after,"SAVE_PENDING_CAUSE_NOT_PROVEN","LOCAL_STATE",0);if(after)old_flags[i>>3]|=1u<<(i&7);else old_flags[i>>3]&=~(1u<<(i&7));}}
 for(int i=0;i<19;i++)for(int j=0;j<16;j++)if(old_dungeons[i*16+j]!=s[0xe0+i*16+j]){dev_events_emit("DUNGEON","DUNGEON_RECORD_CHANGED","Native dungeon record; unknown fields remain numeric",i,j,old_dungeons[i*16+j],s[0xe0+i*16+j],"SAVE_PENDING","LOCAL_STATE",0);old_dungeons[i*16+j]=s[0xe0+i*16+j];}
 for(int i=0;i<0x80;i++)if(old_inventory[i]!=s[0x5540+i]){dev_events_emit("INVENTORY","INVENTORY_BYTE_CHANGED","Verified inventory structure offset",0x5540+i,0,old_inventory[i],s[0x5540+i],"SAVE_PENDING","LOCAL_STATE",0);old_inventory[i]=s[0x5540+i];}
 }
 pthread_mutex_lock(&save_mx);memcpy(expected_save,s,sizeof expected_save);expected_valid=1;
 for(unsigned i=0;i<npending;i++){Pending *p=&pending[i];int state=owned(s,p->item,p->index);if(state==1)dev_events_emit("ITEM","ITEM_GRANT_APPLIED",lao_pickup_name(p->item),p->item,p->index,p->before,state,p->before==0?"NATIVE_OWNERSHIP_CHANGED_SAVE_PENDING":"OWNED_BEFORE_OR_PREVIOUS_STATE_UNVERIFIED",p->remote?"REMOTE_NATIVE_GRANT":"LOCAL_NATIVE_ADD",p->id);if(state==1){if(nsave<512)awaiting_save[nsave++]=*p;else dev_events_emit("SAVE","SAVE_VERIFICATION_OVERFLOW",lao_pickup_name(p->item),p->item,p->index,0,0,"UNVERIFIED","LOCAL_LOG",p->id);}dev_events_emit("ITEM",state==1?"INVENTORY_VERIFIED":state==0?"INVENTORY_MISMATCH":"ITEM_NATIVE_RETURNED",lao_pickup_name(p->item),p->item,p->index,p->before,state,state==1?"OWNED_SAVE_PENDING":state==0?"NOT_OWNED":"UNVERIFIED_ITEM_ADAPTER",p->remote?"REMOTE_NATIVE_GRANT":"LOCAL_NATIVE_ADD",p->id);}npending=0;pthread_mutex_unlock(&save_mx);
}
void lao_dev_save(const char *path,int success){
 if(!strstr(path,"savedata")||strstr(path,"screenshot"))return;
 dev_events_emit("SAVE",success?"SAVE_COMMITTED":"SAVE_PERSISTENCE_FAILED","Native atomic save commit",0,0,0,success,success?"FILE_COMMITTED_ITEM_READBACK_PENDING":"FAILED","LOCAL_SAVE",0);
 if(success){unsigned char data[0x5ee0];FILE *f=fopen(path,"rb");size_t n=f?fread(data,1,sizeof data,f):0;if(f)fclose(f);
  pthread_mutex_lock(&save_mx);
  int comparable=n==sizeof data&&expected_valid&&!memcmp(data+0x5388,expected_save+0x5388,18);
  int equal=comparable&&!memcmp(data+0x5540,expected_save+0x5540,0x80)&&!memcmp(data+0x5dc0,expected_save+0x5dc0,256);
  dev_events_emit("SAVE",equal?"SAVE_PERSISTENCE_VERIFIED":"SAVE_READBACK_UNVERIFIED","Committed inventory and base story flags readback",0,0,0,equal,equal?"DISK_MATCHES_GAME_THREAD_SNAPSHOT":"MISMATCH_OR_DIFFERENT_SLOT","LOCAL_SAVE",0);
  if(equal){for(unsigned i=0;i<nsave;i++){Pending *p=&awaiting_save[i];int state=owned(data,p->item,p->index);dev_events_emit("ITEM",state==1?"SAVE_PERSISTENCE_VERIFIED":"SAVE_PERSISTENCE_FAILED",lao_pickup_name(p->item),p->item,p->index,p->before,state,state==1?"ITEM_OWNERSHIP_READ_BACK_FROM_COMMITTED_FILE":"ITEM_NOT_PRESENT_IN_COMMITTED_FILE",p->remote?"REMOTE_NATIVE_GRANT":"LOCAL_NATIVE_ADD",p->id);}nsave=0;}
  pthread_mutex_unlock(&save_mx);
 }
 dev_events_flush();
}
RT_ON_RESET(lao_dev_reset){sampled=suspended=waiting_logged=0;nseed_names=0;npending=0;remote_depth=0;pthread_mutex_lock(&save_mx);expected_valid=0;nsave=0;pthread_mutex_unlock(&save_mx);}
