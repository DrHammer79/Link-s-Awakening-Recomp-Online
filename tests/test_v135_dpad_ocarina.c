/* V13.5: Ocarina on the D-pad picks a song ("you don't know any songs yet"), and song shortcuts. */
#include <windows.h>
#include <assert.h>
#include "../runtime/hle/dpad_shortcuts.c"
int inventory_items_cursor(void){return -1;}
bool dpad_shortcuts_map_pause_active(void){return 0;}
int laonline_death_active(void){return 0;}int laonline_level_ready(void){return 1;}
const GameAddrs *g_ga;static GameVersion version={.key="v1_0_0"};Settings g_settings;static uint64_t frame=10;
const GameVersion *game_version(void){return &version;}int game_in_play(void){return 1;}bool laonline_ui_paused(void){return 0;}uint64_t renderer_present_count(void){return frame;}
#define SEL (GA(LIVE_SAVE)+0x5561)
static int press(int dir_bit){frame++;dpad_shortcuts_input(0,0);frame++;dpad_shortcuts_input(1ull<<dir_bit,0);int s=atomic_load(&active_slot);frame++;dpad_shortcuts_input(0,0);return s;}
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220,.PLAYER_STATUS_PTR=MAIN_BASE+0x1000};g_ga=&ga;ST64(GA(PLAYER_STATUS_PTR),GA(LIVE_SAVE)+0x54c0);
 atomic_store(&installed,1);g_settings.dpad_shortcuts=true;
 g_settings.dpad_up=12;g_settings.dpad_down=21;g_settings.dpad_left=22;g_settings.dpad_right=20;  /* Up: Ocarina, Down: Mambo, Left: Soul, Right: Ballad */
 ST32(GA(LIVE_SAVE)+0x5568,1u<<12);ST8(GA(LIVE_SAVE)+0x555B,2|4);ST8(SEL,0);  /* Ocarina, Mambo + Soul known, nothing selected */
 int pass=1;
 #define T(n,c) do{int _c=(c);printf("%-62s %s\n",n,_c?"PASS":"FAIL");pass&=_c;}while(0)
 int s=press(13);T("D-pad Ocarina with no song picked: plays the first known (Mambo)",s==12&&LD8(SEL)==2);
 s=press(12);T("D-pad Left = Song of Soul: selects Soul, uses the Ocarina",s==12&&LD8(SEL)==3);
 s=press(13);T("plain Ocarina keeps the last song (Soul)",s==12&&LD8(SEL)==3);
 s=press(14);T("Ballad not learned yet: that shortcut does nothing",s==0||LD8(SEL)==3);
 ST8(GA(LIVE_SAVE)+0x555B,0);ST8(SEL,0);s=press(13);T("no songs at all: Ocarina used as the game would (no song forced)",LD8(SEL)==0);
 T("song shortcuts show the Ocarina icon / are D-pad slots",dpad_slot_item(21)==16&&dpad_slot_supported(22)&&!dpad_slot_supported(23));
 return pass?0:1;
}
