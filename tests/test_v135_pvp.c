/* V13.5: PvP item attacks + hit reaction (laonline_v12.c). Appended to the V12 test harness setup. */
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
 #define T(name,ok) do{int _o=(ok);printf("%-58s %s\n",name,_o?"PASS":"FAIL");pass&=_o;}while(0)
 uint8_t pong[5]={0x89};uint32_t r=LAO_V12_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);r=LAO_PVP135_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 laonline_v12_key(6,1,0);float me[3]={0,0,0};frame+=5;laonline_v12_tick(NULL,frame,"Field","x",me,1);drain(fr,3);
 uint8_t on[4]={LAO_S_PVP,0,1,1};laonline_v12_message(on,4);
 /* walk north (+z) so Link faces the peer at z=5, then press Y (bow) */
 for(int i=1;i<=3;i++){me[2]=i*0.1f;frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}
 laonline_v12_input(1ull<<3,0);laonline_v12_input(0,0);
 frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);frame+=20;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 int k=drain(fr,3),hit=0;for(int i=0;i<k;i++)if(fr[i][2]==LAO_C_PVP&&fr[i][3]==1&&fr[i][4]==1&&fr[i][0]==4&&fr[i][5]==2)hit++;
 T("Bow on Y: arrow hits the PvP player in front (1/2 heart)",hit==1);
 int vis=0;for(int i=0;i<k;i++)if(fr[i][2]==LAO_C_PVP&&fr[i][3]==2&&fr[i][4]==3&&fr[i][0]==19)vis++;
 T("the arrow is also sent for others to SEE",vis==1);
 { uint8_t m[20]={LAO_S_PVP,2,1,2};uint16_t mp=lao_name_id("Field");memcpy(m+4,&mp,2);float pp[3]={1,0,2};memcpy(m+6,pp,12);m[18]=100;m[19]=0;laonline_v12_message(m,20);
   int it[16];float ps2[16][3],dr[16][2],ag[16];uint16_t mm[16];int ne=laonline_v12_effects(it,ps2,dr,ag,mm,16);
   T("a teammate's bomb arrives as an effect to draw here",ne==1&&it[0]==2&&ps2[0][2]==2.f&&dr[0][0]==1.f); }
 /* turn around (walk south) and shoot again: no hit */
 for(int i=1;i<=3;i++){me[2]-=0.1f;frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}
 laonline_v12_input(1ull<<3,0);laonline_v12_input(0,0);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);frame+=20;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 k=drain(fr,3);hit=0;for(int i=0;i<k;i++)if(fr[i][2]==LAO_C_PVP&&fr[i][3]==1)hit++;
 T("facing away: no hit",hit==0);
 laonline_v12_input(1ull<<2,0);laonline_v12_input(0,0);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);frame+=20;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 k=drain(fr,3);hit=0;for(int i=0;i<k;i++)if(fr[i][2]==LAO_C_PVP&&fr[i][3]==1)hit++;
 T("Shovel on X is not an attack",hit==0);
 /* being hit: health down, stagger, flash, rumble */
 uint8_t h[4]={LAO_S_PVP,1,1,4};laonline_v12_message(h,4);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("hit by a 1-heart item: 12 -> 8 quarter hearts",(int16_t)LD16(SV(HP))==8);
 T("hit reaction: stagger + red flash + rumble",laonline_v12_stagger()&&laonline_v12_hit_flash()>0&&rumbles==1);
 /* V15: knockback - pushed away from the attacker (peer at z=5, Link below him) for 0.25 s */
 v15_pc=MAIN_BASE+0x300000;float *kp=(float*)(uintptr_t)(v15_pc+0x80);kp[0]=me[0];kp[1]=0;kp[2]=me[2];float z0=kp[2];
 frame+=100;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 uint8_t h2[4]={LAO_S_PVP,1,1,2};laonline_v12_message(h2,4);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 for(int i=0;i<20;i++){frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}
 printf("   knocked from z %.2f to %.2f\n",z0,kp[2]);
 T("V15 hit: knocked back away from the attacker (~1.6)",z0-kp[2]>1.2f&&z0-kp[2]<2.0f&&fabsf(kp[0]-me[0])<.01f);
 return pass?0:1;
}
