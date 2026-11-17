// V15 Z-targeting (ZL; also L3 / F8) and the learned damage animation for PvP hits. Works offline and online.
//
// Targets: PvP players (both have PvP on) and the actors around Link that MOVE (enemies, bosses - also villagers,
// as in other Zelda games; pots, grass and signs never move, so they are never picked). Actors are seen through the
// per-frame actor check (tools/patches.py MB_SEEN_SITE -> miniboss.c rt_mb_seen -> ztarget_actor_seen).
// Locked: Link turns to face the target every frame (the puppets' own rotation setter, laonline.c), PvP attacks aim
// at it, and the stick circles the target (left / right go around it at the same distance, up / down go to / away).
//
// Nothing here is guessed silently - everything the game's layout decides is checked against Link himself first:
//  - an actor's position: its transform (actor +0x80) holds a position somewhere; the offset is the one at which
//    LINK's transform holds Link's known position (PlayerCollision +0x80) - found once, logged, else no actor targets;
//  - which way a yaw turns Link: learned while he walks (his facing vs the way he moves; 4 conventions compared);
//  - which way the stick moves him: learned while he walks (stick vs movement).
// Until each is learned the dependent part waits (players can still be targeted; Link just isn't turned yet).
//
// Damage animation (PvP hit reaction): when an ENEMY hurts Link, the animation he plays within 0.3 s is noted; the id
// seen in two separate hits is kept in laonline-damage-anim.txt and played on Link when a player hits him in PvP.
//
// (LAS Recomp Co-Op's port) Each of this PC's players has his own Z-target: Player 1 (ZL, L3, F8) and local co-op
// players 2..8 (their own ZL: ztarget_input_of) - his own lock, his Link turned to it, his stick circling it, his PvP
// attacks aimed at it, his own reticle. What is learned (the position in a transform, the yaw convention, the stick
// directions) is the same for every Link and the one camera: learned from Player 1 as before. This PC's own Links are
// never targets (they move, so they were taken for enemies).
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../game_addresses.h"
#include "../hle.h"
#include "../platform.h"

uint64_t doorjump_player(uint64_t frame);
extern int game_in_play(void);
extern int laonline_v12_peer(int id, float pos[3], uint16_t *map, int *local_id);
extern int laonline_v12_pvp_local(void);
extern int laonline_v12_pvp_peer(int id);
extern uint64_t laonline_local_link(void);
extern int laonline_local_yaw(float *yaw);
extern int laonline_link_yaw(uint64_t link, float *yaw);
extern int laonline_link_set_yaw(Cpu *c, uint64_t link, float yaw);
extern int laonline_v12_pvp_on(int self);
extern uint64_t coop_player(int i);
extern uint32_t coop_joined(void);
extern uint16_t laonline_local_anim(uint64_t *at_ms);
extern bool menu_input_captured(void);

#define NPLAYERS 8
#define NSELF 8  /* this PC's players: 0 Player 1, 1..7 local co-op players 2..8 */
static uint64_t now_ms(void) { return plat_now_ns() / 1000000ull; }
static int heap_ptr(uint64_t p) { return p >= HEAP_BASE && p < HEAP_BASE + 0x200000000ull; }

/* ---------------------------------------------------------------- actors seen this frame (game thread) */
typedef struct { uint64_t o, vt, seen; float p[3], p0[3]; float moved; } Act;
#define NACT 256
static Act g_act[NACT];
static int g_pos_off = -1;  /* position inside an actor's transform (found from Link) */
static int read_pos(uint64_t o, float p[3]) {
    if (g_pos_off < 0) return 0;
    uint64_t tr = LD64(o + 0x80);
    if (!heap_ptr(tr)) return 0;
    const float *f = (const float *)(uintptr_t)(tr + (uint64_t)g_pos_off);
    if (!isfinite(f[0]) || !isfinite(f[1]) || !isfinite(f[2]) || fabsf(f[0]) > 1e5f || fabsf(f[2]) > 1e5f) return 0;
    p[0] = f[0], p[1] = f[1], p[2] = f[2];
    return 1;
}
void ztarget_actor_seen(uint64_t o) {
    if (!g_ga || !heap_ptr(o) || g_pos_off < 0) return;
    uint64_t vt = LD64(o);
    if (vt == GA(SHADOW_LINK_VTABLE) || o == laonline_local_link()) return;  /* (players: by their own positions) */
    for (int i = 1; i < NSELF; i++) if ((coop_joined() >> i & 1) && o == coop_player(i)) return;  /* (this PC's other Links) */
    float p[3];
    if (!read_pos(o, p)) return;
    uint64_t t = now_ms();
    unsigned h = (unsigned)((o >> 4) * 2654435761u) % NACT, free_i = NACT;
    for (unsigned k = 0; k < 16; k++) {
        Act *a = &g_act[(h + k) % NACT];
        if (a->o == o && a->vt == vt) {
            float dx = p[0] - a->p0[0], dz = p[2] - a->p0[2], d = sqrtf(dx * dx + dz * dz);
            if (d > a->moved) a->moved = d;
            memcpy(a->p, p, 12); a->seen = t;
            return;
        }
        if (free_i == NACT && (!a->o || t - a->seen > 1500)) free_i = (h + k) % NACT;
    }
    if (free_i == NACT) return;
    Act *a = &g_act[free_i];
    a->o = o, a->vt = vt, a->seen = t, a->moved = 0;
    memcpy(a->p, p, 12); memcpy(a->p0, p, 12);
}

/* ---------------------------------------------------------------- shared state (game / input / UI threads) */
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
typedef struct {                /* one of this PC's players' Z-target (the lock: mx) */
    atomic_int toggle;
    int locked;                 /* 0 none, 1 player, 2 actor */
    int lock_player;
    uint64_t lock_obj;
    float lock_pos[3], me[3], keep_r;
    int have_me;
    float stick[2];             /* last stick sent to the game (-1..1) */
    uint64_t prev;              /* (his ZL) */
    float facing_x, facing_z, last_x, last_z;
} ZSelf;
static ZSelf g_z[NSELF];
static int g_yaw_conv = -1;     /* 0..3: yaw = atan2(sx*dx, sz*dz) family, learned */
static float g_conv_err[4]; static int g_conv_n;
static float g_ax = 1, g_az = -1; static int g_axes_known; static float g_sum_x, g_sum_z; static int g_axes_n;

void ztarget_toggle(void) { atomic_store(&g_z[0].toggle, 1); }
static uint64_t z_input(int s, uint64_t buttons, int menu) {  /* ZL press toggles (the game has nothing on ZL) */
    uint64_t pressed = buttons & ~g_z[s].prev;
    g_z[s].prev = buttons;
    if (!menu && (pressed & (1ull << 8))) atomic_store(&g_z[s].toggle, 1);
    return buttons;
}
uint64_t ztarget_input(uint64_t buttons, int menu) { return z_input(0, buttons, menu); }  /* input.c: Player 1 */
uint64_t ztarget_input_of(int s, uint64_t buttons, int menu) {  /* input.c coop_fill: players 2..8 (s = 1..7) */
    return s >= 1 && s < NSELF ? z_input(s, buttons, menu) : buttons;
}

static float yaw_for(int conv, float dx, float dz) {
    switch (conv) {
    case 0: return atan2f(dx, dz);
    case 1: return atan2f(-dx, dz);
    case 2: return atan2f(dx, -dz);
    default: return atan2f(-dx, -dz);
    }
}
static float ang_diff(float a, float b) { float d = fmodf(a - b + 9.42477796f, 6.28318531f) - 3.14159265f; return fabsf(d); }
int ztarget_yaw_toward(float dx, float dz, float *yaw) {  /* for the PvP hit reaction (face the attacker) */
    pthread_mutex_lock(&mx); int conv = g_yaw_conv; pthread_mutex_unlock(&mx);
    if (conv < 0 || dx * dx + dz * dz < 1e-6f) return 0;
    *yaw = yaw_for(conv, dx, dz);
    return 1;
}
int ztarget_get_of(int s, float pos[3], int *player) {  /* PvP aim + overlay reticle: player s's lock */
    if (s < 0 || s >= NSELF) return 0;
    pthread_mutex_lock(&mx);
    ZSelf *z = &g_z[s];
    int on = z->locked;
    if (on) { memcpy(pos, z->lock_pos, 12); if (player) *player = z->locked == 1 ? z->lock_player : -1; }
    pthread_mutex_unlock(&mx);
    return on;
}
int ztarget_get(float pos[3], int *player) { return ztarget_get_of(0, pos, player); }  /* (Player 1's) */

/* stick (input thread): while locked, sideways goes AROUND the target at the same distance */
static void z_stick(int s, int32_t *lx, int32_t *ly) {
    float sx = *lx / 32767.f, sy = *ly / 32767.f;
    pthread_mutex_lock(&mx);
    ZSelf *z = &g_z[s];
    z->stick[0] = sx, z->stick[1] = sy;
    int on = z->locked && z->have_me && g_axes_known;
    float tx = z->lock_pos[0] - z->me[0], tz = z->lock_pos[2] - z->me[2], keep = z->keep_r, ax = g_ax, az = g_az;
    pthread_mutex_unlock(&mx);
    float mag = sqrtf(sx * sx + sy * sy), r = sqrtf(tx * tx + tz * tz);
    if (!on || mag < .2f || r < .3f) return;
    float wx = sx * ax, wz = sy * az;               /* stick -> world direction */
    float Rx = tx / r, Rz = tz / r, Tx = -Rz, Tz = Rx;  /* to the target, around it */
    float rad = wx * Rx + wz * Rz, tan_ = wx * Tx + wz * Tz;
    if (fabsf(tan_) <= fabsf(rad)) { pthread_mutex_lock(&mx); z->keep_r = r; pthread_mutex_unlock(&mx); return; }
    float pull = (r - keep) * .8f; if (pull > .5f) pull = .5f; if (pull < -.5f) pull = -.5f;
    float ox = (tan_ > 0 ? 1.f : -1.f) * Tx + pull * Rx, oz = (tan_ > 0 ? 1.f : -1.f) * Tz + pull * Rz;
    float on_ = sqrtf(ox * ox + oz * oz); if (on_ < 1e-4f) return;
    ox = ox / on_ * mag, oz = oz / on_ * mag;
    float nx = ox / ax, ny = oz / az;
    if (nx > 1) nx = 1; if (nx < -1) nx = -1; if (ny > 1) ny = 1; if (ny < -1) ny = -1;
    *lx = (int32_t)lrintf(nx * 32767.f), *ly = (int32_t)lrintf(ny * 32767.f);
}
void ztarget_stick(int32_t *lx, int32_t *ly) { z_stick(0, lx, ly); }  /* input.c: Player 1 */
void ztarget_stick_of(int s, int32_t *lx, int32_t *ly) { if (s >= 1 && s < NSELF) z_stick(s, lx, ly); }  /* players 2..8 */

/* ---------------------------------------------------------------- learned damage animation */
static uint16_t g_dmg_ids[8]; static int g_dmg_cnt[8], g_dmg_n; static uint16_t g_dmg_anim; static int g_dmg_loaded;
static atomic_ullong g_pvp_hit_at;
void ztarget_note_pvp_hit(void) { atomic_store(&g_pvp_hit_at, now_ms()); }  /* (laonline_v12.c: not an enemy hit) */
static void dmg_load(void) {
    g_dmg_loaded = 1;
    FILE *f = fopen("laonline-damage-anim.txt", "r");
    if (!f) return;
    unsigned id = 0; if (fscanf(f, "%u", &id) == 1 && id && id < 0x10000) g_dmg_anim = (uint16_t)id;
    fclose(f);
    if (g_dmg_anim) LOG("[ztarget] damage animation %u (learned earlier)\n", g_dmg_anim);
}
uint16_t ztarget_damage_anim(void) { if (!g_dmg_loaded) dmg_load(); return g_dmg_anim; }
static void dmg_tick(void) {
    if (!g_dmg_loaded) dmg_load();
    static int last_hp = -1; static uint64_t hurt_at; static uint16_t anim_before; static uint64_t anim_at_before;
    uint64_t ps = LD64(GA(PLAYER_STATUS_PTR));
    if (!ps) { last_hp = -1; return; }
    int hp = (int16_t)LD16(ps + 0x84);
    uint64_t t = now_ms();
    if (last_hp > 0 && hp < last_hp && hp >= 0 && t - atomic_load(&g_pvp_hit_at) > 1000) {
        hurt_at = t; anim_before = laonline_local_anim(&anim_at_before);
    }
    last_hp = hp;
    if (hurt_at && t - hurt_at < 300) {
        uint64_t at; uint16_t id = laonline_local_anim(&at);
        if (at != anim_at_before && at + 50 >= hurt_at && id && id != anim_before) {
            hurt_at = 0;
            int k = 0; while (k < g_dmg_n && g_dmg_ids[k] != id) k++;
            if (k == g_dmg_n && g_dmg_n < 8) g_dmg_ids[g_dmg_n] = id, g_dmg_cnt[g_dmg_n++] = 0;
            if (k < 8) {
                g_dmg_cnt[k]++;
                LOG("[ztarget] enemy hit -> Link played animation %u (%d time%s)\n", id, g_dmg_cnt[k], g_dmg_cnt[k] > 1 ? "s" : "");
                if (g_dmg_cnt[k] >= 2 && g_dmg_anim != id) {
                    g_dmg_anim = id;
                    FILE *f = fopen("laonline-damage-anim.txt", "w");
                    if (f) { fprintf(f, "%u\n", id); fclose(f); }
                    LOG("[ztarget] damage animation learned: %u (PvP hits play it now)\n", id);
                }
            }
        }
    } else hurt_at = 0;
}

/* ---------------------------------------------------------------- per frame (game thread, warp.c) */
static void calibrate_pos(const float me[3]) {
    static int tries;
    if (g_pos_off >= 0 || tries > 600) return;
    tries++;
    uint64_t link = laonline_local_link(), tr = link ? LD64(link + 0x80) : 0;
    if (!heap_ptr(tr)) return;
    for (int k = 0; k <= 0x1f4; k += 4) {
        const float *f = (const float *)(uintptr_t)(tr + (uint64_t)k);
        if (fabsf(f[0] - me[0]) < .05f && fabsf(f[1] - me[1]) < .3f && fabsf(f[2] - me[2]) < .05f &&
            (fabsf(me[0]) > .5f || fabsf(me[2]) > .5f)) {
            g_pos_off = k;
            LOG("[ztarget] actor position found at transform +%#x (from Link)\n", k);
            return;
        }
    }
    if (tries == 600) LOG("[ztarget] actor position not found in Link's transform: only players can be targeted\n");
}
static void learn(const float me[3], uint64_t t) {
    static float last[3]; static uint64_t last_t;
    if (!last_t) { memcpy(last, me, 12); last_t = t; return; }
    if (t - last_t < 100) return;
    float dx = me[0] - last[0], dz = me[2] - last[2], d = sqrtf(dx * dx + dz * dz), dt = (float)(t - last_t) / 1000.f;
    memcpy(last, me, 12); last_t = t;
    if (d / dt < 1.5f || d > 3.f) return;  /* walking or running, not a warp */
    pthread_mutex_lock(&mx);
    int locked = g_z[0].locked; float sx = g_z[0].stick[0], sy = g_z[0].stick[1];  /* (learned from Player 1) */
    pthread_mutex_unlock(&mx);
    if (locked) return;  /* (we turn him then) */
    float yaw;
    if (g_yaw_conv < 0 && laonline_local_yaw(&yaw)) {
        for (int c = 0; c < 4; c++) g_conv_err[c] += ang_diff(yaw, yaw_for(c, dx, dz));
        if (++g_conv_n >= 40) {
            int best = 0; for (int c = 1; c < 4; c++) if (g_conv_err[c] < g_conv_err[best]) best = c;
            float avg = g_conv_err[best] / g_conv_n;
            if (avg < .35f) { pthread_mutex_lock(&mx); g_yaw_conv = best; pthread_mutex_unlock(&mx); LOG("[ztarget] Link's facing learned (convention %d, %.2f rad)\n", best, avg); }
            else { LOG("[ztarget] facing not clear yet (%.2f rad) - learning again\n", avg); }
            memset(g_conv_err, 0, sizeof g_conv_err); g_conv_n = 0;
        }
    }
    if (!g_axes_known && (fabsf(sx) > .5f || fabsf(sy) > .5f)) {
        g_sum_x += sx * dx, g_sum_z += sy * dz;
        if (++g_axes_n >= 30 && fabsf(g_sum_x) > .5f && fabsf(g_sum_z) > .5f) {
            pthread_mutex_lock(&mx);
            g_ax = g_sum_x > 0 ? 1.f : -1.f, g_az = g_sum_z > 0 ? 1.f : -1.f, g_axes_known = 1;
            pthread_mutex_unlock(&mx);
            LOG("[ztarget] stick directions learned (x %+.0f, z %+.0f)\n", g_ax, g_az);
        }
    }
}
/* this PC's player s: his Link and where he is (Player 1: as V15 found him; the others: their Link's collision) */
static int self_pos(int s, uint64_t frame, uint64_t *link, float me[3]) {
    uint64_t pc;
    if (!s) pc = doorjump_player(frame), *link = laonline_local_link();
    else {
        if (!(coop_joined() >> s & 1)) return 0;
        *link = coop_player(s);
        pc = *link ? LD64(*link + 0x80) : 0;
        if (!heap_ptr(pc) || LD64(pc) != GA(PLAYER_COLLISION_VT)) return 0;
    }
    if (!pc) return 0;
    const float *mp = (const float *)(uintptr_t)(pc + 0x80);
    me[0] = mp[0], me[1] = mp[1], me[2] = mp[2];
    return 1;
}
static void z_tick(Cpu *c, int s, uint64_t frame, uint64_t t) {
    ZSelf *z = &g_z[s];
    uint64_t link = 0; float me[3];
    if (!self_pos(s, frame, &link, me)) {
        pthread_mutex_lock(&mx); z->have_me = 0; if (s) z->locked = 0; pthread_mutex_unlock(&mx);
        if (s) atomic_store(&z->toggle, 0);
        return;
    }
    if (!s) { calibrate_pos(me); learn(me, t); }
    { float dx = me[0] - z->last_x, dz = me[2] - z->last_z, d = sqrtf(dx * dx + dz * dz);
      if (d > .03f && d < 3.f) z->facing_x = dx / d, z->facing_z = dz / d; z->last_x = me[0], z->last_z = me[2]; }
    int want = atomic_exchange(&z->toggle, 0);
    pthread_mutex_lock(&mx);
    int locked = z->locked, was = z->locked, pl = z->lock_player; uint64_t obj = z->lock_obj; float tp[3]; memcpy(tp, z->lock_pos, 12);
    pthread_mutex_unlock(&mx);
    /* keep the current target, or let go */
    if (locked == 1) {
        float p[3]; uint16_t map; int local;
        if (!laonline_v12_pvp_on(s) || !laonline_v12_pvp_peer(pl) || !laonline_v12_peer(pl, p, &map, &local)) locked = 0;
        else memcpy(tp, p, 12);
    } else if (locked == 2) {
        int found = 0;
        for (int i = 0; i < NACT; i++) if (g_act[i].o == obj && t - g_act[i].seen < 500) { memcpy(tp, g_act[i].p, 12); found = 1; break; }
        if (!found) locked = 0;
    }
    if (locked) { float dx = tp[0] - me[0], dz = tp[2] - me[2]; if (dx * dx + dz * dz > 16.f * 16.f || fabsf(tp[1] - me[1]) > 6.f) locked = 0; }
    if (want && was) { locked = 0; LOG("[ztarget] off (player %d)\n", s + 1); }
    else if (want) {  /* pick: the nearest in reach, in front of Link counts double */
        float best = 1e9f; int bpl = -1; uint64_t bobj = 0; float bp[3] = {0};
        float fx = z->facing_x, fz = z->facing_z, yaw;
        if (g_yaw_conv >= 0 && laonline_link_yaw(link, &yaw)) {  /* his real facing: the inverse of the learned convention */
            for (int k = 0; k < 64; k++) { float a = k * 0.0981748f, x = sinf(a), zz = cosf(a); if (ang_diff(yaw_for(g_yaw_conv, x, zz), yaw) < .06f) { fx = x, fz = zz; break; } }
        }
        if (laonline_v12_pvp_on(s))
            for (int id = 0; id < NPLAYERS; id++) {
                float p[3]; uint16_t map; int local;
                if (!laonline_v12_pvp_peer(id) || !laonline_v12_peer(id, p, &map, &local) || id == local) continue;
                float dx = p[0] - me[0], dz = p[2] - me[2], d = sqrtf(dx * dx + dz * dz);
                if (d > 12.f || fabsf(p[1] - me[1]) > 3.f) continue;
                float sc = d * ((d > .1f && (dx * fx + dz * fz) / d > .3f) ? 1.f : 2.f) * .8f;  /* (players first) */
                if (sc < best) best = sc, bpl = id, bobj = 0, memcpy(bp, p, 12);
            }
        for (int i = 0; i < NACT; i++) {
            Act *a = &g_act[i];
            if (!a->o || t - a->seen > 300 || a->moved < .4f) continue;
            float dx = a->p[0] - me[0], dz = a->p[2] - me[2], d = sqrtf(dx * dx + dz * dz);
            if (d > 10.f || d < .2f || fabsf(a->p[1] - me[1]) > 3.f) continue;
            float sc = d * ((dx * fx + dz * fz) / d > .3f ? 1.f : 2.f);
            if (sc < best) best = sc, bpl = -1, bobj = a->o, memcpy(bp, a->p, 12);
        }
        if (best < 1e8f) {
            locked = bpl >= 0 ? 1 : 2; pl = bpl; obj = bobj; memcpy(tp, bp, 12);
            LOG("[ztarget] player %d locked on %s (%.1f away)\n", s + 1, bpl >= 0 ? "a player" : "an actor", best);
        } else LOG("[ztarget] player %d: nothing to lock on to nearby\n", s + 1);
    }
    pthread_mutex_lock(&mx);
    if (locked && !was) { float dx = tp[0] - me[0], dz = tp[2] - me[2]; z->keep_r = sqrtf(dx * dx + dz * dz); }
    z->locked = locked; z->lock_player = pl; z->lock_obj = obj; memcpy(z->lock_pos, tp, 12); memcpy(z->me, me, 12); z->have_me = 1;
    int conv = g_yaw_conv;
    pthread_mutex_unlock(&mx);
    /* face the target */
    if (locked && conv >= 0 && !menu_input_captured()) {
        float dx = tp[0] - me[0], dz = tp[2] - me[2];
        if (dx * dx + dz * dz > .04f) laonline_link_set_yaw(c, link, yaw_for(conv, dx, dz));
    }
}
void ztarget_tick(Cpu *c, uint64_t frame) {
    if (!g_ga || !game_in_play()) {
        pthread_mutex_lock(&mx); for (int s = 0; s < NSELF; s++) g_z[s].locked = 0, g_z[s].have_me = 0; pthread_mutex_unlock(&mx);
        for (int s = 0; s < NSELF; s++) atomic_store(&g_z[s].toggle, 0);
        return;
    }
    dmg_tick();
    uint64_t t = now_ms();
    for (int s = 0; s < NSELF; s++) z_tick(c, s, frame, t);
}
RT_ON_RESET(ztarget_reset) {
    memset(g_act, 0, sizeof g_act);
    pthread_mutex_lock(&mx); for (int s = 0; s < NSELF; s++) g_z[s].locked = 0, g_z[s].have_me = 0; pthread_mutex_unlock(&mx);
}
int ztarget_any(void) {  /* (menu.mm: a reticle to draw - any of this PC's players locked) */
    int on = 0;
    pthread_mutex_lock(&mx); for (int s = 0; s < NSELF; s++) on |= g_z[s].locked != 0; pthread_mutex_unlock(&mx);
    return on;
}
