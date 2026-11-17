#pragma once
/* Protocol v1 session state. No engine pointers, game data or save writes.
 * Keep byte-identical to switch/source/coop/laonline_session.h. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "laonline_appearance.h"

#define LAO_PLAYERS 100
#define LAO_UNKNOWN 0xffffu
#define LAO_STATE_AGE_MS 1500u
#pragma pack(push, 1)
typedef struct {
    uint16_t map, room;
    float x, y, z, rot_y;
    uint16_t anim_id;
    float anim_frame;
    uint8_t flags;
} LaoWireState;
#pragma pack(pop)
typedef struct {
    int joined, samples;
    char name[17];
    uint8_t rgb[3];
    LaoAppearance appearance;
    uint8_t health, max_health;  /* quarter-hearts, delivered separately from movement */
    char location[33];           /* friendly area from the sender's real level/room */
    LaoWireState state;
    uint64_t received_ms, revision;
} LaoPeer;
typedef struct {
    int connected, local_id;
    uint64_t revision;
    LaoPeer peers[LAO_PLAYERS];
} LaoSession;

static inline void lao_session_reset(LaoSession *s) {
    uint64_t next = s->revision + 1;
    memset(s, 0, sizeof *s);
    s->local_id = -1;
    s->revision = next;
}
static inline uint16_t lao_name_id(const char *name) {
    if (!name || !*name) return LAO_UNKNOWN;
    uint32_t h = 0x811c9dc5u;
    for (; *name; ++name) { h ^= (uint8_t)*name; h *= 0x01000193u; }
    uint16_t n = (uint16_t)(h ^ (h >> 16));
    return n == LAO_UNKNOWN ? 0xfffeu : n;
}
/* Switch FromNames hashes the suffix, not Level_Suffix or a path/.leb. */
static inline uint16_t lao_room_id(const char *level, const char *resource) {
    if (!level || !*level || !resource || !*resource) return LAO_UNKNOWN;
    const char *base = resource;
    for (const char *p = resource; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    size_t ll = strlen(level), n = strlen(base);
    if (n > ll + 1 && !memcmp(base, level, ll) && base[ll] == '_') base += ll + 1;
    n = strlen(base);
    if (n >= 4 && !strcmp(base + n - 4, ".leb")) n -= 4;
    if (!n || n >= 65) return LAO_UNKNOWN;
    char suffix[65]; memcpy(suffix, base, n); suffix[n] = 0;
    return lao_name_id(suffix);
}
static inline int lao_valid_state(const LaoWireState *w) {
    return isfinite(w->x) && isfinite(w->y) && isfinite(w->z) &&
           isfinite(w->rot_y) && isfinite(w->anim_frame) &&
           fabsf(w->x) < 100000.f && fabsf(w->y) < 100000.f && fabsf(w->z) < 100000.f &&
           w->anim_frame >= 0.f;
}
static inline int lao_peer_ready(const LaoSession *s, int id, uint16_t map,
                                 uint16_t room, uint64_t now) {
    if (!s->connected || id < 0 || id >= LAO_PLAYERS || id == s->local_id ||
        map == LAO_UNKNOWN) return 0;
    (void)room;  /* V6: the same level is enough; rooms may differ */
    const LaoPeer *p = &s->peers[id];
    return p->joined && p->samples >= 2 && now >= p->received_ms &&
           now - p->received_ms <= LAO_STATE_AGE_MS && lao_valid_state(&p->state) &&
           p->state.map == map;
}
static inline int lao_read_profile(const uint8_t *b, size_t n, size_t *at, LaoPeer *p) {
    if (*at >= n) return 0;
    size_t len = b[(*at)++];
    if (!len || len > 16 || n - *at < len + 3) return 0;
    memcpy(p->name, b + *at, len); p->name[len] = 0; *at += len;
    memcpy(p->rgb, b + *at, 3); *at += 3;
    p->joined = 1;
    return 1;
}
/* b includes message type, excludes u16 frame length. Malformed frames are
 * rejected transactionally. The server's progress snapshot remains ignored. */
static inline int lao_session_message(LaoSession *s, const uint8_t *b, size_t n, uint64_t now) {
    if (!b || !n) return 0;
    if (b[0] == 0x81) {
        if (n < 3 || b[1] >= LAO_PLAYERS || b[2] >= LAO_PLAYERS) return 0;
        LaoSession next = *s;
        lao_session_reset(&next);
        next.local_id = b[1]; next.connected = 1;
        size_t at = 3;
        for (unsigned k = 0; k < b[2]; ++k) {
            if (at >= n) return 0;
            unsigned id = b[at++];
            if (id >= LAO_PLAYERS || id == (unsigned)next.local_id || next.peers[id].joined ||
                !lao_read_profile(b, n, &at, &next.peers[id])) return 0;
        }
        /* WELCOME ends in u32 flag count + u32 flags, u16 item count +
         * (u16 item, s16 amount). Validate lengths without applying progress. */
        if (n - at < 4) return 0;
        size_t count = (size_t)b[at] | ((size_t)b[at + 1] << 8) |
                       ((size_t)b[at + 2] << 16) | ((size_t)b[at + 3] << 24); at += 4;
        if (count > (n - at) / 4) return 0;
        at += count * 4;
        if (n - at < 2) return 0;
        count = b[at] | ((size_t)b[at + 1] << 8); at += 2;
        if (count > (n - at) / 4 || at + count * 4 != n) return 0;
        *s = next;
        return 1;
    }
    if (!s->connected) return 0;
    if (b[0] == 0x82) {
        if (n < 2 || b[1] >= LAO_PLAYERS || b[1] == s->local_id) return 0;
        LaoPeer p = {0}; size_t at = 2;
        if (!lao_read_profile(b, n, &at, &p) || at != n) return 0;
        /* Duplicate JOIN does not replace a live actor's participant. */
        if (s->peers[b[1]].joined) {
            memcpy(s->peers[b[1]].name, p.name, sizeof p.name);
            memcpy(s->peers[b[1]].rgb, p.rgb, sizeof p.rgb);
        } else s->peers[b[1]] = p;
        ++s->revision;
        return 1;
    }
    if (b[0] == 0x83) {
        if (n != 2 || b[1] >= LAO_PLAYERS || b[1] == s->local_id) return 0;
        memset(&s->peers[b[1]], 0, sizeof s->peers[b[1]]);
        ++s->revision;
        return 1;
    }
    if (b[0] == 0x84) {
        if (n != 2 + sizeof(LaoWireState) || b[1] >= LAO_PLAYERS || b[1] == s->local_id) return 0;
        LaoPeer *p = &s->peers[b[1]];
        LaoWireState w; memcpy(&w, b + 2, sizeof w);
        if (!p->joined || !lao_valid_state(&w)) return 0;
        if (!p->samples || w.map != p->state.map ||  /* V9: a room change on the same map keeps him */
            now < p->received_ms || now - p->received_ms > LAO_STATE_AGE_MS) p->samples = 1;
        else if (p->samples < 2) ++p->samples;
        p->state = w; p->received_ms = now; p->revision = ++s->revision;
        return 1;
    }
    if (b[0] == LAO_APPEARANCE_REPLY) {
        if (n != 2 + sizeof(LaoAppearance) || b[1] >= LAO_PLAYERS || b[1] == s->local_id ||
            !s->peers[b[1]].joined) return 0;
        LaoAppearance a; memcpy(&a, b + 2, sizeof a);
        if (!lao_appearance_valid(&a)) return 0;
        s->peers[b[1]].appearance = a; ++s->revision; return 1;
    }
    if (b[0] == 0x8a) {
        if (n != 5 || b[1] >= LAO_PLAYERS || !s->peers[b[1]].joined) return 0;
        memcpy(s->peers[b[1]].rgb, b + 2, 3); return 1;
    }
    if (b[0] == 0x8b) {
        if (n != 4 || b[1] >= LAO_PLAYERS || b[1] == s->local_id || !s->peers[b[1]].joined ||
            !b[3] || b[2] > b[3]) return 0;
        s->peers[b[1]].health = b[2];
        s->peers[b[1]].max_health = b[3];
        return 1;
    }
    if (b[0] == 0x8c) {
        if (n < 3 || b[1] >= LAO_PLAYERS || b[1] == s->local_id || !s->peers[b[1]].joined) return 0;
        size_t len = b[2];
        if (!len || len > 32 || n != 3 + len) return 0;
        memcpy(s->peers[b[1]].location, b + 3, len);
        s->peers[b[1]].location[len] = 0;
        return 1;
    }
    return b[0] == 0x89; /* PONG. No progression writes. */
}
