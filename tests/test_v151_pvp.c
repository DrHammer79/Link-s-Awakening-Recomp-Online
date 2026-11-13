/* V15.1: PvP fixes from the V15 test night - boomerang cooldown / size, no attacks while knocked out, both heart copies, no frozen Link. */
#include <windows.h>
#include <assert.h>
#include <unistd.h>
#include "../runtime/hle/laonline_v12.c"
#define V15_REAL_ZTARGET
#include "v15_stubs.h"
void ztarget_toggle(void){}int ztarget_get(float p[3],int *pl){(void)p;(void)pl;return 0;}void ztarget_note_pvp_hit(void){}
int ztarget_yaw_toward(float dx,float dz,float *y){(void)dx;(void)dz;*y=0;return 1;}uint16_t ztarget_damage_anim(void){return 0x31;}
static int dying;int laonline_death_active(void){return dying;}  /* (laonline_death.c in the game) */
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
#define STATUS_HP (MAIN_BASE+0x200000+0x84)
#define SETHP(v) (ST16(SV(HP),(v)),ST16(STATUS_HP,(v)))
static int hits(uint8_t fr[][32],int k){int h=0;for(int i=0;i<k;i++)if(fr[i][2]==LAO_C_PVP&&fr[i][3]==1&&fr[i][4]==1)h++;return h;}
static float me[3];
static void tick(int n){for(int i=0;i<n;i++){frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);}}
static void press_y(void){laonline_v12_input(1ull<<3,0);laonline_v12_input(0,0);}
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220,.PLAYER_STATUS_PTR=MAIN_BASE+0x100000};g_ga=&ga;ST32(SV(0),0xe);SETHP(12);
 uint64_t ps=MAIN_BASE+0x200000;ST64(GA(PLAYER_STATUS_PTR),ps);ST8(ps+0xa2,5);/* Y = Boomerang */
 mode=3;int pass=1;uint8_t fr[600][32];
 #define T(name,ok) do{int _o=(ok);printf("%-66s %s\n",name,_o?"PASS":"FAIL");pass&=_o;}while(0)
 uint8_t pong[5]={0x89};uint32_t r=LAO_V12_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);r=LAO_PVP135_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 laonline_v12_key(6,1,0);tick(5);drain(fr,3);
 uint8_t on[4]={LAO_S_PVP,0,1,1};laonline_v12_message(on,4);
 for(int i=1;i<=3;i++){me[2]=i*0.1f;tick(1);}  /* face north, peer at z=5 */
 press_y();tick(20);int k=drain(fr,3);
 T("boomerang hits the player in front",hits(fr,k)==1);
 press_y();tick(5);press_y();tick(20);k=drain(fr,3);
 T("V15.1: pressing again while it's still out: no new hits",hits(fr,k)==0);
 tick(60);press_y();tick(20);k=drain(fr,3);
 T("...once it's back (1.2 s), it can hit again",hits(fr,k)==1);
 peer_pos[2]=6.4f;tick(80);press_y();tick(20);k=drain(fr,3);
 T("V15.1: smaller reach - 6.1 away is out of range (was 7)",hits(fr,k)==0);
 peer_pos[2]=5;
 /* hit taken: both heart copies, no forced animation, 1 s invincible */
 v15_anim=0;uint8_t h[4]={LAO_S_PVP,1,1,2};laonline_v12_message(h,4);tick(1);
 T("V15.1: hit writes the save AND Link's live hearts (HUD agrees)",(int16_t)LD16(SV(HP))==10&&(int16_t)LD16(STATUS_HP)==10);
 T("V15.1: no forced damage animation (it froze Link)",v15_anim==0);
 tick(30);laonline_v12_message(h,4);tick(1);
 T("V15.1: 0.5 s later: still invincible (game-like 1 s)",(int16_t)LD16(STATUS_HP)==10);
 tick(40);laonline_v12_message(h,4);tick(1);
 T("after 1 s: hit again",(int16_t)LD16(STATUS_HP)==8);
 T("Link was not held still for long (stagger <= 0.25 s)",(tick(20),!laonline_v12_stagger()));
 /* the game itself changed Link's live hearts (a heart pickup): PvP goes by those */
 ST16(STATUS_HP,16);tick(80);laonline_v12_message(h,4);tick(1);
 T("V15.1: damage taken from Link's live hearts (16 -> 14)",(int16_t)LD16(STATUS_HP)==14&&(int16_t)LD16(SV(HP))==14);
 /* knocked out: lethal hit -> 0 in both, then no attacks */
 uint8_t pk[5]={0x89};r=LAO_PVPKO_REPLY;memcpy(pk+1,&r,4);laonline_v12_message(pk,5);
 SETHP(2);tick(80);uint8_t hk[4]={LAO_S_PVP,1,1,4};laonline_v12_message(hk,4);tick(1);
 T("V15.1: KO takes BOTH heart copies to 0 (the game's own death)",(int16_t)LD16(SV(HP))==0&&(int16_t)LD16(STATUS_HP)==0);
 drain(fr,3);dying=1;tick(90);press_y();laonline_v12_input(1ull<<1,0);laonline_v12_input(0,0);tick(20);k=drain(fr,3);
 int vis=0;for(int i=0;i<k;i++)if(fr[i][2]==LAO_C_PVP&&fr[i][3]==2)vis++;
 T("V15.1: knocked out: your boomerang / sword hit no one",hits(fr,k)==0);
 T("V15.1: ...and nothing is shown for others",vis==0);
 dying=0;SETHP(12);tick(400);press_y();tick(20);k=drain(fr,3);
 T("back up (Continue / fairy): attacks work again",hits(fr,k)==1);
 /* V15.1: death notice cause - the server confirmed Kit (#2) hit Velvet (#1) just before she went down */
 { uint8_t m[5]={LAO_S_PVP,4,2,1,4};laonline_v12_message(m,5);char who[17]="";
   T("V15.1: teammate KO'd right after a PvP hit: notice names who did it",laonline_v12_pvp_killer(1,who,sizeof who)&&!strcmp(who,"Kit"));
   tick(600);T("...but not for a death 10 s later (enemy / fall): stays \"Cause unknown\"",!laonline_v12_pvp_killer(1,who,sizeof who)); }
 return pass?0:1;
}
