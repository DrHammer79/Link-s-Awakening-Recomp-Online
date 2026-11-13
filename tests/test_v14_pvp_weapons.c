/* V14: every weapon in PvP - sword levels, spin attack, bomb arrows, bombs where placed, boots dash, feather stomp, visuals. */
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
 mode=3;int pass=1;static uint8_t fr[600][32];
 #define T(name,ok) do{int _o=(ok);printf("%-64s %s\n",name,_o?"PASS":"FAIL");pass&=_o;}while(0)
 uint8_t pong[5]={0x89};uint32_t r=LAO_V12_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);r=LAO_PVP135_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 r=LAO_PVPKO_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 laonline_v12_key(6,1,0);float me[3]={0,0,0};frame+=5;laonline_v12_tick(NULL,frame,"Field","x",me,1);drain(fr,3);
 uint8_t on[4]={LAO_S_PVP,0,1,1};laonline_v12_message(on,4);
 #define TICK(n_) do{for(int _i=0;_i<(n_);_i++){frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}}while(0)
 #define HITS(q_) ({int _k=drain(fr,3),_h=0;for(int _i=0;_i<_k;_i++)if(fr[_i][2]==LAO_C_PVP&&fr[_i][3]==1&&fr[_i][4]==1&&(q_<0||fr[_i][5]==q_)){_h++;}_h;})
 #define SHOWN(it_) ({int _s=0;for(int _i=0;_i<_k2;_i++)if(fr[_i][2]==LAO_C_PVP&&fr[_i][3]==2&&fr[_i][4]==(it_))_s++;_s;})

 for(int i=1;i<=3;i++){me[2]=i*0.1f;TICK(1);}  /* face +z (peer at z=5) */
 /* sword: L1 = 1/2 heart, Noble Sword = 1 heart, and a slash others see */
 peer_pos[2]=1.5f;ST32(SV(ITEMS),LD32(SV(ITEMS))|1);ST8(SV(UPGRADES),0);
 laonline_v12_input(1ull<<1,0);TICK(1);int _k2=drain(fr,3);int h=0;for(int i=0;i<_k2;i++)if(fr[i][3]==1&&fr[i][5]==2)h++;
 T("sword L1 hits for 1/2 heart",h==1);T("sword slash is sent for others to see",SHOWN(100)==1);
 laonline_v12_input(0,0);frame+=30;ST8(SV(UPGRADES),1);laonline_v12_input(1ull<<1,0);TICK(1);
 T("Noble Sword (L2) hits for 1 heart",HITS(4)==1);
 /* spin attack: hold B, let go after 0.9 s */
 frame+=60;TICK(1);drain(fr,3);peer_pos[2]=-2.f;frame+=60;laonline_v12_input(0,0);TICK(1);drain(fr,3);
 laonline_v12_input(1ull<<1,0);TICK(1);drain(fr,3);frame+=60;laonline_v12_input(0,0);TICK(2);_k2=drain(fr,3);h=0;for(int i=0;i<_k2;i++)if(fr[i][3]==1&&fr[i][5]==8)h++;
 T("spin attack hits all around (behind too), L2 = 2 hearts",h==1);T("spin attack is sent for others to see",SHOWN(101)==1);
 /* bomb arrow: X = Bombs, Y = Bow pressed together */
 ST8(ps+0xa3,2);ST8(ps+0xa2,3);peer_pos[2]=6.f;frame+=60;TICK(1);drain(fr,3);
 laonline_v12_input(1ull<<3,0);laonline_v12_input(0,0);TICK(1);laonline_v12_input(1ull<<2,0);laonline_v12_input(0,0);TICK(1);
 _k2=drain(fr,3);T("Bow + Bombs together = a bomb arrow (others see it)",SHOWN(102)==1);
 frame+=12;TICK(1);T("bomb arrow hits for 1.5 hearts",HITS(6)==1);
 frame+=120;TICK(1);T("...and no separate bomb blast / arrow afterwards",HITS(-1)==0);
 /* bomb: the blast is where it was put down (in front), not on Link */
 peer_pos[2]=3.3f;frame+=60;laonline_v12_input(1ull<<2,0);laonline_v12_input(0,0);TICK(1);drain(fr,3);frame+=100;TICK(1);
 T("bomb put down in front: blast reaches a player 3.3 ahead",HITS(4)==1);
 peer_pos[2]=-3.0f;laonline_v12_input(1ull<<2,0);laonline_v12_input(0,0);TICK(1);drain(fr,3);frame+=100;TICK(1);
 T("...but not one 3 behind",HITS(-1)==0);
 /* Pegasus Boots dash: X = Boots held, running fast at him */
 ST8(ps+0xa3,11);me[2]=0;peer_pos[2]=9.f;frame+=60;TICK(1);drain(fr,3);
 laonline_v12_input(1ull<<2,0);int dh=0,ds=0;
 for(int i=0;i<40;i++){me[2]+=0.3f;frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);int k=drain(fr,3);for(int j=0;j<k;j++){if(fr[j][3]==1&&fr[j][5]==8)dh++;if(fr[j][3]==2&&fr[j][4]==103)ds++;}}
 laonline_v12_input(0,0);
 T("boots dash rams him once (with the Noble Sword: 2 hearts)",dh==1);T("boots dash dust is sent for others to see",ds>=1);
 /* walking (slow) with boots held: no dash */
 me[2]=0;frame+=60;TICK(1);drain(fr,3);laonline_v12_input(1ull<<2,0);dh=0;
 for(int i=0;i<40;i++){me[2]+=0.02f;frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);int k=drain(fr,3);for(int j=0;j<k;j++)if(fr[j][3]==1)dh++;}
 laonline_v12_input(0,0);T("walking slowly with the boots: no ram",dh==0);
 /* Roc's Feather: jump and land on him */
 ST8(ps+0xa2,9);peer_pos[2]=me[2]+0.8f;frame+=60;TICK(1);drain(fr,3);
 laonline_v12_input(1ull<<3,0);laonline_v12_input(0,0);TICK(1);_k2=drain(fr,3);T("feather jump is sent for others to see",SHOWN(104)==1);
 frame+=35;TICK(1);T("landing on him: stomp for 1/2 heart",HITS(2)==1);
 /* receiving: every new visual arrives as an effect to draw */
 int got=0;for(int it=100;it<=104;it++){uint8_t m[20]={LAO_S_PVP,2,1,(uint8_t)it};uint16_t mp=lao_name_id("Field");memcpy(m+4,&mp,2);float pp[3]={1,0,2};memcpy(m+6,pp,12);m[18]=100;laonline_v12_message(m,20);}
 {int it[16];float p2[16][3],dr[16][2],ag[16];uint16_t mm[16];int ne=laonline_v12_effects(it,p2,dr,ag,mm,16);for(int i=0;i<ne;i++)if(it[i]>=100&&it[i]<=104)got++;}
 T("a teammate's sword / spin / bomb arrow / dash / jump all arrive to draw",got==5);
 return pass?0:1;
}
