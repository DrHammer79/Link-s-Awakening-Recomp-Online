/* V13: soul sharing in Co-op Randomizer (laonline_v12.c). Appended to the V12 test harness setup. */
#include <windows.h>
#include <assert.h>
#include "../runtime/hle/laonline_v12.c"
#include "v15_stubs.h"
int laonline_death_active(void){return 0;}  /* (laonline_death.c in the game) */
const GameAddrs *g_ga;static GameVersion version={.key="v1_0_0"};Settings g_settings;
static int mode=2,world=1;static uint8_t flags[8192];static uint64_t frame=100;
int game_in_play(void){return 1;}int laonline_level_ready(void){return 1;}int laonline_launch_choice(void){return mode;}int laonline_coop_world_ready(void){return world;}
bool laonline_ui_paused(void){return 0;}void laonline_notify(const char *t){(void)t;}
bool flags_ext_get(int i){return (flags[i>>3]>>(i&7))&1;}void flags_put(int i,bool on){if(on)flags[i>>3]|=1u<<(i&7);else flags[i>>3]&=~(1u<<(i&7));}
void flags_ext_set(int i){flags_put(i,true);}
static const int souls[]={30001,30002,30003};int souls_flag_at(int i){return i>=0&&i<3?souls[i]:-1;}
void warp_request(const char *l,const char *loc){(void)l;(void)loc;}
uint64_t renderer_present_count(void){return frame;}uint64_t plat_now_ns(void){return frame*16000000ull;}
void input_hit_rumble(int ms){(void)ms;}
const GameVersion *game_version(void){return &version;}const char *rando_seed_dir(void){return 0;}
void dev_events_emit(const char *a,const char *b,const char *c,int d,int e,int64_t f,int64_t g,const char *h,const char *i,uint64_t j){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;}
int laonline_v12_peer(int id,float pos[3],uint16_t *map,int *local){(void)id;(void)pos;(void)map;(void)local;return 0;}
static int drain(uint8_t frames[][32],int launch){int n=0;uint8_t f[32];int k;while((k=laonline_v12_packet(f,sizeof f,launch))>0&&n<600){memcpy(frames[n++],f,32);}return n;}
#define SV(o) (GA(LIVE_SAVE)+(o))
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220};g_ga=&ga;g_settings.laonline_sync_story=1;g_settings.laonline_sync_supplies=1;g_settings.laonline_sync_keys=1;g_settings.laonline_sync_upgrades=1;g_settings.laonline_sync_souls=1;g_settings.laonline_shared_items=1;ST32(SV(0),0xe);
 float me[3]={0};uint8_t fr[600][32];
 /* late join: WELCOME snapshot carries a soul flag (30002), an unrelated extended flag (40000) and a story flag (102) */
 uint8_t w[200];int n=0;w[n++]=0x81;w[n++]=0;w[n++]=1;w[n++]=1;w[n++]=3;memcpy(w+n,"Bob",3);n+=3;w[n++]=1;w[n++]=2;w[n++]=3;
 uint32_t c=3,f1=30002,f2=40000,f3=102;memcpy(w+n,&c,4);n+=4;memcpy(w+n,&f1,4);n+=4;memcpy(w+n,&f2,4);n+=4;memcpy(w+n,&f3,4);n+=4;
 uint16_t ic=0;memcpy(w+n,&ic,2);n+=2;
 laonline_v12_message(w,n);
 laonline_v12_tick(NULL,frame,"Field","Field_10A",me,1);
 printf("%-55s %s\n","late joiner receives the soul from the snapshot",flags_ext_get(30002)?"PASS":"FAIL");
 printf("%-55s %s\n","unrelated extended flag is ignored",!flags_ext_get(40000)?"PASS":"FAIL");
 printf("%-55s %s\n","story flag from the snapshot still applied",flags_ext_get(102)?"PASS":"FAIL");
 /* local player finds soul 30003: published once */
 flags_put(30003,true);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);int k=drain(fr,2),sent=0,sent2=0;
 for(int i=0;i<k;i++){uint32_t v;memcpy(&v,fr[i]+3,4);if(fr[i][2]==3&&v==30003)sent++;if(fr[i][2]==3&&v==30002)sent2++;}
 printf("%-55s %s\n","own soul sent to the room",sent==1?"PASS":"FAIL");
 frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);k=drain(fr,2);int again=0;for(int i=0;i<k;i++){uint32_t v;memcpy(&v,fr[i]+3,4);if(fr[i][2]==3&&v==30003)again++;}
 printf("%-55s %s\n","not sent twice",again==0?"PASS":"FAIL");
 /* regular co-op (not randomizer): souls are not shared */
 mode=1;uint8_t sf[6]={0x85,1};uint32_t f4=30001;memcpy(sf+2,&f4,4);laonline_v12_message(sf,6);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 printf("%-55s %s\n","Story Co-op: soul flags not applied",!flags_ext_get(30001)?"PASS":"FAIL");
 return 0;
}
