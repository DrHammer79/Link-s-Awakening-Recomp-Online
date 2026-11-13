#pragma once
#include <stdint.h>
// Artwork is read only from the user's own Game.arc; no game images are distributed.
struct LaoMapArt {uint64_t texture;float left,top,right,bottom;};
bool laonline_map_art(LaoMapArt *out);

// Native room collision data, loaded from the user's level files; no artwork bundled.
bool laonline_dungeon_art(const char *level,float floor_x,float floor_y,LaoMapArt *out);
bool laonline_dungeon_position(const char *level,float floor_x,float floor_y,const char *room,float x,float z,float *map_x,float *map_z);
