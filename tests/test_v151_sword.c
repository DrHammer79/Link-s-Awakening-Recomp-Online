/* V15.1: random Noble Sword (Mr. Cursey) - the team level is capped by this seed's sword spots actually used. */
#include <windows.h>
#include <assert.h>
#include <sys/stat.h>
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
const GameVersion *game_version(void){return &version;}static char seed_dir[256];const char *rando_seed_dir(void){return seed_dir[0]?seed_dir:0;}
void dev_events_emit(const char *a,const char *b,const char *c,int d,int e,int64_t f,int64_t g,const char *h,const char *i,uint64_t j){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;}
int laonline_v12_peer(int id,float pos[3],uint16_t *map,int *local){(void)id;(void)pos;(void)map;(void)local;return 0;}
static int drain(uint8_t frames[][32],int launch){int n=0;uint8_t f[32];int k;while((k=laonline_v12_packet(f,sizeof f,launch))>0&&n<600){memcpy(frames[n++],f,32);}return n;}
#define SV(o) (GA(LIVE_SAVE)+(o))
#define SV(o) (GA(LIVE_SAVE)+(o))
#define SW() (!(LD32(SV(0x5568))&1u)?0:(LD8(SV(0x555C))&1)?2:1)
static void brloc(int fam,uint16_t map,uint16_t room){uint8_t s[6]={0x86,1};uint16_t key=K_BRLOC+fam*K_BRLOC_N+bracelet_place(map,room);int16_t v=1;memcpy(s+2,&key,2);memcpy(s+4,&v,2);laonline_v12_message(s,6);}
static void wflag(uint32_t f){uint8_t m[6]={0x85,1};memcpy(m+2,&f,4);laonline_v12_message(m,6);}
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220};g_ga=&ga;g_settings.laonline_sync_story=1;g_settings.laonline_sync_upgrades=1;g_settings.laonline_sync_world=1;g_settings.laonline_shared_items=1;ST32(SV(0),0xe);
 snprintf(seed_dir,sizeof seed_dir,"/tmp/lao_sword_seed");mkdir(seed_dir,0755);
 FILE *f=fopen("/tmp/lao_sword_seed/tracker.tsv","w");fprintf(f,"V\t2\nC\tD2-chest\t1\t1\t-1\t787\tchest\tbottle-grotto\t-\t-\t1\tChest\tx\nC\tbeach-chest\t1\t1\t-1\t28\tchest\tbeach\t-\t-\t1\tChest\tx\nC\ttarin\t1\t1\t-1\t258\tother\tmabe\t-\t-\t1\tTarin\tx\n");fclose(f);
 f=fopen("/tmp/lao_sword_seed/spoiler.txt","w");fprintf(f,"settings:\n    OHKO:  False\nbottle-grotto:\n    D2-chest:  sword\nbeach:\n    beach-chest:  sword\nmabe:\n    tarin:  shield\n");fclose(f);
 float me[3]={0};int pass=1;mode=2;
 #define T(name,ok) do{int _o=(ok);printf("%-66s %s\n",name,_o?"PASS":"FAIL");pass&=_o;}while(0)
 frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 /* kit takes the D2 sword: Lv1 for him, place A; his chest flag is set */
 ST32(SV(0x5568),1u);flags_put(787,true);laonline_v12_progressive_found(0,46117,13607);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("one sword found: Sword Lv1",SW()==1);
 /* the same sword counted again at another place (stale room / same chest in another world) */
 brloc(2,46117,1);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("same sword reported at a 2nd place: NO Noble Sword (Cursey's bug)",SW()==1);
 /* unknown place is never recorded */
 laonline_v12_progressive_found(0,0xffff,0xffff);int any=0;for(int i=0;i<K_BRLOC_N/8;i++)any+=__builtin_popcount(bracelet_local[2][i]);
 T("pickup while the place is unknown (level loading): not counted",any==1);
 /* a teammate really takes the beach sword: its chest flag arrives (Share the world) + the place */
 wflag(28);brloc(2,123,456);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("second sword really found by the team: Noble Sword",SW()==2);
 /* shield is at an NPC (not a shared spot): old place rule still decides */
 ST32(SV(0x5568),LD32(SV(0x5568))|2u);laonline_v12_progressive_found(2,3,4);brloc(1,9,9);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("item at an NPC (no shared spot): two places -> Lv2 as before",(LD8(SV(0x555C))&2)!=0);
 return pass?0:1;
}
