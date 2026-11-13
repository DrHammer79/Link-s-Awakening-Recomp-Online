/* V13: shared rupees with a full wallet (Velvet / Sanic logs) in Co-op Randomizer (laonline_v12.c). Appended to the V12 test harness setup. */
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
static int n_failed,n_reapplied,n_full,n_verified;
void dev_events_emit(const char *a,const char *b,const char *c,int d,int e,int64_t f,int64_t g,const char *h,const char *i,uint64_t j){(void)a;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;
 if(!strcmp(b,"V12_SHARED_RUPEES_FAILED"))n_failed++;if(!strcmp(b,"V12_SHARED_RUPEES_REAPPLIED"))n_reapplied++;if(!strcmp(b,"V13_SHARED_RUPEES_WALLET_FULL"))n_full++;if(!strcmp(b,"V12_SHARED_RUPEES_VERIFIED"))n_verified++;}
int laonline_v12_peer(int id,float pos[3],uint16_t *map,int *local){(void)id;(void)pos;(void)map;(void)local;return 0;}
static int drain(uint8_t frames[][32],int launch){int n=0;uint8_t f[32];int k;while((k=laonline_v12_packet(f,sizeof f,launch))>0&&n<600){memcpy(frames[n++],f,32);}return n;}
#define SV(o) (GA(LIVE_SAVE)+(o))
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220};g_ga=&ga;g_settings.laonline_sync_supplies=1;ST32(SV(0),0xe);
 cfg_load();cfg_consumables=1;float me[3]={0};
 ST32(SV(RUPEES),9999);
 for(int i=0;i<20;i++){laonline_v12_resource(102,1);for(int t=0;t<8;t++){frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}}
 printf("%-60s %s\n","full wallet: no FAILED / REAPPLIED spam",(!n_failed&&!n_reapplied)?"PASS":"FAIL");
 printf("%-60s %s\n","full wallet: logged once, wallet still 9999",(n_full==1&&LD32(SV(RUPEES))==9999)?"PASS":"FAIL");
 ST32(SV(RUPEES),100);laonline_v12_resource(105,1);for(int t=0;t<8;t++){frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}
 printf("%-60s %s\n","normal wallet: +50 written and verified",(LD32(SV(RUPEES))==150&&n_verified==1)?"PASS":"FAIL");
 ST32(SV(RUPEES),9990);laonline_v12_resource(106,1);for(int t=0;t<8;t++){frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}
 printf("%-60s %s\n","near full: capped at 9999",LD32(SV(RUPEES))==9999?"PASS":"FAIL");
 return 0;
}
