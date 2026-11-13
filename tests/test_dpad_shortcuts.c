#include <windows.h>
#include <assert.h>
#include "../runtime/hle/dpad_shortcuts.c"
/* Native game pause mock. A paused menu must receive original directions. */
static int native_items_cursor=-1,native_map_pause=0;
int inventory_items_cursor(void){return native_items_cursor;}
bool dpad_shortcuts_map_pause_active(void){return native_map_pause!=0;}
static int dead,load_ready=1;int laonline_death_active(void){return dead;}int laonline_level_ready(void){return load_ready;}
const GameAddrs *g_ga;static GameVersion version={.key="v1_0_0"};Settings g_settings;
static int paused,captured,allowed=1,native_result=1,calls;static uint64_t frame=10,buttons;static HostFn registered[16];static int nregistered;
const GameVersion *game_version(void){return &version;}int game_in_play(void){return allowed;}bool laonline_ui_paused(void){return paused;}uint64_t renderer_present_count(void){return frame;}
static void native(Cpu *c){calls++;assert(LD8(LD64(GA(PLAYER_STATUS_PTR))+0xa3)==(atomic_load(&active_slot)?atomic_load(&active_slot):3));c->x[0]=native_result;}
GuestFn dispatch_lookup(uint64_t at){(void)at;return native;}
uint64_t hle_register(const char *name,HostFn fn){(void)name;registered[nregistered]=fn;return MAIN_BASE+0x2100000+8*nregistered++;}
static uint64_t poll(uint64_t b,int capture){buttons=b;return dpad_shortcuts_input(b,capture);}
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220,.PLAYER_STATUS_PTR=MAIN_BASE+0x1000};g_ga=&ga;ST64(GA(PLAYER_STATUS_PTR),GA(LIVE_SAVE)+0x54c0);ST8(GA(LIVE_SAVE)+0x5563,3);
 for(unsigned i=0;i<12;i++){ST32(MAIN_BASE+hooks[i].fn,hooks[i].word);ST32(MAIN_BASE+hooks[i].fn+hooks[i].cmp_off,hooks[i].cmp_word);ST64(MAIN_BASE+hooks[i].cell,MAIN_BASE+hooks[i].fn);}
 g_settings.dpad_shortcuts=true;g_settings.dpad_up=9;g_settings.dpad_down=2;g_settings.dpad_left=4;g_settings.dpad_right=3;ST32(GA(LIVE_SAVE)+0x5568,(1<<9)|(1<<2)|(1<<4)|(1<<3));
 dpad_shortcuts_install();assert(dpad_shortcuts_available()&&nregistered==12);
 int slots[]={9,2,4,3};for(int d=0;d<4;d++){poll(0,0);uint64_t output=poll(1ull<<directions[d],0);if(slots[d]==2||slots[d]==3){assert(output==0);frame+=3;output=poll(1ull<<directions[d],0);} /* V12: Bow/Bombs wait 3 frames for a pair */assert(output==(1ull<<2)&&atomic_load(&active_slot)==slots[d]);Cpu c={0};native_result=1;registered[d](&c);assert(c.x[0]==1&&LD8(GA(LIVE_SAVE)+0x5563)==3);native_result=0;registered[d](&c);assert(c.x[0]==0&&LD8(GA(LIVE_SAVE)+0x5563)==3);}
 poll(0,0);assert(poll((1ull<<13)|(1ull<<2),0)&(1ull<<2));assert(!atomic_load(&active_slot));poll(0,0);assert(poll(1ull<<3,0)==(1ull<<3));
 poll(0,0);ST32(GA(LIVE_SAVE)+0x5568,0);assert(!(poll(1ull<<13,0)&(1ull<<2)));assert(!atomic_load(&active_slot));
 ST32(GA(LIVE_SAVE)+0x5568,1<<9);paused=1;assert(poll(1ull<<13,0)==(1ull<<13));paused=0;assert(!(poll(1ull<<13,0)&(1ull<<2)));poll(0,0);assert(poll(1ull<<13,0)==(1ull<<2));
 poll(0,1);assert(poll(1ull<<13,1)==(1ull<<13));assert(!(poll(1ull<<13,0)&(1ull<<2)));poll(0,0);
 g_settings.dpad_move=true;assert(poll(1ull<<13,0)==(1ull<<13));g_settings.dpad_move=false;poll(0,0);assert(!(poll((1ull<<13)|(1ull<<14),0)&(1ull<<2)));
 g_settings.dpad_shortcuts=false;poll(0,0);assert(poll(1ull<<13,0)==(1ull<<13));
 g_settings.dpad_shortcuts=true;g_settings.dpad_move=false;
 assert(poll((1ull<<10)|(1ull<<13),0)==((1ull<<10)|(1ull<<13)));
 frame+=10;assert(poll(1ull<<13,0)==(1ull<<13));
 frame+=11;poll(0,0);assert(poll(1ull<<13,0)==(1ull<<2));
 native_items_cursor=0;g_settings.dpad_shortcuts=true;g_settings.dpad_move=false;poll(0,0);
 assert(poll(1ull<<13,0)==(1ull<<13) && !atomic_load(&active_slot));
 native_items_cursor=-1;
 assert(!(poll(1ull<<13,0)&(1ull<<2))); /* still held: never fires after pause */
 poll(0,0); /* release is required before any shortcut */
 native_map_pause=1;
 assert(poll(1ull<<13,0)==(1ull<<13) && !atomic_load(&active_slot));
 native_map_pause=0;
 assert(!(poll(1ull<<13,0)&(1ull<<2)));
 poll(0,0);assert(poll(1ull<<13,0)==(1ull<<2));
 ST32(GA(LIVE_SAVE)+0x5568,(1<<9)|(1<<2)|(1<<4)|(1<<3));
 for(int menu=0;menu<2;menu++)for(int d=0;d<4;d++){uint64_t key=1ull<<directions[d];native_items_cursor=menu? -1:0;native_map_pause=menu;poll(0,0);assert(poll(key,0)==key&&!atomic_load(&active_slot));native_items_cursor=-1;native_map_pause=0;assert(!(poll(key,0)&(1ull<<2)));poll(0,0);uint64_t fired=poll(key,0);if(!fired){frame+=3;fired=poll(key,0);}assert(fired==(1ull<<2));}
 dead=1;poll(0,0);assert(poll(1ull<<13,0)==(1ull<<13));dead=0;assert(!(poll(1ull<<13,0)&(1ull<<2)));poll(0,0);
 load_ready=0;assert(poll(1ull<<13,0)==(1ull<<13));load_ready=1;assert(!(poll(1ull<<13,0)&(1ull<<2)));poll(0,0);
 dpad_shortcuts_pointer_capture(1);assert(poll(1ull<<13,0)==(1ull<<13)&&!atomic_load(&active_slot));dpad_shortcuts_pointer_capture(0);assert(!(poll(1ull<<13,0)&(1ull<<2)));poll(0,0);assert(poll(1ull<<13,0)==(1ull<<2));
 /* V12 Bomb Arrows: Down = Bombs (2), Right = Bow (3); X holds the Bow (3), Y nothing (0). */
 {uint64_t ps=LD64(GA(PLAYER_STATUS_PTR));uint64_t down=1ull<<directions[1],right=1ull<<directions[3];
  ST8(ps+0xa3,3);ST8(ps+0xa2,0);poll(0,0);frame+=5;
  assert(poll(down|right,0)==((1ull<<2)|(1ull<<3)));assert(LD8(ps+0xa3)==3&&LD8(ps+0xa2)==2&&!atomic_load(&active_slot));
  poll(0,0);assert(LD8(ps+0xa3)==3&&LD8(ps+0xa2)==0);
  /* one direction first, the second a frame later: still a pair, no lone shortcut in between */
  frame+=5;assert(poll(down,0)==0);frame++;assert(poll(down|right,0)==((1ull<<2)|(1ull<<3)));assert(LD8(ps+0xa2)==2);poll(0,0);assert(LD8(ps+0xa2)==0);
  /* Bombs on the real Y, Bow on the D-pad */
  ST8(ps+0xa2,2);ST8(ps+0xa3,9);frame+=5;poll(0,0);
  assert(poll(right|(1ull<<3),0)==((1ull<<2)|(1ull<<3)));assert(LD8(ps+0xa3)==3&&LD8(ps+0xa2)==2);
  poll(1ull<<3,0);assert(LD8(ps+0xa3)==9&&LD8(ps+0xa2)==2);poll(0,0);
  /* a menu restores the player's own buttons at once */
  ST8(ps+0xa2,0);ST8(ps+0xa3,3);frame+=5;poll(0,0);assert(poll(down|right,0)==((1ull<<2)|(1ull<<3)));paused=1;poll(down|right,0);paused=0;assert(LD8(ps+0xa3)==3&&LD8(ps+0xa2)==0);poll(0,0);
  /* without the Bow owned it is not a pair */
  ST32(GA(LIVE_SAVE)+0x5568,(1<<9)|(1<<2)|(1<<4));frame+=5;poll(0,0);assert(!(poll(down|right,0)&(1ull<<3)));poll(0,0);}
 puts("PASS V12 Bomb Arrows: D-pad pair, late second direction, real Y + D-pad, menu restore, ownership");
 puts("PASS D-pad policy: four native slots, scoped X restoration, native acceptance/rejection, normal X/Y, ownership, menu release, movement conflict, diagonals and disabled default");return 0;
}
