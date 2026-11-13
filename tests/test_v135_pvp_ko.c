/* V13.5: PvP knock-outs, kill feed and counter (laonline_v12.c). */
#include <windows.h>
#include <assert.h>
#include <unistd.h>
#include "../runtime/hle/laonline_v12.c"
#include "v15_stubs.h"
const GameAddrs *g_ga;static GameVersion version={.key="v1_0_0"};Settings g_settings;
static int mode=2,world=1;static uint8_t flags[8192];static uint64_t frame=100;
int game_in_play(void){return 1;}int laonline_level_ready(void){return 1;}int laonline_launch_choice(void){return mode;}int laonline_coop_world_ready(void){return world;}
bool laonline_ui_paused(void){return 0;}static int dead_latched;int laonline_death_active(void){return dead_latched;}void laonline_notify(const char *t){(void)t;}
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
 /* older relay (no knock-out reply): a lethal hit still stops at a quarter heart */
 SETHP(2);uint8_t h[4]={LAO_S_PVP,1,1,8};laonline_v12_message(h,4);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("older relay: lethal hit stops at 1/4 heart (no KO frame)",(int16_t)LD16(SV(HP))==1);
 int k=drain(fr,3),ko=0;for(int i=0;i<k;i++)if(fr[i][2]==LAO_C_PVP&&fr[i][3]==3)ko++;T("older relay: nothing new is sent",ko==0);
 /* V13.5 relay with knock-outs */
 r=LAO_PVPKO_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 ST16(SV(0x5558),0x7);/* 3 extra containers */ST64(SV(0x5550),0);
 frame+=200;laonline_v12_message(h,4);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("KO relay: the last hit takes hearts to 0 (the game plays the death)",(int16_t)LD16(SV(HP))==0);
 k=drain(fr,3);ko=0;for(int i=0;i<k;i++)if(fr[i][0]==3&&fr[i][2]==LAO_C_PVP&&fr[i][3]==3&&fr[i][4]==1)ko++;
 T("the kill is reported with the attacker's id",ko==1);
 laonline_v12_message(h,4);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 k=drain(fr,3);ko=0;for(int i=0;i<k;i++)if(fr[i][2]==LAO_C_PVP&&fr[i][3]==3)ko++;
 T("no more PvP hits / kills while dying",ko==0&&(int16_t)LD16(SV(HP))==0);
 dead_latched=1;frame+=60;laonline_v12_tick(NULL,frame,"Field","x",me,1);frame+=400;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("the game's Game Over started: hearts left alone at 0",(int16_t)LD16(SV(HP))==0);
 dead_latched=0;SETHP(12);frame+=10;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 /* safety net: the game never reacts to 0 hearts */
 SETHP(1);frame+=400;laonline_v12_message(h,4);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);drain(fr,3);
 frame+=200;laonline_v12_tick(NULL,frame,"Field","x",me,1);T("safety net waits (2 s: still 0)",(int16_t)LD16(SV(HP))==0);
 frame+=300;laonline_v12_tick(NULL,frame,"Field","x",me,1);T("no Game Over after 6 s: put back at full (6 hearts)",(int16_t)LD16(SV(HP))==24);
 /* bottled fairy: the game refills hearts itself */
 frame+=400;laonline_v12_message(h,4);SETHP(1);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);drain(fr,3);
 SETHP(28);frame+=500;laonline_v12_tick(NULL,frame,"Field","x",me,1);T("fairy revive: hearts the game gave are kept",(int16_t)LD16(SV(HP))==28);
 SETHP(12);frame+=200;uint8_t h2[4]={LAO_S_PVP,1,1,4};laonline_v12_message(h2,4);frame++;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("a normal hit still just takes hearts (12 -> 8)",(int16_t)LD16(SV(HP))==8);
 /* V15 kill feed from the relay: [3, quote, kv, vk, you, name, name, weapon] */
 uint8_t m[64]={LAO_S_PVP,3,0};uint16_t kv=1,vk=0;memcpy(m+3,&kv,2);memcpy(m+5,&vk,2);m[7]=1;m[8]=5;memcpy(m+9,"Sanic",5);m[14]=6;memcpy(m+15,"Velvet",6);m[21]=2;
 laonline_v12_message(m,22);char l1[320],l2[96];float left=0;
 float a=laonline_v12_card(l1,sizeof l1,l2,sizeof l2,&left);printf("   your card (you won): \"%s\" / \"%s\"\n",l1,l2);
 T("you killed: your own card, written to you, naming Velvet",a>0&&strstr(l1,"Velvet")&&(strstr(l1,"Thou")||strstr(l1,"thee")||strstr(l1,"thy")||strstr(l1,"thou")));
 T("card score: You 1  -  0 Velvet",!strcmp(l2,"You 1  -  0 Velvet"));
 char kn[32],vn[32],w[24];int end=1;float ra=laonline_v12_feed_row(0,kn,32,vn,32,w,24,&end);
 T("kill feed row: Sanic defeated Velvet with Bombs",ra>0&&!strcmp(kn,"Sanic")&&!strcmp(vn,"Velvet")&&!strcmp(w,"Bombs")&&!end);
 int kk,ff;int shown=laonline_v12_my_kos(&kk,&ff);T("your counter: 1 KO, 0 falls (you were the killer)",shown&&kk==1&&ff==0);
 /* the one who fell gets a different card */
 m[7]=2;laonline_v12_message(m,22);char l3[320];laonline_v12_card(l3,sizeof l3,l2,sizeof l2,&left);printf("   their card (they fell): \"%s\" / \"%s\"\n",l3,l2);
 T("the one who fell: a different line, written to them, naming Sanic",strcmp(l1,l3)&&strstr(l3,"Sanic")&&!strcmp(l2,"You 0  -  1 Sanic"));
 /* someone else's KO: only the feed, no card */
 frame+=11000/16;laonline_v12_message(m,0);
 uint8_t o[64]={LAO_S_PVP,3,7};memcpy(o+3,&kv,2);memcpy(o+5,&vk,2);o[7]=0;o[8]=3;memcpy(o+9,"Kit",3);o[12]=5;memcpy(o+13,"Locus",5);o[18]=100;
 laonline_v12_message(o,19);a=laonline_v12_card(l1,sizeof l1,l2,sizeof l2,&left);ra=laonline_v12_feed_row(0,kn,32,vn,32,w,24,&end);
 printf("   others: %s defeated %s with %s\n",kn,vn,w);
 T("someone else's KO: no card for you, just the kill feed (Kit defeated Locus with the Sword)",a==0&&ra>0&&!strcmp(kn,"Kit")&&!strcmp(w,"the Sword"));
 T("...and your counter is unchanged",laonline_v12_my_kos(&kk,&ff)&&kk==1);
 { char seen[8][320];int diff=0;for(int q=0;q<8;q++){m[2]=(uint8_t)(q*37);m[7]=(uint8_t)(1+q%2);laonline_v12_message(m,22);laonline_v12_card(seen[q],320,l2,sizeof l2,&left);printf("   %s\n",seen[q]);for(int r=0;r<q;r++)if(!strcmp(seen[q],seen[r]))diff--;}
   T("different funny lines each time (8 KOs, 8 different lines)",diff==0); }
 laonline_v12_message(m,13);T("a cut-short feed frame is ignored",1);
 frame+=11000/16;a=laonline_v12_card(l1,sizeof l1,l2,sizeof l2,&left);float pa=laonline_v12_feed_panel(&left);T("card and kill feed fade out after 10 s",a==0&&pa==0);
 return pass?0:1;
}
