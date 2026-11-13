#pragma once
/* LA-Online CO-OP V12 additions (runtime/hle/laonline_v12.c). Additive: every entry point is a no-op when the
 * matching laonline.ini switch is 0, and nothing here runs in Vanilla (offline) play. */
#include <stdint.h>
#include <stddef.h>
struct Cpu;

#define LAO_V12_QUERY 0x4c414f70u   /* relay capability: PvP + Global Randomizer lobby */
#define LAO_V12_REPLY 0x4c414f71u
#define LAO_PVP135_REPLY 0x4c414f72u  /* V13.5 relay: a PvP hit may carry its damage (item attacks) */
#define LAO_PVPKO_REPLY 0x4c414f73u   /* V13.5 relay: PvP knock-outs (kill feed + kill counter) */
#define LAO_GLOBALEND_REPLY 0x4c414f74u  /* V14 relay: "<name> saved Koholint!" announcements */
#define LAO_PVPCOOP_REPLY 0x4c414f75u   /* V15 relay: PvP in Co-op lobbies; S_PVP kind 6 = your kill totals */
#define LAO_C_PVP 0x1cu             /* u8 kind: 0 = state (u8 on), 1 = hit (u8 target id) */
#define LAO_S_PVP 0xa2u             /* u8 kind, u8 player id, u8 value (state on / hit quarter-hearts) */

/* game thread */
void laonline_v12_tick(struct Cpu *c, uint64_t frame, const char *level, const char *room, const float me[3], int have_me);
int laonline_v12_resource(int item, int count);           /* shared consumables: 1 = applied here (skip AddItem) */
int laonline_v12_hide_puppet(const char *level);          /* last resort (ini): remote Links hidden in the Egg */
int laonline_v12_ghost_level(const char *level);          /* remote Links visible, all colliders off */
uint64_t laonline_v12_input(uint64_t buttons, int menu);  /* observes B / R3; never changes the buttons */
/* UI/event thread */
void laonline_v12_key(int fkey, int down, int repeat);     /* F6 PvP, F7 dive assist */
/* network thread */
int laonline_v12_packet(uint8_t *out, int capacity, int launch_mode);
void laonline_v12_message(const uint8_t *b, size_t n);
/* any thread */
int laonline_v12_pvp_local(void);
int laonline_v12_pvp_peer(int id);
/* V13.5 kill feed (overlay): entry i (0 = newest) -> its two lines and how visible it still is (0 = gone) */
float laonline_v12_killfeed(int i, char *line1, int cap1, char *line2, int cap2);
/* V15: the kill feed (everyone, bottom left) and the personal KO card (killer / victim, bottom right) */
float laonline_v12_feed_row(int i, char *k, int kc, char *v, int vc, char *weapon, int wc, int *ending);
float laonline_v12_feed_panel(float *left_s);
float laonline_v12_card(char *line, int lc, char *score, int sc, float *left_s);
int laonline_v12_my_kos(int *kills, int *falls);
int laonline_v12_totals(int *kills, int *losses);  /* V15: server Kills / Losses (Global); 0 = not shown */
/* V14 PvP hit markers: recent confirmed hits -> where (the hit player now), age in s, quarter hearts, 1 = your hit,
 * 1 = you were the one hit */
int laonline_v12_hitmarks(float pos[][3], float *age_s, int *quarters, int *mine, int *on_me, int cap);
/* V14 PvP lock-on (L3 / F8): 1 + the locked player's position, 0 = not locked */
int laonline_v12_lockon(float pos[3]);
void laonline_v12_ending(void);  /* V14: the ending movie started (movie.c) - Global announces it */  /* this session's knock-outs of / by you; 0 when PvP is off */
void laonline_v12_global_variant(int randomizer);
const char *laonline_v12_global_lobby(void);
int laonline_v12_global_is_randomizer(void);
