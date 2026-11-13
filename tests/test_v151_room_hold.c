/* V15.1: a shared battle / puzzle-room flag waits while you are in that room (Cursey: froze in Eagle Tower / Bottle Grotto). */
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
#define SV(o) (GA(LIVE_SAVE)+(o))
static void story(uint32_t f){uint8_t m[6]={0x85,1};memcpy(m+2,&f,4);laonline_v12_message(m,6);}
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220};g_ga=&ga;g_settings.laonline_sync_story=1;g_settings.laonline_sync_world=1;ST32(SV(0),0xe);
 float me[3]={0};int pass=1;mode=2;
 #define T(name,ok) do{int _o=(ok);printf("%-70s %s\n",name,_o?"PASS":"FAIL");pass&=_o;}while(0)
 #define TICK(lv,rm) do{frame+=25;laonline_v12_tick(NULL,frame,lv,rm,me,1);}while(0)
 /* Sanic stands in the "match the shapes" room (04B) when Raygar clears it */
 TICK("Lv07EagleTower","Lv07EagleTower_04B");story(959);TICK("Lv07EagleTower","Lv07EagleTower_04B");
 T("teammate clears the room you're in (TboxPop_Vegas_L07_04B): held",!flags_ext_get(959));
 story(950);TICK("Lv07EagleTower","Lv07EagleTower_04B");
 T("a flag for the NEXT room (DestroyBtl_L07_04A) applies right away",flags_ext_get(950));
 TICK("Lv07EagleTower","Lv07EagleTower_04B");TICK("Lv07EagleTower","Lv07EagleTower_04B");
 T("still held while you stay in 04B",!flags_ext_get(959));
 TICK("Lv07EagleTower","Lv07EagleTower_05B");
 T("you leave the room: applied (it loads cleared next time)",flags_ext_get(959));
 /* Bottle Grotto: room written without the leading zero in the flag name (KeyPop_Btl_L02_7D), room 07D */
 TICK("Lv02BottleGrotto","Lv02BottleGrotto_07D");story(790);TICK("Lv02BottleGrotto","Lv02BottleGrotto_07D");
 T("Bottle Grotto battle key (KeyPop_Btl_L02_7D) held in room 07D",!flags_ext_get(790));
 /* the dungeon key spot (world flag KeyGet_L02_7D = 787) too */
 story(787);TICK("Lv02BottleGrotto","Lv02BottleGrotto_07D");
 T("the Small Key spot of that room (KeyGet_L02_7D) held too",!flags_ext_get(787));
 /* you clear it yourself meanwhile: nothing else happens */
 flags_put(790,true);TICK("Lv02BottleGrotto","Lv02BottleGrotto_07D");
 TICK("Lv02BottleGrotto","Lv02BottleGrotto_06D");
 T("left: the key spot is applied",flags_ext_get(787));
 /* same room code in ANOTHER dungeon is not held */
 TICK("Lv03KeyCavern","Lv03KeyCavern_04B");story(958);TICK("Lv03KeyCavern","Lv03KeyCavern_04B");
 T("Eagle's Tower flag while in Key Cavern room 04B: applied (different dungeon)",flags_ext_get(958));
 return pass?0:1;
}
