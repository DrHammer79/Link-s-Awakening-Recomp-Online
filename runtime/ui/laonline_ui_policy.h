#pragma once
#include <stdbool.h>
#include <math.h>
static inline bool lao_world_plate_visible(bool online, bool username, bool settings,
                                           bool pause, bool map, bool play) {
    return online && username && !settings && !pause && !map && play;
}
/* Native 1.0.0 map conversion from main+e910a8..e91160. Live-checked against
 * L_MapPlayer_00: (-387.4367,-89.8256) floors to the game's (-388,-90). */
static inline bool lao_map_native_point(const float world[3], const float plane[3],
                                         const float matrix[12], float *x, float *y) {
    if (!isfinite(world[0]) || !isfinite(world[1]) || !isfinite(world[2]) ||
        !(plane[2]>0 && plane[2]<10000)) return false;
    float px=floorf(plane[0]+plane[2]/240.0f*world[0]);
    float py=floorf(plane[1]+((world[2]-8.25f)*0.79f+(32.25f-world[1])*1.1f)*-2.7f);
    *x=matrix[0]*px+matrix[1]*py+matrix[3];
    *y=matrix[4]*px+matrix[5]*py+matrix[7];
    return isfinite(*x) && isfinite(*y);
}
