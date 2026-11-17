/* LA-Online CO-OP V12 (2026-10-30). Everything new in V12 lives here; existing files only call in with single
 * added lines. Nothing in this file runs offline (Vanilla), and every feature has a laonline.ini switch:
 *
 *   v12_story_sync = 1        shared story/boss/dungeon progress flags (relay C_FLAG_SET, already in protocol v1)
 *   v12_item_mirror = 1       shared ownership: Bombs, Magic Powder, Bow, bottles, bag upgrades, leaves, sensor ...
 *   v12_shared_consumables = 1  rupees, bombs, arrows and Magic Powder picked up by anyone count for everyone
 *   v12_ghost_puppets = 1     remote Links stay VISIBLE but every collider on them is switched off: 1 = in the Wind
 *                             Fish's Egg (Dethl), 2 = everywhere, 0 = off (V10 behaviour)
 *   v12_final_boss_hide = 0   last resort: 1 hides remote Links in the Wind Fish's Egg
 *   v12_dive_assist = 1       Martha's Bay: hold R3 / F7 for 2 s to enter Catfish's Maw when diving fails
 *   v12_pvp = 1               Global Online: R3 tap / F6 toggles PvP (red players hit each other)
 *
 * Co-op modes only (Story Co-op = 1, Co-op Randomizer = 2) for sharing; Global (3) never shares anything: there
 * only PvP and the Global Randomizer lobby choice apply. Saves are only touched through the same verified save
 * offsets the upstream save editor / cheats use (runtime/ui/save_editor.mm, notes/save_format.md). */
#include "laonline_v12.h"
#include "laonline_v12_flags.h"
#include "laonline_session.h"
#include "../hle.h"
#include "../game_addresses.h"
#include "../settings.h"
#include "../dev_events.h"
#include "../platform.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern int game_in_play(void), laonline_level_ready(void), laonline_launch_choice(void), laonline_coop_world_ready(void);
extern bool laonline_ui_paused(void);
extern void laonline_notify(const char *);
extern bool flags_ext_get(int);
extern void flags_put(int, bool);
extern void warp_request(const char *level, const char *locator);
extern uint64_t renderer_present_count(void);
/* runtime/hle/laonline.c (V12 accessor): 1 = joined peer, position + map of its last state */
extern int laonline_v12_peer(int id, float pos[3], uint16_t *map, int *local_id);

/* ------------------------------------------------------------------ save layout (save_editor.mm) */
#define SAVE (GA(LIVE_SAVE))
enum { RUPEES = 0x5540, HP = 0x5544, TRADE = 0x555A, UPGRADES = 0x555C, BOMBS = 0x555E, ARROWS = 0x555F,
       POWDER = 0x5560, ITEMS = 0x5568, SENSOR = 0x556E, LEAVES = 0x5582 };
/* C_ITEM_GET keys used by V12 (u16 key, i16 value; the relay keeps the highest value per key and replays it to
 * late joiners in WELCOME). 0x71xx never collides with the legacy item ids (0..126). */
enum { K_ITEMS = 0x7100, K_UPGRADES = 0x7120, K_SENSOR = 0x7130, K_LEAVES = 0x7140, K_TRADE = 0x7150,
       K_LEVEL = 0x7160,                    /* V13: + family: highest level any player has (1 / 2) */
       K_BRLOC = 0x7200, K_BRLOC_N = 1024,  /* V13: + family * 1024: one key per place it was picked up (value 1) */
       K_END = K_BRLOC + 3 * K_BRLOC_N };
/* V13 progressive equipment families: Power Bracelet (V13 first fix), Shield / Mirror Shield and Sword / Noble Sword
 * (Mr. Cursey: "Kit had to get both copies of the shield for it to give us Mirror Shield, even though I picked up a
 * shield first"). The family index 0 bracelet keeps the keys the first V13 build used (0x7160 / 0x7200). */
static const struct { int item_bit, up_bit, lv1, lv2; const char *name, *lv2_name; } k_prog[3] = {
    {10, 2, 13, 14, "Power Bracelet", "Power Bracelet Lv2"}, {1, 1, 2, 3, "Shield", "Mirror Shield"}, {0, 0, 0, 1, "Sword", "Noble Sword"}};
static int prog_family(int item) { for (int f = 0; f < 3; f++) if (item == k_prog[f].lv1 || item == k_prog[f].lv2) return f; return -1; }
/* V13 Power Bracelet (Mr. Cursey: "we got both bracelets and the game didn't give Lv2"). The randomizer's bracelet is
 * progressive: a pickup gives Lv2 only if THAT player already holds Lv1. When two players each picked one up before
 * the other's had synced, both pickups became Lv1, the shared receipt for Lv1 was already owned, and nobody got Lv2.
 * Now every bracelet pickup place is shared (K_BRLOC + place hash, kept by the relay for late joiners): two different
 * places found by the team = Lv2 for everyone, one = Lv1. K_BRACELET carries a player's own level too (older saves). */
static uint8_t bracelet_local[3][K_BRLOC_N / 8];
static int bracelet_place(uint16_t map, uint16_t room) { return (int)((map * 131u + room * 7u + 1u) % 1021u); }
#define ITEM_MIRROR_BITS 0xFFFCu  /* ITEMS bits 2..15; sword/shield (0/1) stay with the verified reward sync */

/* ------------------------------------------------------------------ config */
static int cfg_loaded, cfg_story = 1, cfg_items = 1, cfg_consumables = 1, cfg_boss_hide = 0, cfg_ghost = 1, cfg_dive = 1, cfg_pvp = 1;
static int cfg_pvp_anim = 0;  /* V15.1: the learned damage animation froze Link (Velvet, Sanic's video) - off unless pvp_damage_anim = 1 */
static void cfg_load(void) {
    if (cfg_loaded) return;
    cfg_loaded = 1;
    FILE *f = fopen("laonline.ini", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[64]; int v;
        if (sscanf(line, " %63[a-z0-9_] = %d", key, &v) != 2) continue;
        if (!strcmp(key, "v12_story_sync")) cfg_story = v;
        else if (!strcmp(key, "v12_item_mirror")) cfg_items = v;
        else if (!strcmp(key, "v12_shared_consumables")) cfg_consumables = v;
        else if (!strcmp(key, "v12_final_boss_hide")) cfg_boss_hide = v;
        else if (!strcmp(key, "v12_ghost_puppets")) cfg_ghost = v;
        else if (!strcmp(key, "v12_dive_assist")) cfg_dive = v;
        else if (!strcmp(key, "v12_pvp")) cfg_pvp = v;
        else if (!strcmp(key, "pvp_damage_anim")) cfg_pvp_anim = v;
    }
    fclose(f);
    LOG("[laonline] V12 switches: story=%d items=%d consumables=%d final_boss_hide=%d ghost_puppets=%d dive_assist=%d pvp=%d\n",
        cfg_story, cfg_items, cfg_consumables, cfg_boss_hide, cfg_ghost, cfg_dive, cfg_pvp);
}

static int coop_mode(void) { int m = laonline_launch_choice(); return m == 1 || m == 2; }
static int global_mode(void) { return laonline_launch_choice() == 3; }
/* V15: PvP in Co-op and Co-op Randomizer too (community request), not only Global */
static int pvp_mode(void) { int m = laonline_launch_choice(); return m == 1 || m == 2 || m == 3; }
static uint64_t ms_now(void) { return plat_now_ns() / 1000000ull; }
static int save_ready(void) {
    if (!g_ga || !game_version() || !game_in_play()) return 0;
    return LD32(SAVE) == 0xe;  /* (the loaded-save marker laonline_coop.c checks) */
}

/* ------------------------------------------------------------------ shared state (network <-> game thread) */
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
typedef struct { uint8_t n; uint8_t b[24]; } Frame;  /* V13.5: up to 24 bytes (PvP attack visuals) */
static Frame outq[512];
static unsigned out_head, out_count;
static uint8_t flag_in[2048 / 8], flag_sent[2048 / 8];
extern int souls_flag_at(int i);
extern void flags_ext_set(int idx);
static uint32_t ext_in[64]; static unsigned ext_n;  /* V13: received extended flags (souls), see take_flag */
static uint8_t soul_sent[32 / 8];
/* V13.5: other players' item attacks, drawn by the overlay (laonline_overlay.mm) at their world position */
typedef struct { int from, item; uint16_t map; float pos[3], dir[2]; uint64_t start; } PvpFx;
static PvpFx fx_ring[16]; static unsigned fx_head; static uint16_t fx_my_map;
#define WORLD_FLAGS 8192  /* V13.5 Share the world (see world_load) */
/* V13.5: readable record of what everyone picked up / opened / cut: laonline-world-log.txt in the game folder
 * (kept under 2 MB: the old one becomes laonline-world-log.old.txt). Plain grass has no saved state in the game and
 * rocks are not tracked by it, so those can't be listed; item bushes, pots and every pickup are. */
#include <stdarg.h>
#include <time.h>
void laonline_world_log(const char *fmt, ...) {
    static pthread_mutex_t lm = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lm);
    FILE *f = fopen("laonline-world-log.txt", "a");
    if (f) {
        fseek(f, 0, SEEK_END);
        if (ftell(f) > 2 * 1024 * 1024) { fclose(f); remove("laonline-world-log.old.txt"); rename("laonline-world-log.txt", "laonline-world-log.old.txt"); f = fopen("laonline-world-log.txt", "a"); }
    }
    if (f) {
        time_t now = time(NULL); struct tm tm;
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char stamp[32]; strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
        fprintf(f, "%s  ", stamp);
        va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
        fputc('\n', f); fclose(f);
    }
    pthread_mutex_unlock(&lm);
}
static uint8_t world_kind[WORLD_FLAGS];        /* 1 pot, 2 item bush, 3 bush cut, 4 pickup spot, 5 dungeon key spot */
static uint16_t world_level[WORLD_FLAGS]; static char world_levels[256][40]; static int world_level_n;
static const char *world_kind_name(int k) { return k == 1 ? "opened a pot" : k == 2 ? "took the item in a bush" : k == 3 ? "cut an item bush" : k == 4 ? "picked up an item" : k == 5 ? "took a dungeon Small Key" : k == 6 ? "opened a chest" : k == 7 ? "took a Piece of Heart" : k == 8 ? "dug up / found a seashell spot" : k == 9 ? "took a freestanding item" : "used a spot"; }
static void world_note(int f, int kind, const char *level) {
    if (f < 1 || f >= WORLD_FLAGS) return;
    world_kind[f] = (uint8_t)kind;
    int i = 0; for (; i < world_level_n; i++) if (!strcmp(world_levels[i], level)) break;
    if (i == world_level_n && world_level_n < 256) snprintf(world_levels[world_level_n++], sizeof world_levels[0], "%s", level);
    world_level[f] = (uint16_t)(i < 256 ? i : 0);
}
static uint8_t world_is[WORLD_FLAGS / 8], world_sent[WORLD_FLAGS / 8];
/* V15.1 (Mr. Cursey: "We randomly received Noble's Sword, and none of us picked up a second sword"): where this seed put
 * each bracelet / shield / sword (spoiler.txt + tracker.tsv). When every one of those places is a shared world spot, the
 * team's level can be no higher than the number of those places already used in this world - one sword taken twice
 * (the same chest opened in two worlds, or one pickup counted at two places) can't make a Noble Sword any more. */
static int prog_flags[3][4], prog_nflags[3], prog_known[3];
static uint32_t world_in[1024]; static unsigned world_n;
static char world_dir[512]; static int world_count;
static int16_t item_in[K_END - K_ITEMS], item_sent[K_END - K_ITEMS];
static int relay_v12, relay_pvp135, relay_pvpko, relay_ending, relay_pvpcoop, welcome_seen;
static int tot_kills, tot_losses, tot_known;  /* V15: the server's totals for your name (Global) */
static atomic_int global_rando;
/* (LAS Recomp Co-Op's port) PvP for each of this PC's players: [0] Player 1 - the main connection, as V15.2's one
 * player - and [1..7] local co-op players 2..8, each on his own relay connection (laonline_local.c). Each has his own
 * switch (his R3; F6 is Player 1's), his own attacks, and takes his own hits (below: PvpSelf, pvp_tick). */
#define PVP_SELVES 8
static atomic_int pvp_on_of[PVP_SELVES], pvp_sent_state = -1;
static _Atomic unsigned char pvp_peer[LAO_PLAYERS];
static int hit_from[PVP_SELVES][16], hit_n[PVP_SELVES];  /* (mx) hits taken, from the relay: who, how much */
static uint8_t hit_amount[PVP_SELVES][16];

static int push_frame(const uint8_t *b, int n) {  /* caller holds mx; 0 = queue full (retried next scan) */
    if (n > (int)sizeof outq[0].b || out_count >= sizeof outq / sizeof outq[0]) return 0;
    Frame *f = &outq[(out_head + out_count++) % (sizeof outq / sizeof outq[0])];
    f->n = (uint8_t)n; memcpy(f->b, b, (size_t)n);
    return 1;
}
static void send_flag(int flag) {
    if (flag < 0 || flag >= 2048 || (flag_sent[flag >> 3] >> (flag & 7) & 1)) return;
    uint8_t f[7] = {5, 0, 0x03}; uint32_t v = (uint32_t)flag; memcpy(f + 3, &v, 4);
    if (push_frame(f, 7)) flag_sent[flag >> 3] |= (uint8_t)(1u << (flag & 7));
}
static void send_item(int key, int value) {
    if (key < K_ITEMS || key >= K_END || value <= item_sent[key - K_ITEMS]) return;
    uint8_t f[7] = {5, 0, 0x04}; uint16_t k = (uint16_t)key; int16_t v = (int16_t)value;
    memcpy(f + 3, &k, 2); memcpy(f + 5, &v, 2);
    if (push_frame(f, 7)) item_sent[key - K_ITEMS] = (int16_t)value;
}

/* ------------------------------------------------------------------ network thread */
int laonline_v12_packet(uint8_t *out, int capacity, int launch_mode) {
    cfg_load();
    int n = 0;
    pthread_mutex_lock(&mx);
    int want = atomic_load(&pvp_on_of[0]);
    int pvp_ok = launch_mode == 3 || ((launch_mode == 1 || launch_mode == 2) && relay_pvpcoop);  /* V15: co-op too */
    if (pvp_ok && relay_v12 && want != atomic_load(&pvp_sent_state) && capacity >= 5) {
        out[0] = 3; out[1] = 0; out[2] = LAO_C_PVP; out[3] = 0; out[4] = (uint8_t)want; n = 5;
        atomic_store(&pvp_sent_state, want);
    } else if (out_count && capacity >= outq[out_head].n) {
        Frame *f = &outq[out_head];
        /* PvP frames (0x1c) only in Global; progress frames (0x03 / 0x04) only in co-op. */
        int ok = f->b[2] == LAO_C_PVP ? pvp_ok : (launch_mode == 1 || launch_mode == 2);
        if (ok) { memcpy(out, f->b, f->n); n = f->n; }
        out_head = (out_head + 1) % (sizeof outq / sizeof outq[0]); out_count--;
        if (!ok) n = -1;
    }
    pthread_mutex_unlock(&mx);
    return n < 0 ? 0 : n;
}

static void reset_connection_state(void) {  /* caller holds mx */
    memset(flag_in, 0, sizeof flag_in); memset(flag_sent, 0, sizeof flag_sent);
    memset(item_in, 0, sizeof item_in); memset(item_sent, 0, sizeof item_sent);
    ext_n = 0; memset(soul_sent, 0, sizeof soul_sent);
    world_n = 0; memset(world_sent, 0, sizeof world_sent);
    out_head = out_count = 0; memset(hit_n, 0, sizeof hit_n); welcome_seen = 1; relay_v12 = 0; relay_pvp135 = 0; relay_pvpko = 0; relay_ending = 0; relay_pvpcoop = 0; tot_known = 0;
    atomic_store(&pvp_sent_state, -1);
    for (int i = 0; i < LAO_PLAYERS; i++) atomic_store(&pvp_peer[i], 0);
}
/* V13: boss / mini-boss souls (randomizer "boss-souls" / "miniboss-souls") are extended event flags (2048+), set in
 * one seed's souls.tsv. Co-op Randomizer shares them like story flags: a soul anyone finds lets the boss appear for
 * everyone. Received ones queue here (network thread) and are applied on the game thread only if they ARE a soul of
 * this seed - any other extended flag is ignored. */
/* V13.5 "Share the world" (Co-op Randomizer). Each seed lists the event flag of every item pot (pots.tsv), item bush
 * and its cut state (bushes.tsv) and every quick-pickup location (quick_pickups.tsv). When a teammate takes the item
 * there, its flag is shared, so for everyone else the pot / bush / item is already used when they enter that room:
 * a Small Key (or rupee, heart...) can no longer be picked up again by each player (the "duplicated keys" report).
 * Only flags from these seed lists are ever written. Nothing changes live inside a room you are already in. */
extern const char *rando_seed_dir(void);
/* Vanilla "KeyGet_*" flags (dev_flag_names.inc): a dungeon Small Key that was taken. Shared in Story Co-op too, so a key
 * a teammate picked up (and that was added to your keys) is not still lying there for you. */
static const int k_keyget[] = {776, 787, 788, 807, 813, 818, 820, 822, 825, 830, 833, 881, 898, 899, 951, 955, 965, 967, 969, 971, 1012, 1014};
static int keyget_flag(uint32_t f) { for (unsigned i = 0; i < sizeof k_keyget / sizeof *k_keyget; i++) if ((uint32_t)k_keyget[i] == f) return 1; return 0; }
static int world_flag(uint32_t f) { return f < WORLD_FLAGS && ((world_is[f >> 3] >> (f & 7)) & 1); }
static void world_add(int f) {
    if (f < 1 || f >= WORLD_FLAGS || world_flag((uint32_t)f)) return;
    for (int i = 0, s; (s = souls_flag_at(i)) >= 0; i++) if (s == f) return;            /* souls: own setting */
    for (size_t i = 0; i < LAO_V12_STORY_COUNT; i++) if (lao_v12_story_flags[i] == f) return;  /* story: own setting */
    world_is[f >> 3] |= (uint8_t)(1u << (f & 7)); world_count++;
}
static void world_load(void) {  /* game thread; re-reads when the seed folder changes */
    const char *dir = rando_seed_dir();
    if (!dir) dir = "";
    if (world_count && !strcmp(dir, world_dir)) return;
    pthread_mutex_lock(&mx);
    memset(world_is, 0, sizeof world_is); memset(world_sent, 0, sizeof world_sent); world_count = 0; world_n = 0;
    snprintf(world_dir, sizeof world_dir, "%s", dir);
    for (unsigned i = 0; i < sizeof k_keyget / sizeof *k_keyget; i++) { world_add(k_keyget[i]); world_note(k_keyget[i], 5, "a dungeon"); }
    if (!dir[0]) { pthread_mutex_unlock(&mx); return; }
    static const char *files[] = {"pots.tsv", "bushes.tsv", "quick_pickups.tsv"};
    for (int k = 0; k < 3; k++) {
        char path[640]; snprintf(path, sizeof path, "%s/%s", dir, files[k]);
        FILE *fp = fopen(path, "r"); if (!fp) continue;
        char line[512];
        while (fgets(line, sizeof line, fp)) {
            char *col[8] = {0}; int n = 0;
            for (char *s = line; n < 8 && s;) { col[n++] = s; s = strchr(s, '\t'); if (s) *s++ = 0; }
            if (k == 0 && n >= 5) { world_add(atoi(col[4])); world_note(atoi(col[4]), 1, col[0]); }                   /* pot: item flag */
            if (k == 1 && n >= 6) { world_add(atoi(col[4])); world_add(atoi(col[5])); world_note(atoi(col[4]), 2, col[0]); world_note(atoi(col[5]), 3, col[0]); }  /* bush: item + cut */
            /* quick pickup: the event flag is column 2 (column 5 is the item's index - V13.5..V15 read that one, so
             * freestanding items were never shared: Mr. Cursey, V15) */
            if (k == 2 && n >= 5 && atoi(col[1]) > 0) { world_add(atoi(col[1])); if (!world_kind[atoi(col[1]) & (WORLD_FLAGS - 1)]) world_note(atoi(col[1]), 4, col[0]); }
        }
        fclose(fp);
    }
    {   /* V15.1: chests and freestanding items (Mr. Cursey: "only chests and freestanding items do not sync"). The seed's
         * tracker.tsv lists every check with its event flag: chests (TreasureBoxOpen...), Pieces of Heart, seashell
         * spots and loose items. NPC gifts and trades are left out - those are events, not things lying in a room. */
        char path[640]; snprintf(path, sizeof path, "%s/tracker.tsv", dir);
        FILE *fp = fopen(path, "r");
        static char cname[2048][40]; static int cflag[2048]; int cn = 0;
        memset(prog_nflags, 0, sizeof prog_nflags); memset(prog_known, 0, sizeof prog_known);
        static const char *npc[] = {"tarin", "mamu", "manbo", "goriya-trader", "mermaid-cave", "washed-up", "sale"};
        char line[512];
        while (fp && fgets(line, sizeof line, fp)) {
            char *col[14] = {0}; int n = 0;
            for (char *s = line; n < 14 && s;) { col[n++] = s; s = strchr(s, '\t'); if (s) *s++ = 0; }
            if (n < 8 || strcmp(col[0], "C")) continue;
            if (cn < 2048) { snprintf(cname[cn], sizeof cname[0], "%s", col[1]); cflag[cn++] = atoi(col[5]); }
            int f = atoi(col[5]), kind = !strcmp(col[6], "chest") ? 6 : !strcmp(col[6], "heart") ? 7 : !strcmp(col[6], "shell") ? 8 : !strcmp(col[6], "other") ? 9 : 0;
            if (f < 1 || !kind) continue;
            int skip = 0; for (unsigned i = 0; i < sizeof npc / sizeof *npc; i++) if (!strcmp(col[1], npc[i]) || strstr(col[1], "trade")) skip = 1;
            if (skip) continue;
            world_add(f); if (!world_kind[f & (WORLD_FLAGS - 1)]) world_note(f, kind, col[7]);
        }
        if (fp) fclose(fp);
        snprintf(path, sizeof path, "%s/spoiler.txt", dir);
        static const char *pname[3] = {"bracelet", "shield", "sword"};  /* (k_prog order) */
        int bad[3] = {0, 0, 0};
        fp = cn ? fopen(path, "r") : NULL;
        while (fp && fgets(line, sizeof line, fp)) {
            char name[48], item[48];
            if (line[0] != ' ' || sscanf(line, " %47[^:]: %47s", name, item) != 2) continue;
            for (int fm = 0; fm < 3; fm++) if (!strcmp(item, pname[fm])) {
                int f = -1; for (int i = 0; i < cn; i++) if (!strcmp(cname[i], name)) { f = cflag[i]; break; }
                if (f < 1 || !world_flag((uint32_t)f) || prog_nflags[fm] >= 4) bad[fm] = 1;
                else prog_flags[fm][prog_nflags[fm]++] = f;
            }
        }
        if (fp) fclose(fp);
        for (int fm = 0; fm < 3; fm++) {
            prog_known[fm] = prog_nflags[fm] >= 2 && !bad[fm];
            if (prog_known[fm]) LOG("[laonline] V15.1 %s places in this seed: flags %d and %d (shared spots)\n", pname[fm], prog_flags[fm][0], prog_flags[fm][1]);
        }
    }
    pthread_mutex_unlock(&mx);
    LOG("[laonline] V15.1 share the world: %d chest / pickup / pot / bush flags from this seed\n", world_count);
}
static void take_flag(uint32_t flag) {  /* caller holds mx */
    if (world_flag(flag)) { if (world_n < sizeof world_in / sizeof *world_in) world_in[world_n++] = flag; world_sent[flag >> 3] |= (uint8_t)(1u << (flag & 7)); return; }
    if (flag >= 2048) { if (flag < 65536 && ext_n < 64) ext_in[ext_n++] = flag; return; }
    for (size_t i = 0; i < LAO_V12_STORY_COUNT; i++)
        if (lao_v12_story_flags[i] == flag) { flag_in[flag >> 3] |= (uint8_t)(1u << (flag & 7)); flag_sent[flag >> 3] |= (uint8_t)(1u << (flag & 7)); return; }
}
static void take_item(uint16_t key, int16_t value) {  /* caller holds mx */
    if (key < K_ITEMS || key >= K_END || value <= 0) return;
    if (value > item_in[key - K_ITEMS]) item_in[key - K_ITEMS] = value;
    if (value > item_sent[key - K_ITEMS]) item_sent[key - K_ITEMS] = value;
}

/* ------------------------------------------------------------------ V13.5 PvP knock-outs: kill feed + counter */
static int ko_kills, ko_falls;
static void ko_fill(char *out, size_t cap, const char *fmt, const char *k, const char *v) {
    size_t o = 0;
    for (const char *p = fmt; *p && o + 1 < cap; p++) {
        const char *ins = p[0] == '%' && p[1] == 'k' ? k : p[0] == '%' && p[1] == 'v' ? v : NULL;
        if (ins) { for (; *ins && o + 1 < cap; ins++) out[o++] = *ins; p++; }
        else out[o++] = *p;
    }
    out[o] = 0;
}
/* V15: the Global kill announcement - sarcastic, old-tongue ("Thy be getteth me angry"), Zelda-flavoured, and built
 * fresh each time from three parts (12 x 26 x 18 = 5616 lines). The server's random byte + the names + the score pick
 * the parts, so every player sees the same line. All lines are original. */
static const char *const kv15_open[] = {"Lo!", "Hark!", "Behold!", "Alas!", "Verily,", "Woe!", "Hear ye, hear ye!",
    "By the Wind Fish,", "Thus it is written:", "Rejoice!", "O ye of little hearts,", "Gather round, Koholint:"};
/* V15 (user's picture): two views of a KO in Global.
 *  - the two players in it get a personal CARD (bottom right): a funny old-tongue line written for THEM ("Thou hast
 *    smitten Velvet!" / "Thou hast tasted defeat! Verily, Sanic reigneth!") - different for each of them - and their
 *    head-to-head score;
 *  - everyone sees the plain KILL FEED (bottom left, like a shooter's): "Sanic  defeated  Velvet  with Bombs".
 * Works for any names (Global holds up to 100 players). */
static const char *const kv15_won[] = {
    "Thou hast smitten %v most thoroughly!", "%v hath tasted defeat at thy hand!", "Thou didst humble %v before all of Koholint!",
    "Thou hast sent %v back unto Marin's bed!", "The hearts of %v are emptied, and thou laughest!", "Thy blade found %v wanting!",
    "%v didst forget the shield. Thou didst not forget the sword!", "Thou hast made of %v an example unto the Moblins!",
    "Behold thy might, for %v is no more!", "%v thought it wise to anger thee. It was not wise!",
    "Thou hast taken %v's last heart as a trophy!", "Thou comest, and %v goeth!", "%v hath met their match - and it is thee!",
    "Thou hast delivered %v unto Dampe's shovel!"};
static const char *const kv15_won_close[] = {"", "Thou reignest!", "Koholint trembleth.", "The Owl approveth.",
    "Gloat not... too much.", "Thy legend groweth.", "Even the Wind Fish stirreth.", "Verily, a mighty hero!", "Rejoice!"};
static const char *const kv15_lost[] = {
    "Thou hast tasted defeat! Verily, %k reigneth!", "%k hath smitten thee most thoroughly.", "Thy hearts are emptied, and %k laugheth.",
    "%k hath sent thee back unto Marin's bed.", "Thou didst forget the shield. %k forgot not the sword.",
    "%k hath taken thy last heart as a trophy.", "Thou art fallen, and great was the fall thereof.",
    "Thou trusted in a bottled fairy that was not there. %k was.", "%k cometh, and thou goest.", "Thou didst anger %k. It went poorly.",
    "Thou walkest now in the valley of the Game Over screen, courtesy of %k.", "A terrible hero hath fallen - and it is thee.",
    "%k hath weighed thee in the balance, and found thee wanting.", "Thou hast met thy match, and their name is %k."};
static const char *const kv15_lost_close[] = {"", "Might as well get thee off the game.", "Oh, look who died again.",
    "Thy controller weepeth.", "Pray, practice.", "Rise, and be smitten anew.", "Thy be getteth me angry!", "Verily, a skill issue.",
    "Get thee to a Great Fairy.", "Tarin is disappointed in thee.", "Even the Octoroks are laughing."};
static const char *weapon_name(unsigned w) {
    switch (w) {
    case 2: return "Bombs"; case 3: return "the Bow"; case 4: return "the Hookshot"; case 5: return "the Boomerang";
    case 6: return "the Magic Rod"; case 8: return "Magic Powder"; case 100: return "the Sword"; case 101: return "a Spin Attack";
    case 102: return "a Bomb Arrow"; case 103: return "the Pegasus Boots"; case 104: return "Roc's Feather"; default: return "";
    }
}
enum { FEED_ROWS = 5, FEED_SHOW_MS = 10000, CARD_SHOW_MS = 10000 };
static struct { char k[32], v[32], weapon[24]; int ending, randomizer, you; uint64_t at; } feed[FEED_ROWS];
static unsigned feed_head;
static struct { char line[320], score[96]; uint64_t at; int you; } card;
static void card_line(char *out, size_t cap, unsigned quote, const char *k, const char *v, unsigned kv, unsigned vk, unsigned you) {
    uint32_t h = 2166136261u;
    for (const char *p = k; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    for (const char *p = v; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    h ^= quote * 2654435761u; h ^= (kv * 97u + vk * 131u + you * 7919u) * 2246822519u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    const unsigned no = sizeof kv15_open / sizeof *kv15_open;
    const char *const *bodies = you == 1 ? kv15_won : kv15_lost, *const *closers = you == 1 ? kv15_won_close : kv15_lost_close;
    unsigned nb = you == 1 ? sizeof kv15_won / sizeof *kv15_won : sizeof kv15_lost / sizeof *kv15_lost;
    unsigned nc = you == 1 ? sizeof kv15_won_close / sizeof *kv15_won_close : sizeof kv15_lost_close / sizeof *kv15_lost_close;
    const char *o = kv15_open[h % no], *b = bodies[(h / no) % nb], *c = closers[(h / (no * nb)) % nc];
    char body[200]; ko_fill(body, sizeof body, b, k, v);
    size_t ol = strlen(o); if (ol && (o[ol - 1] == '!' || o[ol - 1] == ':') && body[0] >= 'a' && body[0] <= 'z') body[0] = (char)(body[0] - 32);
    snprintf(out, cap, "%s %s%s%s", o, body, c[0] ? " " : "", c);
}
static void kill_feed_add(unsigned quote, const char *k, const char *v, unsigned kv, unsigned vk, unsigned you, unsigned weapon) {  /* (mx held) */
    unsigned slot = feed_head++ % FEED_ROWS;
    snprintf(feed[slot].k, sizeof feed[slot].k, "%s", k); snprintf(feed[slot].v, sizeof feed[slot].v, "%s", v);
    snprintf(feed[slot].weapon, sizeof feed[slot].weapon, "%s", weapon_name(weapon));
    feed[slot].ending = 0, feed[slot].you = (int)you, feed[slot].at = ms_now();
    if (you == 1 || you == 2) {  /* one of the two: the personal card */
        card_line(card.line, sizeof card.line, quote, k, v, kv, vk, you);
        if (you == 1) snprintf(card.score, sizeof card.score, "You %u  -  %u %s", kv, vk, v);
        else snprintf(card.score, sizeof card.score, "You %u  -  %u %s", vk, kv, k);
        card.at = ms_now(); card.you = (int)you;
    }
    if (you == 1) ko_kills++;
    if (you == 2) ko_falls++;
    LOG("[laonline] PvP KO: %s defeated %s%s%s%s%s\n", k, v, weapon_name(weapon)[0] ? " with " : "", weapon_name(weapon),
        you ? " | card: " : "", you ? card.line : "");
}
/* V14: "<name> saved Koholint!" - someone in Global finished the game (the ending movie): a gold kill-feed row */
static void ending_feed_add(const char *name, unsigned you, unsigned randomizer) {  /* (mx held) */
    unsigned slot = feed_head++ % FEED_ROWS;
    snprintf(feed[slot].k, sizeof feed[slot].k, "%s", you ? "You" : name); feed[slot].v[0] = 0; feed[slot].weapon[0] = 0;
    feed[slot].ending = 1, feed[slot].randomizer = (int)randomizer, feed[slot].you = (int)you, feed[slot].at = ms_now();
    LOG("[laonline] Global: %s woke the Wind Fish and saved Koholint! (%s)\n", feed[slot].k, randomizer ? "Global Randomizer" : "Global");
}
static atomic_int ending_req;
void laonline_v12_ending(void) { atomic_store(&ending_req, 1); }  /* runtime/hle/movie.c: the ending movie started */
static float fade(uint64_t age, uint64_t show) {
    if (age >= show) return 0;
    return age < 300 ? .2f + .8f * (float)age / 300.f : age > show - 1500 ? (float)(show - age) / 1500.f : 1.f;
}
/* the kill feed (bottom left): row i, newest first. Returns its visibility (0 = gone). */
float laonline_v12_feed_row(int i, char *k, int kc, char *v, int vc, char *weapon, int wc, int *ending) {
    float a = 0;
    pthread_mutex_lock(&mx);
    if (i >= 0 && i < FEED_ROWS && (unsigned)i < feed_head) {
        unsigned slot = (feed_head - 1 - (unsigned)i) % FEED_ROWS;
        a = fade(ms_now() - feed[slot].at, FEED_SHOW_MS);
        if (a > 0) { snprintf(k, (size_t)kc, "%s", feed[slot].k); snprintf(v, (size_t)vc, "%s", feed[slot].v);
                     snprintf(weapon, (size_t)wc, "%s", feed[slot].weapon); *ending = feed[slot].ending; }
    }
    pthread_mutex_unlock(&mx);
    return a;
}
float laonline_v12_feed_panel(float *left_s) {  /* the panel fades with its newest row */
    pthread_mutex_lock(&mx);
    float a = 0; *left_s = 0;
    if (feed_head) { uint64_t age = ms_now() - feed[(feed_head - 1) % FEED_ROWS].at; a = fade(age, FEED_SHOW_MS); if (a > 0) *left_s = (float)(FEED_SHOW_MS - age) / 1000.f; }
    pthread_mutex_unlock(&mx);
    return a;
}
/* the personal card (bottom right): only for the killer and the one who fell */
float laonline_v12_card(char *line, int lc, char *score, int sc, float *left_s) {
    pthread_mutex_lock(&mx);
    float a = card.at ? fade(ms_now() - card.at, CARD_SHOW_MS) : 0;
    if (a > 0) { snprintf(line, (size_t)lc, "%s", card.line); snprintf(score, (size_t)sc, "%s", card.score); *left_s = (float)(CARD_SHOW_MS - (ms_now() - card.at)) / 1000.f; }
    pthread_mutex_unlock(&mx);
    return a;
}
/* (tests / logs) i = 0: the card if one shows, else the newest feed row as text */
float laonline_v12_killfeed(int i, char *line1, int cap1, char *line2, int cap2) {
    float left; float a = i == 0 ? laonline_v12_card(line1, cap1, line2, cap2, &left) : 0;
    if (a > 0) return a;
    char k[32], v[32], w[24]; int ending = 0;
    a = laonline_v12_feed_row(i, k, sizeof k, v, sizeof v, w, sizeof w, &ending);
    if (a <= 0) return 0;
    if (ending) { snprintf(line1, (size_t)cap1, "%s woke the Wind Fish and saved Koholint!", k); snprintf(line2, (size_t)cap2, "Global - congratulations!"); }
    else { snprintf(line1, (size_t)cap1, "%s defeated %s%s%s", k, v, w[0] ? " with " : "", w); line2[0] = 0; }
    return a;
}
int laonline_v12_totals(int *kills, int *losses) {
    pthread_mutex_lock(&mx);
    int on = tot_known && global_mode() && atomic_load(&pvp_on_of[0]);
    *kills = tot_kills, *losses = tot_losses;
    pthread_mutex_unlock(&mx);
    return on;
}
int laonline_v12_my_kos(int *kills, int *falls) {
    pthread_mutex_lock(&mx);
    *kills = ko_kills, *falls = ko_falls;
    int on = relay_pvpko;
    pthread_mutex_unlock(&mx);
    return on && global_mode() && atomic_load(&pvp_on_of[0]);
}

/* ------------------------------------------------------------------ V14 PvP hit markers + lock-on */
enum { HM_N = 8 };
static struct { int attacker, victim, quarters; float pos[3]; uint64_t at; int resolved, mine, on_me; } hm[HM_N];
static unsigned hm_head;
static struct { int attacker; uint64_t at; } last_pvp_hit[LAO_PLAYERS];  /* V15.1: who hit whom last (death notices) */
static void hitmark_add(int attacker, int victim, int quarters) {  /* (mx held) - placed on the game thread */
    if (victim >= 0 && victim < LAO_PLAYERS) { last_pvp_hit[victim].attacker = attacker; last_pvp_hit[victim].at = ms_now(); }
    unsigned k = hm_head++ % HM_N;
    hm[k].attacker = attacker, hm[k].victim = victim, hm[k].quarters = quarters, hm[k].at = ms_now(), hm[k].resolved = 0;
}
/* V15.1 (Velvet's log: every PvP knock-out showed "Velvet was defeated. Cause unknown"): a teammate who goes down
 * within 8 s of a PvP hit the server confirmed was knocked out by that player. 1 = found (name: "you" if it was us). */
int laonline_v12_pvp_killer(int victim, char *name, int cap) {
    if (victim < 0 || victim >= LAO_PLAYERS || !name || cap < 2) return 0;
    int attacker; uint64_t at;
    pthread_mutex_lock(&mx); attacker = last_pvp_hit[victim].attacker; at = last_pvp_hit[victim].at; pthread_mutex_unlock(&mx);
    if (!at || ms_now() - at > 8000 || attacker == victim) return 0;
    float p[3]; uint16_t map; int local = -1;
    for (int id = 0; id < LAO_PLAYERS && local < 0; id++) if (laonline_v12_peer(id, p, &map, &local)) break;
    if (attacker == local) { snprintf(name, (size_t)cap, "you"); return 1; }
    extern int laonline_remote_tag_at(int id, char *name, int cap, float pos[3], uint8_t rgb[3], int *health, int *max_health);
    uint8_t rgb[3]; int h, mh;
    if (!laonline_remote_tag_at(attacker, name, cap, p, rgb, &h, &mh) || !name[0]) snprintf(name, (size_t)cap, "another player");
    return 1;
}
int laonline_v12_hitmarks(float pos[][3], float *age_s, int *quarters, int *mine, int *on_me, int cap) {
    int n = 0; uint64_t now = ms_now();
    pthread_mutex_lock(&mx);
    for (int i = 0; i < HM_N && n < cap; i++) {
        if (!hm[i].at || !hm[i].resolved || now - hm[i].at > 900) continue;
        memcpy(pos[n], hm[i].pos, 12); age_s[n] = (now - hm[i].at) / 1000.f; quarters[n] = hm[i].quarters;
        mine[n] = hm[i].mine; on_me[n] = hm[i].on_me; n++;
    }
    pthread_mutex_unlock(&mx);
    return n;
}
int laonline_v12_lockon(float pos[3]) { extern int ztarget_get(float p[3], int *player); return ztarget_get(pos, NULL); }  /* V15 */

void laonline_v12_message(const uint8_t *b, size_t n) {
    if (!b || !n) return;
    pthread_mutex_lock(&mx);
    if (b[0] == 0x81 && n >= 3) {  /* WELCOME: new connection; its tail is the lobby's stored progress */
        reset_connection_state();
        size_t at = 3;
        int ok = 1;
        for (unsigned k = 0; k < b[2] && ok; k++) {  /* id, str name, rgb */
            if (at + 2 > n) { ok = 0; break; }
            at++; size_t len = b[at++];
            if (at + len + 3 > n) ok = 0; else at += len + 3;
        }
        if (ok && n - at >= 4) {
            uint32_t count; memcpy(&count, b + at, 4); at += 4;
            if (count <= (n - at) / 4) {
                for (uint32_t i = 0; i < count; i++) { uint32_t f; memcpy(&f, b + at + 4 * i, 4); take_flag(f); }
                at += (size_t)count * 4;
                if (n - at >= 2) {
                    uint16_t items; memcpy(&items, b + at, 2); at += 2;
                    if ((size_t)items * 4 <= n - at)
                        for (unsigned i = 0; i < items; i++) {
                            uint16_t k; int16_t v; memcpy(&k, b + at + 4 * i, 2); memcpy(&v, b + at + 4 * i + 2, 2);
                            take_item(k, v);
                        }
                }
            }
        }
    } else if (b[0] == 0x85 && n == 6) {        /* S_FLAG_SET: u8 from, u32 flag */
        uint32_t f; memcpy(&f, b + 2, 4); take_flag(f);
    } else if (b[0] == 0x86 && n == 6) {        /* S_ITEM_GET: u8 from, u16 key, i16 value */
        uint16_t k; int16_t v; memcpy(&k, b + 2, 2); memcpy(&v, b + 4, 2); take_item(k, v);
    } else if (b[0] == 0x89 && n == 5) {        /* S_PONG */
        uint32_t r; memcpy(&r, b + 1, 4);
        if (r == LAO_V12_REPLY && !relay_v12) { relay_v12 = 1; LOG("[laonline] V12 relay: PvP and Global Randomizer supported\n"); }
        if (r == LAO_PVP135_REPLY && !relay_pvp135) { relay_pvp135 = 1; LOG("[laonline] V13.5 relay: PvP item attacks supported\n"); }
        if (r == LAO_PVPCOOP_REPLY && !relay_pvpcoop) { relay_pvpcoop = 1; LOG("[laonline] V15 relay: PvP in Co-op too\n"); }
        if (r == LAO_GLOBALEND_REPLY && !relay_ending) { relay_ending = 1; LOG("[laonline] V14 relay: Global finish announcements supported\n"); }
        if (r == LAO_PVPKO_REPLY && !relay_pvpko) { relay_pvpko = 1; LOG("[laonline] V13.5 relay: PvP knock-outs and kill feed supported\n"); }
    } else if (b[0] == LAO_S_PVP && n == 20 && b[1] == 2 && b[2] < LAO_PLAYERS) {  /* V13.5: someone's item attack */
        PvpFx fx; fx.from = b[2]; fx.item = b[3]; memcpy(&fx.map, b + 4, 2); memcpy(fx.pos, b + 6, 12);
        fx.dir[0] = (int8_t)b[18] / 100.f; fx.dir[1] = (int8_t)b[19] / 100.f; fx.start = ms_now();
        if (isfinite(fx.pos[0]) && isfinite(fx.pos[1]) && isfinite(fx.pos[2])) { fx_ring[fx_head % 16] = fx; fx_head++; }
    } else if (b[0] == LAO_S_PVP && n == 6 && b[1] == 6) {  /* V15: your Kills / Losses (server, by name) */
        uint16_t k, l; memcpy(&k, b + 2, 2); memcpy(&l, b + 4, 2); tot_kills = k, tot_losses = l, tot_known = 1;
    } else if (b[0] == LAO_S_PVP && n >= 6 && b[1] == 5) {  /* V14: someone in Global finished the game */
        /* u8 kind 5, u8 you (1 = it was you), u8 randomizer, u8 len + name */
        size_t nl = b[4];
        if (nl && nl < 32 && 5 + nl <= n) { char nm[32]; memcpy(nm, b + 5, nl); nm[nl] = 0; ending_feed_add(nm, b[2], b[3]); }
    } else if (b[0] == LAO_S_PVP && n >= 12 && b[1] == 3) {  /* V13.5: a knock-out somewhere in Global (kill feed) */
        /* u8 kind 3, u8 quote, u16 killer-vs-victim, u16 victim-vs-killer, u8 you (1 killer / 2 victim / 0),
         * u8 len + killer name, u8 len + victim name */
        size_t kl = b[8], vl = 9 + kl < n ? b[9 + kl] : 0;
        if (kl && vl && kl < 32 && vl < 32 && 10 + kl + vl <= n) {
            char k[32], v[32];
            memcpy(k, b + 9, kl); k[kl] = 0;
            memcpy(v, b + 10 + kl, vl); v[vl] = 0;
            uint16_t kv, vk; memcpy(&kv, b + 3, 2); memcpy(&vk, b + 5, 2);
            kill_feed_add(b[2], k, v, kv, vk, b[7], 10 + kl + vl < n ? b[10 + kl + vl] : 0);  /* (V15: the weapon after the names) */
        }
    } else if (b[0] == LAO_S_PVP && n == 5 && b[1] == 4 && b[2] < LAO_PLAYERS && b[3] < LAO_PLAYERS) {
        hitmark_add(b[2], b[3], b[4]);  /* V14: the server confirmed a PvP hit (attacker, victim, quarter hearts) */
    } else if (b[0] == LAO_S_PVP && n == 4 && b[2] < LAO_PLAYERS) {
        if (b[1] == 0) atomic_store(&pvp_peer[b[2]], b[3] ? 1 : 0);
        else if (b[1] == 1 && hit_n[0] < 16) { hit_from[0][hit_n[0]] = b[2]; hit_amount[0][hit_n[0]++] = b[3]; }  /* (Player 1: the main connection) */
    } else if (b[0] == 0x83 && n == 2 && b[1] < LAO_PLAYERS) {
        atomic_store(&pvp_peer[b[1]], 0);
    }
    pthread_mutex_unlock(&mx);
}

/* ------------------------------------------------------------------ shared consumables (game thread) */
static struct { uint32_t before, expected; int tries; uint64_t frame; } rupee_check;
/* LAS Recomp Co-Op's port (LAS Recomp's review: no direct save writes): what the team shares is granted through the
 * game's own AddItem (ADD_ITEM, main+0x7e18b0 on 1.0.1: item, count, index, auto-equip), which sets the item's bits
 * and counts as the game does - the bag's or quiver's size (bombs past it are clipped), its "got" bit (+0x55E4) -
 * instead of writing the save. Checked item by item on 1.0.1 (LARECOMP_GIVE_ITEM, the save before and after):
 * k_item_bit_id below, the capacity upgrades 123 / 124 / 125 (+0x555C bits 3 / 4 / 5), the Seashell Sensor 116,
 * golden leaf 23 (index: the leaf, its bit), the trade steps (trade_item), the levels 13 / 14, 2 / 3, 0 / 1 (Power
 * Bracelet, shield, sword: Lv1 its ITEMS bit, Lv2 its UPGRADES bit), rupees (Rupee1 .. Rupee300: one call adds the
 * item's value once, whatever the count). A teammate's grant: laonline_coop_remote_grant keeps it from being reported
 * back to the team as this player's pickup (laonline_coop.c), the journal marks it REMOTE. Game thread. */
static void native_grant(struct Cpu *c, int item, int count, int index) {
    extern void laonline_coop_remote_grant(int);
    extern void lao_dev_remote(int);
    struct Cpu saved = *c;
    c->x[0] = (uint64_t)item, c->x[1] = (uint64_t)count, c->x[2] = (uint64_t)(int64_t)index, c->x[3] = 0;
    laonline_coop_remote_grant(1), lao_dev_remote(1);
    guest_call(c, GA(ADD_ITEM));
    lao_dev_remote(0), laonline_coop_remote_grant(0);
    *c = saved;
    LOG("[laonline] team grant: item %d x%d (index %d)\n", item, count, index);
}
/* ITEMS bit -> its item (-1: not mirrored here: sword / shield, the verified reward sync). Bits 13 to 15 are the three
 * bottles (item 113, index 0 to 2). Bit 10 is the Power Bracelet Lv1. */
static const int8_t k_item_bit_id[16] = {-1, -1, 4, 5, 6, 7, 8, 9, 11, 12, 13, 15, 16, 113, 113, 113};
/* the trade byte's step -> its item: 1 to 12 are items 29 to 40 (Yoshi Doll .. Necklace), 13 / 14 items 42 / 43
 * (Mermaid's Scale, Magnifying Lens) - as the game's own Inventory.SetWarashibeItem gives them (main+0x7e2a10) */
static int trade_item(int t) { return t >= 1 && t <= 12 ? 28 + t : t == 13 ? 42 : t == 14 ? 43 : -1; }
/* V13: a sword / shield / bracelet (either level) picked up here, at this map / room (laonline_coop.c, local AddItem) */
void laonline_v12_progressive_found(int item, uint16_t map, uint16_t room) {
    int f = prog_family(item);
    if (f < 0) return;
    if (map == 0xffffu || room == 0xffffu || !map) {  /* V15.1: place not known yet (level loading) - never a "second" place */
        dev_events_emit("ITEM", "V151_PROGRESSIVE_PLACE_UNKNOWN", k_prog[f].name, 0, map, room, 0, "IGNORED", "LOCAL", 0); return; }
    int i = bracelet_place(map, room);
    pthread_mutex_lock(&mx); bracelet_local[f][i >> 3] |= (uint8_t)(1u << (i & 7)); pthread_mutex_unlock(&mx);
    dev_events_emit("ITEM", "V13_PROGRESSIVE_PLACE", k_prog[f].name, i, map, room, 1, "LOCAL", "LOCAL", 0);
}
void laonline_v12_bracelet_found(uint16_t map, uint16_t room) { laonline_v12_progressive_found(13, map, room); }
int laonline_v12_resource(struct Cpu *c, int item, int count) {
    cfg_load();
    if (!cfg_consumables || !coop_mode() || !save_ready() || count < 1 || count > 999) return 0;
    int value = 0;
    switch (item) {
    case 101: value = 1; break;  case 102: value = 5; break;   case 104: value = 20; break;
    case 105: value = 50; break; case 106: value = 100; break; case 108: value = 300; break;
    case 4: case 60: case 11: {  /* (the bag / bow / powder already held: laonline_coop.c checks it first) */
        uint32_t off = item == 4 ? BOMBS : item == 60 ? ARROWS : POWDER;
        int had = LD8(SAVE + off);
        native_grant(c, item, count, -1);
        int got = LD8(SAVE + off) - had;
        dev_events_emit("ITEM", item == 4 ? "V12_SHARED_BOMBS" : item == 60 ? "V12_SHARED_ARROWS" : "V12_SHARED_POWDER",
                        item == 4 ? "Bombs" : item == 60 ? "Arrows" : "Magic Powder", item, count, had, had + got,
                        got ? "NATIVE_ADD_ITEM" : "FULL", "REMOTE", 0);
        return 1;
    }
    default: return 0;
    }
    /* Rupees through AddItem as well, one call per rupee item (the count isn't the game's for rupees). His V12 wrote
     * the wallet because "the native AddItem from this hook is undone by the game a frame later (231 -> 236 -> 231)";
     * an AddItem from this same game-thread tick stayed on 1.0.1 (LARECOMP_GIVE_ITEM: 100 -> 156, kept 5 s). The next
     * ticks still check it (rupee_tick) and log a revert, without writing the wallet again. */
    uint32_t before = LD32(SAVE + RUPEES), next = before + (uint32_t)(value * count);
    if (next > 9999) next = 9999;
    if (next == before) {  /* V13: wallet already full. V12 logged every one of these as a "native revert" and rewrote
                            * the wallet 3 more times (Velvet / Sanic logs: all 461 "failures" were at 9999 rupees). */
        static uint64_t full_logged;
        uint64_t f = renderer_present_count();
        if (!full_logged || f - full_logged > 3600) { full_logged = f;
            dev_events_emit("ITEM", "V13_SHARED_RUPEES_WALLET_FULL", "Rupees", item, count, before, before, "NOTHING_TO_ADD", "REMOTE", 0); }
        return 1;
    }
    for (int i = 0; i < count; i++) native_grant(c, item, 1, -1);
    next = LD32(SAVE + RUPEES);
    rupee_check.before = before; rupee_check.expected = next; rupee_check.tries = 0; rupee_check.frame = renderer_present_count();
    dev_events_emit("ITEM", "V12_SHARED_RUPEES", "Rupees", item, count, before, next, "NATIVE_ADD_ITEM_VERIFY_PENDING", "REMOTE", 0);
    return 1;
}
static void rupee_tick(uint64_t frame) {
    if (!rupee_check.expected || frame < rupee_check.frame + 2) return;
    uint32_t now = LD32(SAVE + RUPEES);
    if (now == rupee_check.before)  /* (put back by the game: logged, not written again - its cause is the thing to find) */
        LOG("[laonline] shared rupees undone: %u -> %u -> %u\n", rupee_check.before, rupee_check.expected, now);
    dev_events_emit("ITEM", now == rupee_check.before ? "V12_SHARED_RUPEES_FAILED" : "V12_SHARED_RUPEES_VERIFIED", "Rupees", 0,
                    rupee_check.tries, rupee_check.before, now, now == rupee_check.before ? "NATIVE_REVERT" : "WALLET_READBACK", "REMOTE", 0);
    rupee_check.expected = 0;
}

/* ------------------------------------------------------------------ shared progress (game thread) */
static uint64_t last_scan;
/* V15.1 (Mr. Cursey: "I froze in Eagle Tower, in the second floor checker room with the little match the shapes
 * dudes. Raygar froze in Bottle Grotto"): a teammate cleared a battle / puzzle room (TboxPop_Vegas_L07_04B, then
 * DestroyBtl_L07_04A - Sanic's log 20:41:36 / 20:41:55; Bottle Grotto: KeyPop_Btl / TboxPop_Btl / DoorOpen_Btl) while
 * you were IN that room in your own world. The room's fight or puzzle was then finished under the game's feet and
 * Link was stuck. A shared flag that belongs to the room you are standing in now waits until you leave it - when you
 * come back, the room loads already cleared, like any room you cleared yourself. */
#include "laonline_flag_rooms.h"
static char cur_level[64], cur_room[96];
static int room_matches(const LaoFlagRoom *r) {
    size_t n = strlen(r->level);
    if (strncmp(cur_level, r->level, n) && strncmp(cur_room, r->level, n)) return 0;
    const char *u = strrchr(cur_room, '_'); if (!u) return 0;
    int num = 0; const char *q = u + 1; if (*q < '0' || *q > '9') return 0;
    while (*q >= '0' && *q <= '9') num = num * 10 + (*q++ - '0');
    return num == r->num && *q == r->letter;
}
static int flag_in_my_room(int f) {
    if (!cur_room[0]) return 0;
    for (unsigned i = 0; i < LAO_FLAG_ROOM_COUNT; i++) if (lao_flag_rooms[i].flag == f) return room_matches(&lao_flag_rooms[i]);
    return 0;
}
static uint8_t held_story[2048 / 8]; static uint32_t held_world[256]; static unsigned held_world_n;
static void hold_note(int f) {
    static uint8_t noted[WORLD_FLAGS / 8];
    if (f < 0 || f >= WORLD_FLAGS || ((noted[f >> 3] >> (f & 7)) & 1)) return;
    noted[f >> 3] |= (uint8_t)(1u << (f & 7));
    LOG("[laonline] V15.1 shared flag %d is for the room you are in (%s) - applied when you leave it\n", f, cur_room);
    dev_events_emit("STORY_FLAG", "V151_FLAG_HELD_IN_ROOM", "Shared flag waits until you leave the room", f, 0, 0, 0, "HELD", "REMOTE", 0);
}
static void progress_tick(struct Cpu *c, uint64_t frame) {
    if (!coop_mode() || !save_ready() || !laonline_level_ready() || laonline_ui_paused() || !laonline_coop_world_ready()) return;
    if (frame - last_scan < 20) return;
    last_scan = frame;
    static uint64_t last_resend;
    if (frame - last_resend >= 1800) {  /* every ~30 s: offer everything again (the relay ignores repeats; covers
                                         * frames it dropped while it was still verifying a Randomizer world) */
        last_resend = frame;
        pthread_mutex_lock(&mx); memset(flag_sent, 0, sizeof flag_sent); memset(item_sent, 0, sizeof item_sent); memset(soul_sent, 0, sizeof soul_sent); pthread_mutex_unlock(&mx);
    }
    uint8_t fin[sizeof flag_in]; int16_t iin[K_END - K_ITEMS];
    uint32_t ein[64]; unsigned en;
    static uint32_t win[1024]; unsigned wn;
    world_load();
    pthread_mutex_lock(&mx);
    wn = world_n; memcpy(win, world_in, wn * sizeof *win); world_n = 0;
    memcpy(fin, flag_in, sizeof fin); memset(flag_in, 0, sizeof flag_in);
    en = ext_n; memcpy(ein, ext_in, sizeof ein); ext_n = 0;
    memcpy(iin, item_in, sizeof iin);
    pthread_mutex_unlock(&mx);
    /* 1. apply what others reached (V13: each kind only if this player chose to receive it - Co-Op settings) */
    for (unsigned i = 0; i < sizeof fin; i++) { fin[i] |= held_story[i]; held_story[i] = 0; }  /* V15.1: held ones */
    if (cfg_story && g_settings.laonline_sync_story)
        for (size_t i = 0; i < LAO_V12_STORY_COUNT; i++) {
            int f = lao_v12_story_flags[i];
            if (!((fin[f >> 3] >> (f & 7)) & 1) || flags_ext_get(f)) continue;
            if (flag_in_my_room(f)) { held_story[f >> 3] |= (uint8_t)(1u << (f & 7)); hold_note(f); continue; }  /* V15.1 */
            flags_put(f, true);
            dev_events_emit("STORY_FLAG", flags_ext_get(f) ? "V12_PROGRESS_APPLIED" : "V12_PROGRESS_REJECTED", "Shared progress flag", f, 0, 0,
                            flags_ext_get(f), "NATIVE_FLAG_READBACK", "REMOTE", 0);
        }
    if (cfg_items && g_settings.laonline_shared_items) {
        uint32_t items = LD32(SAVE + ITEMS), want = items;
        for (int bit = 0; bit < 16; bit++) if (((ITEM_MIRROR_BITS >> bit) & 1) && iin[K_ITEMS + bit - K_ITEMS] > 0) want |= 1u << bit;
        if (want != items) {
            uint32_t added = want & ~items;
            for (int bit = 2; bit < 16; bit++)  /* (a bag, the powder: with 10, as before, when none is held) */
                if (((added >> bit) & 1) && k_item_bit_id[bit] >= 0)
                    native_grant(c, k_item_bit_id[bit], bit == 2 ? (LD8(SAVE + BOMBS) ? 1 : 10) : bit == 8 ? (LD8(SAVE + POWDER) ? 1 : 10) : 1,
                                 bit >= 13 ? bit - 13 : -1);
            if (((added >> 3) & 1) && !LD8(SAVE + ARROWS)) native_grant(c, 60, 10, -1);  /* (the bow's 10 arrows) */
            dev_events_emit("ITEM", "V12_ITEMS_SHARED", "Equipment bits", 0, 0, items, LD32(SAVE + ITEMS), "NATIVE_ADD_ITEM_READBACK", "REMOTE", 0);
            laonline_notify("Shared items received from your team!");
        }
        uint8_t up = LD8(SAVE + UPGRADES), up_want = up;
        for (int bit = 3; bit <= 5; bit++) if (iin[K_UPGRADES + bit - K_ITEMS] > 0) up_want |= (uint8_t)(1u << bit);
        for (int bit = 3; bit <= 5; bit++) if (((up_want & ~up) >> bit) & 1) native_grant(c, 120 + bit, 1, -1);
        if (iin[K_SENSOR - K_ITEMS] > 0 && !(LD8(SAVE + SENSOR) & 1)) native_grant(c, 116, 1, -1);
        uint8_t lv = LD8(SAVE + LEAVES), lv_want = lv;
        for (int bit = 0; bit < 5; bit++) if (iin[K_LEAVES + bit - K_ITEMS] > 0) lv_want |= (uint8_t)(1u << bit);
        for (int bit = 0; bit < 5; bit++) if (((lv_want & ~lv) >> bit) & 1) native_grant(c, 23, 1, bit);
        /* Trade sequence: Story Co-op only (Randomizer places trade items on its own). Highest step wins. */
        int trade = iin[K_TRADE - K_ITEMS];
        if (laonline_launch_choice() == 1 && trade_item(trade) >= 0 && LD8(SAVE + TRADE) < trade)
            native_grant(c, trade_item(trade), 1, -1);
    }
    if (cfg_items && g_settings.laonline_sync_upgrades)  /* V13: bracelet / shield / sword level from distinct places */
        for (int f = 0; f < 3; f++) {
            int places = 0, level_peer = iin[K_LEVEL + f - K_ITEMS];
            for (int i = 0; i < K_BRLOC_N; i++)
                if (iin[K_BRLOC + f * K_BRLOC_N + i - K_ITEMS] > 0 || ((bracelet_local[f][i >> 3] >> (i & 7)) & 1)) places++;
            int want = places > level_peer ? places : level_peer;
            if (want > 2) want = 2;
            if (prog_known[f] && g_settings.laonline_sync_world && laonline_launch_choice() == 2) {  /* V15.1 */
                int used = 0; for (int i = 0; i < prog_nflags[f]; i++) used += flags_ext_get(prog_flags[f][i]) ? 1 : 0;
                if (want > used && want > 1) {
                    static int capped_logged[3];
                    if (!capped_logged[f]) { capped_logged[f] = 1;
                        LOG("[laonline] V15.1 %s: %d place(s) reported but only %d of this seed's %s spots are used - level kept at %d\n",
                            k_prog[f].name, places, used, k_prog[f].name, used < 1 ? 1 : used);
                        dev_events_emit("ITEM", "V151_PROGRESSIVE_CAPPED", k_prog[f].name, want, places, used, used < 1 ? 1 : used, "SEED_PLACES", "REMOTE", 0); }
                    want = used < 1 ? 1 : used;
                }
            }
            uint32_t items = LD32(SAVE + ITEMS); uint8_t up = LD8(SAVE + UPGRADES);
            int have = !(items & (1u << k_prog[f].item_bit)) ? 0 : (up & (1u << k_prog[f].up_bit)) ? 2 : 1;
            if (want > have) {  /* (Lv1 its ITEMS bit, Lv2 its UPGRADES bit; the game sets the "got" bits too) */
                if (have < 1) native_grant(c, k_prog[f].lv1, 1, -1);
                if (want == 2) native_grant(c, k_prog[f].lv2, 1, -1);
                dev_events_emit("ITEM", "V13_PROGRESSIVE_LEVEL", k_prog[f].name, want, places, have, want, "NATIVE_ADD_ITEM", "REMOTE", 0);
                char note[96];
                if (want == 2) { snprintf(note, sizeof note, "%s - your team found both!", k_prog[f].lv2_name); laonline_notify(note); }
                else laonline_notify("Shared items received from your team!");
            }
        }
    int rando_coop = laonline_launch_choice() == 2;
    if (cfg_story && rando_coop && g_settings.laonline_sync_souls)  /* V13: souls others found */
        for (unsigned k = 0; k < en; k++)
            for (int i = 0, f; (f = souls_flag_at(i)) >= 0; i++)
                if ((uint32_t)f == ein[k] && !flags_ext_get(f)) {
                    flags_ext_set(f);
                    dev_events_emit("STORY_FLAG", "V13_SOUL_SHARED", "Boss / mini-boss soul", f, 0, 0, flags_ext_get(f), "NATIVE_FLAG_READBACK", "REMOTE", 0);
                }
    if (g_settings.laonline_sync_world) {  /* V13.5: pots / bushes / pickups / keys the team already used */
        int applied = 0;
        for (unsigned k = 0; k < held_world_n && wn < sizeof win / sizeof *win; k++) win[wn++] = held_world[k];  /* V15.1 */
        held_world_n = 0;
        for (unsigned k = 0; k < wn; k++) {
            int f = (int)win[k];
            if (!world_flag((uint32_t)f) || flags_ext_get(f) || (!rando_coop && !keyget_flag((uint32_t)f))) continue;
            if (flag_in_my_room(f)) {
                int dup = 0; for (unsigned h = 0; h < held_world_n; h++) dup |= held_world[h] == (uint32_t)f;
                if (!dup && held_world_n < sizeof held_world / sizeof *held_world) held_world[held_world_n++] = (uint32_t)f;
                hold_note(f); continue; }
            if (f < 2048) flags_put(f, true); else flags_ext_set(f);
            applied++;
            laonline_world_log("TEAM  %s in %s (flag %d) - now used for you too", world_kind_name(world_kind[f]), world_levels[world_level[f]], f);
        }
        if (applied) dev_events_emit("STORY_FLAG", "V135_WORLD_SHARED", "Pickup / pot / bush used by the team", 0, applied, 0, applied, "NATIVE_FLAG_WRITE", "REMOTE", 0);
    }
    /* 2. publish what we reached */
    pthread_mutex_lock(&mx);
    if (world_count)  /* V13.5: what we picked up / opened / cut (each flag once per connection; Story Co-op: keys only) */
        for (int f = 1; f < WORLD_FLAGS; f++)
            if (((world_is[f >> 3] >> (f & 7)) & 1) && !((world_sent[f >> 3] >> (f & 7)) & 1) && (rando_coop || keyget_flag((uint32_t)f)) && flags_ext_get(f)) {
                uint8_t fr[7] = {5, 0, 0x03}; uint32_t v = (uint32_t)f; memcpy(fr + 3, &v, 4);
                if (!push_frame(fr, 7)) break;  /* queue full: the rest go next scan */
                world_sent[f >> 3] |= (uint8_t)(1u << (f & 7));
                static uint8_t logged[WORLD_FLAGS / 8];
                if (!((logged[f >> 3] >> (f & 7)) & 1)) { logged[f >> 3] |= (uint8_t)(1u << (f & 7)); } else continue;
                laonline_world_log("YOU   %s in %s (flag %d) - shared with your team", world_kind_name(world_kind[f]), world_levels[world_level[f]], f);
            }
    if (cfg_items) {
        uint32_t items = LD32(SAVE + ITEMS); uint8_t up = LD8(SAVE + UPGRADES);
        for (int f = 0; f < 3; f++) {
            int level = !(items & (1u << k_prog[f].item_bit)) ? 0 : (up & (1u << k_prog[f].up_bit)) ? 2 : 1;
            if (level) send_item(K_LEVEL + f, level);
            for (int i = 0; i < K_BRLOC_N; i++) if ((bracelet_local[f][i >> 3] >> (i & 7)) & 1) send_item(K_BRLOC + f * K_BRLOC_N + i, 1);
        }
    }
    if (cfg_story && rando_coop)
        for (int i = 0, f; i < 32 && (f = souls_flag_at(i)) >= 0; i++)
            if (flags_ext_get(f) && !(soul_sent[i >> 3] >> (i & 7) & 1)) {
                uint8_t fr[7] = {5, 0, 0x03}; uint32_t v = (uint32_t)f; memcpy(fr + 3, &v, 4);
                if (push_frame(fr, 7)) soul_sent[i >> 3] |= (uint8_t)(1u << (i & 7));
            }
    if (cfg_story)
        for (size_t i = 0; i < LAO_V12_STORY_COUNT; i++)
            if (flags_ext_get(lao_v12_story_flags[i])) send_flag(lao_v12_story_flags[i]);
    if (cfg_items) {
        uint32_t items = LD32(SAVE + ITEMS);
        for (int bit = 0; bit < 16; bit++) if (((ITEM_MIRROR_BITS >> bit) & 1) && ((items >> bit) & 1)) send_item(K_ITEMS + bit, 1);
        uint8_t up = LD8(SAVE + UPGRADES);
        for (int bit = 3; bit <= 5; bit++) if ((up >> bit) & 1) send_item(K_UPGRADES + bit, 1);
        if (LD8(SAVE + SENSOR) & 1) send_item(K_SENSOR, 1);
        uint8_t lv = LD8(SAVE + LEAVES);
        for (int bit = 0; bit < 5; bit++) if ((lv >> bit) & 1) send_item(K_LEAVES + bit, 1);
        if (laonline_launch_choice() == 1 && LD8(SAVE + TRADE) && LD8(SAVE + TRADE) <= 14) send_item(K_TRADE, LD8(SAVE + TRADE));
    }
    pthread_mutex_unlock(&mx);
}

/* ------------------------------------------------------------------ final boss */
/* Remote Links that can't touch anything: laonline.c puppet_ghost() switches off every collider on the puppet. */
int laonline_v12_ghost_level(const char *level) {
    cfg_load();
    if (!level || !*level || !cfg_ghost) return 0;
    return cfg_ghost >= 2 || !strncmp(level, "Lv09", 4);
}
int laonline_v12_hide_puppet(const char *level) {
    cfg_load();
    if (!cfg_boss_hide || !level || strncmp(level, "Lv09", 4)) return 0;
    static int told;
    if (!told) {
        told = 1;
        laonline_notify("Wind Fish's Egg: other players are hidden here so the final boss can't get stuck on them.");
        LOG("[laonline] V12 final boss: remote Links hidden in %s (v12_final_boss_hide = 0 shows them)\n", level);
    }
    return 1;
}

/* ------------------------------------------------------------------ input (game thread) / keys (UI thread) */
static atomic_int key_f6, key_f7_down, key_lock;
static uint64_t assist_since;
static _Atomic uint64_t research_press_ms;
/* V13.5 research for the next PvP step (real bombs / arrows / knockback on the other screen): while PvP is on, every
 * native actor the game creates within 0.4 s of your B / X / Y press is written to laonline-pvp-research.log (actor
 * type + its spawn info). Read-only; at most 300 lines per session. Testers send this file so the exact actor types
 * and spawn data of Bomb / Arrow / Boomerang / Hookshot / Magic Rod / Powder can be used to spawn them for others. */
void laonline_v12_actor_created(uint32_t type, uint64_t info) {
    static int lines;
    if (!atomic_load(&pvp_on_of[0]) || !info || lines >= 300) return;
    uint64_t t = atomic_load(&research_press_ms), now = ms_now();
    if (!t || now - t > 400) return;
    FILE *f = fopen("laonline-pvp-research.log", "a"); if (!f) return;
    int x = -1, y = -1;
    if (g_ga) { uint64_t ps = LD64(GA(PLAYER_STATUS_PTR));
        if ((ps >= MAIN_BASE && ps + 0x100 < SDK_BASE) || (ps >= HEAP_BASE && ps + 0x100 < HEAP_BASE + HEAP_MAX)) { x = LD8(ps + 0xa3); y = LD8(ps + 0xa2); } }
    fprintf(f, "t=%llu dt=%llums type=0x%x equipX=%d equipY=%d info=", (unsigned long long)now, (unsigned long long)(now - t), type, x, y);
    for (int i = 0; i < 0x200; i++) fprintf(f, "%02x", LD8(info + (uint64_t)i));
    fputc('\n', f); fclose(f); lines++;
}
/* V13.5 PvP item attacks: X / Y presses, resolved in pvp_tick with Link's position and facing */
typedef struct { int item; float range, cone; int delay_ms, quarters; const char *name; float ahead; } PvpAttack;
/* item ids = the equip byte values (dpad_shortcuts.c predicates); 100+ = moves that are not one item press (V14) */
enum { PVP_SWORD = 100, PVP_SPIN = 101, PVP_BOMB_ARROW = 102, PVP_DASH = 103, PVP_STOMP = 104 };
static const PvpAttack k_pvp_items[] = {
    {2, 2.6f, -2.f, 1500, 4, "Bomb", .9f},  /* blast around where it was placed (in front of Link), ~1.5 s later */
    {3, 9.0f, .85f, 150, 2, "Arrow", 0},
    {4, 7.0f, .90f, 100, 1, "Hookshot", 0},
    {5, 5.5f, .90f, 150, 2, "Boomerang", 0},  /* V15.1: was 7.0 wide (Mr. Cursey: "the hitbox is very large") */
    {6, 9.0f, .85f, 200, 4, "Magic Rod", 0},
    {8, 2.6f, .40f, 0, 1, "Magic Powder", 0},
};
/* V14: every weapon - the sword (Noble Sword hits twice as hard), the spin attack (hold B, let go), bomb arrows
 * (Bow + Bombs together), the Pegasus Boots dash and a Roc's Feather stomp (land on someone). */
static const PvpAttack k_pvp_moves[] = {
    {PVP_SPIN, 2.7f, -2.f, 0, 4, "Spin attack", 0},
    {PVP_BOMB_ARROW, 9.0f, .85f, 150, 6, "Bomb arrow", 0},
    {PVP_STOMP, 1.2f, -2.f, 480, 2, "Feather stomp", 0},
};
/* V15.1 (Mr. Cursey: "Boomerang has NO cooldown"): an item can only hit again once the real one could be used again -
 * pressing the button while the boomerang / hookshot is still out did a new hit every press. */
static int pvp_cooldown_ms(int item) {
    switch (item) { case 2: return 900; case 3: return 350; case 4: return 900; case 5: return 1200; case 6: return 500; case 8: return 500;
                    case PVP_BOMB_ARROW: return 900; case PVP_STOMP: return 700; default: return 0; }
}
static const PvpAttack *pvp_move(int id) {
    for (unsigned k = 0; k < sizeof k_pvp_moves / sizeof *k_pvp_moves; k++) if (k_pvp_moves[k].item == id) return &k_pvp_moves[k];
    return NULL;
}
typedef struct { const PvpAttack *a; uint64_t at, made; float x, y, z, fx, fz; unsigned id; int quarters; } PvpShot;
/* (LAS Recomp Co-Op's port) One of this PC's players in PvP - what V15.2 kept for Player 1 alone, now for each:
 * [0] Player 1, [1..7] local co-op players 2..8 (each on his own relay connection, laonline_local.c). */
typedef struct {
    uint64_t prev_buttons, r3_since, swing_until, b_down_at, x_down_at, y_down_at;  /* his buttons (the held: mx) */
    unsigned swing_id, shown_swing, hit_swing[LAO_PLAYERS];
    int spin_req, item_press_slot[4], item_press_n;  /* (mx) */
    float face_x, face_z, last_me[3]; int have_last;  /* facing = the way he last moved (or his Z-target) */
    PvpShot shots[8]; int shot_n;
    uint64_t ready_at[128];  /* V15.1 cooldowns */
    uint64_t invulnerable_until, knock_until, knock_last, stagger_until, flash_until;
    float knock_dx, knock_dz;
    float dash_last[3]; uint64_t dash_t, dash_shown; unsigned dash_hit[LAO_PLAYERS], dash_seq; int dashing;
    int sent_on, sent_id;  /* (players 2..8: the switch his connection last sent, and as which relay id) */
} PvpSelf;
static PvpSelf g_pvp[PVP_SELVES];
static atomic_int pvp_toggle_req[PVP_SELVES];  /* (players 2..8: their R3; Player 1's is key_f6, F6 too) */
static unsigned shot_seq;
static uint64_t ko_watch_until;  /* the game's hearts at 0 from a PvP hit: waiting for its death (pvp_tick) */
static void pvp_selves_reset(void) {
    memset(g_pvp, 0, sizeof g_pvp);
    for (int s = 0; s < PVP_SELVES; s++) g_pvp[s].face_z = 1, g_pvp[s].sent_on = g_pvp[s].sent_id = -1, atomic_store(&pvp_toggle_req[s], 0);
}
static int pvp_cooldown_ok(PvpSelf *p, int item, uint64_t now) {
    int ms = pvp_cooldown_ms(item); if (item < 0 || item >= 128) return 1;
    if (now < p->ready_at[item]) return 0;
    p->ready_at[item] = now + (uint64_t)ms; return 1;
}
int laonline_v12_effects(int *item, float pos[][3], float dir[][2], float *age_s, uint16_t *map, int cap) {  /* overlay */
    int n = 0; uint64_t now = ms_now();
    pthread_mutex_lock(&mx);
    for (unsigned i = 0; i < 16 && n < cap; i++) {
        PvpFx *f = &fx_ring[i]; if (!f->start || now - f->start > 2500 || f->map != fx_my_map) continue;
        item[n] = f->item; memcpy(pos[n], f->pos, 12); memcpy(dir[n], f->dir, 8); age_s[n] = (now - f->start) / 1000.f; map[n] = f->map; n++;
    }
    pthread_mutex_unlock(&mx);
    return n;
}
/* V13.5 hit reaction (we can't call the game's own knockback yet): a short stagger - your stick is ignored for a
 * moment - a red screen flash and a controller rumble. Read by input.c and the overlay. (Players 2..8: their stick,
 * input.c coop_fill; the flash and the rumble are Player 1's screen and pad.) */
int laonline_v12_stagger(void) { return ms_now() < g_pvp[0].stagger_until; }
int laonline_v12_stagger_of(int s) { return s >= 0 && s < PVP_SELVES && ms_now() < g_pvp[s].stagger_until; }
float laonline_v12_hit_flash(void) { uint64_t n = ms_now(); return n < g_pvp[0].flash_until ? (float)(g_pvp[0].flash_until - n) / 450.f : 0.f; }
void laonline_v12_key(int fkey, int down, int repeat) {
    if (fkey == 6 && down && !repeat) atomic_store(&key_f6, 1);
    if (fkey == 7) atomic_store(&key_f7_down, down ? 1 : 0);
    if (fkey == 8 && down && !repeat) { extern void ztarget_toggle(void); ztarget_toggle(); }  /* F8: Z-target (V15) */
}
/* The buttons of this PC's player s as the game is given them: B a sword swing (held 0.8 s and let go: a spin
 * attack), X / Y the item there, R3 a tap his PvP switch. */
static void pvp_input(int s, uint64_t buttons, int menu) {
    PvpSelf *p = &g_pvp[s];
    uint64_t now = ms_now(), pressed = buttons & ~p->prev_buttons, released = p->prev_buttons & ~buttons;
    p->prev_buttons = buttons;
    if (menu) { p->r3_since = 0; return; }
    int on = atomic_load(&pvp_on_of[s]);
    if (pressed & (1ull << 1)) { p->swing_until = now + 260; p->swing_id++; }  /* B: sword */
    pthread_mutex_lock(&mx);
    if (pressed & (1ull << 1)) p->b_down_at = now;
    if (released & (1ull << 1)) {  /* V14: B held 0.8 s+ then let go = spin attack */
        if (p->b_down_at && now - p->b_down_at >= 800 && on) p->spin_req = 1;
        p->b_down_at = 0;
    }
    if (pressed & (1ull << 2)) p->x_down_at = now;
    if (released & (1ull << 2)) p->x_down_at = 0;
    if (pressed & (1ull << 3)) p->y_down_at = now;
    if (released & (1ull << 3)) p->y_down_at = 0;
    /* V13.5 PvP: X / Y - which item is equipped there is read on the game thread */
    if ((pressed & (3ull << 2)) && on && p->item_press_n < 4) p->item_press_slot[p->item_press_n++] = (pressed & (1ull << 2)) ? 0xa3 : 0xa2;
    pthread_mutex_unlock(&mx);
    if (!s && (pressed & (7ull << 1)) && on) research_press_ms = now;  /* B / X / Y: see actor_created */
    if (pressed & (1ull << 5)) p->r3_since = now;                      /* R3 */
    if ((released & (1ull << 5)) && p->r3_since) {
        if (now - p->r3_since < 450 && pvp_mode() && cfg_pvp) atomic_store(s ? &pvp_toggle_req[s] : &key_f6, 1);
        p->r3_since = 0;
    }
}
uint64_t laonline_v12_input(uint64_t buttons, int menu) {  /* Player 1 (input.c) */
    uint64_t pressed = buttons & ~g_pvp[0].prev_buttons;
    pvp_input(0, buttons, menu);
    if (!menu && (pressed & (1ull << 4))) { extern void ztarget_toggle(void); ztarget_toggle(); }  /* L3: Z-target (V15; ZL too) */
    return buttons;
}
/* (LAS Recomp Co-Op's port) players 2..8 (input.c coop_fill: player = 1 for player 2) */
uint64_t laonline_v12_input_of(int s, uint64_t buttons, int menu) {
    if (s >= 1 && s < PVP_SELVES) pvp_input(s, buttons, menu);
    return buttons;
}
/* (LAS Recomp Co-Op's port) a PvP hit on player s (1..7) of this PC, from his own connection (laonline_local.c) */
void laonline_v12_hit_for(int s, int from, int quarters) {
    if (s < 1 || s >= PVP_SELVES || from < 0 || from >= LAO_PLAYERS) return;
    pthread_mutex_lock(&mx);
    if (hit_n[s] < 16) hit_from[s][hit_n[s]] = from, hit_amount[s][hit_n[s]++] = (uint8_t)quarters;
    pthread_mutex_unlock(&mx);
}

/* ------------------------------------------------------------------ PvP (game thread) */
int laonline_v12_pvp_local(void) { return pvp_mode() && atomic_load(&pvp_on_of[0]); }
int laonline_v12_pvp_on(int s) { return pvp_mode() && s >= 0 && s < PVP_SELVES && atomic_load(&pvp_on_of[s]); }  /* (ztarget.c) */
int laonline_v12_pvp_peer(int id) { return pvp_mode() && id >= 0 && id < LAO_PLAYERS && atomic_load(&pvp_peer[id]); }
/* V15.1 (Velvet: "the heart on the HUD looks kind of funny", "sometimes when I die, it doesn't trigger the death
 * animation"): Link's hearts live twice - the save (what the HUD's saved value / Game Over read) and PlayerStatus+0x84
 * (what the game plays with: Link's death, heart pickups). PvP wrote only the save, so the two disagreed (6 -> 10 in
 * Sanic's log) and a KO could reach the Game Over without the game's own death. (LAS Recomp Co-Op's port: PlayerStatus
 * is in the live save itself - its +0x84 is the save's 0x5544, docs/save-format.md - so it is the one value, taken as
 * the game's own damage takes it: pvp_take.) */
static uint64_t pvp_status(void) {
    uint64_t ps = g_ga && GA(PLAYER_STATUS_PTR) ? LD64(GA(PLAYER_STATUS_PTR)) : 0;
    return ps && ((ps >= MAIN_BASE && ps + 0x100 < SDK_BASE) || (ps >= HEAP_BASE && ps + 0x100 < HEAP_BASE + HEAP_MAX)) ? ps : 0;
}
static int pvp_hp(void) { uint64_t ps = pvp_status(); return ps ? (int16_t)LD16(ps + 0x84) : (int16_t)LD16(SAVE + HP); }
/* (LAS Recomp Co-Op's port) A PvP hit of `q` quarter hearts on this PC's player s, taken as the game takes its own hits
 * (LAS Recomp's review, point 2: no hearts written of its own): the difficulty setting first (rt_damage: Hero Mode
 * doubles it), then local co-op's split (rt_coop_damage_of, as at the game's damage sites: with "own hearts" a player
 * 2..8's hit comes off his own and knocks him out at 0, for a revive), then what is left off the game's hearts as its
 * damage command takes it (main+0xd7c870: PlayerStatus+0x84 less the hit, not below 0). keep_one: a relay without
 * knock-outs (older than V13.5) - the game's hearts stop at a quarter heart, as V15.2 had it. Returns the game's hearts
 * after (-1: not touched). */
static int pvp_take(struct Cpu *c, int s, int q, int keep_one) {
    extern int32_t rt_damage(int32_t, int32_t); extern int rt_hero_mode(void);
    extern int32_t rt_coop_damage_of(struct Cpu *, int32_t, uint64_t); extern uint64_t coop_player(int);
    int dmg = rt_coop_damage_of(c, rt_damage(q, rt_hero_mode()), coop_player(s));
    uint64_t ps = pvp_status();
    if (dmg <= 0 || !ps) return -1;
    int hp = (int16_t)LD16(ps + 0x84), next = hp - dmg < 0 ? 0 : hp - dmg;
    if (keep_one && next < 1) next = hp < 1 ? hp : 1;
    ST16(ps + 0x84, (uint16_t)next);
    return next;
}
/* V15.1 (Mr. Cursey: "We can also attack while dead"): no attacks while your hearts are 0 / the game's death runs
 * (players 2..8 with their own hearts: while knocked out) */
static int pvp_self_down(int s) {
    extern int laonline_death_active(void), coop_player_down(int);
    return !save_ready() || pvp_hp() <= 0 || ko_watch_until || laonline_death_active() || (s && coop_player_down(s));
}
/* (LAS Recomp Co-Op's port) this PC's player s: his Link's PlayerCollision (his position at +0x80) - Player 1's as
 * V15.2 found it, the others' as laonline_local.c does; 0: not in the game */
static uint64_t self_collision(int s, uint64_t frame) {
    extern uint64_t doorjump_player(uint64_t frame), coop_player(int); extern uint32_t coop_joined(void);
    if (!s) return doorjump_player(frame);
    if (!(coop_joined() >> s & 1)) return 0;
    uint64_t link = coop_player(s), col = link ? LD64(link + 0x80) : 0;
    return col >= HEAP_BASE && col < HEAP_BASE + HEAP_MAX - 0xc0 && LD64(col) == GA(PLAYER_COLLISION_VT) ? col : 0;
}
static int self_relay_id(int s) {  /* (-1: not on the relay) */
    extern int laonline_local_relay_id(int);
    if (s) return laonline_local_relay_id(s);
    float tmp[3]; uint16_t tm; int my_id = -1;
    for (int id = 0; id < LAO_PLAYERS && my_id < 0; id++) { int l = -1; if (laonline_v12_peer(id, tmp, &tm, &l)) my_id = l; }
    return my_id;
}
static int pvp_send(int s, const uint8_t *f, int n) {  /* (mx held) Player 1's connection or player s's own */
    extern int laonline_local_push(int, const uint8_t *, int);
    return s ? laonline_local_push(s, f, n) : push_frame(f, n);
}
/* V15 PvP hit reaction (user's videos: Link gets knocked back like an enemy hit): pushed away from the attacker for
 * 0.25 s (PlayerCollision position, as souls.c moves him), turned to face the attacker, the game's own damage animation
 * once it was learned from an enemy hit (runtime/hle/ztarget.c), plus the V13.5 stagger / red flash / rumble. */
static void knock_tick(int s, uint64_t frame) {
    PvpSelf *p = &g_pvp[s];
    if (!p->knock_until) return;
    uint64_t now = ms_now();
    if (now >= p->knock_until) { p->knock_until = 0; return; }
    uint64_t pc = self_collision(s, frame);
    if (!pc) return;
    float dt = (float)(now - p->knock_last) / 1000.f; p->knock_last = now;
    if (dt <= 0 || dt > .1f) return;
    float *pos = (float *)(uintptr_t)(pc + 0x80), *pos2 = (float *)(uintptr_t)(pc + 0xb0), speed = 6.4f;  /* ~1.6 in 0.25 s */
    pos[0] = pos2[0] = pos[0] + p->knock_dx * speed * dt;
    pos[2] = pos2[2] = pos[2] + p->knock_dz * speed * dt;
}
/* The hits player s took (from the relay: who, how much), taken as the game's own (pvp_take). */
static void pvp_hits(struct Cpu *c, int s, uint64_t frame, const float me[3], int have_me, int ko_ok, uint64_t now) {
    PvpSelf *p = &g_pvp[s];
    int amounts[16], froms[16], n;
    pthread_mutex_lock(&mx);
    n = hit_n[s]; for (int i = 0; i < n; i++) amounts[i] = hit_amount[s][i], froms[i] = hit_from[s][i]; hit_n[s] = 0;
    pthread_mutex_unlock(&mx);
    extern int coop_player_down(int);
    for (int i = 0; i < n; i++) {
        if (!atomic_load(&pvp_on_of[s]) || (s && !have_me) || !save_ready() || laonline_ui_paused() || now < p->invulnerable_until ||
            pvp_self_down(s)) continue;  /* (players 2..8: only while in the game and on the relay) */
        int hp = pvp_hp();
        if (hp <= 0 || (hp <= 1 && !ko_ok)) continue;
        int dmg = amounts[i] < 1 ? 1 : amounts[i] > 8 ? 8 : amounts[i];
        if (!s) { extern void ztarget_note_pvp_hit(void); ztarget_note_pvp_hit(); }
        int after = pvp_take(c, s, dmg, !ko_ok);
        int knocked = (s && coop_player_down(s)) || after == 0;
        if (knocked) {
            /* V13.5 PvP death (user: "an actual death animation and gameover"): hearts go to 0 like any lethal hit, so
             * the game itself plays Link's death and its Game Over (or a bottled fairy saves him - the game decides).
             * The attacker gets the kill (kill feed + counter). Safety net: if the game has NOT started its Game Over
             * within 6 s while the hearts are still 0, Link is put back at full hearts (never stuck at 0). (Players
             * 2..8 with their own hearts: knocked out as co-op does, for a revive.) */
            p->knock_until = 0;  /* (nothing of ours in the way of the game's death) */
            if (after == 0) ko_watch_until = now + 6000;
            p->invulnerable_until = now + 6000;
            p->stagger_until = now + 400;
            if (!s) { p->flash_until = now + 1200; extern void input_hit_rumble(int ms); input_hit_rumble(600); }
            uint8_t f[5] = {3, 0, LAO_C_PVP, 3, (uint8_t)froms[i]};
            pthread_mutex_lock(&mx); pvp_send(s, f, 5); pthread_mutex_unlock(&mx);
            LOG("[laonline] V13.5 PvP: player %d knocked out by player #%d's hit%s\n", s + 1, froms[i],
                after == 0 ? " - waiting for the game's death / Game Over" : " (co-op: down for a revive)");
            dev_events_emit("PVP", "V135_PVP_DEATH", "PvP", 0, dmg, hp, 0, "HEALTH_WRITE", "REMOTE", 0);
            continue;
        }
        p->invulnerable_until = now + 1000;  /* V15.1: about the game's own invincibility after a hit (was 0.7 s) */
        p->stagger_until = now + 250;
        if (!s) p->flash_until = now + 450;
        { extern int ztarget_yaw_toward(float, float, float *); extern uint16_t ztarget_damage_anim(void);
          extern int laonline_link_set_yaw(struct Cpu *, uint64_t, float); extern int laonline_local_play_anim(struct Cpu *, uint16_t);
          extern uint64_t coop_player(int);
          float ap[3]; uint16_t amap; int alocal;
          if (have_me && laonline_v12_peer(froms[i], ap, &amap, &alocal)) {
              float dx = me[0] - ap[0], dz = me[2] - ap[2], d = sqrtf(dx * dx + dz * dz), yaw;
              if (d > .05f) { p->knock_dx = dx / d; p->knock_dz = dz / d; p->knock_until = now + 250; p->knock_last = now;
                  if (ztarget_yaw_toward(-dx, -dz, &yaw)) laonline_link_set_yaw(c, coop_player(s), yaw); }  /* face the one who hit him */
          }
          uint16_t anim = !s && cfg_pvp_anim ? ztarget_damage_anim() : 0;  /* V15.1: off by default (it froze Link) */
          if (anim && laonline_local_play_anim(c, anim)) LOG("[laonline] V15 PvP hit: played Link's damage animation %u\n", anim);
        }
        if (!s) { extern void input_hit_rumble(int ms); input_hit_rumble(220); }
        if (!s) laonline_notify(dmg >= 4 ? "PvP: you were hit! (-1 heart)" : dmg >= 2 ? "PvP: you were hit! (-1/2 heart)" : "PvP: you were hit! (-1/4 heart)");
        else { char t[64]; snprintf(t, sizeof t, "PvP: P%d was hit! (-%s heart)", s + 1, dmg >= 4 ? "1" : dmg >= 2 ? "1/2" : "1/4"); laonline_notify(t); }
        LOG("[laonline] PvP: player %d hit by player #%d for %d/4 (the game's hearts %d -> %d)\n", s + 1, froms[i], dmg, hp, after < 0 ? hp : after);
        dev_events_emit("PVP", "V12_PVP_HIT_TAKEN", "PvP", 0, dmg, hp, after < 0 ? hp : after, "HEALTH_WRITE", "REMOTE", 0);
    }
    (void)frame;
}
/* The attacks player s makes: each one (1) everyone else is sent what to DRAW (kind 2 visual frame) and (2) after its
 * delay we check who it reaches and send the hit (kind 1, with damage) for the server to pass on - on his own
 * connection, so the relay names him the attacker. */
static void pvp_attacks(int s, const float me[3], uint16_t my_map, int my_id, int sword_lv, uint64_t ps, uint64_t now) {
    PvpSelf *p = &g_pvp[s];
    int presses[4]; int pn; pthread_mutex_lock(&mx); pn = p->item_press_n; memcpy(presses, p->item_press_slot, sizeof presses); p->item_press_n = 0; pthread_mutex_unlock(&mx);
    if (pvp_self_down(s)) {  /* V15.1: knocked out - nothing you press reaches anyone, queued attacks are dropped */
        p->shot_n = 0; p->swing_until = 0;
        pthread_mutex_lock(&mx); p->spin_req = 0; pthread_mutex_unlock(&mx);
        return;
    }
    #define PVP_SHOW(item_) do { if (relay_pvp135) { \
        uint8_t f_[21] = {19, 0, LAO_C_PVP, 2, (uint8_t)(item_)}; memcpy(f_ + 5, &my_map, 2); memcpy(f_ + 7, me, 12); \
        f_[19] = (uint8_t)(int8_t)lrintf(p->face_x * 100.f); f_[20] = (uint8_t)(int8_t)lrintf(p->face_z * 100.f); \
        pthread_mutex_lock(&mx); pvp_send(s, f_, 21); pthread_mutex_unlock(&mx); } } while (0)
    #define PVP_QUEUE(a_, q_) do { if (p->shot_n < 8) p->shots[p->shot_n++] = (PvpShot){(a_), now + (uint64_t)(a_)->delay_ms, now, me[0], me[1], me[2], p->face_x, p->face_z, ++shot_seq, (q_)}; } while (0)
    #define PVP_HIT(id_, q_, item_) do { \
        uint8_t f_[7] = {5, 0, LAO_C_PVP, 1, (uint8_t)(id_), (uint8_t)(q_), (uint8_t)(item_)};  /* V15: + the weapon (kill feed) */ \
        pthread_mutex_lock(&mx); if (!relay_pvpcoop) f_[0] = 4; pvp_send(s, f_, relay_pvpcoop ? 7 : 6); if (!relay_pvpko) hitmark_add(my_id, (id_), (q_)); pthread_mutex_unlock(&mx); } while (0)
    for (int i = 0; i < pn; i++) {
        int item = LD8(ps + presses[i]);
        const PvpAttack *a = NULL;
        for (unsigned k = 0; k < sizeof k_pvp_items / sizeof *k_pvp_items; k++) if (k_pvp_items[k].item == item) a = &k_pvp_items[k];
        if (!a) {
            if (item == 9 && pvp_cooldown_ok(p, PVP_STOMP, now)) { PVP_QUEUE(pvp_move(PVP_STOMP), 2); PVP_SHOW(PVP_STOMP); }  /* Roc's Feather: land on them */
            continue;
        }
        /* Bow + Bombs pressed together (within 0.25 s, either order) = a bomb arrow */
        int combo = -1;
        for (int s2 = 0; s2 < p->shot_n; s2++)
            if (now - p->shots[s2].made <= 250 && now < p->shots[s2].at &&
                ((item == 3 && p->shots[s2].a->item == 2) || (item == 2 && p->shots[s2].a->item == 3))) combo = s2;
        if (combo >= 0) {
            p->shots[combo] = p->shots[--p->shot_n];
            PVP_QUEUE(pvp_move(PVP_BOMB_ARROW), 6); PVP_SHOW(PVP_BOMB_ARROW);
            continue;
        }
        if (!pvp_cooldown_ok(p, item, now)) continue;  /* V15.1: still out / reloading - no new hit */
        PVP_QUEUE(a, a->quarters); PVP_SHOW(item);
    }
    /* sword: a slash everyone sees (the hit itself is the swing check below) */
    if (now <= p->swing_until && p->shown_swing != p->swing_id) { p->shown_swing = p->swing_id; PVP_SHOW(PVP_SWORD); }
    /* spin attack: B charged and let go */
    int spin; pthread_mutex_lock(&mx); spin = p->spin_req; p->spin_req = 0; pthread_mutex_unlock(&mx);
    if (spin) { PVP_QUEUE(pvp_move(PVP_SPIN), 4 * sword_lv); PVP_SHOW(PVP_SPIN); }
    /* Pegasus Boots dash: the boots button held 0.35 s+ while Link is running fast -> rams whoever is in front */
    uint64_t xd, yd; pthread_mutex_lock(&mx); xd = p->x_down_at; yd = p->y_down_at; pthread_mutex_unlock(&mx);
    int boots_held = (xd && now - xd >= 350 && LD8(ps + 0xa3) == 11) || (yd && now - yd >= 350 && LD8(ps + 0xa2) == 11);
    float speed = 0;
    if (p->dash_t && now > p->dash_t) { float dx = me[0] - p->dash_last[0], dz = me[2] - p->dash_last[2]; speed = sqrtf(dx * dx + dz * dz) * 1000.f / (float)(now - p->dash_t); }
    memcpy(p->dash_last, me, 12); p->dash_t = now;
    int dash_now = boots_held && speed > 4.f && speed < 60.f;
    if (dash_now && !p->dashing) p->dash_seq++;
    p->dashing = dash_now;
    if (p->dashing) {
        if (now - p->dash_shown > 300) { p->dash_shown = now; PVP_SHOW(PVP_DASH); }
        for (int id = 0; id < LAO_PLAYERS && relay_pvp135; id++) {
            float o[3]; uint16_t map; int local;
            if (!atomic_load(&pvp_peer[id]) || p->dash_hit[id] == p->dash_seq || !laonline_v12_peer(id, o, &map, &local) || id == local || map != my_map) continue;
            float dx = o[0] - me[0], dz = o[2] - me[2], d = sqrtf(dx * dx + dz * dz);
            if (d > 1.7f || fabsf(o[1] - me[1]) > 1.5f || (d > .3f && (dx * p->face_x + dz * p->face_z) / d < .3f)) continue;
            p->dash_hit[id] = p->dash_seq;
            PVP_HIT(id, LD32(SAVE + ITEMS) & 1 ? 4 * sword_lv : 2, PVP_DASH);
            LOG("[laonline] V14 PvP Boots dash (player %d) reached player #%d\n", s + 1, id);
        }
    }
    for (int k = 0; k < p->shot_n;) {
        if (now < p->shots[k].at) { k++; continue; }
        PvpShot sh = p->shots[k]; p->shots[k] = p->shots[--p->shot_n];
        if (!relay_pvp135) continue;  /* older server: sword only */
        float cx = sh.x + sh.fx * sh.a->ahead, cz = sh.z + sh.fz * sh.a->ahead;  /* (a bomb: where it was put down) */
        for (int id = 0; id < LAO_PLAYERS; id++) {
            if (!atomic_load(&pvp_peer[id])) continue;
            float o[3]; uint16_t map; int local;
            if (!laonline_v12_peer(id, o, &map, &local) || id == local || map != my_map) continue;
            float dx = o[0] - cx, dy = o[1] - sh.y, dz = o[2] - cz, d = sqrtf(dx * dx + dz * dz);
            if (d > sh.a->range || fabsf(dy) > 2.f) continue;
            if (sh.a->cone > -1.5f && d > .5f && (dx * sh.fx + dz * sh.fz) / d < sh.a->cone) continue;  /* not in front */
            int q = sh.quarters < 1 ? 1 : sh.quarters > 8 ? 8 : sh.quarters;
            PVP_HIT(id, q, sh.a->item);
            LOG("[laonline] V13.5 PvP %s (player %d) reached player #%d\n", sh.a->name, s + 1, id);
        }
    }
    if (now <= p->swing_until)
        for (int id = 0; id < LAO_PLAYERS; id++) {
            if (!atomic_load(&pvp_peer[id]) || p->hit_swing[id] == p->swing_id) continue;
            float o[3]; uint16_t map; int local;
            if (!laonline_v12_peer(id, o, &map, &local) || id == local || map != my_map) continue;
            float dx = o[0] - me[0], dy = o[1] - me[1], dz = o[2] - me[2];
            if (dx * dx + dz * dz > 1.9f * 1.9f || fabsf(dy) > 1.5f) continue;
            p->hit_swing[id] = p->swing_id;
            if (relay_pvp135) PVP_HIT(id, 2 * sword_lv, PVP_SWORD);  /* V14: sword damage by level (Noble Sword = 1 heart) */
            else {
                uint8_t f[5] = {3, 0, LAO_C_PVP, 1, (uint8_t)id};
                pthread_mutex_lock(&mx); pvp_send(s, f, 5); if (!relay_pvpko) hitmark_add(my_id, id, 2 * sword_lv); pthread_mutex_unlock(&mx);
            }
            LOG("[laonline] V12 PvP swing (player %d) reached player #%d\n", s + 1, id);
        }
    #undef PVP_SHOW
    #undef PVP_QUEUE
    #undef PVP_HIT
}
static int self_ids[PVP_SELVES];  /* (each frame: self_relay_id) */
static int pvp_self_of_id(int id) {  /* this PC's player whose relay id this is (-1: someone else's) */
    for (int s = 0; s < PVP_SELVES; s++) if (id >= 0 && self_ids[s] == id) return s;
    return -1;
}
static void pvp_tick(struct Cpu *c, uint64_t frame, const char *level, const float me[3], int have_me) {
    for (int s = 0; s < PVP_SELVES; s++) {  /* each player's PvP switch: Player 1's R3 / F6, the others' R3 */
        if (!(s ? atomic_exchange(&pvp_toggle_req[s], 0) : atomic_exchange(&key_f6, 0)) || !cfg_pvp) continue;
        if (!pvp_mode()) { laonline_notify("PvP is available when you play online."); continue; }
        int on = !atomic_load(&pvp_on_of[s]);
        atomic_store(&pvp_on_of[s], on);
        int relay;
        pthread_mutex_lock(&mx); relay = relay_v12 && (global_mode() || relay_pvpcoop); pthread_mutex_unlock(&mx);
        char t[96];
        if (s) snprintf(t, sizeof t, "P%d: PvP %s", s + 1, on ? "ON" : "OFF");
        laonline_notify(s ? t : on ? (relay ? "PvP ON - red players can hit each other. R3 / F6 turns it off."
                                            : "PvP ON - but this server is not updated for PvP yet (host: update the relay).")
                                   : "PvP OFF");
        LOG("[laonline] V12 PvP %s (player %d)\n", on ? "on" : "off", s + 1);
    }
    if (!pvp_mode()) { atomic_store(&ending_req, 0); return; }
    if (!global_mode()) atomic_store(&ending_req, 0);  /* (the "saved Koholint" announcement is Global's) */
    else {   /* V14: you finished the game in Global -> everyone in Global gets "<you> saved Koholint!" (once per ending) */
        static int ending_sent;
        int at_end = atomic_exchange(&ending_req, 0) || (level && !strcmp(level, "Ending"));
        if (!at_end && level && level[0] && strcmp(level, "Ending")) ending_sent = 0;
        int cap; pthread_mutex_lock(&mx); cap = relay_ending; pthread_mutex_unlock(&mx);  /* (an older relay would drop us for kind 5) */
        if (at_end && !ending_sent && cap) {
            ending_sent = 1;
            uint8_t f[5] = {3, 0, LAO_C_PVP, 5, (uint8_t)(laonline_v12_global_is_randomizer() ? 1 : 0)};
            pthread_mutex_lock(&mx); push_frame(f, 5); pthread_mutex_unlock(&mx);
            LOG("[laonline] V14 Global: you finished the game - telling everyone in Global\n");
        }
    }
    int ko_ok, pvp_ok;
    pthread_mutex_lock(&mx);
    ko_ok = relay_pvpko;
    pvp_ok = relay_v12 && (global_mode() || relay_pvpcoop);  /* (as laonline_v12_packet sends Player 1's) */
    pthread_mutex_unlock(&mx);
    uint64_t now = ms_now();
    /* where each of this PC's players is: Player 1 as V15.2 had him (me), the others their Link's collision */
    float pos[PVP_SELVES][3]; int here[PVP_SELVES];
    for (int s = 0; s < PVP_SELVES; s++) {
        uint64_t col = s ? self_collision(s, frame) : 0;
        self_ids[s] = self_relay_id(s);
        here[s] = s ? col && self_ids[s] >= 0 : have_me;
        if (!s && have_me) memcpy(pos[0], me, 12);
        else if (col) memcpy(pos[s], (const float *)(uintptr_t)(col + 0x80), 12);
    }
    /* players 2..8: each one's switch to his own connection (again for a new connection: a new relay id) */
    for (int s = 1; s < PVP_SELVES; s++) {
        int id = self_ids[s], on = atomic_load(&pvp_on_of[s]);
        if (id < 0 || !pvp_ok || (on == g_pvp[s].sent_on && id == g_pvp[s].sent_id)) continue;
        uint8_t f[5] = {3, 0, LAO_C_PVP, 0, (uint8_t)on};
        int sent; pthread_mutex_lock(&mx); sent = pvp_send(s, f, 5); pthread_mutex_unlock(&mx);
        if (sent) g_pvp[s].sent_on = on, g_pvp[s].sent_id = id;
    }
    for (int s = 0; s < PVP_SELVES; s++) {
        pvp_hits(c, s, frame, pos[s], here[s], ko_ok, now);
    }
    if (ko_watch_until) {  /* V13.5 PvP death: did the game take it from here? */
        extern int laonline_death_active(void);
        int hp_now = save_ready() ? pvp_hp() : -1;
        if (laonline_death_active()) { LOG("[laonline] V13.5 PvP death: the game started its Game Over\n"); ko_watch_until = 0; }
        else if (hp_now > 0) { LOG("[laonline] V13.5 PvP death: back up with %d/4 hearts (fairy or continue)\n", hp_now); ko_watch_until = 0; }
        else if (now >= ko_watch_until && hp_now == 0) {
            int full = 12 + 4 * (__builtin_popcount(LD16(SAVE + 0x5558) & 0x1FF) + __builtin_popcountll(LD64(SAVE + 0x5550) & 0xFFFFFFFFull) / 4);
            uint64_t ps = pvp_status(); if (ps) ST16(ps + 0x84, (uint16_t)full);  /* (the safety net: never left at 0) */
            ko_watch_until = 0; g_pvp[0].invulnerable_until = now + 2500;
            laonline_notify("PvP: you were knocked out! (the game's death didn't start - please send laonline.log)");
            LOG("[laonline] V13.5 PvP death: NO Game Over 6 s after hearts hit 0 - put back at %d/4 hearts\n", full);
        }
    }
    uint16_t my_map = lao_name_id(level);
    fx_my_map = my_map;
    for (int s = 0; s < PVP_SELVES; s++) {  /* facing = direction each one last moved */
        PvpSelf *p = &g_pvp[s];
        if (!here[s]) { p->have_last = 0; continue; }
        if (p->have_last) { float dx = pos[s][0] - p->last_me[0], dz = pos[s][2] - p->last_me[2], d = sqrtf(dx * dx + dz * dz);
            if (d > .03f && d < 3.f) { p->face_x = dx / d; p->face_z = dz / d; } }
        memcpy(p->last_me, pos[s], sizeof p->last_me); p->have_last = 1;
    }
    {   /* V14: hit markers follow the player who was hit; lock-on (L3 / F8) */
        pthread_mutex_lock(&mx);
        for (int i = 0; i < HM_N; i++) {
            if (!hm[i].at || now - hm[i].at > 900) continue;
            int vs = pvp_self_of_id(hm[i].victim);
            hm[i].mine = pvp_self_of_id(hm[i].attacker) >= 0; hm[i].on_me = vs >= 0;
            float o[3]; uint16_t map; int local;
            if (vs >= 0 && here[vs]) { memcpy(hm[i].pos, pos[vs], 12); hm[i].resolved = 1; }
            else if (laonline_v12_peer(hm[i].victim, o, &map, &local) && map == my_map) { memcpy(hm[i].pos, o, 12); hm[i].resolved = 1; }
        }
        pthread_mutex_unlock(&mx);
        /* V15: L3 / F8 toggle the Z-target too (ZL is the main button: runtime/hle/ztarget.c). Locked on anything
         * (player or enemy): every PvP attack aims at it. (Player 1's.) */
        if (atomic_exchange(&key_lock, 0)) { extern void ztarget_toggle(void); ztarget_toggle(); }
        { extern int ztarget_get_of(int, float pos[3], int *player); float tp[3];  /* (each player's own lock) */
          for (int s = 0; s < PVP_SELVES; s++)
              if (here[s] && ztarget_get_of(s, tp, NULL)) { float dx = tp[0] - pos[s][0], dz = tp[2] - pos[s][2], d = sqrtf(dx * dx + dz * dz);
                  if (d > .05f) { g_pvp[s].face_x = dx / d; g_pvp[s].face_z = dz / d; } } }
    }
    uint64_t ps = g_ga && GA(PLAYER_STATUS_PTR) ? LD64(GA(PLAYER_STATUS_PTR)) : 0;
    int valid = ps && ((ps >= MAIN_BASE && ps + 0x100 < SDK_BASE) || (ps >= HEAP_BASE && ps + 0x100 < HEAP_BASE + HEAP_MAX));
    int sword_lv = save_ready() && (LD32(SAVE + ITEMS) & 1) ? 1 + (LD8(SAVE + UPGRADES) & 1) : 1;  /* Noble Sword = L2 */
    for (int s = 0; s < PVP_SELVES; s++) {
        int my_id = self_ids[s];
        if (!atomic_load(&pvp_on_of[s]) || !here[s] || !valid || (s && (my_id < 0 || !pvp_ok))) {
            pthread_mutex_lock(&mx); g_pvp[s].item_press_n = 0; g_pvp[s].spin_req = 0; pthread_mutex_unlock(&mx);
            g_pvp[s].shot_n = 0;
            continue;
        }
        pvp_attacks(s, pos[s], my_map, my_id, sword_lv, ps, now);
    }
}

/* ------------------------------------------------------------------ dive assist (game thread) */
static void dive_tick(const char *level, const char *room) {
    if (!cfg_dive || !save_ready() || !laonline_level_ready() || laonline_ui_paused()) { assist_since = 0; return; }
    int bay = !strcmp(level, "Field") && room &&
              (strstr(room, "14J") || strstr(room, "13J") || strstr(room, "14I") || strstr(room, "15J") || strstr(room, "14K"));
    int flippers = LD32(SAVE + 0x556C) & 1;
    static int hinted;
    if (bay && flippers && !hinted) {
        hinted = 1;
        laonline_notify("Catfish's Maw: if diving in doesn't work, hold R3 (or F7) for 2 seconds.");
    }
    int held = (g_pvp[0].prev_buttons >> 5 & 1) || atomic_load(&key_f7_down);
    if (!bay || !flippers || !held) { assist_since = 0; return; }
    uint64_t now = ms_now();
    if (!assist_since) assist_since = now;
    if (now - assist_since < 2000) return;
    assist_since = 0;
    warp_request("CatfishsMawEnter", "CatfishsMawEnter_01A");
    dev_events_emit("SCENE", "V12_DIVE_ASSIST", "Catfish's Maw entrance", 0, 0, 0, 1, "WARP_REQUESTED", "LOCAL", 0);
    LOG("[laonline] V12 dive assist: Catfish's Maw entrance requested from %s\n", room);
}

void laonline_v12_tick(struct Cpu *c, uint64_t frame, const char *level, const char *room, const float me[3], int have_me) {
    cfg_load();
    if (!level) level = "";
    if (cfg_consumables && coop_mode() && save_ready()) rupee_tick(frame);
    snprintf(cur_level, sizeof cur_level, "%s", level); snprintf(cur_room, sizeof cur_room, "%s", room ? room : "");  /* V15.1 */
    progress_tick(c, frame);
    pvp_tick(c, frame, level, me, have_me);
    for (int s = 0; s < PVP_SELVES; s++) knock_tick(s, frame);
    dive_tick(level, room);
}

/* ------------------------------------------------------------------ Global lobby choice (launcher) */
void laonline_v12_global_variant(int randomizer) { atomic_store(&global_rando, randomizer ? 1 : 0); }
int laonline_v12_global_is_randomizer(void) { return atomic_load(&global_rando); }
const char *laonline_v12_global_lobby(void) { return atomic_load(&global_rando) ? "V10-GLOBAL-RANDO" : "V10-GLOBAL"; }

RT_ON_RESET(laonline_v12_reset) {
    memset(bracelet_local, 0, sizeof bracelet_local);
    pthread_mutex_lock(&mx);
    out_head = out_count = 0; memset(hit_n, 0, sizeof hit_n);
    memset(flag_in, 0, sizeof flag_in); memset(item_in, 0, sizeof item_in);
    pthread_mutex_unlock(&mx);
    rupee_check.expected = 0; last_scan = 0; assist_since = 0;
    pvp_selves_reset(); for (int s = 0; s < PVP_SELVES; s++) atomic_store(&pvp_on_of[s], 0);
    atomic_store(&pvp_sent_state, -1); ko_watch_until = 0; memset(hm, 0, sizeof hm);
}
