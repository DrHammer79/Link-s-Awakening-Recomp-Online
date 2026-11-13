/* V14: PvP hit markers + lock-on (laonline_v12.c). */
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
#define SETHP(v) (ST16(SV(HP),(v)),ST16(MAIN_BASE+0x200000+0x84,(v)))
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220,.PLAYER_STATUS_PTR=MAIN_BASE+0x100000};g_ga=&ga;ST32(SV(0),0xe);SETHP(12);
 uint64_t ps=MAIN_BASE+0x200000;ST64(GA(PLAYER_STATUS_PTR),ps);ST8(ps+0xa2,3);/* Y = Bow */ST8(ps+0xa3,7);/* X = Shovel */
 mode=3;int pass=1;uint8_t fr[600][32];
 #define T(name,ok) do{int _o=(ok);printf("%-62s %s\n",name,_o?"PASS":"FAIL");pass&=_o;}while(0)
 uint8_t pong[5]={0x89};uint32_t r=LAO_V12_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);r=LAO_PVP135_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 laonline_v12_key(6,1,0);float me[3]={0,0,0};frame+=5;laonline_v12_tick(NULL,frame,"Field","x",me,1);drain(fr,3);
 uint8_t on[4]={LAO_S_PVP,0,1,1};laonline_v12_message(on,4);
 float pos[8][3],age[8];int q[8],mine[8],onme[8],n;
 /* older relay (no V14 confirmations): your own arrow that reaches gives a marker right away */
 for(int i=1;i<=3;i++){me[2]=i*0.1f;frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}
 laonline_v12_input(1ull<<3,0);laonline_v12_input(0,0);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);frame+=20;laonline_v12_tick(NULL,frame,"Field","x",me,1);drain(fr,3);
 frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 n=laonline_v12_hitmarks(pos,age,q,mine,onme,8);
 T("older relay: your arrow hit shows a hit marker on that player",n==1&&mine[0]&&!onme[0]&&pos[0][2]==5.f&&q[0]==2);
 frame+=60;laonline_v12_tick(NULL,frame,"Field","x",me,1);T("the marker is gone after 0.9 s",laonline_v12_hitmarks(pos,age,q,mine,onme,8)==0);
 /* V14 relay: markers come from the server's confirmation */
 r=LAO_PVPKO_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 laonline_v12_input(1ull<<3,0);laonline_v12_input(0,0);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);frame+=20;laonline_v12_tick(NULL,frame,"Field","x",me,1);drain(fr,3);
 T("V14 relay: no marker until the server confirms the hit",laonline_v12_hitmarks(pos,age,q,mine,onme,8)==0);
 uint8_t c1[5]={LAO_S_PVP,4,0,1,2};laonline_v12_message(c1,5);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 n=laonline_v12_hitmarks(pos,age,q,mine,onme,8);T("confirmed: X marker + damage on the player you hit",n==1&&mine[0]&&q[0]==2&&pos[0][2]==5.f);
 frame+=60;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 uint8_t c2[5]={LAO_S_PVP,4,1,0,4};laonline_v12_message(c2,5);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 n=laonline_v12_hitmarks(pos,age,q,mine,onme,8);T("you were hit: the burst shows on your own Link",n==1&&onme[0]&&!mine[0]&&pos[0][2]==me[2]);
 uint8_t c3[5]={LAO_S_PVP,4,2,3,2};laonline_v12_message(c3,5);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("a hit between two players not here: not drawn",laonline_v12_hitmarks(pos,age,q,mine,onme,8)==1);
 frame+=60;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 /* lock-on (V15: ztarget.c owns it - here: L3 / F8 reach it, and PvP aims at whatever it is locked on) */
 int before=zt_toggles;laonline_v12_input(1ull<<4,0);laonline_v12_input(0,0);laonline_v12_key(8,1,0);
 T("L3 and F8 toggle the Z-target",zt_toggles==before+2);
 zt_on=1;zt_pos[0]=0;zt_pos[1]=0;zt_pos[2]=5;peer_pos[2]=5;
 for(int i=1;i<=3;i++){me[2]-=0.1f;frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}  /* walk away from him */
 laonline_v12_input(1ull<<3,0);laonline_v12_input(0,0);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);frame+=20;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 int k=drain(fr,3),hit=0;for(int i=0;i<k;i++)if(fr[i][2]==LAO_C_PVP&&fr[i][3]==1&&fr[i][4]==1)hit++;
 T("Z-targeted: the arrow still flies at him while you back off",hit==1);
 zt_on=0;
 return pass?0:1;
}
