// Map-cell decoder for hash-based Field room identifiers from protocol v1.
// No guessed player world coordinates: if a hash is unknown or ambiguous,
// no pin is shown, only the player's area label.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "laonline_session.h"

static inline int lao_field_cell(uint16_t map, uint16_t room, int *col, int *row) {
    if (map != lao_name_id("Field") || room == LAO_UNKNOWN) return 0;
    int matches = 0, cx = -1, ry = -1;
    for (int r = 0; r < 16; ++r) for (int c = 0; c < 16; ++c) {
        char resource[16];
        snprintf(resource, sizeof resource, "Field_%02d%c", r, 'A' + c);
        if (lao_room_id("Field", resource) == room) { ++matches; cx = c; ry = r; }
    }
    if (matches != 1) return 0;
    if (col) *col = cx;
    if (row) *row = ry;
    return 1;
}
