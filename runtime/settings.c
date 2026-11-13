#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "hle.h"
#include "input_bind.h"

#define SETTINGS_FILE settings_file()

// LARECOMP_SETTINGS=<path>: another settings file (test runs never touch the player's larecomp.ini)
static const char *settings_file(void) {
    const char *e = getenv("LARECOMP_SETTINGS");
    return e && *e ? e : "larecomp.ini";
}

const char *const g_enemy_colour_modes[N_ENEMY_COLOUR_MODES] = {"Off", "Random per seed", "Random every load"};
static const char *const k_enemy_colour_keys[N_ENEMY_COLOUR_MODES] = {"off", "seed", "load"};  // (larecomp.ini)
const char *const g_screen_filters[N_SCREEN_FILTERS] = {"Off", "Game Boy", "Game Boy Color", "Grayscale"};
static const char *const k_screen_filter_keys[N_SCREEN_FILTERS] = {"off", "gameboy", "gameboy_color", "grayscale"};  // (else: off)

Settings g_settings = {
    .dpad_hud=true,.dpad_opacity=80,.dpad_x=86,.dpad_y=22,
    .aspect = ASPECT_UNLOCKED,
    .fullscreen = false,
    .volume = 1.0f,
    .reverb = true,
    .movie_audio = true,
    .rumble = true,
    .rumble_strength = 100,
    .controller_light = true,
    .menu_confirm = MENU_CONFIRM_CONTROLLER,
    .show_fps = false,
    .laonline_username = true,
    .laonline_chat = true,.laonline_bubbles=true,.laonline_typing=true,.laonline_markers=true,.laonline_hearts=true,.laonline_indicators=true,
    .laonline_chat_seconds=6,
    .laonline_death_notices=true,.laonline_death_style=0,
    .laonline_tracker_x=2,.laonline_tracker_y=65,.laonline_tracker_opacity=80,.laonline_tracker_size=1,
    .laonline_dungeon_minimap=true,.laonline_shared_items=true,.laonline_pickup_notifications=true,.laonline_collection_counters=true,
    .laonline_sync_story=true,.laonline_sync_supplies=true,.laonline_sync_keys=true,.laonline_sync_upgrades=true,.laonline_sync_souls=true,.laonline_sync_world=true,
    .laonline_feed=true,.laonline_always_feed=false,.laonline_minimap=true,.laonline_feed_seconds=10,.laonline_minimap_size=0,
    .inv_keys = true,
    .tracker = true,
    .tracker_filters = -1,
    .chaos_letters = CHAOS_LETTERS_MAP,
    .diff_damage = 1.0f,
    .walk_speed = 1.0f,
    .walk_speed_dungeons = false,
    .upscaler = UPSCALE_SHARP,  // (MetalFX on macOS, AMD FSR 1 on Windows)
    .upscaler_auto = true,
    .res_auto = true,
    .livesplit_host = "localhost:16834",
#ifndef __APPLE__
    .bg_input = true,  // (as before the setting; on macOS it needs Input Monitoring permission, so Off there)
#endif
};
static const Settings initial_settings = {
    .dpad_hud=true,.dpad_opacity=80,.dpad_x=86,.dpad_y=22,
    .aspect = ASPECT_UNLOCKED,
    .fullscreen = false,
    .volume = 1.0f,
    .reverb = true,
    .movie_audio = true,
    .rumble = true,
    .rumble_strength = 100,
    .controller_light = true,
    .menu_confirm = MENU_CONFIRM_CONTROLLER,
    .show_fps = false,
    .laonline_username = true,
    .laonline_chat = true,.laonline_bubbles=true,.laonline_typing=true,.laonline_markers=true,.laonline_hearts=true,.laonline_indicators=true,
    .laonline_chat_seconds=6,
    .laonline_death_notices=true,.laonline_death_style=0,
    .laonline_tracker_x=2,.laonline_tracker_y=65,.laonline_tracker_opacity=80,.laonline_tracker_size=1,
    .laonline_dungeon_minimap=true,.laonline_shared_items=true,.laonline_pickup_notifications=true,.laonline_collection_counters=true,
    .laonline_sync_story=true,.laonline_sync_supplies=true,.laonline_sync_keys=true,.laonline_sync_upgrades=true,.laonline_sync_souls=true,.laonline_sync_world=true,
    .laonline_feed=true,.laonline_always_feed=false,.laonline_minimap=true,.laonline_feed_seconds=10,.laonline_minimap_size=0,
    .inv_keys = true,
    .tracker = true,
    .tracker_filters = -1,
    .chaos_letters = CHAOS_LETTERS_MAP,
    .diff_damage = 1.0f,
    .walk_speed = 1.0f,
    .walk_speed_dungeons = false,
    .upscaler = UPSCALE_SHARP,  // (MetalFX on macOS, AMD FSR 1 on Windows)
    .upscaler_auto = true,
    .res_auto = true,
    .livesplit_host = "localhost:16834",
#ifndef __APPLE__
    .bg_input = true,  // (as before the setting; on macOS it needs Input Monitoring permission, so Off there)
#endif
};
static bool editing;
static Settings edit_before;
static int16_t edit_pad[BIND_PAD_COUNT],edit_key[BIND_KEY_COUNT];
bool settings_edit_active(void){return editing;}
void settings_edit_begin(void){if(editing)return;edit_before=g_settings;memcpy(edit_pad,g_bind_pad,sizeof edit_pad);memcpy(edit_key,g_bind_key,sizeof edit_key);editing=true;}
void settings_edit_commit(void){editing=false;settings_save();}
void settings_edit_cancel(void){if(!editing)return;Settings live=g_settings;g_settings=edit_before;
/* Explicit seed selection/generation is an action, not a previewed display preference. */
memcpy(g_settings.rando_seed,live.rando_seed,sizeof live.rando_seed);memcpy(g_settings.laonline_rando_seed,live.laonline_rando_seed,sizeof live.laonline_rando_seed);g_settings.laonline_play_mode=live.laonline_play_mode;
memcpy(g_bind_pad,edit_pad,sizeof edit_pad);memcpy(g_bind_key,edit_key,sizeof edit_key);editing=false;}
void settings_defaults(Settings *out){*out=initial_settings;out->upscaler=g_default_upscaler;out->res_mode=g_default_res_mode;}


// The game's own text languages: its RomFS carries these per region (regionUS/USen|USes|USfr, regionEU/EUen|EUes|...,
// regionJP/JPja, regionKR/KRkr, regionTW/TWch, regionCN/CNch), and the game picks one from the system language it
// reads back (runtime/services/am.c), exactly as it does on the console. Codes as the Switch reports them, except for
// the Chinese ones: the game only recognizes zh-Hans / zh-Hant and falls back to Japanese when told zh-CN / zh-TW.
const char *const g_language_codes[N_LANGUAGES] = {"auto", "ja", "en-US", "es", "fr", "de", "it", "nl", "ru", "ko",
                                                   "zh-Hans", "zh-Hant"};
const char *const g_language_names[N_LANGUAGES] = {"Auto", "Japanese", "English", "Spanish", "French", "German",
                                                   "Italian", "Dutch", "Russian", "Korean", "Chinese (Simplified)",
                                                   "Chinese (Traditional)"};

// LANG_AUTO: the host's own language (LANG=es_ES.UTF-8 -> es), en-US when it is none of the game's. Linux only:
// macOS / Windows apps get no locale variables from the system, so Auto is English there (pick one in settings).
static const char *host_language(void) {
#ifndef __linux__
    return "en-US";
#endif
    const char *e = getenv("LC_ALL");
    if (!e || !*e) e = getenv("LC_MESSAGES");
    if (!e || !*e) e = getenv("LANG");
    if (!e || !*e || !strcmp(e, "C") || !strcmp(e, "POSIX")) return "en-US";
    if (!strncmp(e, "zh", 2) && (!e[2] || strchr("_-.@", e[2]))) {
        char locale[64];
        snprintf(locale, sizeof locale, "%s", e);
        locale[strcspn(locale, ".@")] = 0;
        for (char *p = locale; *p; ++p) *p = (char)tolower((unsigned char)*p);
        bool traditional = false;
        char *next;
        for (char *part = strtok_r(locale, "_-", &next); part; part = strtok_r(NULL, "_-", &next)) {
            // An explicit script takes precedence over the region, e.g. zh-Hans-HK.
            if (!strcmp(part, "hant")) return "zh-Hant";
            if (!strcmp(part, "hans")) return "zh-Hans";
            if (!strcmp(part, "tw") || !strcmp(part, "hk") || !strcmp(part, "mo")) traditional = true;
        }
        return traditional ? "zh-Hant" : "zh-Hans";
    }
    char l[8] = {0};
    size_t n = 0;
    for (const char *p = e; *p && *p != '.' && *p != '@' && *p != '_' && n < sizeof l - 1; p++) l[n++] = *p;
    static const struct { const char *loc, *code; } map[] = {{"ja", "ja"}, {"ko", "ko"}, {"ru", "ru"}, {"nl", "nl"},
                                                             {"de", "de"}, {"it", "it"}, {"fr", "fr"}, {"es", "es"}};
    for (size_t i = 0; i < sizeof map / sizeof *map; i++)
        if (!strcmp(l, map[i].loc)) return map[i].code;
    return "en-US";
}

const char *settings_language(void) {
    int i = g_settings.language;
    return i > LANG_AUTO && i < N_LANGUAGES ? g_language_codes[i] : host_language();
}

const char *settings_language_name(void) {
    const char *c = settings_language();
    for (int i = 0; i < N_LANGUAGES; i++)
        if (!strcmp(c, g_language_codes[i])) return g_language_names[i];
    return "English";
}

int g_default_upscaler = UPSCALE_SHARP;
int g_default_res_mode = RES_MATCH;
bool g_gpu_integrated;

static const char *res_name(int m) {
    static char n[8];
    if (m >= 1 && m <= 8) { snprintf(n, sizeof n, "%dx", m); return n; }
    return m == RES_DYNAMIC ? "dynamic" : m == RES_Q75 ? "75%" : m == RES_Q50 ? "50%" : m == RES_Q25 ? "25%" : "match";
}

void settings_resolve_defaults(bool integrated) {
    const char *e = getenv("LARECOMP_INTEGRATED");
    if (e && *e) integrated = atoi(e) != 0;
    g_gpu_integrated = integrated;
    g_default_upscaler = integrated ? UPSCALE_BILINEAR : UPSCALE_SHARP;
    g_default_res_mode = RES_MATCH;  // (Dynamic is not a default until it is reliable: user 2026-10-18)
    if (g_settings.upscaler_auto) g_settings.upscaler = g_default_upscaler;
    if (g_settings.res_auto) g_settings.res_mode = g_default_res_mode;
    LOG("[settings] %s GPU: upscaler %s (%s), resolution %s (%s), anti-aliasing %s\n", integrated ? "integrated" : "discrete / other",
        g_settings.upscaler == UPSCALE_BILINEAR ? "bilinear" : g_settings.upscaler == UPSCALE_FSR1 ? "fsr1" : "metalfx",
        g_settings.upscaler_auto ? "default" : "chosen", res_name(g_settings.res_mode), g_settings.res_auto ? "default" : "chosen",
        g_settings.antialias == AA_SMAA ? "smaa" : "off");
}

void settings_load(void) {
    static bool loaded;
    if (loaded) return;
    loaded = true;
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) return;
    char line[256];
    bool tip_key = false, aspect_key = false;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char key[64];
        char val[128] = {0};
        int fields = sscanf(line, " %63[^= ] = %127[^\n]", key, val);
        if (fields < 1) continue;
        if (!strcmp(key, "rando_seed")) {
            strlcpy(g_settings.rando_seed, val, sizeof g_settings.rando_seed);
            continue;
        }
        if (fields != 2) continue;
        if (!strcmp(key, "aspect")) g_settings.aspect = !strcmp(val, "original") ? ASPECT_ORIGINAL : ASPECT_UNLOCKED;
        else if (!strcmp(key, "fullscreen")) g_settings.fullscreen = atoi(val) != 0;
        else if (!strcmp(key, "volume")) g_settings.volume = (float)atof(val);
        else if (!strcmp(key, "reverb")) g_settings.reverb = atoi(val) != 0;
        else if (!strcmp(key, "movie_audio")) g_settings.movie_audio = atoi(val) != 0;
        else if (!strcmp(key, "show_fps")) g_settings.show_fps = atoi(val) != 0;
        else if (!strcmp(key, "resolution")) {  // (auto: never chosen, the GPU's default)
            int n = atoi(val);
            g_settings.res_auto = !strcmp(val, "auto");
            if (g_settings.res_auto) { g_settings.res_mode = g_default_res_mode; continue; }
            g_settings.res_mode = !strcmp(val, "dynamic") ? RES_DYNAMIC : (!strcmp(val, "75%") || !strcmp(val, "67%")) ? RES_Q75 :
                                  !strcmp(val, "50%") ? RES_Q50 : !strcmp(val, "25%") ? RES_Q25 :
                                  (n >= 1 && n <= 4) ? n : n > 4 ? 4 : RES_MATCH;  // (5x-8x were removed)
        }
        else if (!strcmp(key, "upscaler")) {  // (metalfx / fsr1: this OS's; auto: never chosen, the GPU's default)
            g_settings.upscaler_auto = !strcmp(val, "auto");
            if (!g_settings.upscaler_auto) g_settings.upscaler = !strcmp(val, "bilinear") ? UPSCALE_BILINEAR : UPSCALE_SHARP;
            else g_settings.upscaler = g_default_upscaler;
        }
        else if (!strcmp(key, "antialias")) g_settings.antialias = !strcmp(val, "off") ? AA_OFF : AA_SMAA;  // ("fxaa": SMAA now)
        else if (!strcmp(key, "inv_keys")) g_settings.inv_keys = atoi(val) != 0;
        else if (!strcmp(key,"dpad_shortcuts"))g_settings.dpad_shortcuts=atoi(val)!=0;
        else if (!strcmp(key,"dpad_hud"))g_settings.dpad_hud=atoi(val)!=0;
        else if (!strcmp(key,"dpad_up"))g_settings.dpad_up=(atoi(val)>=0&&atoi(val)<=12)||(atoi(val)>=20&&atoi(val)<=22)?atoi(val):0;
        else if (!strcmp(key,"dpad_down"))g_settings.dpad_down=(atoi(val)>=0&&atoi(val)<=12)||(atoi(val)>=20&&atoi(val)<=22)?atoi(val):0;
        else if (!strcmp(key,"dpad_left"))g_settings.dpad_left=(atoi(val)>=0&&atoi(val)<=12)||(atoi(val)>=20&&atoi(val)<=22)?atoi(val):0;
        else if (!strcmp(key,"dpad_right"))g_settings.dpad_right=(atoi(val)>=0&&atoi(val)<=12)||(atoi(val)>=20&&atoi(val)<=22)?atoi(val):0;
        else if (!strcmp(key,"dpad_size"))g_settings.dpad_size=atoi(val)>=0&&atoi(val)<=3?atoi(val):0;
        else if (!strcmp(key,"dpad_opacity"))g_settings.dpad_opacity=atoi(val)>=0&&atoi(val)<=100?atoi(val):80;
        else if (!strcmp(key,"dpad_x"))g_settings.dpad_x=atoi(val)>=0&&atoi(val)<=100?atoi(val):86;
        else if (!strcmp(key,"dpad_y"))g_settings.dpad_y=atoi(val)>=0&&atoi(val)<=100?atoi(val):22;
        else if (!strcmp(key, "dpad_move")) g_settings.dpad_move = atoi(val) != 0;
        else if (!strcmp(key, "rumble")) g_settings.rumble = atoi(val) != 0;
        else if (!strcmp(key, "rumble_strength")) g_settings.rumble_strength = atoi(val);
        else if (!strcmp(key, "controller_light")) g_settings.controller_light = atoi(val) != 0;
        else if (!strcmp(key, "background_input")) g_settings.bg_input = atoi(val) != 0;
        else if (!strcmp(key, "move_360")) g_settings.move_360 = atoi(val) != 0;
        else if (!strcmp(key, "blur_removal")) g_settings.blur_removal = atoi(val) != 0;
        else if (!strcmp(key, "hide_hero_icon")) g_settings.hide_hero_icon = atoi(val) != 0;
        else if (!strcmp(key, "mirror_mode")) g_settings.mirror_mode = atoi(val) != 0;
        else if (!strcmp(key, "prompts")) g_settings.prompts = !strcmp(val, "nintendo") ? PROMPTS_NINTENDO : !strcmp(val, "xbox") ?
                                                              PROMPTS_XBOX : !strcmp(val, "playstation") ? PROMPTS_PLAYSTATION : PROMPTS_AUTO;
        else if (!strcmp(key, "menu_confirm")) g_settings.menu_confirm = !strcmp(val, "game") ? MENU_CONFIRM_GAME
                                                                                                  : MENU_CONFIRM_CONTROLLER;
        else if (!strcmp(key, "language")) {  // ("auto" or one of the game's codes; anything else: auto)
            // (zh-CN / zh-TW: what a settings file from before this wrote; the game reads zh-Hans / zh-Hant)
            const char *v = !strcmp(val, "zh-CN") ? "zh-Hans" : !strcmp(val, "zh-TW") ? "zh-Hant" : val;
            g_settings.language = LANG_AUTO;
            for (int i = 1; i < N_LANGUAGES; i++)
                if (!strcmp(v, g_language_codes[i])) g_settings.language = i;
        }
        else if (!strcmp(key, "tracker")) g_settings.tracker = atoi(val) != 0;
        else if (!strcmp(key, "tracker_filters")) g_settings.tracker_filters = atoi(val);
        else if (!strcmp(key, "menu_tip_seen")) g_settings.menu_tip_seen = atoi(val) != 0, tip_key = true;
        else if (!strcmp(key, "aspect_asked")) g_settings.aspect_asked = atoi(val) != 0, aspect_key = true;
        else if (!strcmp(key, "onedrive_kept")) g_settings.onedrive_kept = atoi(val) != 0;
        else if (!strcmp(key, "chaos_letters"))
            g_settings.chaos_letters = !strcmp(val, "off") ? CHAOS_LETTERS_OFF : !strcmp(val, "explored") ? CHAOS_LETTERS_EXPLORED : CHAOS_LETTERS_MAP;
        else if (!strcmp(key,"cheat_keep_bowwow"))g_settings.cheat_keep_bowwow=atoi(val)!=0;
        else if (!strcmp(key,"laonline_play_mode"))g_settings.laonline_play_mode=atoi(val)>=0&&atoi(val)<=3?atoi(val):0;
        else if (!strcmp(key,"laonline_rando_seed"))snprintf(g_settings.laonline_rando_seed,sizeof g_settings.laonline_rando_seed,"%s",val);
        else if (!strcmp(key, "cheat_hearts")) g_settings.cheat_hearts = atoi(val) != 0;
        else if (!strcmp(key, "cheat_ammo")) g_settings.cheat_ammo = atoi(val) != 0;
        else if (!strcmp(key, "cheat_rupees")) g_settings.cheat_rupees = atoi(val) != 0;
        else if (!strcmp(key, "cheat_hookshot")) g_settings.cheat_hookshot = atoi(val) != 0;
        else if (!strcmp(key, "no_buff_drops")) g_settings.no_buff_drops = atoi(val) != 0;
        else if (!strcmp(key, "diff_damage")) g_settings.diff_damage = (float)atof(val);
        else if (!strcmp(key, "walk_speed")) g_settings.walk_speed = (float)atof(val);
        else if (!strcmp(key, "walk_speed_dungeons")) g_settings.walk_speed_dungeons = atoi(val) != 0;
        else if (!strcmp(key, "diff_ohko")) g_settings.diff_ohko = atoi(val) != 0;
        else if (!strcmp(key, "diff_hero")) g_settings.diff_hero = atoi(val) != 0;
        else if (!strcmp(key, "timer")) g_settings.timer = atoi(val) != 0;
        else if (!strcmp(key, "timer_position")) g_settings.timer_pos = !strcmp(val, "right") ? TIMER_POS_RIGHT : TIMER_POS_LEFT;
        else if (!strcmp(key, "livesplit")) g_settings.livesplit = atoi(val) != 0;
        else if (!strcmp(key, "rando_options")) strlcpy(g_settings.rando_options, val, sizeof g_settings.rando_options);
        else if (!strcmp(key, "livesplit_host")) strlcpy(g_settings.livesplit_host, val, sizeof g_settings.livesplit_host);
        else if (!strcmp(key, "laonline_chat")) g_settings.laonline_chat = atoi(val) != 0;
        else if (!strcmp(key, "laonline_bubbles")) g_settings.laonline_bubbles = atoi(val) != 0;
        else if (!strcmp(key, "laonline_typing")) g_settings.laonline_typing = atoi(val) != 0;
        else if (!strcmp(key, "laonline_markers")) g_settings.laonline_markers = atoi(val) != 0;
        else if (!strcmp(key, "laonline_hearts")) g_settings.laonline_hearts = atoi(val) != 0;
        else if (!strcmp(key, "laonline_indicators")) g_settings.laonline_indicators = atoi(val) != 0;
        else if (!strcmp(key,"laonline_chat_seconds")) {int v=atoi(val);g_settings.laonline_chat_seconds=v>=3&&v<=15?v:6;}
        else if (!strcmp(key,"laonline_feed"))g_settings.laonline_feed=atoi(val)!=0;
        else if (!strcmp(key,"laonline_always_feed"))g_settings.laonline_always_feed=atoi(val)!=0;
        else if (!strcmp(key,"laonline_dungeon_minimap"))g_settings.laonline_dungeon_minimap=atoi(val)!=0;
        else if (!strcmp(key,"laonline_shared_items"))g_settings.laonline_shared_items=atoi(val)!=0;
        else if (!strcmp(key,"laonline_sync_story"))g_settings.laonline_sync_story=atoi(val)!=0;
        else if (!strcmp(key,"laonline_sync_supplies"))g_settings.laonline_sync_supplies=atoi(val)!=0;
        else if (!strcmp(key,"laonline_sync_keys"))g_settings.laonline_sync_keys=atoi(val)!=0;
        else if (!strcmp(key,"laonline_sync_world"))g_settings.laonline_sync_world=atoi(val)!=0;
        else if (!strcmp(key,"laonline_sync_upgrades"))g_settings.laonline_sync_upgrades=atoi(val)!=0;
        else if (!strcmp(key,"laonline_sync_souls"))g_settings.laonline_sync_souls=atoi(val)!=0;
        else if (!strcmp(key,"laonline_pickup_notifications"))g_settings.laonline_pickup_notifications=atoi(val)!=0;
        else if (!strcmp(key,"laonline_death_notices"))g_settings.laonline_death_notices=atoi(val)!=0;
        else if (!strcmp(key,"laonline_death_style"))g_settings.laonline_death_style=atoi(val)==1;
        else if (!strcmp(key,"laonline_item_tracker"))g_settings.laonline_item_tracker=atoi(val)!=0;
        else if (!strcmp(key,"laonline_tracker_x")){int v=atoi(val);g_settings.laonline_tracker_x=v>=0&&v<=100?v:2;}
        else if (!strcmp(key,"laonline_tracker_y")){int v=atoi(val);g_settings.laonline_tracker_y=v>=0&&v<=100?v:65;}
        else if (!strcmp(key,"laonline_tracker_opacity")){int v=atoi(val);g_settings.laonline_tracker_opacity=v>=20&&v<=100?v:80;}
        else if (!strcmp(key,"laonline_tracker_size")){int v=atoi(val);g_settings.laonline_tracker_size=v>=0&&v<=2?v:1;}
        else if (!strcmp(key,"laonline_collection_counters"))g_settings.laonline_collection_counters=atoi(val)!=0;
        else if (!strcmp(key,"laonline_minimap"))g_settings.laonline_minimap=atoi(val)!=0;
        else if (!strcmp(key,"laonline_feed_seconds")){int v=atoi(val);g_settings.laonline_feed_seconds=v>=3&&v<=15?v:10;}
        else if (!strcmp(key,"laonline_minimap_size"))g_settings.laonline_minimap_size=atoi(val)==1;
        else if (!strcmp(key, "laonline_username")) g_settings.laonline_username = atoi(val) != 0;
        else if (!strcmp(key, "tunic")) col_parse(&g_settings.tunic, val);  // (colour rows: runtime/cosmetic_colour.c)
        else if (!strcmp(key, "hearts")) col_parse(&g_settings.hearts, val);
        else if (!strcmp(key, "hair")) col_parse(&g_settings.hair, val);
        else if (!strcmp(key, "shield")) col_parse(&g_settings.shield, val);
        else if (!strcmp(key, "skin")) col_parse(&g_settings.skin, val);
        else if (!strcmp(key, "leaves")) col_parse(&g_settings.leaves, val);
        else if (!strcmp(key, "grass")) col_parse(&g_settings.grass, val);
        else if (!strcmp(key, "sand")) col_parse(&g_settings.sand, val);
        else if (!strcmp(key, "screen_filter")) {
            for (int i = 0; i < N_SCREEN_FILTERS; i++) if (!strcmp(val, k_screen_filter_keys[i])) g_settings.screen_filter = i;
        }
        else if (!strcmp(key, "enemy_colours")) {
            for (int i = 0; i < N_ENEMY_COLOUR_MODES; i++) if (!strcmp(val, k_enemy_colour_keys[i])) g_settings.enemy_colours = i;
        }
        else bind_settings_load(key, val);  // bind_pad_* / bind_key_*: runtime/hle/bindings.c
    }
    fclose(f);
    if (!tip_key) g_settings.menu_tip_seen = true;  // (a settings file from before the note: not a first launch)
    if (!aspect_key) g_settings.aspect_asked = true;  // (likewise: the aspect ratio was chosen already)
    if (g_settings.volume < 0) g_settings.volume = 0;
    if (g_settings.volume > 2) g_settings.volume = 2;
    if (g_settings.rumble_strength < 25 || g_settings.rumble_strength > 100) g_settings.rumble_strength = 100;
    LOG("[settings] loaded %s\n", SETTINGS_FILE);
}

void settings_save(void) {
    if(editing)return;
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) { LOG("[settings] cannot write %s\n", SETTINGS_FILE); return; }
    fprintf(f, "# LAS Recomp settings (edited from the in-game menu: F1, Esc, or Minus+Plus on a controller)\n");
    fprintf(f, "aspect = %s\n", g_settings.aspect == ASPECT_ORIGINAL ? "original" : "unlocked");
    fprintf(f, "fullscreen = %d\n", g_settings.fullscreen);
    fprintf(f, "volume = %.2f\n", g_settings.volume);
    fprintf(f, "reverb = %d\n", g_settings.reverb);
    fprintf(f, "movie_audio = %d\n", g_settings.movie_audio);
    fprintf(f, "show_fps = %d\n", g_settings.show_fps);
    fprintf(f, "upscaler = %s\n", g_settings.upscaler_auto ? "auto" : g_settings.upscaler == UPSCALE_METALFX ? "metalfx" :
                                   g_settings.upscaler == UPSCALE_FSR1 ? "fsr1" : "bilinear");
    fprintf(f, "antialias = %s\n", g_settings.antialias == AA_SMAA ? "smaa" : "off");
    fprintf(f, "resolution = %s\n", g_settings.res_auto ? "auto" : res_name(g_settings.res_mode));
    fprintf(f, "inv_keys = %d\n", g_settings.inv_keys);
    fprintf(f, "dpad_move = %d\n", g_settings.dpad_move);
    fprintf(f,"dpad_shortcuts = %d\n",g_settings.dpad_shortcuts);
    fprintf(f,"dpad_hud = %d\n",g_settings.dpad_hud);
    fprintf(f,"dpad_up = %d\n",g_settings.dpad_up);
    fprintf(f,"dpad_down = %d\n",g_settings.dpad_down);
    fprintf(f,"dpad_left = %d\n",g_settings.dpad_left);
    fprintf(f,"dpad_right = %d\n",g_settings.dpad_right);
    fprintf(f,"dpad_size = %d\n",g_settings.dpad_size);
    fprintf(f,"dpad_opacity = %d\n",g_settings.dpad_opacity);
    fprintf(f,"dpad_x = %d\n",g_settings.dpad_x);
    fprintf(f,"dpad_y = %d\n",g_settings.dpad_y);

    fprintf(f, "rumble = %d\n", g_settings.rumble);
    fprintf(f, "rumble_strength = %d\n", g_settings.rumble_strength);
    fprintf(f, "controller_light = %d\n", g_settings.controller_light);
    fprintf(f, "background_input = %d\n", g_settings.bg_input);
    fprintf(f, "move_360 = %d\nblur_removal = %d\nhide_hero_icon = %d\n", g_settings.move_360, g_settings.blur_removal,
            g_settings.hide_hero_icon);
    static const char *const prompts[] = {"auto", "nintendo", "xbox", "playstation"};
    fprintf(f, "prompts = %s\n", prompts[g_settings.prompts & 3]);
    fprintf(f, "menu_confirm = %s\n", g_settings.menu_confirm == MENU_CONFIRM_CONTROLLER ? "controller" : "game");
    fprintf(f, "mirror_mode = %d\n", g_settings.mirror_mode);
    fprintf(f, "language = %s\n", g_language_codes[g_settings.language > LANG_AUTO && g_settings.language < N_LANGUAGES
                                                     ? g_settings.language : LANG_AUTO]);
    fprintf(f, "tracker = %d\n", g_settings.tracker);
    if (g_settings.tracker_filters >= 0) fprintf(f, "tracker_filters = %d\n", g_settings.tracker_filters);
    fprintf(f, "aspect_asked = %d\n", g_settings.aspect_asked);
    fprintf(f, "menu_tip_seen = %d\n", g_settings.menu_tip_seen);
    fprintf(f, "onedrive_kept = %d\n", g_settings.onedrive_kept);
    static const char *const letters[] = {"off", "explored", "map"};
    fprintf(f, "chaos_letters = %s\n", letters[g_settings.chaos_letters]);
    fprintf(f, "cheat_hearts = %d\ncheat_ammo = %d\ncheat_rupees = %d\n", g_settings.cheat_hearts, g_settings.cheat_ammo,
            g_settings.cheat_rupees);
    fprintf(f,"cheat_keep_bowwow = %d\nlaonline_play_mode = %d\nlaonline_rando_seed = %s\n",g_settings.cheat_keep_bowwow,g_settings.laonline_play_mode,g_settings.laonline_rando_seed);
    fprintf(f, "cheat_hookshot = %d\n", g_settings.cheat_hookshot);
    fprintf(f, "no_buff_drops = %d\n", g_settings.no_buff_drops);
    fprintf(f, "diff_damage = %.2f\ndiff_ohko = %d\ndiff_hero = %d\n", g_settings.diff_damage, g_settings.diff_ohko,
            g_settings.diff_hero);
    fprintf(f, "walk_speed = %.2f\n", g_settings.walk_speed);
    fprintf(f, "walk_speed_dungeons = %d\n", g_settings.walk_speed_dungeons);
    fprintf(f, "timer_position = %s\n", g_settings.timer_pos == TIMER_POS_RIGHT ? "right" : "left");
    fprintf(f, "timer = %d\nlivesplit = %d\nlivesplit_host = %s\n", g_settings.timer, g_settings.livesplit,
            g_settings.livesplit_host);
    fprintf(f,"laonline_dungeon_minimap = %d\n",g_settings.laonline_dungeon_minimap);
    fprintf(f,"laonline_death_notices = %d\nlaonline_death_style = %d\n",g_settings.laonline_death_notices,g_settings.laonline_death_style);
    fprintf(f,"laonline_item_tracker = %d\nlaonline_tracker_x = %d\nlaonline_tracker_y = %d\nlaonline_tracker_opacity = %d\nlaonline_tracker_size = %d\n",g_settings.laonline_item_tracker,g_settings.laonline_tracker_x,g_settings.laonline_tracker_y,g_settings.laonline_tracker_opacity,g_settings.laonline_tracker_size);
    fprintf(f,"laonline_sync_story = %d\nlaonline_sync_supplies = %d\nlaonline_sync_keys = %d\nlaonline_sync_upgrades = %d\nlaonline_sync_souls = %d\n",
            g_settings.laonline_sync_story,g_settings.laonline_sync_supplies,g_settings.laonline_sync_keys,g_settings.laonline_sync_upgrades,g_settings.laonline_sync_souls);
    fprintf(f,"laonline_sync_world = %d\n",g_settings.laonline_sync_world);
    fprintf(f,"laonline_shared_items = %d\nlaonline_pickup_notifications = %d\nlaonline_collection_counters = %d\n",g_settings.laonline_shared_items,g_settings.laonline_pickup_notifications,g_settings.laonline_collection_counters);
    fprintf(f,"laonline_feed = %d\nlaonline_always_feed = %d\nlaonline_minimap = %d\nlaonline_feed_seconds = %d\nlaonline_minimap_size = %d\n",g_settings.laonline_feed,g_settings.laonline_always_feed,g_settings.laonline_minimap,g_settings.laonline_feed_seconds,g_settings.laonline_minimap_size);
    fprintf(f,"laonline_chat = %d\n",g_settings.laonline_chat);
    fprintf(f,"laonline_bubbles = %d\n",g_settings.laonline_bubbles);
    fprintf(f,"laonline_typing = %d\n",g_settings.laonline_typing);
    fprintf(f,"laonline_markers = %d\n",g_settings.laonline_markers);
    fprintf(f,"laonline_hearts = %d\n",g_settings.laonline_hearts);
    fprintf(f,"laonline_indicators = %d\n",g_settings.laonline_indicators);
    fprintf(f,"laonline_chat_seconds = %d\n",g_settings.laonline_chat_seconds);
    fprintf(f, "laonline_username = %d\n", g_settings.laonline_username);
    static const struct { const char *key; const CosColour *c; } cols[] = {
        {"tunic", &g_settings.tunic}, {"hearts", &g_settings.hearts}, {"hair", &g_settings.hair},
        {"shield", &g_settings.shield}, {"skin", &g_settings.skin},
        {"leaves", &g_settings.leaves}, {"grass", &g_settings.grass}, {"sand", &g_settings.sand}};
    for (size_t i = 0; i < sizeof cols / sizeof *cols; i++) {
        char v[16];
        col_format(cols[i].c, v, sizeof v);
        fprintf(f, "%s = %s\n", cols[i].key, v);
    }
    fprintf(f, "enemy_colours = %s\n", k_enemy_colour_keys[g_settings.enemy_colours]);
    fprintf(f, "screen_filter = %s\n", k_screen_filter_keys[g_settings.screen_filter]);
    bind_settings_save(f);
    fprintf(f, "rando_seed = %s\n", g_settings.rando_seed);
    if (g_settings.rando_options[0]) fprintf(f, "rando_options = %s\n", g_settings.rando_options);
    fclose(f);
}
