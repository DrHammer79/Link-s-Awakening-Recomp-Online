// LA-Online: read-only snapshot for visual map markers.
// No actor pointers, gameplay mutation, or game assets in this interface.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct LaoMapOverlayPlayer {
    char name[17], location[33];
    uint16_t map, room;
    uint8_t rgb[3], local;
    uint8_t cap_rgb[3],hair_rgb[3],avatar_known;
    float world[3];
    float raw[3],yaw;
    uint8_t position_valid,interior,approximate,fresh,id;
} LaoMapOverlayPlayer;

// Returns number of lobby players (including self), or -1 if not connected.
// Copies index into out when valid; caller never receives mutable session state.
int laonline_map_player(int i, LaoMapOverlayPlayer *out);

#ifdef __cplusplus
}
#endif
