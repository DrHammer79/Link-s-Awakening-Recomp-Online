/* Optional local shortcuts. Native predicates retain action, ammunition and cooldown logic.
 * Only game 1.0.0's fingerprinted item predicate vtables are supported. No generated-code change.
 * The native X equip byte is substituted only during a synchronous native predicate call,
 * then restored before the action runs. Persistent X/Y assignments are never replaced. */
#include "dpad_shortcuts.h"
#include "../hle.h"
#include "../settings.h"
#include "../game_addresses.h"
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif
extern int game_in_play(void);
extern bool laonline_ui_paused(void);
extern int laonline_death_active(void),laonline_level_ready(void);
/* Verified native Items/Map page signals. Never consume directions in the game's
 * own pause screens; the game retains its original D-pad navigation. */
extern int inventory_items_cursor(void);
extern bool dpad_shortcuts_map_pause_active(void);
static int native_pause_active(void){
 return inventory_items_cursor() >= 0 || dpad_shortcuts_map_pause_active();
}
extern uint64_t renderer_present_count(void);
static _Atomic uint32_t inventory;
static _Atomic int visible,active_slot,installed,pointer_busy,cancel_pending;
void dpad_shortcuts_pointer_capture(int on){atomic_store(&pointer_busy,on!=0);if(on){atomic_store(&cancel_pending,1);atomic_store(&active_slot,0);}}
static uint64_t active_frame;
static uint64_t latched;
static int release_required;
static uint64_t pause_button_guard_until; /* Plus/Minus: cover the native menu opening animation. */
static const unsigned directions[]={13,15,12,14}; /* Up Down Left Right: existing logical Switch bits. */
uint32_t dpad_shortcuts_inventory(int *v){*v=atomic_load(&visible);return atomic_load(&inventory);}
int dpad_shortcuts_available(void){return atomic_load(&installed);}
/* V13.5 (Randomizer: songs and the Ocarina arrive in any order, as items): the song the Ocarina plays is the one
 * picked in the item menu (save +0x5561, 1-based). A song given as a randomizer item does not pick itself, so the
 * Ocarina said "you don't know any songs yet" until the player chose one in the menu. Once a second: when the Ocarina
 * and at least one song are owned but no known song is selected, the first known song is selected. */
void dpad_ocarina_song_tick(uint64_t frame){
 if(frame%60||!g_ga||!game_version()||strcmp(game_version()->key,"v1_0_0")||!game_in_play())return;
 uint64_t save=GA(LIVE_SAVE);if(LD32(save)!=0xe)return;
 unsigned songs=LD8(save+0x555B)&7;if(!songs||!(LD32(save+0x5568)&(1u<<12)))return;
 uint8_t sel=LD8(save+0x5561);if(sel>=1&&sel<=3&&((songs>>(sel-1))&1))return;
 for(int s=0;s<3;s++)if((songs>>s)&1){ST8(save+0x5561,(uint8_t)(s+1));LOG("[controls] V13.5 Ocarina: no known song was selected, picked song %d\n",s+1);break;}
}
unsigned dpad_shortcuts_songs(void){return (g_ga&&game_version()&&!strcmp(game_version()->key,"v1_0_0")&&game_in_play())?LD8(GA(LIVE_SAVE)+0x555B)&7:0;}
/* V12 Bomb Arrows. The game fires Bomb Arrows when the buttons holding the Bow and the Bombs are pressed
 * together. A D-pad shortcut alone only lends its item to X inside the native predicates, so a pair where one or both
 * items live on the D-pad never reached the game as "X and Y together". While such a pair is held, the native X/Y
 * equip bytes (PlayerStatus +0xa3 X / +0xa2 Y, the bytes the predicates read) hold Bow/Bombs and X+Y are pressed;
 * the player's own X/Y assignments are put back the moment the pair is released, a menu opens or the game resets.
 * Pairs: Up/Down/Left/Right + another direction, or a direction + the real X or Y button. Slots: 2 Bombs, 3 Bow. */
static int combo_on;static uint8_t combo_saved_x,combo_saved_y,combo_x,combo_y;static uint64_t combo_wait_until,combo_wait_d;
static uint64_t combo_status(void){uint64_t ps=g_ga?LD64(GA(PLAYER_STATUS_PTR)):0;
 return ((ps>=MAIN_BASE&&ps+0x100<SDK_BASE)||(ps>=HEAP_BASE&&ps+0x100<HEAP_BASE+HEAP_MAX))?ps:0;}
static void combo_restore(void){
 if(!combo_on)return;combo_on=0;uint64_t ps=combo_status();if(!ps)return;
 if(LD8(ps+0xa3)==combo_x)ST8(ps+0xa3,combo_saved_x);
 if(LD8(ps+0xa2)==combo_y)ST8(ps+0xa2,combo_saved_y);
 LOG("[controls] V12 Bomb Arrow pair released; X/Y assignments restored\n");
}
static int combo_pair(int a,int b,uint32_t own){return ((a==2&&b==3)||(a==3&&b==2))&&(own&4u)&&(own&8u);}
static int combo_input(uint64_t buttons,uint64_t d,uint32_t own,uint64_t now,uint64_t *out){
 uint64_t ps=combo_status();if(!ps||!g_settings.dpad_shortcuts){combo_restore();return 0;}
 int values[]={g_settings.dpad_up,g_settings.dpad_down,g_settings.dpad_left,g_settings.dpad_right};
 int s[4],n=0;for(unsigned i=0;i<4;i++)if(d&(1ull<<directions[i]))s[n++]=values[i];
 uint8_t ex=combo_on?combo_saved_x:LD8(ps+0xa3),ey=combo_on?combo_saved_y:LD8(ps+0xa2);
 int x_held=(buttons>>2)&1,y_held=(buttons>>3)&1,cx=-1,cy=-1;
 if(n==2&&combo_pair(s[0],s[1],own)){cx=3;cy=2;}
 else if(n==1&&y_held&&!x_held&&combo_pair(s[0],ey,own)){cx=s[0];cy=ey;}
 else if(n==1&&x_held&&!y_held&&combo_pair(s[0],ex,own)){cx=ex;cy=s[0];}
 if(cx<0){
  combo_restore();
  /* Both items on the D-pad: give the second direction 3 frames to arrive before a lone shortcut fires. */
  int both=0;for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++)if(i!=j&&combo_pair(values[i],values[j],own))both=1;
  if(both&&n==1&&!x_held&&!y_held&&(s[0]==2||s[0]==3)){
   if(combo_wait_d!=d){combo_wait_d=d;combo_wait_until=now+3;}
   if(now<combo_wait_until){*out=buttons&~(15ull<<12);return 1;}
  } else if(!n)combo_wait_d=0;
  return 0;
 }
 if(!combo_on){combo_saved_x=LD8(ps+0xa3);combo_saved_y=LD8(ps+0xa2);combo_on=1;LOG("[controls] V12 Bomb Arrow pair pressed: X=%d Y=%d (own X=%d Y=%d)\n",cx,cy,combo_saved_x,combo_saved_y);}
 combo_x=(uint8_t)cx;combo_y=(uint8_t)cy;ST8(ps+0xa3,combo_x);ST8(ps+0xa2,combo_y);
 atomic_store(&active_slot,0);latched=d;release_required=1;combo_wait_d=0;
 *out=(buttons&~(15ull<<12))|(1ull<<2)|(1ull<<3);return 1;
}
uint64_t dpad_shortcuts_input(uint64_t buttons,int captured){
 atomic_store(&active_slot,0);atomic_store(&visible,0);
 if(atomic_exchange(&cancel_pending,0))release_required=1;
 uint64_t d=buttons&(15ull<<12);
 /* Native pause/map controls always win. Block item shortcuts during the
  * opening animation before a native page object becomes visible. */
 uint64_t now=renderer_present_count();
 if(buttons & ((1ull<<10)|(1ull<<11))) pause_button_guard_until=now+20;
 if(g_ga&&game_version()&&!strcmp(game_version()->key,"v1_0_0")&&game_in_play())atomic_store(&inventory,LD32(GA(LIVE_SAVE)+0x5568));else atomic_store(&inventory,0);
 /* Native pause menus get the ORIGINAL button bits; never synthesize X or
  * activate extra-item predicates while a menu owns the D-pad. */
 int allowed=g_settings.dpad_shortcuts&&!g_settings.dpad_move&&!captured&&!atomic_load(&pointer_busy)&&g_ga&&game_version()&&!strcmp(game_version()->key,"v1_0_0")&&game_in_play()&&!laonline_ui_paused()&&!laonline_death_active()&&laonline_level_ready()&&!native_pause_active()&&now>=pause_button_guard_until;
 if(!allowed){combo_restore();release_required=1;latched=0;return buttons;}
 uint32_t own=LD32(GA(LIVE_SAVE)+0x5568);atomic_store(&inventory,own);atomic_store(&visible,1);
 if(!atomic_load(&installed)){combo_restore();return buttons;}
 /* V12: Bomb Arrows with the Bow and/or Bombs on the D-pad (see combo_input). */
 {uint64_t combo;if(combo_input(buttons,d,own,now,&combo))return combo;}
 /* Menu-close, simultaneous directions and normal X/Y/Plus/Minus require release before a shortcut. */
 if(!d){release_required=0;latched=0;return buttons;}
 if(release_required||(buttons&((1ull<<2)|(3ull<<10)))||(d&(d-1))||(latched&&latched!=d)){release_required=1;return buttons&~(15ull<<12);}  /* V10.1: Y held is allowed (X+Y combos, e.g. Bomb Arrows) */
 int values[]={g_settings.dpad_up,g_settings.dpad_down,g_settings.dpad_left,g_settings.dpad_right};
 int slot=0;for(unsigned i=0;i<4;i++)if(d==(1ull<<directions[i]))slot=values[i];
 unsigned songs=LD8(GA(LIVE_SAVE)+0x555B)&7;
 latched=d;if(!dpad_slot_supported(slot)||!dpad_slot_owned(slot,own,songs))return buttons&~(15ull<<12);
 /* V13.5 Ocarina from the D-pad ("you don't know any songs yet"): the game plays the song picked in the item menu,
  * which is never picked when the Ocarina only sits on the D-pad. A song shortcut selects its song; the plain Ocarina
  * keeps the current choice, or picks the first song you know when none is selected / the selected one isn't known. */
 if(dpad_slot_native(slot)==12&&songs){uint8_t *sel=(uint8_t*)(uintptr_t)(GA(LIVE_SAVE)+0x5561);int want=dpad_slot_song(slot);
  if(want<0&&(*sel<1||*sel>3||!((songs>>(*sel-1))&1)))for(int s=0;s<3;s++)if((songs>>s)&1){want=s;break;}
  if(want>=0&&*sel!=(uint8_t)(want+1))*sel=(uint8_t)(want+1);}
 slot=dpad_slot_native(slot);
 atomic_store(&active_slot,slot);active_frame=renderer_present_count();return (buttons&~(15ull<<12))|(1ull<<2);
}
#ifdef _WIN32
struct Hook{uint32_t cell,fn,word,cmp_off,cmp_word;GuestFn original;uint64_t stub;};
static struct Hook hooks[]={
 {0x1c18f70,0xd52880,0xa9be4ff4,0x84,0x7100093f}, /* Bombs */
 {0x1c18fb8,0xd52050,0xa9be4ff4,0x80,0x71000d1f}, /* Bow */
 {0x1c19000,0xd51f30,0xf81e0ff3,0x7c,0x7100111f}, /* Hookshot */
 {0x1c19048,0xd51d90,0xa9be4ff4,0x80,0x7100151f}, /* Boomerang */
 {0x1c19090,0xd51520,0xa9be4ff4,0x80,0x7100191f}, /* Magic Rod */
 {0x1c18f28,0xd52ac0,0xf81e0ff3,0x7c,0x71001d1f}, /* Shovel */
 {0x1c18e98,0xd52d30,0xf81e0ff3,0x7c,0x7100213f}, /* Powder */
 {0x1c18e08,0xd52f00,0xf81e0ff3,0x7c,0x7100251f}, /* Feather */
 {0x1c18ee0,0xd52be0,0xa9be4ff4,0x80,0x7100313f}, /* Ocarina */
 /* Bottle predicates are also scoped so a shortcut cannot activate the player's original bottle on X. */
 {0x1c191f8,0xd510c0,0xf81e0ff3,0x7c,0x7100353f},
 {0x1c19240,0xd50f90,0xf81e0ff3,0x7c,0x7100393f},
 {0x1c19288,0xd50e60,0xf81e0ff3,0x7c,0x71003d3f},
};
static void predicate(Cpu *c,unsigned i){
 int slot=atomic_load(&active_slot);uint64_t ps=g_ga?LD64(GA(PLAYER_STATUS_PTR)):0;
 int valid=(ps>=MAIN_BASE&&ps+0x100<SDK_BASE)||(ps>=HEAP_BASE&&ps+0x100<HEAP_BASE+HEAP_MAX);
 if(!slot){hooks[i].original(c);return;}
 if(atomic_load(&pointer_busy)||atomic_load(&cancel_pending)||!valid||renderer_present_count()-active_frame>1||!g_settings.dpad_shortcuts){c->x[0]=0;return;}
 uint8_t previous=LD8(ps+0xa3);ST8(ps+0xa3,slot);hooks[i].original(c);ST8(ps+0xa3,previous);
}
#define WRAPPER(n) static void predicate_##n(Cpu *c){predicate(c,n);}
WRAPPER(0) WRAPPER(1) WRAPPER(2) WRAPPER(3) WRAPPER(4) WRAPPER(5) WRAPPER(6) WRAPPER(7) WRAPPER(8) WRAPPER(9) WRAPPER(10) WRAPPER(11)
static HostFn wrappers[]={predicate_0,predicate_1,predicate_2,predicate_3,predicate_4,predicate_5,predicate_6,predicate_7,predicate_8,predicate_9,predicate_10,predicate_11};
void dpad_shortcuts_install(void){
 if(atomic_load(&installed)||!g_settings.dpad_shortcuts||!g_ga||!game_version()||strcmp(game_version()->key,"v1_0_0"))return;
 /* Validate the complete set before changing any vtable. Unknown native code fails closed. */
 for(unsigned i=0;i<12;i++){struct Hook *h=&hooks[i];uint64_t fn=MAIN_BASE+h->fn;
  if(LD32(fn)!=h->word||LD32(fn+h->cmp_off)!=h->cmp_word|| (LD64(MAIN_BASE+h->cell)!=fn&&LD64(MAIN_BASE+h->cell)!=h->stub))return;
  h->original=dispatch_lookup(fn);if(!h->original)return;
 }
 for(unsigned i=0;i<12;i++){struct Hook *h=&hooks[i];char name[64];snprintf(name,sizeof name,"lao_dpad_predicate_%u",i);if(!h->stub)h->stub=hle_register(name,wrappers[i]);DWORD old,ignored;uint64_t at=MAIN_BASE+h->cell;
  if(!h->stub||!VirtualProtect((void*)(uintptr_t)at,8,PAGE_READWRITE,&old))return;
  InterlockedExchange64((volatile LONG64*)(uintptr_t)at,h->stub);VirtualProtect((void*)(uintptr_t)at,8,old,&ignored);
 }
 atomic_store(&installed,1);LOG("[controls] D-pad native item predicates installed; X/Y preferences retained; native pause guard active\n");
}
#else
void dpad_shortcuts_install(void){}
#endif
RT_ON_RESET(dpad_shortcuts_reset){combo_on=0;combo_wait_d=0;atomic_store(&pointer_busy,0);atomic_store(&cancel_pending,0);atomic_store(&installed,0);atomic_store(&active_slot,0);atomic_store(&visible,0);atomic_store(&inventory,0);active_frame=latched=pause_button_guard_until=0;release_required=1;
#ifdef _WIN32
 for(unsigned i=0;i<12;i++){hooks[i].original=NULL;hooks[i].stub=0;}
#endif
}
