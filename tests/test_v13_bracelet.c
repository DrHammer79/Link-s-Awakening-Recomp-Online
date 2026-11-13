/* V13: Power Bracelet team level in Co-op Randomizer (laonline_v12.c). Appended to the V12 test harness setup. */
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
 float me[3]={0};uint8_t fr[600][32];mode=2;
 #define LV() (!(LD32(SV(0x5568))&(1u<<10))?0:(LD8(SV(0x555C))&4)?2:1)
 /* Cursey's case: both players picked a bracelet before syncing - each got Lv1 locally */
 ST32(SV(0x5568),1u<<10);laonline_v12_bracelet_found(12,34);
 uint8_t s[6]={0x86,1};uint16_t key=K_BRLOC+bracelet_place(40,7);int16_t v=1;memcpy(s+2,&key,2);memcpy(s+4,&v,2);laonline_v12_message(s,6);
 frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 printf("%-60s %s\n","two bracelets found in different places -> Lv2",LV()==2?"PASS":"FAIL");
 int k=drain(fr,2),sent=0;for(int i=0;i<k;i++){uint16_t kk;memcpy(&kk,fr[i]+3,2);if(fr[i][2]==4&&kk==K_BRLOC+bracelet_place(12,34))sent=1;}
 printf("%-60s %s\n","own pickup place published to the room",sent?"PASS":"FAIL");
 /* fresh player (late joiner) with nothing: gets Lv2 from the relay's stored places */
 memset(bracelet_local,0,sizeof bracelet_local);ST32(SV(0x5568),0);ST8(SV(0x555C),0);
 frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 printf("%-60s %s\n","late joiner without bracelet: places known -> Lv1 at least",LV()>=1?"PASS":"FAIL");
 /* Shield (Cursey: Kit had to get both copies): Lv1 here, a teammate's shield elsewhere -> Mirror Shield */
 ST32(SV(0x5568),LD32(SV(0x5568))|(1u<<1));laonline_v12_progressive_found(2,3,4);
 key=K_BRLOC+1*K_BRLOC_N+bracelet_place(9,9);memcpy(s+2,&key,2);laonline_v12_message(s,6);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 printf("%-60s %s\n","two shields in different places -> Mirror Shield",(LD8(SV(0x555C))&2)?"PASS":"FAIL");
 /* receiving turned off: a teammate's sword places don't upgrade this player */
 g_settings.laonline_sync_upgrades=0;key=K_BRLOC+2*K_BRLOC_N+bracelet_place(1,1);memcpy(s+2,&key,2);laonline_v12_message(s,6);key=K_BRLOC+2*K_BRLOC_N+bracelet_place(2,2);memcpy(s+2,&key,2);laonline_v12_message(s,6);
 ST32(SV(0x5568),LD32(SV(0x5568))&~1u);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 printf("%-60s %s\n","\"Share upgrades\" off: nothing applied",!(LD32(SV(0x5568))&1)?"PASS":"FAIL");
 g_settings.laonline_sync_upgrades=1;frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 printf("%-60s %s\n","turned back on: Noble Sword from two sword places",((LD32(SV(0x5568))&1)&&(LD8(SV(0x555C))&1))?"PASS":"FAIL");
 return 0;
}
