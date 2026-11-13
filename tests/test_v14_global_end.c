/* V14: "<name> saved Koholint!" in Global + nothing from co-op is applied in Global. */
#include <windows.h>
#include <assert.h>
#include <unistd.h>
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
const GameVersion *game_version(void){return &version;}const char *rando_seed_dir(void){return 0;}
void dev_events_emit(const char *a,const char *b,const char *c,int d,int e,int64_t f,int64_t g,const char *h,const char *i,uint64_t j){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;}
static float peer_pos[3]={0,0,5};static int rumbles;void input_hit_rumble(int ms){(void)ms;rumbles++;}
int laonline_v12_peer(int id,float pos[3],uint16_t *map,int *local){if(id!=1)return 0;memcpy(pos,peer_pos,12);*map=lao_name_id("Field");*local=0;return 1;}
static int drain(uint8_t frames[][32],int launch){int n=0;uint8_t f[32];int k;while((k=laonline_v12_packet(f,sizeof f,launch))>0&&n<600){memcpy(frames[n++],f,32);}return n;}
#define SV(o) (GA(LIVE_SAVE)+(o))
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220,.PLAYER_STATUS_PTR=MAIN_BASE+0x100000};g_ga=&ga;ST32(SV(0),0xe);ST16(SV(HP),12);
 uint64_t ps=MAIN_BASE+0x200000;ST64(GA(PLAYER_STATUS_PTR),ps);ST8(ps+0xa2,3);/* Y = Bow */ST8(ps+0xa3,7);/* X = Shovel */
 mode=3;int pass=1;static uint8_t fr[600][32];
 #define T(name,ok) do{int _o=(ok);printf("%-64s %s\n",name,_o?"PASS":"FAIL");pass&=_o;}while(0)
 float me[3]={0,0,0};
 uint8_t pong[5]={0x89};uint32_t r=LAO_V12_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 r=LAO_PVP135_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);r=LAO_PVPKO_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 #define ENDS() ({int _k=drain(fr,3),_e=0;for(int _i=0;_i<_k;_i++)if(fr[_i][2]==LAO_C_PVP&&fr[_i][3]==5)_e++;_e;})
 frame++;laonline_v12_ending();laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("relay without the V14 announcement: nothing sent (no disconnect)",ENDS()==0);
 r=LAO_GLOBALEND_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 frame++;laonline_v12_ending();laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("the ending movie in Global: the announcement is sent once",ENDS()==1);
 frame++;laonline_v12_tick(NULL,frame,"Ending","x",me,1);frame++;laonline_v12_tick(NULL,frame,"Ending","x",me,1);
 T("still in the ending: not sent again",ENDS()==0);
 mode=2;frame++;laonline_v12_ending();laonline_v12_tick(NULL,frame,"Field","x",me,1);mode=3;
 T("Co-op (not Global): no announcement",ENDS()==0);
 uint8_t m[32]={LAO_S_PVP,5,0,1,6};memcpy(m+5,"Velvet",6);laonline_v12_message(m,11);
 char l1[160],l2[96];float a=laonline_v12_killfeed(0,l1,sizeof l1,l2,sizeof l2);printf("   \"%s\" / \"%s\"\n",l1,l2);
 T("everyone sees (kill feed): Velvet woke the Wind Fish and saved Koholint!",a>0&&!strcmp(l1,"Velvet woke the Wind Fish and saved Koholint!"));
 frame+=8500/16;a=laonline_v12_killfeed(0,l1,sizeof l1,l2,sizeof l2);T("still shown after 8.5 s (10 s in all)",a>0);
 frame+=2000/16;a=laonline_v12_killfeed(0,l1,sizeof l1,l2,sizeof l2);T("gone after 10 s",a==0);
 m[2]=1;laonline_v12_message(m,11);laonline_v12_killfeed(0,l1,sizeof l1,l2,sizeof l2);T("your own: You woke the Wind Fish and saved Koholint!",!strncmp(l1,"You woke",8));
 /* Global = your own world: a teammate's story flag / item never applies */
 g_settings.laonline_sync_story=1;g_settings.laonline_shared_items=1;uint8_t fl[6]={0x85,1};uint32_t flag=(uint32_t)lao_v12_story_flags[0];memcpy(fl+2,&flag,4);laonline_v12_message(fl,6);
 uint8_t it[6]={0x86,1};uint16_t key=0x7100;int16_t val=0x7fff;memcpy(it+2,&key,2);memcpy(it+4,&val,2);laonline_v12_message(it,6);
 uint32_t items=LD32(SV(ITEMS));frame+=100;for(int i=0;i<5;i++){frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}
 T("Global: a story flag sent by someone else is NOT applied",!flags_ext_get((int)lao_v12_story_flags[0]));
 T("Global: items sent by someone else are NOT applied",LD32(SV(ITEMS))==items);
 return pass?0:1;
}
