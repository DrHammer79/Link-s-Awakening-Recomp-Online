/* V13.5: Share the world (pots / bushes / pickups) in Co-op Randomizer (laonline_v12.c). Appended to the V12 test harness setup. */
#include <windows.h>
#include <assert.h>
#include <sys/stat.h>
#include "../runtime/hle/laonline_v12.c"
#include "v15_stubs.h"
int laonline_death_active(void){return 0;}  /* (laonline_death.c in the game) */
const GameAddrs *g_ga;static GameVersion version={.key="v1_0_0"};Settings g_settings;
static int mode=2,world=1;static uint8_t flags[8192];static char seed_dir[256];const char *rando_seed_dir(void){return seed_dir[0]?seed_dir:0;}static uint64_t frame=100;
int game_in_play(void){return 1;}int laonline_level_ready(void){return 1;}int laonline_launch_choice(void){return mode;}int laonline_coop_world_ready(void){return world;}
bool laonline_ui_paused(void){return 0;}void laonline_notify(const char *t){(void)t;}
bool flags_ext_get(int i){return (flags[i>>3]>>(i&7))&1;}void flags_put(int i,bool on){if(on)flags[i>>3]|=1u<<(i&7);else flags[i>>3]&=~(1u<<(i&7));}
void flags_ext_set(int i){flags_put(i,true);}
static const int souls[]={30001,30002,30003};int souls_flag_at(int i){return i>=0&&i<3?souls[i]:-1;}
void warp_request(const char *l,const char *loc){(void)l;(void)loc;}
uint64_t renderer_present_count(void){return frame;}uint64_t plat_now_ns(void){return frame*16000000ull;}
void input_hit_rumble(int ms){(void)ms;}
const GameVersion *game_version(void){return &version;}
void dev_events_emit(const char *a,const char *b,const char *c,int d,int e,int64_t f,int64_t g,const char *h,const char *i,uint64_t j){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;}
int laonline_v12_peer(int id,float pos[3],uint16_t *map,int *local){(void)id;(void)pos;(void)map;(void)local;return 0;}
static int drain(uint8_t frames[][32],int launch){int n=0;uint8_t f[32];int k;while((k=laonline_v12_packet(f,sizeof f,launch))>0&&n<600){memcpy(frames[n++],f,32);}return n;}
#define SV(o) (GA(LIVE_SAVE)+(o))
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220};g_ga=&ga;g_settings.laonline_sync_story=1;g_settings.laonline_sync_world=1;g_settings.laonline_sync_souls=1;ST32(SV(0),0xe);
 snprintf(seed_dir,sizeof seed_dir,"/tmp/lao_world_seed");mkdir(seed_dir,0755);
 FILE *f=fopen("/tmp/lao_world_seed/pots.tsv","w");fprintf(f,"ArmosShrine\t5.25\t1.5\t30.75\t1263\n");fclose(f);
 f=fopen("/tmp/lao_world_seed/bushes.tsv","w");fprintf(f,"Field\t24.75\t13.5\t27.75\t2176\t2177\t0.4\t1.0\tField_03B\t4788\n");fclose(f);
 f=fopen("/tmp/lao_world_seed/quick_pickups.tsv","w");fprintf(f,"TalTal\t391\t22\t1\t3006\tSE_SY_GET_ITEM\nWoods\t-1\t106\t1\t-1\tSE\n");fclose(f);
 f=fopen("/tmp/lao_world_seed/tracker.tsv","w");fprintf(f,"V\t2\nC\tbeach-chest\t5.5\t14.5\t-1\t28\tchest\tbeach\t-\t-\t18\tChest on the beach\tnorth-east\nC\tfield-chest\t1\t1\t-1\t27\tchest\tfield\t-\t-\t1\tChest\tx\nC\tanimal-village-cave\t13.5\t12.5\t-1\t1120\theart\tanimal-village\t-\t-\t3\tCave\tinside\nC\ttarin\t1\t1\t-1\t258\tother\tmabe\t-\t-\t1\tTarin\tx\nC\tpothole-final\t1\t1\t-1\t363\tother\tfield\t-\t-\t1\tPothole\tx\n");fclose(f);
 float me[3]={0};uint8_t fr[600][32];int pass=1;
 #define T(name,ok) do{int _o=(ok);printf("%-60s %s\n",name,_o?"PASS":"FAIL");pass&=_o;}while(0)
 /* local: Sanic cuts the key bush (item 2176 + cut 2177) -> both flags sent once */
 flags_put(2176,true);flags_put(2177,true);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 int k=drain(fr,2),a=0,b=0,other=0;for(int i=0;i<k;i++){uint32_t v;memcpy(&v,fr[i]+3,4);if(fr[i][2]==3&&v==2176)a++;else if(fr[i][2]==3&&v==2177)b++;else if(fr[i][2]==3&&v>=1000)other++;}
 T("own key bush (item + cut flag) sent to the room",a==1&&b==1);
 frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);k=drain(fr,2);int again=0;for(int i=0;i<k;i++){uint32_t v;memcpy(&v,fr[i]+3,4);if(fr[i][2]==3&&(v==2176||v==2177))again++;}
 T("not sent again",again==0);
 /* remote: Velvet opened the pot (1263) and took the quick pickup (3006); a non-listed flag (1500) is ignored */
 uint32_t rf[8]={1263,391,1500,3006,28,1120,258,363};for(int i=0;i<8;i++){uint8_t m[6]={0x85,1};memcpy(m+2,&rf[i],4);laonline_v12_message(m,6);}
 frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("teammate's pot is used here too",flags_ext_get(1263));
 T("teammate's freestanding item (quick pickup flag 391) is used here too",flags_ext_get(391));
 T("V15.1: the item's index column (3006) is NOT treated as a flag",!flags_ext_get(3006));
 T("V15.1: teammate's chest (flag 28) shows opened here too",flags_ext_get(28));
 T("V15.1: teammate's Piece of Heart (flag 1120) is gone here too",flags_ext_get(1120));
 T("V15.1: loose item (pothole-final, 363) is gone here too",flags_ext_get(363));
 T("V15.1: NPC gift (Tarin, 258) is not touched",!flags_ext_get(258));
 T("flag not in the seed lists is never written",!flags_ext_get(1500));
 /* setting off: nothing applied */
 g_settings.laonline_sync_world=0;uint32_t r2=2176+0;flags_put(2176,false);uint8_t m2[6]={0x85,1};memcpy(m2+2,&r2,4);laonline_v12_message(m2,6);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("\"Share the world\" off: not applied",!flags_ext_get(2176));
 /* Story Co-op: not shared */
 g_settings.laonline_sync_world=1;mode=1;uint32_t r3=1263;flags_put(1263,false);uint8_t m3[6]={0x85,1};memcpy(m3+2,&r3,4);laonline_v12_message(m3,6);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("Story Co-op (no seed lists in play): not applied",!flags_ext_get(1263));
 /* Story Co-op: a vanilla dungeon key a teammate took (KeyGet_L02_7D = 787) is gone here too */
 uint32_t r4=787;uint8_t m4[6]={0x85,1};memcpy(m4+2,&r4,4);laonline_v12_message(m4,6);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);
 T("Story Co-op: teammate's vanilla Small Key spot (KeyGet) is used here too",flags_ext_get(787));
 flags_put(27,true);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);k=drain(fr,1);int cs=0;for(int i=0;i<k;i++){uint32_t v;memcpy(&v,fr[i]+3,4);if(fr[i][2]==3&&v==27)cs++;}
 T("Story Co-op: chests are not shared (keys only)",cs==0);
 mode=2;frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);k=drain(fr,2);cs=0;for(int i=0;i<k;i++){uint32_t v;memcpy(&v,fr[i]+3,4);if(fr[i][2]==3&&v==27)cs++;}
 T("V15.1: own chest opened -> sent to the team (Co-op Randomizer)",cs==1);
 mode=1;flags_put(820,true);frame+=25;laonline_v12_tick(NULL,frame,"Field","x",me,1);k=drain(fr,1);int ks=0;for(int i=0;i<k;i++){uint32_t v;memcpy(&v,fr[i]+3,4);if(fr[i][2]==3&&v==820)ks++;}
 T("own vanilla Small Key pickup (KeyGet) sent in Story Co-op",ks==1);
 return pass?0:1;
}
