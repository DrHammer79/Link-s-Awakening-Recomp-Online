#pragma once
#include "laonline_places.h"
/* Resolve exact transmitted IDs using verified runtime warp metadata. Ambiguous
 * or unmapped cells get an honest broad area, never a neighbouring cell's name. */
static inline void lao_location_ids(uint16_t map, uint16_t room, char out[33]) {
    const char *level = NULL, *area = NULL, *level_area = NULL;
    int mixed = 0, mixed_level = 0;
    for (unsigned i = 0; i < sizeof lao_places / sizeof lao_places[0]; ++i) {
        if (lao_name_id(lao_places[i].level) != map) continue;
        if (level && strcmp(level, lao_places[i].level)) { snprintf(out,33,"In game"); return; }
        level = lao_places[i].level;
        if (level_area && strcmp(level_area, lao_places[i].area)) mixed_level = 1;
        level_area = lao_places[i].area;
        if (lao_room_id(level, lao_places[i].room) != room) continue;
        if (area && strcmp(area, lao_places[i].area)) mixed = 1;
        area = lao_places[i].area;
    }
    const char *label = area && !mixed ? area : level && !strcmp(level,"Field") ? "Overworld" :
                        level_area && !mixed_level ? level_area : map == LAO_UNKNOWN ? "Loading" : "Interior";
    snprintf(out,33,"%s",label);
}
