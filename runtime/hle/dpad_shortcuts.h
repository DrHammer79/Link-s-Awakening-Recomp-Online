#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Slots are native inventory bit/equipment identities, not AddItem ids. */
/* V13.5: 20 / 21 / 22 = the Ocarina with a chosen song (Ballad of the Wind Fish / Manbo's Mambo / Frog's Song of
 * Soul). The game plays the song selected in the save (+0x5561, 1-based; 0 = none - "you don't know any songs"). */
static inline int dpad_slot_supported(int slot){return (slot>=2&&slot<=9)||slot==12||(slot>=20&&slot<=22);}
static inline int dpad_slot_native(int slot){return slot>=20&&slot<=22?12:slot;}
static inline int dpad_slot_song(int slot){return slot>=20&&slot<=22?slot-20:-1;}
static inline int dpad_slot_item(int slot){static const int ids[]={-1,-1,4,5,6,7,8,9,11,12,-1,-1,16};if(slot>=20&&slot<=22)return 16;  /* (song shortcuts show the Ocarina) */return slot>=0&&slot<13?ids[slot]:-1;}
static inline int dpad_slot_owned(int slot,uint32_t items,unsigned songs){int s=dpad_slot_song(slot);return (items&(1u<<dpad_slot_native(slot)))&&(s<0||((songs>>s)&1));}
uint64_t dpad_shortcuts_input(uint64_t buttons,int captured);
void dpad_shortcuts_install(void);
void dpad_shortcuts_pointer_capture(int active);
uint32_t dpad_shortcuts_inventory(int *visible);
unsigned dpad_shortcuts_songs(void);  /* V13.5: songs known (bit 0 Ballad, 1 Mambo, 2 Soul) */
int dpad_shortcuts_available(void);
#ifdef __cplusplus
}
#endif
