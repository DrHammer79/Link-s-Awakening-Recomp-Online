// Teleport to a teammate - the picker (runtime/hle/laonline_teleport.h). LAS Recomp Co-Op, online play in a co-op
// session (a room, or a lobby laonline.ini names - not Global, where every player is in a world of their own: their
// spot may be somewhere this player's save can't be yet).
//
// The teleport button (Controls > Teleport button: automatically D-pad Up, or Minus + D-pad Up when the D-pad moves
// Link or uses items; F5 on the keyboard) opens a card per teammate along the top of the screen: their name in their
// colour, where they are, their hearts. Left / Right picks one, Confirm teleports, Back (or the button again) closes
// it; while it is up Player 1's controls are the picker's (runtime/hle/input.c). Teleporting is the game's own level
// jump - its fade and load - to the arrival point of their level nearest them (warp_data.h: in their square of the
// island or their dungeon room, else the nearest by the grid of 15 x 12 squares), then Link is put where they stand
// (runtime/hle/warp.c warp_request_place). Not while Player 1 is in a scene, a door, a talk, a fall or a level change,
// nor into or out of a boss's arena (runtime/hle/escape.c rt_boss_arena_at); a teammate it can't take him to (knocked
// out, in a boss fight, somewhere without an arrival point) shows why on the card.
#include <SDL3/SDL.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>
#include <string>

#include "imgui.h"
#include "i18n.h"
#include "warp_data.h"
extern "C" {
#include "../game_addresses.h"
#include "../hle.h"
#include "../settings.h"
#include "../input_bind.h"
#include "../hle/laonline_teleport.h"
#include "../hle/laonline_session.h"
#include "../hle/laonline_places.h"
#include "../hle/laonline_location.h"
}

extern "C" {
int game_in_play(void);                                   // hle/warp.c
int laonline_level_ready(void);
int warp_busy(void);
bool warp_blocked(const char *locator);
void warp_request_place(const char *level, const char *locator, float x, float y, float z);
int laonline_connection_status(void);                     // hle/laonline.c
int laonline_relay_login(char *host, size_t hn, char *port, size_t pn, char *name, char *lobby, char *password);
void laonline_notify(const char *text);
int coop_p1_free(void);                                   // hle/coop.c
int rt_boss_arena_at(const char *level, float x, float z); // hle/escape.c
bool doorjump_player_xz(uint64_t frame, float *x, float *z); // hle/doorjump.c
uint64_t renderer_present_count(void);
int cheats_link_health(int *max);                         // ui/cheats.mm
bool input_player1_pad(SDL_JoystickID id);                // hle/input.c
}

#define MATES 16
static std::mutex g_lock;  // (the picker's state: the event thread and the overlay's)
static bool g_open;
static int g_sel;
static std::string g_sel_name;  // (the one picked, kept by name across refreshes)
static uint64_t g_drawn_at;     // (the overlay drew it: a picker not drawn - a menu, the map - isn't up)
static LaoTeammate g_mates[MATES];
static int g_n;
static bool g_last_keyboard;

// ---------------------------------------------------------------- where things are
static const char *current_level(void) {
    uint64_t ps = LD64(GA(PLAYER_STATUS_PTR));
    return ps ? (const char *)(uintptr_t)ps : "";
}
static const char *level_of(uint16_t map) {  // (a level with an arrival point the warp tab knows)
    for (const WarpSpot &w : kWarpSpots)
        if (lao_name_id(w.level) == map) return w.level;
    return nullptr;
}
static const char *room_of(const char *level, uint16_t room) {
    for (auto &p : lao_places)
        if (!strcmp(p.level, level) && lao_room_id(level, p.room) == room) return p.room;
    return nullptr;
}
// "<level>_<row 01..><column A..>[...]": its square (0-based); false for any other name
static bool square_of(const char *name, const char *level, int *r, int *c) {
    size_t n = strlen(level);
    if (!name || strncmp(name, level, n) || name[n] != '_') return false;
    const char *s = name + n + 1;
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9' || s[2] < 'A' || s[2] > 'Z') return false;
    *r = (s[0] - '0') * 10 + (s[1] - '0') - 1, *c = s[2] - 'A';
    return true;
}
// The arrival point of their level nearest them: in their square / room by its name, else by their position on the
// level's grid (15 x 12 a square, x across and z down, as the island map and the boss arenas have it)
static const char *arrival_for(const char *level, const LaoTeammate &t) {
    int r, c;
    if (!square_of(room_of(level, t.room), level, &r, &c)) c = (int)floorf(t.pos[0] / 15.f), r = (int)floorf(t.pos[2] / 12.f);
    const char *best = nullptr, *any = nullptr;
    float best_d = 1e30f;
    for (const WarpSpot &w : kWarpSpots) {
        if (strcmp(w.level, level) || !strcmp(w.locator, "OcarinaWarpPoint") || warp_blocked(w.locator)) continue;
        if (!any) any = w.locator;
        int wr, wc;
        if (!square_of(w.locator, level, &wr, &wc)) continue;
        float dx = (wc - c) * 15.f, dz = (wr - r) * 12.f, d = dx * dx + dz * dz;
        if (d < best_d) best_d = d, best = w.locator;
    }
    return best ? best : any;
}

// ---------------------------------------------------------------- the rules
extern "C" bool laonline_teleport_available(void) {
    if (!game_in_play() || laonline_connection_status() != 2) return false;
    char host[96], port[16], name[17], lobby[33], password[65];
    int launch = laonline_relay_login(host, sizeof host, port, sizeof port, name, lobby, password);
    return launch == 1 || launch == 2;  // (a co-op session: not Global, 3)
}
// Why Player 1 can't teleport right now (nullptr: he can)
static const char *now_blocked(void) {
    if (!laonline_teleport_available()) return "Teleporting works in an online co-op session.";
    if (!laonline_level_ready() || warp_busy()) return "Wait a moment: the area is still loading.";
    if (!coop_p1_free()) return "Not during a scene, a door or a talk.";
    const char *level = current_level();
    float x, z;
    if (!strncmp(level, "Panel", 5) || (doorjump_player_xz(renderer_present_count(), &x, &z) && rt_boss_arena_at(level, x, z)))
        return "Not during a boss fight.";
    int max = 0;
    if (cheats_link_health(&max) <= 0 && max > 0) return "Not while you're knocked out.";
    return nullptr;
}
// Why not to this teammate (nullptr: fine); *level: theirs
static const char *mate_blocked(const LaoTeammate &t, const char **level) {
    *level = level_of(t.map);
    if (!*level || !strncmp(*level, "Panel", 5)) return "Somewhere teleporting can't reach";
    if (t.down) return "Knocked out";
    if (rt_boss_arena_at(*level, t.pos[0], t.pos[2])) return "In a boss fight";
    return nullptr;
}

// ---------------------------------------------------------------- the button
extern "C" void laonline_teleport_chord(int *mod, int *key) {
    int k = g_settings.laonline_teleport_key;
    if (k == LAO_TP_KEY_AUTO) k = g_settings.dpad_mode == DPAD_NOTHING ? LAO_TP_KEY_UP : LAO_TP_KEY_MINUS_UP;
    *mod = k == LAO_TP_KEY_MINUS_UP || k == LAO_TP_KEY_MINUS_DOWN ? SDL_GAMEPAD_BUTTON_BACK : -1;
    *key = k == LAO_TP_KEY_UP || k == LAO_TP_KEY_MINUS_UP ? SDL_GAMEPAD_BUTTON_DPAD_UP
         : k == LAO_TP_KEY_DOWN || k == LAO_TP_KEY_MINUS_DOWN ? SDL_GAMEPAD_BUTTON_DPAD_DOWN
         : k == LAO_TP_KEY_LEFT ? SDL_GAMEPAD_BUTTON_DPAD_LEFT : k == LAO_TP_KEY_RIGHT ? SDL_GAMEPAD_BUTTON_DPAD_RIGHT : -1;
}

static void refresh(void) {  // (g_lock held) the teammates now; the pick kept by name
    g_n = laonline_teammates(g_mates, MATES);
    int keep = -1;
    for (int i = 0; i < g_n; i++)
        if (g_sel_name == g_mates[i].name) keep = i;
    g_sel = keep >= 0 ? keep : g_n ? (g_sel < g_n ? g_sel : g_n - 1) : 0;
    if (g_n) g_sel_name = g_mates[g_sel].name;
}

extern "C" bool laonline_teleport_open(void) {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_open && renderer_present_count() <= g_drawn_at + 2;
}

static void set_open(bool on) {  // (g_lock held)
    if (on == g_open) return;
    g_open = on;
    if (on) {
        g_drawn_at = renderer_present_count();
        refresh();
        LOG("[teleport] picker open: %d teammate(s)\n", g_n);
    } else LOG("[teleport] picker closed\n");
}

extern "C" void laonline_teleport_toggle(void) {
    std::lock_guard<std::mutex> lock(g_lock);
    set_open(!g_open && laonline_teleport_available());
}

// Teleport to teammate i of the list (g_lock held). false: not now (the toast says why)
static bool go(int i) {
    if (i < 0 || i >= g_n) return false;
    const LaoTeammate &t = g_mates[i];
    const char *level = nullptr, *why = now_blocked();
    if (!why) why = mate_blocked(t, &level);
    char text[160];
    if (why) {
        snprintf(text, sizeof text, "%s %s", L("Can't teleport now:"), L(why));
        laonline_notify(text);
        LOG("[teleport] to '%s' refused: %s\n", t.name, why);
        return false;
    }
    const char *at = arrival_for(level, t);
    if (!at) {
        laonline_notify(L("Can't teleport there: no way into that place."));
        LOG("[teleport] to '%s' refused: no arrival point in %s\n", t.name, level);
        return false;
    }
    warp_request_place(level, at, t.pos[0], t.pos[1], t.pos[2]);
    snprintf(text, sizeof text, "%s %s%s%s", L("Teleporting to"), t.name, t.location[0] ? " - " : "", t.location);
    laonline_notify(text);
    LOG("[teleport] to '%s' (relay #%d) in %s: %s / %s, then (%.2f %.2f %.2f)\n", t.name, t.id,
        t.location[0] ? t.location : "?", level, at, t.pos[0], t.pos[1], t.pos[2]);
    set_open(false);
    return true;
}

// ---------------------------------------------------------------- its keys (menu.mm, each event first)
extern "C" bool laonline_teleport_event(const SDL_Event *e) {
    std::lock_guard<std::mutex> lock(g_lock);
    bool key_down = e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat;
    if (key_down && e->key.scancode == SDL_SCANCODE_F5) {  // (the keyboard's teleport key)
        g_last_keyboard = true;
        if (g_open) { set_open(false); return true; }
        if (!laonline_teleport_available()) return false;
        set_open(true);
        return true;
    }
    if (!g_open) return false;
    if (!laonline_teleport_available()) { set_open(false); return false; }
    int step = 0;
    bool confirm = false, back = false;
    if (key_down) {
        g_last_keyboard = true;
        SDL_Scancode k = e->key.scancode;
        if (k == SDL_SCANCODE_LEFT || k == SDL_SCANCODE_A) step = -1;
        else if (k == SDL_SCANCODE_RIGHT || k == SDL_SCANCODE_D) step = 1;
        else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER || k == SDL_SCANCODE_SPACE) confirm = true;
        else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_BACKSPACE) back = true;
        else return false;
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && input_player1_pad(e->gbutton.which)) {
        g_last_keyboard = false;
        int b = e->gbutton.button, mod, key;
        laonline_teleport_chord(&mod, &key);
        int act = bind_ui_action(b);  // (our menus' Confirm / Back: LAS Menu's buttons, runtime/hle/bindings.c)
        if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) step = -1;
        else if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) step = 1;
        else if (b == key) back = true;  // (the button again closes it)
        else if (act == BIND_UI_CONFIRM) confirm = true;
        else if (act == BIND_UI_BACK) back = true;
        else return true;  // (the others: nothing - and not the game's)
    } else if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX &&
               input_player1_pad(e->gaxis.which)) {
        static int held;  // (the stick picks one per push)
        int dir = e->gaxis.value < -20000 ? -1 : e->gaxis.value > 20000 ? 1 : 0;
        if (dir && dir != held) step = dir;
        if (abs(e->gaxis.value) < 8000 || dir) held = dir;
        if (!step) return false;
        g_last_keyboard = false;
    } else return false;
    if (step && g_n) {
        g_sel = (g_sel + step + g_n) % g_n;
        g_sel_name = g_mates[g_sel].name;
    }
    if (back) set_open(false);
    if (confirm) go(g_sel);
    return true;
}

// ---------------------------------------------------------------- the picker
// LAONLINE_TEST_TELEPORT=<frame>:<name>[,<frame>:<name>...] (test, render frames): the picker up at each frame, on that
// teammate, then the teleport 2 s later - as Confirm would (tools/coop_tests/online_teleport.sh); "?<name>": the picker
// shown on them, then closed (for a window shot of it)
static void test_hook(void) {
    static const char *spec = getenv("LAONLINE_TEST_TELEPORT");
    static const char *next = spec;
    static int stage;
    static uint64_t at;
    static char name[17];
    unsigned long frame = 0;
    if (!next || !*next) return;
    uint64_t now = renderer_present_count();
    if (stage == 0) {
        if (sscanf(next, "%lu:%16[^,]", &frame, name) != 2) { next = nullptr; return; }
        if (now < frame || !laonline_teleport_available()) return;
        set_open(true);
        g_sel_name = name + (name[0] == '?');
        refresh();
        stage = 1, at = now + 120;
        LOG("[teleport] test: picker up for '%s'\n", name);
    } else if (now >= at) {
        stage = 0;
        refresh();
        if (name[0] == '?') set_open(false);
        else if (g_n && g_sel_name == g_mates[g_sel].name) go(g_sel);
        else LOG("[teleport] test: '%s' isn't a teammate here\n", name), set_open(false);
        next = strchr(next, ',');
        if (next) next++;
    }
}

static ImU32 rgba(const uint8_t c[3], int a) { return IM_COL32(c[0], c[1], c[2], a); }

extern "C" void laonline_teleport_draw(ImDrawList *dl, ImVec2 display, ImFont *font, float scale) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_open && renderer_present_count() > g_drawn_at + 2) set_open(false);  // (hidden a while - the menu, the map, a
    test_hook();                                                                // level change: closed, not back later)
    if (!g_open) return;
    if (!laonline_teleport_available()) { set_open(false); return; }
    g_drawn_at = renderer_present_count();
    refresh();
    const ImU32 card = IM_COL32(38, 31, 21, 240), edge = IM_COL32(145, 126, 76, 255), gold = IM_COL32(255, 211, 65, 255),
                cream = IM_COL32(255, 245, 220, 255), dim = IM_COL32(196, 184, 156, 255), red = IM_COL32(245, 120, 100, 255);
    // (sizes: the overlay's scale, as its session bar and toasts; the picker sits under the session bar - 12 + 64 - so
    // the two never overlap)
    float s = scale, cw = 250 * s, ch = 136 * s, gap = 16 * s, pad = 22 * s;
    float tf = 30 * s, nf = 26 * s, wf = 20 * s, sf = 18 * s, af = 24 * s, hf = 19 * s;
    int shown = g_n < 1 ? 1 : g_n > 4 ? 4 : g_n;  // (up to 4 cards at once, around the one picked)
    int first = g_n <= 4 ? 0 : g_sel < 2 ? 0 : g_sel > g_n - 3 ? g_n - 4 : g_sel - 1;
    float cards = shown * cw + (shown - 1) * gap;
    float w = fmaxf(cards, 620 * s) + 2 * pad;
    float title_h = tf + 14 * s, foot_h = af + hf + 30 * s;
    float h = pad + title_h + ch + 14 * s + foot_h;
    ImVec2 a((display.x - w) * .5f, 92 * s), b(a.x + w, a.y + h);
    dl->AddRectFilled(a, b, card, 14 * s);
    dl->AddRect(a, b, edge, 14 * s, 0, 2 * s);
    dl->AddText(font, tf, ImVec2(a.x + pad, a.y + pad * .7f), cream, L("Teleport to a teammate"));
    float cy = a.y + pad + title_h, cx = a.x + (w - cards) * .5f;
    // what the buttons do: the bottom line
    int fam = bind_label_family();
    std::string hint = g_last_keyboard ? std::string(L("Left / Right: choose")) + "     Enter: " + L("teleport") + "     Esc: " + L("close")
                                       : std::string(L("D-pad: choose")) + "     " + bind_pad_label(bind_ui_host(BIND_UI_CONFIRM), fam) + ": " +
                                             L("teleport") + "     " + bind_pad_label(bind_ui_host(BIND_UI_BACK), fam) + ": " + L("close");
    ImVec2 hs = font->CalcTextSizeA(hf, FLT_MAX, 0, hint.c_str());
    dl->AddText(font, hf, ImVec2(a.x + (w - hs.x) * .5f, b.y - pad * .6f - hf), dim, hint.c_str());
    if (!g_n) {
        const char *none = L("No teammates are online in this session right now.");
        ImVec2 ns = font->CalcTextSizeA(wf, FLT_MAX, 0, none);
        dl->AddText(font, wf, ImVec2(a.x + (w - ns.x) * .5f, cy + (ch - wf) * .5f), dim, none);
        return;
    }
    for (int k = 0; k < shown; k++) {
        int i = first + k;
        const LaoTeammate &t = g_mates[i];
        const char *level, *why = mate_blocked(t, &level);
        bool sel = i == g_sel;
        ImVec2 p(cx + k * (cw + gap), cy), q(p.x + cw, p.y + ch);
        dl->AddRectFilled(p, q, sel ? IM_COL32(74, 59, 31, 248) : IM_COL32(52, 43, 28, 235), 10 * s);
        dl->AddRect(p, q, sel ? gold : IM_COL32(110, 96, 62, 255), 10 * s, 0, (sel ? 3.f : 1.5f) * s);
        // their colour as a badge with their initial, then their name in it (a dark colour: the name in cream)
        float lum = .3f * t.rgb[0] + .59f * t.rgb[1] + .11f * t.rgb[2];
        ImVec2 c(p.x + 34 * s, p.y + 36 * s);
        dl->AddCircleFilled(c, 22 * s, rgba(t.rgb, why ? 110 : 255));
        dl->AddCircle(c, 22 * s, IM_COL32(255, 245, 220, why ? 90 : 210), 0, 2 * s);
        char initial[2] = {t.name[0] ? t.name[0] : '?', 0};
        ImVec2 is = font->CalcTextSizeA(nf, FLT_MAX, 0, initial);
        dl->AddText(font, nf, ImVec2(c.x - is.x * .5f, c.y - is.y * .5f), lum > 140 ? IM_COL32(30, 25, 18, 255) : cream, initial);
        dl->PushClipRect(ImVec2(p.x + 8 * s, p.y), ImVec2(q.x - 8 * s, q.y), true);
        ImU32 name_col = why ? dim : lum < 115 ? cream : rgba(t.rgb, 255);  // (a dark colour - red, blue - reads badly
                                                                            //  on the card: cream, the badge has it)
        dl->AddText(font, nf, ImVec2(p.x + 66 * s, c.y - nf * .5f), name_col, t.name);
        // where, and hearts (or why not)
        char area[33];  // (players 2..8 of a PC send no area of their own: worked out from their map and room)
        if (t.location[0]) snprintf(area, sizeof area, "%s", t.location);
        else lao_location_ids(t.map, t.room, area);
        const char *where = area;
        dl->AddText(font, wf, ImVec2(p.x + 16 * s, p.y + 70 * s), why ? dim : cream, where);
        char line[64];
        if (why) snprintf(line, sizeof line, "%s", L(why));
        else if (t.max_hp) snprintf(line, sizeof line, "%s %d / %d", L("Hearts"), (t.hp + 3) / 4, t.max_hp / 4);
        else line[0] = 0;
        dl->AddText(font, sf, ImVec2(p.x + 16 * s, p.y + 100 * s), why ? red : dim, line);
        dl->PopClipRect();
        if (sel && g_n > 1) {  // (arrows either side of the one picked)
            float my = p.y + ch * .5f, r = 10 * s;
            dl->AddTriangleFilled(ImVec2(p.x - 4 * s, my - r), ImVec2(p.x - 4 * s, my + r), ImVec2(p.x - 4 * s - r, my), gold);
            dl->AddTriangleFilled(ImVec2(q.x + 4 * s, my - r), ImVec2(q.x + 4 * s, my + r), ImVec2(q.x + 4 * s + r, my), gold);
        }
    }
    // what Confirm does now - or why not now
    const LaoTeammate &t = g_mates[g_sel];
    const char *level, *why = now_blocked();
    if (!why) why = mate_blocked(t, &level);
    char line[160];
    if (why) snprintf(line, sizeof line, "%s", L(why));
    else snprintf(line, sizeof line, "%s %s", L("Teleport to"), t.name);
    ImVec2 ls = font->CalcTextSizeA(af, FLT_MAX, 0, line);
    dl->AddText(font, af, ImVec2(a.x + (w - ls.x) * .5f, cy + ch + 16 * s), why ? red : gold, line);
}
