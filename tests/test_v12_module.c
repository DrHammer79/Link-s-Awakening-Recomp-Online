#include <windows.h>
#include <assert.h>
#include "../runtime/hle/laonline_v12.c"
int laonline_death_active(void){return 0;}  /* (laonline_death.c in the game) */
const GameAddrs *g_ga;static GameVersion version={.key="v1_0_0"};Settings g_settings;
static int mode=1,ready=1,paused=0,world=1;static uint8_t flags[256];static char note[200];static uint64_t frame=100,clockms=1000;
int game_in_play(void){return 1;}int laonline_level_ready(void){return ready;}int laonline_launch_choice(void){return mode;}int laonline_coop_world_ready(void){return world;}
bool laonline_ui_paused(void){return paused;}void laonline_notify(const char *t){snprintf(note,sizeof note,"%s",t);}
bool flags_ext_get(int i){return (flags[i>>3]>>(i&7))&1;}void flags_put(int i,bool on){if(on)flags[i>>3]|=1u<<(i&7);else flags[i>>3]&=~(1u<<(i&7));}
static char warped[80];void warp_request(const char *l,const char *loc){snprintf(warped,sizeof warped,"%s/%s",l,loc);}
uint64_t renderer_present_count(void){return frame;}uint64_t plat_now_ns(void){return clockms*1000000ull;}
const GameVersion *game_version(void){return &version;}
void dev_events_emit(const char *a,const char *b,const char *c,int d,int e,int64_t f,int64_t g,const char *h,const char *i,uint64_t j){(void)a;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;printf("  dev %s %lld->%lld\n",b,(long long)f,(long long)g);}
static float peer_pos[3];static int peer_on=1;
int laonline_v12_peer(int id,float pos[3],uint16_t *map,int *local){pos[0]=peer_pos[0];pos[1]=peer_pos[1];pos[2]=peer_pos[2];*map=lao_name_id("Field");*local=0;return id==1&&peer_on;}
static int drain(uint8_t frames[][16],int launch){int n=0;uint8_t f[16];int k;while((k=laonline_v12_packet(f,sizeof f,launch))>0){memcpy(frames[n++],f,16);}return n;}
#define SV(o) (GA(LIVE_SAVE)+(o))
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220};g_ga=&ga;ST32(SV(0),0xe);
 float me[3]={0,0,0};uint8_t fr[600][16];
 /* WELCOME with one peer, 3 flags (one allowed-list story flag 102, one not 9999, one 755), items: key 0x7102 (bombs) and legacy 5 */
 uint8_t w[200];int n=0;w[n++]=0x81;w[n++]=0;w[n++]=1;w[n++]=1;w[n++]=3;memcpy(w+n,"Bob",3);n+=3;w[n++]=1;w[n++]=2;w[n++]=3;
 uint32_t c=3,f1=102,f2=9999,f3=755;memcpy(w+n,&c,4);n+=4;memcpy(w+n,&f1,4);n+=4;memcpy(w+n,&f2,4);n+=4;memcpy(w+n,&f3,4);n+=4;
 uint16_t ic=2,k1=0x7102,k2=5;int16_t v1=1,v2=1;memcpy(w+n,&ic,2);n+=2;memcpy(w+n,&k1,2);n+=2;memcpy(w+n,&v1,2);n+=2;memcpy(w+n,&k2,2);n+=2;memcpy(w+n,&v2,2);n+=2;
 laonline_v12_message(w,n);
 /* local: owns bow (bit 3) and has flag 349 */
 ST32(SV(0x5568),1u<<3);flags_put(349,true);
 laonline_v12_tick(NULL,frame,"Field","Field_10A",me,1);
 assert(flags_ext_get(102)&&flags_ext_get(755)&&!flags_ext_get(9999%2048));
 assert(LD32(SV(0x5568))==((1u<<3)|(1u<<2))&&LD8(SV(0x555E))==10);
 int k=drain(fr,1);printf("frames after tick: %d\n",k);
 int saw349=0,sawbow=0,saw102=0;for(int i=0;i<k;i++){uint32_t v;memcpy(&v,fr[i]+3,4);if(fr[i][2]==3&&v==349)saw349=1;if(fr[i][2]==3&&v==102)saw102=1;uint16_t kk;memcpy(&kk,fr[i]+3,2);if(fr[i][2]==4&&kk==0x7103)sawbow=1;}
 assert(saw349&&sawbow&&!saw102);
 /* S_FLAG_SET later */
 uint8_t sf[6]={0x85,1};uint32_t f4=29;memcpy(sf+2,&f4,4);laonline_v12_message(sf,6);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);assert(flags_ext_get(29));
 /* not applied while paused, applied after */
 f4=30;memcpy(sf+2,&f4,4);laonline_v12_message(sf,6);paused=1;frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);assert(!flags_ext_get(30));paused=0;frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);assert(flags_ext_get(30));
 /* consumables */
 ST32(SV(0x5540),231);assert(laonline_v12_resource(105,1));assert(LD32(SV(0x5540))==281);
 ST32(SV(0x5540),231);/* game reverts */frame+=3;laonline_v12_tick(NULL,frame,"Field","x",me,1);assert(LD32(SV(0x5540))==281);
 frame+=3;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 ST8(SV(0x555E),25);assert(laonline_v12_resource(4,10));assert(LD8(SV(0x555E))==30);
 ST8(SV(0x555C),1u<<4);ST8(SV(0x555E),25);laonline_v12_resource(4,10);assert(LD8(SV(0x555E))==35);
 ST8(SV(0x555F),0);laonline_v12_resource(60,10);assert(LD8(SV(0x555F))==10);
 assert(laonline_v12_resource(11,5));/* no powder bag: nothing */assert(LD8(SV(0x5560))==0);
 assert(!laonline_v12_resource(100,1));
 mode=3;assert(!laonline_v12_resource(102,1));mode=1;
 /* Global: nothing applies, frames for co-op are dropped */
 mode=3;f4=31;memcpy(sf+2,&f4,4);laonline_v12_message(sf,6);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);assert(!flags_ext_get(31));
 /* PvP: relay supports it */
 uint8_t pong[5]={0x89};uint32_t r=LAO_V12_REPLY;memcpy(pong+1,&r,4);laonline_v12_message(pong,5);
 laonline_v12_key(6,1,0);laonline_v12_tick(NULL,frame,"Field","x",me,1);assert(laonline_v12_pvp_local());printf("note: %s\n",note);
 k=drain(fr,3);int state=0;for(int i=0;i<k;i++)if(fr[i][2]==0x1c&&fr[i][3]==0&&fr[i][4]==1)state=1;assert(state);
 uint8_t ps[4]={0xa2,0,1,1};laonline_v12_message(ps,4);assert(laonline_v12_pvp_peer(1));
 /* swing near peer */
 peer_pos[0]=1.0f;laonline_v12_input(1ull<<1,0);laonline_v12_tick(NULL,frame,"Field","x",me,1);k=drain(fr,3);int hit=0;for(int i=0;i<k;i++)if(fr[i][2]==0x1c&&fr[i][3]==1&&fr[i][4]==1)hit=1;assert(hit);
 laonline_v12_tick(NULL,frame,"Field","x",me,1);assert(drain(fr,3)==0);/* once per swing */
 laonline_v12_input(0,0);
 /* far peer: no hit */
 peer_pos[0]=5;laonline_v12_input(1ull<<1,0);laonline_v12_tick(NULL,frame,"Field","x",me,1);assert(drain(fr,3)==0);laonline_v12_input(0,0);
 /* we get hit: half heart, never below a quarter */
 ST16(SV(0x5544),12);uint8_t h[4]={0xa2,1,1,2};laonline_v12_message(h,4);laonline_v12_tick(NULL,frame,"Field","x",me,1);assert((int16_t)LD16(SV(0x5544))==10);
 laonline_v12_message(h,4);laonline_v12_tick(NULL,frame,"Field","x",me,1);assert((int16_t)LD16(SV(0x5544))==10);/* 700 ms invulnerable */
 clockms+=800;ST16(SV(0x5544),2);laonline_v12_message(h,4);laonline_v12_tick(NULL,frame,"Field","x",me,1);assert((int16_t)LD16(SV(0x5544))==1);
 /* R3 tap toggles off */
 laonline_v12_input(1ull<<5,0);clockms+=100;laonline_v12_input(0,0);laonline_v12_tick(NULL,frame,"Field","x",me,1);assert(!laonline_v12_pvp_local());
 /* dive assist: flippers + bay room + R3 held 2 s */
 mode=1;ST32(SV(0x556C),1);laonline_v12_input(1ull<<5,0);laonline_v12_tick(NULL,frame,"Field","Field_14J",me,1);clockms+=2100;laonline_v12_tick(NULL,frame,"Field","Field_14J",me,1);
 assert(!strcmp(warped,"CatfishsMawEnter/CatfishsMawEnter_01A"));
 assert(!laonline_v12_hide_puppet("Lv09WindFishsEgg"));/* V12: visible by default */
 assert(laonline_v12_ghost_level("Lv09WindFishsEgg")&&!laonline_v12_ghost_level("Field"));
 laonline_v12_global_variant(1);assert(!strcmp(laonline_v12_global_lobby(),"V10-GLOBAL-RANDO"));
 puts("PASS V12 module: welcome replay, allow-list, flags/items apply+publish, pause gate, rupee revert retry, ammo caps, Global isolation, PvP state/hit/rate/floor, R3 toggle, dive assist, final boss ghost (visible), Global lobby");
 return 0;}
