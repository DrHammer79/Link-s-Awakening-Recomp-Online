// Teleport to a teammate (LAS Recomp Co-Op, online play in a co-op session): a button opens a picker of the lobby's
// other players, and Player 1 is warped to the one picked - the game's own level jump to an arrival point of that
// player's level, then put where that player stands (runtime/hle/warp.c warp_request_place). This PC's other players
// follow him as co-op does. runtime/ui/laonline_teleport.mm (the picker, the rules, the warp), runtime/hle/input.c (its
// button), runtime/hle/laonline.c (the teammates).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union SDL_Event SDL_Event;

// The teleport button (g_settings.laonline_teleport_key). Automatic: D-pad Up, or Minus + D-pad Up when the D-pad
// moves Link or uses items (Controls > D-pad).
enum { LAO_TP_KEY_AUTO, LAO_TP_KEY_UP, LAO_TP_KEY_DOWN, LAO_TP_KEY_LEFT, LAO_TP_KEY_RIGHT, LAO_TP_KEY_MINUS_UP,
       LAO_TP_KEY_MINUS_DOWN, LAO_TP_KEY_OFF, LAO_TP_KEY_COUNT };

typedef struct {
    int id;              // the relay's id
    char name[17];
    uint8_t rgb[3];
    uint16_t map, room;  // lao_name_id / lao_room_id of where they are
    float pos[3];
    uint8_t hp, max_hp;  // quarter hearts (max 0: not known)
    bool down;           // knocked out: their hearts at 0
    char location[33];   // the area, as their game names it
} LaoTeammate;

// runtime/hle/laonline.c: this lobby's other players with a position (not this PC's own), up to cap; how many
int laonline_teammates(LaoTeammate *out, int cap);

// runtime/ui/laonline_teleport.mm
bool laonline_teleport_available(void);              // in play, online in a co-op session: the button is teleport's
void laonline_teleport_chord(int *mod, int *key);    // its controller buttons now (SDL_GamepadButton; -1: none)
bool laonline_teleport_open(void);                   // the picker is up: Player 1's controls are its
void laonline_teleport_toggle(void);                 // the button (input.c) or F5: open / close it
bool laonline_teleport_event(const SDL_Event *e);    // runtime/ui/menu.mm, each event: true = the picker took it

#ifdef __cplusplus
}
#endif
