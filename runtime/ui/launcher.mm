extern "C" void menu_settings_cancel(void);
extern "C" bool menu_settings_take_close(void);
extern "C" void rando_ui_settings_reloaded(void);
#include "tropical_ui.h"
#include "session_ui.h"
#include "settings_look.h"
#include "lao_updater.h"  // V13: Update Now
// Start screen, shown before the game boots (user 2026-10-08, styled after Dusklight's launcher): PLAY (with
// ‹ Vanilla / seed › picker), SETTINGS, QUIT, game-files status at the lower left. Nothing of the game ships with
// LAS Recomp: on first launch the user picks the folder with their own game files (1.0.0, 1.0.1 or 1.1.0), we check
// the main module's build id (runtime/game_addresses/versions.h), copy the files and build the game's data image; PLAY stays disabled until that worked. The background is a frame of the game's own title movie (from the user's files) once extracted.
//
// Skipped with --play, LARECOMP_NO_LAUNCHER=1, or any scripted test run (LARECOMP_INPUT): tests boot straight in
// (with LARECOMP_NO_LAUNCHER, set by tools/game_slot.sh, the app exits rather than show it: runtime/main.c).
// The in-game menu's "Title screen" comes back here without leaving the process (runtime/main.c reboots the game).
#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <fstream>
#include <atomic>

#include "imgui.h"
#include "imgui_internal.h"  // (SetKeyOwner)
#include "option_rows.h"
#include "osk.h"
#include "pad_nav.h"
#ifdef __APPLE__
#include "imgui_impl_metal.h"
#else
#include "imgui_impl_vulkan.h"
#endif
#include "imgui_impl_sdl3.h"
#include "laonline_build.h"  // V12: release label for What's New?

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include "platform.h"
#include "paths.h"
#include "gamebuild.h"
#include "hle/laonline_diag.h"
#include "build_cache.h"
#include "game_addresses.h"
#include "gamelib.h"
#include "romfs_build.h"
#include "settings.h"
#include "input_bind.h"
#include "nvn/shader_prep.h"
bool controls_ui_took_back(void);  // ui/controls_ui.mm: B went back a step on Configure inputs
bool menu_tabs_took_back(void);    // ui/menu.mm (V12.5): B went from a settings page back to the category list
void laonline_launch_mode(int enabled);
void laonline_session_override(const char *,const char *,const char *);
void laonline_v12_global_variant(int randomizer);  // V12: Global Vanilla / Global Randomizer lobby
int laonline_seed_metadata(const char *,char *,size_t,char *,size_t);
int laonline_seed_descriptor(char *,size_t,char *,size_t,int *,int *,char *,size_t);
int laonline_seed_descriptor_named(char *,size_t,char *,size_t,int *,int *,char *,size_t,char *,size_t);
int laonline_seed_metadata_named(const char *,const char *,char *,size_t,char *,size_t);
void ui_window_init(void);
SDL_Window *ui_window(void);
#ifdef __APPLE__
void *ui_metal_layer(void);
void *ui_metal_device(void);
#else
// runtime/nvn/vulkan: window frames and ImGui plumbing
bool vk_frame_begin(const float clear[4]);
void vk_frame_end(const char *shot_path);
void *vk_frame_cmd(void);
bool vk_imgui_backend_init(void);
void vk_imgui_backend_shutdown(void);
uint64_t vk_imgui_texture_rgba(const void *rgba, int w, int h);
#endif
}
#ifdef __APPLE__
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// (test) BGRA texture -> PNG (the game renderer's saver isn't up yet before the game boots)
static void save_png(id<MTLTexture> tex, const char *path) {
    size_t w = tex.width, h = tex.height;
    std::vector<uint8_t> px(w * h * 4);
    [tex getBytes:px.data() bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, cs,
                                             kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, strlen(path), false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, (__bridge CFStringRef)UTTypePNG.identifier, 1, NULL);
    if (dest) {  // (NULL when the file can't be created, e.g. its folder isn't there: CFRelease(NULL) traps)
        CGImageDestinationAddImage(dest, img, NULL);
        CGImageDestinationFinalize(dest);
        CFRelease(dest);
    } else fprintf(stderr, "[launcher] can't write %s\n", path);
    CFRelease(url), CGImageRelease(img), CGContextRelease(ctx), CGColorSpaceRelease(cs);
}
#endif

// The release tag this build is (also names the release files; Makefile / tools/bundle_macos.sh / make_dmg.sh read
// it). Under the hood every release is a semantic version (0.<n>.<m> during the beta: .0 releases bring features, the
// rest are fixes). Tags: beta_<n> and beta_<n>.<m> ("Beta <n>.<m>", = 0.<n>.<m>) up to Beta 5; from Beta 6
// v0.<n>.<m> ("Beta <n>", "Beta <n>.<m>"); after the beta v<major>.<minor>.<patch> ("1.0.0"). A
// test build (a pre-release for a few players to try a fix) is test-<name>: the start screen says "Test build <name>",
// and it previews LARECOMP_TEST_OF - the update check offers that release (or a later one) as soon as it's out, so its
// players move to it.
#define LARECOMP_VERSION "beta_5.4"
#define LARECOMP_TEST_OF "beta_5.4"
extern "C" const char *larecomp_version(void) { return LARECOMP_VERSION; }  // (the shader caches' tag)

// ------------------------------------------------------------------ update check
// The start screen asks GitHub for the project's releases once per launch (a background curl, which macOS and
// Windows 10+ ship) and offers the best one if it outranks this build (runtime/ui/update_tags.h): any v1.0.0+ above
// every beta, betas by number, a beta's fixes releases (beta_<n>.<m>, v0.<n>.<m>) between it and the next beta, and
// v0.<n>.<m> the same as beta_<n>.<m>; other tags (the alpha_* ones) are ignored. (Beta 5 and earlier only know
// beta_<n>: they never offer a hotfix; builds before Beta 5.3 don't know v0.<n>.<m> either.) Offline / no curl /
// anything unexpected: nothing is shown.
#define RELEASES_URL "https://api.github.com/repos/peiikai/LASRecomp/releases?per_page=100"
#define RELEASE_PAGE "https://github.com/peiikai/LASRecomp/releases/tag/"

#include "update_tags.h"  // tag_rank, tag_label

static std::string g_update_tag;  // a newer release's tag (g_update_lock)
static pthread_mutex_t g_update_lock = PTHREAD_MUTEX_INITIALIZER;

static void *update_thread_body(void *);
// V12.5: never let an exception in this background check end the game (std::terminate)
static void *update_thread(void *arg) {
    try { return update_thread_body(arg); } catch (...) { fprintf(stderr, "[launcher] release check failed (contained)\n"); return NULL; }
}
static void *update_thread_body(void *) {
    std::string body;
    if (const char *t = getenv("LARECOMP_UPDATE_TAGS")) {  // (test) "beta_2,v1.0.0": these releases, no network
        for (const char *p = t; *p;) {
            const char *c = strchr(p, ',');
            body += "\"tag_name\": \"" + std::string(p, c ? c - p : strlen(p)) + "\"\n";
            p = c ? c + 1 : p + strlen(p);
        }
    } else {
#ifdef _WIN32
        const char *sr = getenv("SystemRoot");
        std::string curl = std::string(sr ? sr : "C:\\Windows") + "\\System32\\curl.exe";
#else
        std::string curl = "/usr/bin/curl";
#endif
        std::string out = std::string(rt_data_root()) + "/cache/releases.json";
        mkdir((std::string(rt_data_root()) + "/cache").c_str(), 0755);
        const char *av[] = {curl.c_str(), "-fsSL", "--max-time", "15", "-H", "Accept: application/vnd.github+json",
                            "-A", "LASRecomp/" LARECOMP_VERSION, RELEASES_URL, NULL};
        PlatProc p = plat_spawn(av, out.c_str(), false);
        if (!p || plat_proc_wait(p, true) != 0) { remove(out.c_str()); return NULL; }
        if (FILE *f = fopen(out.c_str(), "rb")) {
            char buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0 && body.size() < (8u << 20)) body.append(buf, n);
            fclose(f);
        }
        remove(out.c_str());
    }
    std::string best;
    // (a test build ranks just below the release it previews; other test builds rank -1: never offered)
    long long best_rank = strncmp(LARECOMP_VERSION, "test-", 5) ? tag_rank(LARECOMP_VERSION) : tag_rank(LARECOMP_TEST_OF) - 1;
    for (size_t at = 0; (at = body.find("\"tag_name\"", at)) != std::string::npos;) {
        size_t q1 = body.find('"', body.find(':', at) + 1), q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string tag = body.substr(q1 + 1, q2 - q1 - 1);
        if (tag_rank(tag) > best_rank) best_rank = tag_rank(tag), best = tag;
        at = q2;
    }
    if (!best.empty()) fprintf(stderr, "[launcher] update available: %s (this build %s)\n", best.c_str(), LARECOMP_VERSION);
    pthread_mutex_lock(&g_update_lock);
    g_update_tag = best;
    pthread_mutex_unlock(&g_update_lock);
    return NULL;
}

// ------------------------------------------------------------------ game files
static std::string g_root;  // extracted game: <root>/exefs, <root>/romfs

static bool file_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// The game's RomFS (the same in every supported version) has exactly ROMFS_FILES files: a copy with fewer is incomplete (a copy or unzip that
// stopped partway) and the game crashes on the first one it misses (issues #24, #35). Only the number ships.
// Their sizes add up to game_romfs_bytes() (per version): every file there with another total is a damaged or
// changed file (issue #391). The game still plays then; the start screen says so.
// With check (base: where the path inside romfs starts), the files that aren't the game's are named in the log, the
// first ROMFS_ODD_LOGGED of them (check->odd counts them all), and copies of the game code at the romfs root
// (game_romfs_exefs_copy, game: the game folder) are left out of the count and the bytes (check->copies).
enum { ROMFS_FILES = 3836, ROMFS_ODD_LOGGED = 20 };
struct RomfsCheckWalk { GameRomfsCheck *check; size_t base; int odd; const char *game; int copies; };
static int count_files(const std::string &dir, int depth = 0, uint64_t *bytes = NULL, RomfsCheckWalk *check = NULL) {
    DIR *d = depth < 12 ? opendir(dir.c_str()) : NULL;
    if (!d) return 0;
    int n = 0;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;  // (".", "..", .DS_Store)
        std::string p = dir + "/" + e->d_name;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) n += count_files(p, depth + 1, bytes, check);
        else if (S_ISREG(st.st_mode)) {
            if (check && depth == 0 && game_romfs_exefs_copy(check->game, e->d_name, (uint64_t)st.st_size)) {
                check->copies++;
                continue;
            }
            n++;
            if (bytes) *bytes += (uint64_t)st.st_size;
            if (check && !game_romfs_check_file(check->check, p.c_str() + check->base, (uint64_t)st.st_size) &&
                check->odd++ < ROMFS_ODD_LOGGED)
                fprintf(stderr, "[launcher] romfs: not one of the game's files: %s (%llu bytes)\n", p.c_str() + check->base,
                        (unsigned long long)st.st_size);
        }
    }
    closedir(d);
    return n;
}
// The copied romfs' file count and bytes, counted once (the checks below run every frame): -1 = count again (game
// files set up, deleted)
static int g_romfs_count = -1;
static uint64_t g_romfs_bytes, g_romfs_want;  // (want: this version's total, 0 unknown)
static int g_romfs_missing;  // the game's files not there by name and size (a renamed or moved file: same count)
static std::atomic<bool> g_build_cache_changed{true};
static std::atomic<int> g_files_changes{0};  // (the version label on the start screen is read again)
static void game_files_changed(void) { g_romfs_count = -1; g_build_cache_changed = true; ++g_files_changes; }
// The setup page redraws every frame; read the build cache's disk state only after the files/build state changes.
static bool g_resumable, g_has_build_cache;
static void read_build_cache(void) {
    if (!g_build_cache_changed.exchange(false)) return;
    g_resumable = build_cache_resumable(g_root.c_str());
    g_has_build_cache = build_cache_present(g_root.c_str());
}
static bool resumable_build(void) { read_build_cache(); return g_resumable; }
static bool has_build_cache(void) { read_build_cache(); return g_has_build_cache; }

static std::string incomplete(int have) {
    char b[96];
    snprintf(b, sizeof b, "Game files incomplete (%d of %d files)", have, (int)ROMFS_FILES);
    return b;
}

// The copied game files alone ("" = there), without the game code built from them
static std::string check_copied_files(const std::string &root) {
    if (!file_exists(root + "/exefs/main")) return "No game files yet";
    if (!game_version_of(root.c_str())) return "Wrong version of the game";
    if (!file_exists(root + "/romfs/region_common")) return incomplete(0);
    if (g_romfs_count < 0) {
        g_romfs_bytes = 0;
        const GameVersion *v = game_version_of(root.c_str());
        RomfsCheckWalk check = {game_romfs_check_begin(v), root.size() + strlen("/romfs/"), 0, root.c_str(), 0};
        g_romfs_count = count_files(root + "/romfs", 0, &g_romfs_bytes, &check);
        int missing = game_romfs_check_end(check.check);
        g_romfs_missing = missing;
        if (check.copies)
            fprintf(stderr, "[launcher] romfs: %d %s of the game code at the root, ignored\n", check.copies,
                    check.copies == 1 ? "copy" : "copies");
        if (check.odd > ROMFS_ODD_LOGGED)
            fprintf(stderr, "[launcher] romfs: %d more files that aren't the game's\n", check.odd - ROMFS_ODD_LOGGED);
        if (missing) fprintf(stderr, "[launcher] romfs: %d of the game's files missing or changed\n", missing);
        g_romfs_want = game_romfs_bytes(v);
        if (g_romfs_count >= ROMFS_FILES && g_romfs_want && g_romfs_bytes != g_romfs_want)
            fprintf(stderr, "[launcher] romfs: %llu bytes in %d files, this version's are %llu: damaged or changed "
                    "game files\n", (unsigned long long)g_romfs_bytes, g_romfs_count, (unsigned long long)g_romfs_want);
    }
    if (g_romfs_count < ROMFS_FILES) return incomplete(g_romfs_count);
    return "";
}

// Every file there, but not all of them the game's (name and size) or not the game's sizes: a damaged, changed,
// renamed or moved file (issue #391). Playable, so not a check_game_files() problem: the start screen shows it beside
// "ready".
static bool files_changed(const std::string &root) {
    return check_copied_files(root).empty() && (g_romfs_missing > 0 || (g_romfs_want && g_romfs_bytes != g_romfs_want));
}
static const char *const CHANGED_HEAD = "Game files damaged or changed";
static const char *const CHANGED_HINT = "Copy your game files again, then choose that folder and set them up again.";

// "" = ready to play, else what is wrong
static std::string check_game_files(const std::string &root) {
    std::string why = check_copied_files(root);
    if (why.empty() && !gamelib_present(root.c_str())) return "Game code not built yet";
    return why;
}

// What to do about a check_game_files() problem (the line under it on the start screen)
static const char *status_hint(const std::string &why) {
    if (why.rfind("Game files incomplete", 0) == 0)
        return "Some of the game's files are missing, usually from a copy or unzip that stopped partway. Copy your game "
               "files again, then choose that folder and set them up again.";
    if (why == "Wrong version of the game") return "LAS Recomp needs version 1.0.0, 1.0.1 or 1.1.0 of the game.";
    if (why == "Game code not built yet") return "Set up the game files to build it (a few minutes, only once).";
    return "Set up the game files to play.";
}

// ------------------------------------------------------------------ setup (background thread)
// The player brings their own game files: a folder holding exefs/ and romfs/. They're copied into the data root (APFS
// clones / hard links where the file system allows); the game reads romfs/ in place (runtime/services/fs.c).
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static std::string g_src;           // the picked folder
static std::string g_job;           // current setup step / result for the status line
static std::string g_job_hint;      // a failure: what to do about it (shown under it)
static bool g_busy, g_job_failed;
static bool g_confirm_delete;       // game-files page: "Delete the game files?" shown

static void set_job(const std::string &s, bool failed = false, const std::string &hint = "") {
    pthread_mutex_lock(&g_lock);
    g_job = s;
    g_job_failed = failed;
    g_job_hint = hint;
    pthread_mutex_unlock(&g_lock);
}

// (the not-found failure's hint, and the caption over the game files page's example folder)
static const char *const PICK_FOLDER_HINT = "Choose the folder that has the game's exefs and romfs folders in it.";

// Setup failures: a short headline and what to do next (also LARECOMP_LAUNCHER_SHOT=<f>:fail<1-6>, screenshots)
enum { FAIL_NOT_FOUND = 1, FAIL_UNUSABLE, FAIL_COPY, FAIL_VERSION, FAIL_MISSING, FAIL_BUILD };
static void set_failure(int k, int have = -1, const char *reason = NULL) {
    static const char *const head[] = {"", "No game files found in that folder", "Those are game packages, not game files",
                                       "Setup didn't finish", "Wrong version of the game", "Your game files are incomplete",
                                       "The game code didn't build"};
    static const char *const hint[] = {
        "",
        PICK_FOLDER_HINT,
        "LAS Recomp needs the game's own files (the exefs and romfs folders), not .xci / .nsp / .nca files.",
        "Make sure there's about 12 GB free and try again.",
        "LAS Recomp needs version 1.0.0, 1.0.1 or 1.1.0 of the game.",
        "Some of the game's files are missing - usually a copy or unzip that stopped partway. Copy the game files "
        "again so none are missing, then choose the folder again.",
        "Make sure there's about 2 GB free and try again. If it keeps happening, report it on GitHub."};
    if (k < 1 || k > 6) return;
    if (k == FAIL_MISSING && have >= 0) {
        char b[320];
        snprintf(b, sizeof b, "The romfs folder has %d of the game's %d files - some are missing, usually from a copy or "
                 "unzip that stopped partway. Copy the game files again so none are missing, then choose the folder "
                 "again.", have, (int)ROMFS_FILES);
        set_job(head[k], true, b);
        return;
    }
    set_job(head[k], true, reason ? std::string(reason) + " " + hint[k] : hint[k]);
}

static bool is_dir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// exefs: the folder with main and main.npdm; romfs: the folder with region_common (the game's data root). Found
// in the picked folder or a few levels below it (some folder layouts nest them).
static void find_game(const std::string &dir, int depth, std::string &exefs, std::string &romfs, bool &unusable) {
    if (depth > 4 || (!exefs.empty() && !romfs.empty())) return;
    if (exefs.empty() && file_exists(dir + "/main") && file_exists(dir + "/main.npdm")) exefs = dir;
    if (romfs.empty() && is_dir(dir + "/region_common")) romfs = dir;
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> sub;
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n[0] == '.') continue;
        std::string ext = n.size() > 4 ? n.substr(n.size() - 4) : "";
        for (auto &ch : ext) ch = (char)tolower((unsigned char)ch);
        if (ext == ".xci" || ext == ".nsp" || ext == ".nca" || ext == ".nsz" || ext == ".xcz") unusable = true;
        if (n != "region_common" && is_dir(dir + "/" + n)) sub.push_back(dir + "/" + n);
    }
    closedir(d);
    std::sort(sub.begin(), sub.end());
    for (auto &s : sub) find_game(s, depth + 1, exefs, romfs, unusable);
}

// A supported game version (runtime/game_addresses/versions.h)? Logs which one.
static bool build_id_ok(const std::string &exefs) {
    char label[32];
    const char *v = game_release_label(exefs.c_str(), label, sizeof label);
    fprintf(stderr, "[launcher] game files: %s\n", v ? (std::string("version ") + v).c_str() : "an unsupported version");
    return v != NULL;
}

// progress (the overlay's bar): the romfs copy (bytes)
static std::atomic<unsigned long long> g_done, g_total;
// The bar: copying is the first tenth, building the game code (runtime/gamebuild.c) the rest
static unsigned long long g_phase_base;
static std::atomic<float> g_build_frac{-1};  // -1: not building
static std::atomic<bool> g_cancel_build{false};
static bool build_cancelled(void) { return g_cancel_build.load(); }
static bool g_copy_skipped;                  // (only the game code to build: the bar is all of that)
static bool g_auto_update;                   // started by itself: game files there, game code from an older version
static void setup_progress(uint64_t done, uint64_t) { g_done = g_phase_base + done; }
static void build_progress(double f, const char *step) {
    g_build_frac = (float)f;
    static const char *last;
    if (step != last) last = step, set_job(step);
}
// Shader preparation, after the game files are ready (runtime/nvn/shader_prep.h): its own bar, and Skip
static std::atomic<float> g_prep_frac{-1};  // -1: not preparing
static volatile int g_prep_skip;
static bool g_prep_skip_ask;  // Skip pressed: "are you sure?" in front of the bar (LARECOMP_LAUNCHER_SHOT=<f>:skipask)
static void prep_progress(int done, int total) { g_prep_frac = total ? (float)done / (float)total : 0.0f; }
static void prepare_shaders(bool background = false) {
    g_prep_skip = 0, g_prep_frac = 0;
    if (!background) set_job("Preparing shaders");
    shader_prep_run((g_root + "/romfs").c_str(), prep_progress, &g_prep_skip);
    g_prep_frac = -1;
}

// Every later start: the same preparation in the background as soon as the start screen is up (quick: the system's
// shader caches already hold the compiled code). Only a PLAY before it is done shows its bar (g_prep_wait).
static std::atomic<bool> g_bg_prep{false};
static bool g_prep_wait;
static void start_background_prep(void) {
    static bool started;  // (once per process: the prepared objects stay for later boots)
    if (started) return;
    started = true;
    g_bg_prep = true;
    pthread_t th;
    pthread_create(&th, NULL, [](void *) -> void * { prepare_shaders(true); g_bg_prep = false; return NULL; }, NULL);
    pthread_detach(th);
}
// Before the game files change (setup, delete): the background preparation stops reading them
static void stop_background_prep(void) {
    if (!g_bg_prep) return;
    g_prep_skip = 1;
    while (g_bg_prep) SDL_Delay(5);
}

static float extract_progress(void) {
    if (float p = g_prep_frac; p >= 0) return p > 1 ? 1 : p;
    unsigned long long t = g_total, d = g_done;
    float b = g_build_frac, f = b >= 0 ? (g_copy_skipped ? b : 0.1f + 0.9f * b) : t ? 0.1f * (float)((double)d / (double)t) : 0;
    return f < 0 ? 0 : f > 0.99f ? 0.99f : f;
}

// Builds the game code from the copied files (a few minutes); false = failed (the job line says so)
static bool build_game_code(void) {
    int cores = SDL_GetNumLogicalCPUCores(), ram_gb = SDL_GetSystemRAM() / 1024;
    GameBuildOpts o = {build_progress, cores < ram_gb - 2 ? cores : ram_gb - 2 > 1 ? ram_gb - 2 : 1, build_cancelled};
    char why[256] = "";
    g_build_frac = 0;
    bool ok = gamebuild_run(g_root.c_str(), &o, why, sizeof why);
    if (!ok) {
        fprintf(stderr, "[launcher] game code: %s\n", why);
        if (g_cancel_build) set_job("Compilation cancelled. Completed files are kept.");
        else if (gamebuild_cfg_stopped())
            set_job("The game code didn't build", true,
                    "The tool that builds the game code was stopped by Windows security settings (Control Flow Guard). "
                    "If you changed Control flow guard in Exploit protection, set it back to the default and try again. "
                    "If it keeps happening, report it on GitHub.");
        else set_failure(FAIL_BUILD, -1, gamebuild_killed() ? why : NULL);
    }
    return ok;
}

static void *extract_thread(void *) {
    stop_background_prep();
    std::string src;
    pthread_mutex_lock(&g_lock);
    src = g_src;
    pthread_mutex_unlock(&g_lock);
    g_cancel_build = false;
    g_done = 0, g_total = 0, g_phase_base = 0, g_build_frac = -1, g_copy_skipped = false;
    game_files_changed();
    std::string exefs, romfs;
    bool unusable = false;  // (game packages rather than its files)
    if (src.empty() && check_copied_files(g_root).empty()) {  // copied already (an update needs new game code)
        g_copy_skipped = true;
        if (!build_game_code()) goto done;
        goto check;
    }
    set_job("Looking for the game files...");
    find_game(src, 0, exefs, romfs, unusable);
    if (exefs.empty() || romfs.empty()) { set_failure(unusable ? FAIL_UNUSABLE : FAIL_NOT_FOUND); goto done; }
    if (!build_id_ok(exefs)) { set_failure(FAIL_VERSION); goto done; }
    if (int have = count_files(romfs); have < ROMFS_FILES) {  // (an incomplete copy: refused before copying it)
        fprintf(stderr, "[launcher] %s: %d of %d romfs files\n", romfs.c_str(), have, (int)ROMFS_FILES);
        set_failure(FAIL_MISSING, have);
        goto done;
    }
    {
        // (the picked folder can't be LAS Recomp's own copy: setting up again deletes that first)
        char a[4096], b[4096];
        mkdir(g_root.c_str(), 0755);
        if (realpath(exefs.c_str(), a) && realpath(g_root.c_str(), b) && !strncmp(a, b, strlen(b)) &&
            (a[strlen(b)] == '/' || a[strlen(b)] == '\\' || !a[strlen(b)])) {
            set_failure(FAIL_NOT_FOUND);
            goto done;
        }
        rt_remove_game_files(g_root.c_str());  // (an earlier or interrupted setup's; removes the folder too)
        game_files_changed();
        mkdir(g_root.c_str(), 0755);
        unsigned long long rbytes = romfs_dir_bytes(romfs.c_str());
        g_total = rbytes;
        set_job("Copying the game files...");
        if (!romfs_copy_tree(exefs.c_str(), (g_root + "/exefs").c_str(), NULL) ||
            !romfs_copy_tree(romfs.c_str(), (g_root + "/romfs").c_str(), setup_progress)) {
            set_failure(FAIL_COPY);
            goto done;
        }
        g_phase_base = rbytes, g_done = rbytes;
        // Retrying a cancelled compilation must use the copied files and their cache.
        pthread_mutex_lock(&g_lock);
        g_src.clear();
        pthread_mutex_unlock(&g_lock);
        if (!build_game_code()) goto done;
    }
check:
    {
        game_files_changed();
        std::string why = check_game_files(g_root);
        if (why.empty()) prepare_shaders();  // (the GPU device is up: the start screen draws with it)
        if (why.empty() && files_changed(g_root)) set_job(CHANGED_HEAD, true, CHANGED_HINT);  // (playable all the same)
        else if (why.empty()) set_job("Game files ready.");
        else if (why.find("version") != std::string::npos) set_failure(FAIL_VERSION);
        else set_failure(FAIL_MISSING, g_romfs_count);
    }
done:
    pthread_mutex_lock(&g_lock);
    g_busy = false;
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

// ------------------------------------------------------------------ folder dialog (SDL calls back on any thread)
// files NULL: no window could be shown (an empty list is a cancel). Linux shows it through the desktop's file chooser
// portal or zenity, and a desktop with neither (a bare window manager) has none: say so instead of Choose doing nothing.
static const char *const PICKER_FAILED = "Couldn't open a folder window";
static std::string picker_hint(const char *what) {
    std::string err = SDL_GetError();
    fprintf(stderr, "[launcher] %s window: %s\n", what, err.c_str());
#ifdef __linux__
    return std::string("LAS Recomp opens the ") + what + " window through your desktop's file chooser "
           "(xdg-desktop-portal) or zenity. Install either one, then try again.";
#else
    return err;
#endif
}

static void SDLCALL picked(void *, const char *const *files, int) {
    if (!files) { set_job(PICKER_FAILED, true, picker_hint("folder")); return; }
    if (!files[0]) return;
    pthread_mutex_lock(&g_lock);
    g_src = files[0];
    if (g_job == PICKER_FAILED) g_job.clear(), g_job_hint.clear(), g_job_failed = false;
    pthread_mutex_unlock(&g_lock);
}

// ------------------------------------------------------------------ OneDrive (Windows)
// A data root inside a OneDrive folder uploads every game file to the player's OneDrive: a warning on the start screen
// with roughly how much, and Choose another folder (the Data folder's Change...: move the data there) or Keep here
// (settings.onedrive_kept). Checked once per launch on the current data root (plat_onedrive_synced); the
// size in the background: the data root's own once set up, before that the picked game files (or a typical copy) plus
// what the setup adds. LARECOMP_TEST_ONEDRIVE=1 (test): shown on any system.
#define GAME_FILES_GB 6.3   // exefs + romfs, as players bring them
#define SETUP_ADDS_GB 0.4   // the built game code and its kept pieces (0.23, a beta_5 setup), shader caches
static int g_onedrive = -1;                // -1: not checked yet
static std::atomic<double> g_od_gb{-1};    // the estimate (-1: not known yet)
static std::atomic<int> g_od_job;
static std::string g_od_for = "\x02";      // what the estimate is for: "" set up, "\x01" nothing picked, else the folder

static void *onedrive_size_thread(void *arg) {
    std::string *src = (std::string *)arg;
    int job = g_od_job;
    double gb;
    if (src->empty()) gb = rt_tree_size(rt_data_root()) / 1e9;
    else {
        std::string exefs, romfs;
        bool unusable = false;
        if (*src != "\x01") find_game(*src, 0, exefs, romfs, unusable);
        gb = (exefs.empty() || romfs.empty() ? GAME_FILES_GB : (rt_tree_size(exefs.c_str()) + rt_tree_size(romfs.c_str())) / 1e9)
             + SETUP_ADDS_GB;
    }
    if (job == g_od_job) g_od_gb = gb;
    delete src;
    return NULL;
}

// The warning, w wide at the cursor (the start screen's upper right; before the setup, under the Game Files rows).
// 1: Keep here pressed, 2: Choose another folder (offered when the data folder can be changed and nothing is running)
static int onedrive_notice(float s, float w, bool ready) {
    std::string src;
    pthread_mutex_lock(&g_lock);
    src = ready ? "" : g_src.empty() ? "\x01" : g_src;
    pthread_mutex_unlock(&g_lock);
    if (src != g_od_for) {  // (set up, or another folder picked: estimate again)
        g_od_for = src, g_od_job++;
        pthread_t th;
        if (pthread_create(&th, NULL, onedrive_size_thread, new std::string(src)) == 0) pthread_detach(th);
    }
    double gb = g_od_gb;
    if (gb < 0) return 0;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.04f, 0.05f, 0.86f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * s, 18 * s));
    ImGui::BeginChild("##onedrive", ImVec2(w, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NavFlattened);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(ImVec4(1, 0.72f, 0.4f, 1), "!  This folder is synced by OneDrive");
    ImGui::TextColored(ImVec4(0.86f, 0.85f, 0.80f, 1), "All game files (about %.0f GB) will be uploaded to your OneDrive.",
                       fmax(1.0, gb));
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 4 * s));
    int r = 0;
    pthread_mutex_lock(&g_lock);
    bool busy = g_busy;
    pthread_mutex_unlock(&g_lock);
    if (!rt_data_root_fixed()) {
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Choose another folder")) r = 2;
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (ImGui::Button("Keep here")) r = 1;
    ImGui::EndChild();
    return r;
}
static void onedrive_keep(void) {
    g_settings.onedrive_kept = true;
    settings_save();
    fprintf(stderr, "[launcher] OneDrive warning: kept here\n");
}

// ------------------------------------------------------------------ the user's own Nintendo fonts (not shipped)
// runtime/hle/pl.c loads fonts/ first, else a close substitute of the requested language. Import copies recognized
// files only: the console's own names for the shared fonts (Standard, Chinese Simplified / its Ext, Chinese
// Traditional, Korean and NintendoExt).
static const char *FONT_STD[] = {"nintendo_udsg-r_std_003.bfttf", "nintendo_udsg-r_std_003.ttf",
                                 "nintendo_udsgr_std_003.ttf", "nintendo_udsgr_std_003.bfttf"};
static const char *FONT_LANG[] = {"nintendo_udsg-r_org_zh-cn_003.bfttf", "nintendo_udsg-r_org_zh-cn_003.ttf",
                                  "nintendo_udsg-r_ext_zh-cn_003.bfttf", "nintendo_udsg-r_ext_zh-cn_003.ttf",
                                  "nintendo_udjxh-db_zh-tw_003.bfttf", "nintendo_udjxh-db_zh-tw_003.ttf",
                                  "nintendo_udsg-r_ko_003.bfttf", "nintendo_udsg-r_ko_003.ttf"};
static const char *FONT_EXT[] = {"nintendo_ext_003.bfttf", "nintendo_ext_003.ttf"};
static std::vector<std::string> g_font_picks;
static std::string g_font_msg, g_font_err;  // (g_font_err: the file window couldn't open; from the dialog's thread)

static bool have_font(const char *const *names, int n) {
    for (int i = 0; i < n; i++)
        if (file_exists(std::string("fonts/") + names[i])) return true;
    return false;
}

static void SDLCALL picked_fonts(void *, const char *const *files, int) {
    std::string err = files ? "" : std::string("Couldn't open a file window. ") + picker_hint("file");
    pthread_mutex_lock(&g_lock);
    for (int i = 0; files && files[i]; i++) g_font_picks.push_back(files[i]);
    if (!files) g_font_err = err;
    pthread_mutex_unlock(&g_lock);
}

static void import_fonts() {
    pthread_mutex_lock(&g_lock);
    std::vector<std::string> picks;
    picks.swap(g_font_picks);
    if (!g_font_err.empty()) g_font_msg = g_font_err, g_font_err.clear();
    pthread_mutex_unlock(&g_lock);
    if (picks.empty()) return;
    mkdir("fonts", 0755);
    int ok = 0, skipped = 0;
    for (auto &src : picks) {
        std::string base = src.substr(src.find_last_of("/\\") + 1), low = base;  // (Windows paths use '\\' too)
        for (auto &c : low) c = (char)tolower(c);
        bool known = false;
        for (auto n : FONT_STD) known |= low == n;
        for (auto n : FONT_LANG) known |= low == n;
        for (auto n : FONT_EXT) known |= low == n;
        if (!known) { skipped++; continue; }
        FILE *in = fopen(src.c_str(), "rb"), *out = fopen(("fonts/" + low).c_str(), "wb");
        if (in && out) {
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
            ok++;
        }
        if (in) fclose(in);
        if (out) fclose(out);
    }
    g_font_msg = std::to_string(ok) + " font file(s) imported" + (skipped ? ", " + std::to_string(skipped) + " not recognized" : "") +
                 ". Used from the next game start.";
}

static void remove_fonts() {
    for (auto n : FONT_STD) unlink((std::string("fonts/") + n).c_str());
    for (auto n : FONT_LANG) unlink((std::string("fonts/") + n).c_str());
    for (auto n : FONT_EXT) unlink((std::string("fonts/") + n).c_str());
    g_font_msg = "Imported fonts removed: the substitute fonts are used.";
}

// ------------------------------------------------------------------ background: a title movie frame
#ifdef __APPLE__
static id<MTLTexture> g_bg;
#define BG_TEXID ((ImTextureID)(__bridge void *)g_bg)
typedef id<MTLDevice> GpuDevice;
#else
static uint64_t g_bg;  // ImGui texture id (Vulkan descriptor set)
#define BG_TEXID ((ImTextureID)g_bg)
typedef void *GpuDevice;
#endif
static int g_bg_w, g_bg_h;

static void load_background(GpuDevice dev) {
    std::string path = g_root + "/romfs/region_common/movie/title.webm";
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path.c_str(), NULL, NULL) < 0) return;
    int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (si >= 0 && avformat_find_stream_info(fmt, NULL) >= 0) {
        AVStream *st = fmt->streams[si];
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        AVCodecContext *cc = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(cc, st->codecpar);
        if (codec && avcodec_open2(cc, codec, NULL) == 0) {
            AVPacket *pkt = av_packet_alloc();
            AVFrame *fr = av_frame_alloc();
            int64_t want = (int64_t)(6.0 / av_q2d(st->time_base));  // a few seconds in: the island, not black
            av_seek_frame(fmt, si, want, AVSEEK_FLAG_BACKWARD);
            bool got = false;
            while (!got && av_read_frame(fmt, pkt) >= 0) {
                if (pkt->stream_index == si && avcodec_send_packet(cc, pkt) == 0)
                    while (avcodec_receive_frame(cc, fr) == 0)
                        if (fr->best_effort_timestamp >= want) { got = true; break; }
                av_packet_unref(pkt);
            }
            if (got) {
                int w = fr->width, h = fr->height;
                std::vector<uint8_t> rgba((size_t)w * h * 4);
                SwsContext *sws = sws_getContext(w, h, (AVPixelFormat)fr->format, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
                uint8_t *dst[1] = {rgba.data()};
                int dst_ls[1] = {w * 4};
                sws_scale(sws, fr->data, fr->linesize, 0, h, dst, dst_ls);
                sws_freeContext(sws);
#ifdef __APPLE__
                MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                              width:w height:h mipmapped:NO];
                g_bg = [dev newTextureWithDescriptor:td];
                [g_bg replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:rgba.data() bytesPerRow:w * 4];
#else
                (void)dev;
                g_bg = vk_imgui_texture_rgba(rgba.data(), w, h);
#endif
                g_bg_w = w, g_bg_h = h;
            }
            av_frame_free(&fr);
            av_packet_free(&pkt);
        }
        avcodec_free_context(&cc);
    }
    avformat_close_input(&fmt);
}

// ------------------------------------------------------------------ seeds
static std::vector<std::string> list_seeds() {
    std::vector<std::string> out;
    if (DIR *d = opendir("rando")) {
        while (struct dirent *e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string dir = std::string("rando/") + e->d_name;
            if (file_exists(dir + "/settings.json") && file_exists(dir + "/romfs")) out.push_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}


bool rando_launcher_seed_id(const char *,char *,size_t);void rando_coop_context(bool);
bool rando_check_seed_id(const char *,char *,size_t);
bool rando_launcher_prepare_seed(const char *,char *,size_t);
bool rando_launcher_take_prepared(std::string &);
#include "laonline_seed_share.h"
bool rando_launcher_reconstruct(const LaoSeedRecipe &,char *,size_t);void rando_launcher_new_seed();
static LaoSeedRecipe coop_expected,coop_received,coop_export;
static bool coop_received_pending=false,coop_received_resolve=false;
static std::string coop_export_path,coop_export_source;static bool coop_export_dialog=false;static int coop_total=0,coop_ready=0;
static std::atomic<bool> coop_file_job{false};
static bool coop_copy(const std::string &text,std::string &status){
 if(text.empty()){status="No validated seed ID to copy.";return false;}
 if(!SDL_SetClipboardText(text.c_str())){status="Clipboard unavailable. Select the Seed ID and press Ctrl+C.";return false;}
 status="Seed ID copied to clipboard!";return true;
}
static void coop_text(const char *label,char *text,size_t capacity,bool readonly,std::string &status){
 ImGui::InputText(label,text,capacity,readonly?ImGuiInputTextFlags_ReadOnly:0);
 if(ImGui::BeginPopupContextItem()){
  if(ImGui::MenuItem("Copy"))coop_copy(text,status);
  if(!readonly&&ImGui::MenuItem("Paste")){char *clip=SDL_GetClipboardText();if(clip){if(strlen(clip)<capacity)snprintf(text,capacity,"%s",clip);else status="Pasted text is too long for a seed ID.";SDL_free(clip);}else status="Clipboard unavailable.";}
  ImGui::EndPopup();
 }
}
static std::string coop_canonical_id(const char *input){
 std::string raw=input?input:"";while(!raw.empty()&&isspace((unsigned char)raw.back()))raw.pop_back();
 size_t first=0;while(first<raw.size()&&isspace((unsigned char)raw[first]))first++;raw.erase(0,first);
 std::string clean;for(unsigned char c:raw){if(c=='-'||c==' ')continue;c=toupper(c);if(c=='O')c='0';if(c=='I'||c=='L')c='1';clean+=c;}
 std::string out;for(size_t i=0;i<clean.size();i++){if(i&&i%5==0)out+='-';out+=clean[i];}return out;
}
#include <mutex>
static std::mutex coop_pick_lock;static std::string coop_picked_seed,coop_metadata_status,coop_downloaded_id,coop_imported_seed;static std::atomic<bool> coop_metadata_busy{false};
static const SDL_DialogFileFilter coop_seed_filter[]={ {"LAS Randomizer seed recipe or metadata","laoseed;zip;json;txt"} };
static void SDLCALL picked_coop_seed(void*,const char *const *files,int){if(files&&files[0]){std::lock_guard<std::mutex> lock(coop_pick_lock);coop_picked_seed=files[0];}}
static void SDLCALL saved_coop_seed(void*,const char *const *files,int){{std::lock_guard<std::mutex> lock(coop_pick_lock);if(files&&files[0])coop_export_path=files[0];coop_export_dialog=false;}}
static const SDL_DialogFileFilter coop_export_filter[]={{"LA-Online HF3 seed recipe","laoseed"}};
static void coop_fetch(bool resolve,const std::string &requested=""){
 if(coop_metadata_busy.exchange(true))return;
 try{std::thread([resolve,requested](){char id[96]={0},hash[65]={0},display[96]={0},error[256]={0};int total=0,ready=0;
  int ok=laonline_seed_descriptor_named(id,sizeof id,hash,sizeof hash,&total,&ready,display,sizeof display,error,sizeof error);
  {std::lock_guard<std::mutex> lock(coop_pick_lock);
   if(ok&&!requested.empty()&&requested!=id){ok=0;snprintf(error,sizeof error,"Seed ID differs from the host world. Use SYNC SEED.");}
   if(ok){coop_received={id,hash,display};coop_received_pending=true;coop_received_resolve=resolve;coop_total=total;coop_ready=ready;}
   coop_metadata_status=ok?"Host seed identity received. Local world verification is required.":!requested.empty()&&error[0]?error:"Could not obtain the host world. Ask the host to Upload Seed and update the relay, then retry. You can also import their Download Seed file.";}
  coop_metadata_busy=false;
 }).detach();}catch(...){coop_metadata_busy=false;}
}
static std::string validate_seed_root(const std::filesystem::path &);
// V13: connection details of the joined room for the seed helper (tools/rando/lao_seed_transfer.py reads server, name,
// lobby + ".rando" and password). Private to this PC; overwritten for each room.
static bool coop_room_ini(const SessionRoomInfo &room){
 std::error_code e;std::filesystem::create_directories("rando",e);std::ofstream f("rando/.lao-room.ini",std::ios::trunc);if(!f)return false;
 std::string lobby=room.lobby;if(lobby.size()>6&&lobby.compare(lobby.size()-6,6,".rando")==0)lobby.resize(lobby.size()-6);
 f<<"server = "<<room.server<<"\nname = "<<room.name<<"\nlobby = "<<lobby<<"\npassword = "<<room.password<<"\n";return (bool)f;
}
static void coop_transfer(bool importing,const std::string &source,const std::string &output){
 if(coop_metadata_busy.exchange(true))return;
 std::string base=std::filesystem::absolute(std::filesystem::path(g_root)/"romfs").string();
 try{std::thread([importing,source,output,base](){
  char helper[1200];std::string result_path=std::filesystem::absolute("rando/.lao-transfer-result.txt").string();
  std::string log_path=std::filesystem::absolute("laonline-seed-transfer.log").string();
  std::error_code make_error;std::filesystem::create_directories("rando",make_error);std::string destination=std::filesystem::absolute(importing?"rando":output).string();
  // V13: "@room" = the co-op room this player is in (relay, room lobby and password written by coop_room_ini)
  bool room=source=="@room"||output=="@room";
  bool syncing=importing&&(source=="@lobby"||source=="@room"),publishing=!importing&&(output=="@lobby"||output=="@room");
  std::string config=std::filesystem::absolute(room?"rando/.lao-room.ini":"laonline.ini").string();
  std::string absolute_source=syncing?config:std::filesystem::absolute(source).string();
  if(publishing)destination=config;
  std::string status="Seed transfer helper missing. Install the complete HF3 Seed Repair update.";std::string imported;
  try{if(rt_resource("rando-gen/rando-gen" PLAT_EXE_SUFFIX,NULL,helper,sizeof helper)){
   const char *argv[]={helper,syncing?"--lao-sync":publishing?"--lao-publish":importing?"--lao-import":"--lao-export",absolute_source.c_str(),base.c_str(),destination.c_str(),importing?result_path.c_str():publishing?NULL:"replace",NULL};
   PlatProc process=plat_spawn(argv,log_path.c_str(),false);coop_file_job=process!=0;int code=process?plat_proc_wait(process,true):-1;coop_file_job=false;
   if(!code){
    if(importing){std::ifstream f(result_path);std::getline(f,imported);while(!imported.empty()&&(imported.back()=='\r'||imported.back()=='\n'))imported.pop_back();if(imported.empty()||imported.find_first_of("/\\:")!=std::string::npos)throw std::runtime_error("Invalid imported folder result");
     std::string error=validate_seed_root(std::filesystem::path("rando")/imported);if(!error.empty())throw std::runtime_error(error);
     status="SEED VERIFIED: imported exact world. No randomizer generation was run.";
    }else if(publishing)status="Named world uploaded. The other PC can now click Sync Seed without generating a new randomizer.";else status="Named world downloaded. Share the .laoseed file; Browse Seeds imports and verifies its patches without running the randomizer.";
   }else{std::ifstream f(log_path);std::string line;while(std::getline(f,line))if(!line.empty())status=line.substr(0,500);}
   {std::ifstream proof(log_path);std::string line;while(std::getline(proof,line))if(line.rfind("{",0)==0)fprintf(stderr,"[laonline] seed.transfer %s\n",line.c_str());}
  }}catch(const std::exception &e){status=e.what();imported.clear();}
  fprintf(stderr,"[laonline] seed transfer (%s): %s\n",syncing?"download":publishing?"upload":importing?"import":"export",status.c_str());  /* V15.2 */
  {std::lock_guard<std::mutex> lock(coop_pick_lock);coop_metadata_status=status;if(!imported.empty())coop_imported_seed=imported;}
  coop_metadata_busy=false;
 }).detach();}catch(...){coop_metadata_busy=false;}
}
/* V13.5 (Velvet: "Seed transfer failed: Host patch package unavailable"): the host uploaded the room's seed only once,
 * from the Play Co-Op page. When that upload was lost (the server's saves folder was deleted, or the host went straight
 * into the game), joiners could never get the seed. The host's game now re-uploads it while hosting a Co-op
 * Randomizer room - also during play: after 1 minute, then every minute for 10 minutes, then every 5 minutes. */
static bool seed_tries_left(int tries){return tries<45;}
/* V15.2 (LAO-7VVLF / LAO-XADXC: joiners stuck on "Waiting for the host's seed"): the keepalive used the seed in the
 * host's SETTINGS - when that was not the room's seed it silently never uploaded. It now finds the host's own seed
 * folder that IS the room's world (same seed ID and world checksum), uploads it 15 s after the room is known, then
 * every minute for 10 minutes and every 5 minutes after, and writes each try to lasrecomp.log. */
extern "C" void laonline_host_seed_keepalive(void){
 static double last=0;static int uploads=0;static std::string room_key,seed;static bool logged_missing;
 SessionRoomInfo r=session_ui_room_info();
 double now=SDL_GetTicks()/1000.0;
 if(!r.in_room||!r.rando||!r.host||r.world.empty()){room_key.clear();return;}
 if(room_key!=r.code+r.world){
  room_key=r.code+r.world;uploads=0;last=now-45;seed.clear();logged_missing=false;
  for(const auto &name:list_seeds()){LaoSeedRecipe local;if(lao_seed_recipe(std::filesystem::path("rando")/name,local)&&local.id==r.seed&&local.hash==r.world){seed=name;break;}}
 }
 if(seed.empty()){if(!logged_missing){logged_missing=true;fprintf(stderr,"[laonline] V15.2 host seed upload: none of this PC's seeds is the room's world %s (%s)\n",r.world.substr(0,16).c_str(),r.seed.c_str());}return;}
 if(now-last<(uploads<10?60.0:300.0)||coop_metadata_busy.load())return;
 last=now;
 if(!coop_room_ini(r)){fprintf(stderr,"[laonline] V15.2 host seed upload: could not write rando/.lao-room.ini\n");return;}
 uploads++;fprintf(stderr,"[laonline] V15.2 host seed upload #%d: %s -> room %s\n",uploads,seed.c_str(),r.code.c_str());
 coop_transfer(false,"rando/"+seed,"@room");
}
/* V14 Global Randomizer: YOUR seed, picked by YOU. V13.5 silently skipped every seed that came from a .laoseed
 * file (lao_source_identity.json) - also a player's own seed she had exported and imported again - so Velvet could
 * not join at all (V13.1 simply used the last seed). Now Join Global Randomizer opens a list of ALL seeds on this
 * PC (yours first, then imported ones, each labelled), plus "Load a seed file" and "Make a new seed". The choice is
 * remembered in laonline-global-seed.txt. Nothing is synchronized in Global: it is only your own world. */
struct GlobalSeed { std::string folder, label; bool imported; long long when; };
static std::vector<GlobalSeed> global_seeds(){
 std::vector<GlobalSeed> out;
 for(const auto &name:list_seeds()){
  std::filesystem::path dir=std::filesystem::path("rando")/name;std::error_code ec;
  bool imported=std::filesystem::exists(dir/"lao_source_identity.json",ec);
  auto t=std::filesystem::last_write_time(dir/"settings.json",ec);
  GlobalSeed g{name,lao_seed_display_name(dir),imported,ec?0:(long long)t.time_since_epoch().count()};
  if(g.label.empty())g.label=name;
  out.push_back(g);
 }
 std::sort(out.begin(),out.end(),[](const GlobalSeed &a,const GlobalSeed &b){if(a.imported!=b.imported)return !a.imported;return a.when>b.when;});
 return out;
}
static std::string global_seed_saved(){std::string saved;std::ifstream f("laonline-global-seed.txt");std::getline(f,saved);
 while(!saved.empty()&&(saved.back()=='\r'||saved.back()==' '))saved.pop_back();return saved;}
static void global_seed_save(const std::string &folder){std::ofstream f("laonline-global-seed.txt",std::ios::trunc);f<<folder<<"\n";}
/* the seed Global Randomizer starts with: the one you chose last, else your newest own seed, else the newest one */
static std::string global_seed_pick(int){
 auto all=global_seeds();if(all.empty())return "";
 std::string saved=global_seed_saved();
 for(auto &g:all)if(g.folder==saved)return g.folder;
 return all[0].folder;
}
// Strict bounded JSON syntax check before handing settings to upstream code.
struct CoopJson {
 const std::string &s;size_t p=0;int depth=0;
 void ws(){while(p<s.size()&&(s[p]==' '||s[p]=='\r'||s[p]=='\n'||s[p]=='\t'))p++;}
 bool string(){if(p>=s.size()||s[p++]!='"')return false;while(p<s.size()){unsigned char c=s[p++];if(c=='"')return true;if(c<32)return false;if(c=='\\'){if(p>=s.size())return false;char e=s[p++];if(e=='u'){for(int i=0;i<4;i++)if(p>=s.size()||!isxdigit((unsigned char)s[p++]))return false;}else if(!strchr("\"\\/bfnrt",e))return false;}}return false;}
 bool value(){ws();if(++depth>64||p>=s.size())return false;bool ok=false;char c=s[p];
 if(c=='"')ok=string();
 else if(c=='{'||c=='['){p++;ws();char end=c=='{'?'}':']';if(p<s.size()&&s[p]==end){p++;ok=true;}else for(;;){if(c=='{'){if(!string())break;ws();if(p>=s.size()||s[p++]!=':')break;}if(!value())break;ws();if(p>=s.size())break;if(s[p]==end){p++;ok=true;break;}if(s[p++]!=',')break;ws();}}
 else if(c=='t'||c=='f'||c=='n'){const char *word=c=='t'?"true":c=='f'?"false":"null";size_t n=strlen(word);ok=s.compare(p,n,word)==0;if(ok)p+=n;}
 else {size_t begin=p;if(s[p]=='-')p++;if(p<s.size()&&s[p]=='0')p++;else{if(p>=s.size()||s[p]<'1'||s[p]>'9'){depth--;return false;}while(p<s.size()&&isdigit((unsigned char)s[p]))p++;}ok=p>begin;if(p<s.size()&&s[p]=='.'){p++;size_t digits=p;while(p<s.size()&&isdigit((unsigned char)s[p]))p++;ok=ok&&p>digits;}if(p<s.size()&&(s[p]=='e'||s[p]=='E')){p++;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))p++;size_t digits=p;while(p<s.size()&&isdigit((unsigned char)s[p]))p++;ok=ok&&p>digits;}}
 depth--;return ok;
 }
 bool valid(){ws();if(p>=s.size()||s[p]!='{')return false;bool ok=value();ws();return ok&&p==s.size();}
};
static std::string coop_json_seed(const std::string &json){
 CoopJson parser{json};parser.p=1;
 for(;;){parser.ws();size_t key=parser.p;if(!parser.string())return "";std::string name=json.substr(key+1,parser.p-key-2);
  parser.ws();if(parser.p>=json.size()||json[parser.p++]!=':')return "";parser.ws();
  if(name=="seed"){size_t value=parser.p;if(!parser.string())return "";return json.substr(value+1,parser.p-value-2);}
  if(!parser.value())return "";parser.ws();if(parser.p>=json.size()||json[parser.p++]!=',')return "";
 }
}
static std::string validate_seed_root(const std::filesystem::path &root){
 namespace fs=std::filesystem;std::error_code ec;
 if(!fs::is_directory(root/"romfs",ec)||!fs::is_directory(root/"exefs",ec)||!fs::is_regular_file(root/"settings.json",ec)||!fs::is_regular_file(root/"exefs/1.0.0.pchtxt",ec))return "Choose a complete generated LAS Recomp seed folder, not just a seed name or screenshot.";
 for(const char *file:{"tracker.tsv","logic.json","flags.tsv","check_flags.tsv"})if(!fs::is_regular_file(root/file,ec))return "Seed generation is incomplete: required tracker/logic metadata is missing.";
 if(fs::file_size(root/"settings.json",ec)>1024*1024)return "Seed settings are too large.";
 std::ifstream config(root/"settings.json",std::ios::binary);std::string json((std::istreambuf_iterator<char>(config)),{});if(!CoopJson{json}.valid())return "Seed settings.json is invalid. Generate or import a complete supported seed.";
 std::ifstream patches(root/"exefs/1.0.0.pchtxt");std::string line;bool compatible=false;unsigned char build[20]={0};
 while(std::getline(patches,line)){if(line.rfind("@nsobid-",0))continue;if(line.size()<48)break;bool valid=true;for(int i=0;i<20;i++){char hex[3]={line[8+2*i],line[9+2*i],0};char *end;unsigned long v=strtoul(hex,&end,16);if(*end){valid=false;break;}build[i]=(uint8_t)v;}const GameVersion *version=valid?game_version_by_build_id(build):nullptr;compatible=version&&!strcmp(version->key,"v1_0_0");break;}
 if(!compatible)return "The seed patches do not identify a supported game revision.";

 std::ifstream idfile(root/"seed_id.txt");std::string id;std::getline(idfile,id);char error[256];
 while(!id.empty()&&isspace((unsigned char)id.back()))id.pop_back();
 if(coop_canonical_id(coop_json_seed(json).c_str())!=coop_canonical_id(id.c_str()))return "Seed ID does not match the generated settings.json. Regenerate this world using the host's ID.";
 if(id.empty())return "This seed needs a valid upstream LAS Recomp seed_id.txt. Open Randomizer settings to generate a supported seed.";if(!rando_check_seed_id(id.c_str(),error,sizeof error))return error;
 return "";
}
static std::string validate_coop_seed(const std::string &name){
 if(name.empty()||name=="."||name==".."||name.find_first_of("/\\:")!=std::string::npos)return "Select a prepared seed before starting.";
 auto root=std::filesystem::path("rando")/name;std::string error=validate_seed_root(root);if(!error.empty())return error;
 LaoSeedRecipe local;if(!lao_seed_recipe(root,local))return "Could not verify the prepared world.";
 if(!coop_expected.hash.empty()){
  fprintf(stderr,"[laonline] seed.compare id_host=%s id_client=%s hash_host=%s hash_client=%s active_folder=%s generator=%s\n",coop_expected.id.c_str(),local.id.c_str(),coop_expected.hash.c_str(),local.hash.c_str(),name.c_str(),local.generator_id.c_str());
  laonline_diag_seed("prelaunch",coop_expected.id.c_str(),local.id.c_str(),coop_expected.hash.c_str(),local.hash.c_str(),name.c_str(),local.id!=coop_expected.id?"canonical_id":local.hash!=coop_expected.hash?"world_sha256":"verified");
  if(local.id!=coop_expected.id)return "Canonical seed ID mismatch: expected "+coop_expected.id+"; selected "+local.id+". No folder-name comparison was used.";
  if(local.hash!=coop_expected.hash)return "Full world checksum mismatch: expected "+coop_expected.hash+"; selected "+local.hash+". Existing seeds were preserved.";}
 return "";
}
static std::string import_coop_seed(const std::string &picked,std::string &selected){
 namespace fs=std::filesystem;try{
 fs::path root=fs::path(picked).parent_path();std::string error=validate_seed_root(root);if(!error.empty())return error;
 std::string name=root.filename().string();if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_- ()")!=std::string::npos)return "Seed folder names may use letters, numbers, spaces, parentheses, underscores or hyphens.";
 fs::path destination=fs::path("rando")/name;
 if(fs::weakly_canonical(root)==fs::weakly_canonical(destination)){selected=name;return "";}
 std::error_code import_ec;
 LaoSeedRecipe original;if(!lao_seed_recipe(root,original))return "Could not verify the source world.";
 for(const auto &entry:fs::directory_iterator(fs::path("rando"),import_ec)){
  if(!entry.is_directory()||entry.path().filename().string()[0]=='.')continue;LaoSeedRecipe existing;
  if(lao_seed_recipe(entry.path(),existing)&&existing.id==original.id&&existing.hash==original.hash){selected=entry.path().filename().string();return "";}
 }
 for(int suffix=2;fs::exists(destination);suffix++)destination=fs::path("rando")/(name+" ("+std::to_string(suffix)+")");
 uint64_t total=0;unsigned count=0;std::vector<fs::path> files;
 for(const auto &entry:fs::recursive_directory_iterator(root)){
  auto rel=entry.path().lexically_relative(root);if(*rel.begin()=="save"){continue;}
  if(entry.is_symlink())return "Seed packages cannot contain symbolic links.";
  if(entry.is_directory())continue;if(!entry.is_regular_file())return "Unsupported seed file.";
  auto ext=entry.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(tolower(c));});if(ext==".exe"||ext==".dll"||ext==".bat"||ext==".ps1"||ext==".py"||ext==".cmd")return "Executable files are not allowed in seed imports.";
  total+=entry.file_size();if(++count>30000||total>1024ull*1024*1024)return "Seed exceeds the import limits.";files.push_back(rel);
 }
 // Validate before creating anything. Destination never overwrites existing seed/save data.
 fs::create_directories("rando");fs::path stage=fs::path("rando")/(".import-"+name+"-"+std::to_string(SDL_GetTicksNS()));if(!fs::create_directory(stage))return "Could not create a seed import staging folder.";for(const auto &rel:files){fs::create_directories((stage/rel).parent_path());fs::copy_file(root/rel,stage/rel);}LaoSeedRecipe copied;std::string checked=validate_seed_root(stage);
 if(!checked.empty()||!lao_seed_recipe(stage,copied)||copied.id!=original.id||copied.hash!=original.hash)return "Staged import does not match the source. Existing worlds were preserved; no imported world was activated.";
 {std::ofstream label(stage/"seed_display_name.txt",std::ios::binary);label<<original.name<<"\n";if(!label)return "Could not preserve imported name.";}
 fs::rename(stage,destination);
 selected=destination.filename().string();return "";
 }catch(const std::exception &e){return "Seed import failed. The original seed and existing saves were preserved.";}
}
// ------------------------------------------------------------------ screen
static ImFont *g_font_title, *g_font_menu, *g_font_small;
static bool g_need_update_popup;  // V14.1: online play was blocked by an available update - show the update window
static bool launcher_quit_requested;
enum Page { MAIN, SETTINGS, SETUP, COOP_RANDO, COOP_HOST, LOBBY_BROWSER, GLOBAL_ONLINE, LEADERBOARDS, CREDITS };  // SETTINGS: the in-game menu's tabs (runtime/ui/menu.mm)
static int g_refocus = MAIN;  // back on the main page: select the item of the page we came from (controllers)

// A big menu line: gold + a marker when focused / hovered (keyboard, gamepad or mouse), grey when disabled
static bool g_menu_small;  // sub pages: buttons in the small font
void menu_tabs_draw(float scale, ImFont *font, ImFont *font_big, bool in_game, void (*prelaunch)(float));  // menu.mm
void menu_tabs_focus(bool first_tab);
extern int g_tab_request_ext;
extern "C" void menu_apply_window_requests(void *sdl_window);
extern "C" void input_cursor_event(const SDL_Event *e);  // runtime/hle/input.c
extern "C" bool input_cursor_hidden(void);
extern "C" int g_start_screen;  // runtime/hle/warp.c: the start screen is up (no game loaded: don't read its memory)
// The Randomizer tab's Play / Play Now on the start screen (rando_tab.mm): start the game with that seed
static std::string g_play_seed;
static bool g_play_seed_req, g_play_pending;
extern "C" void launcher_play_seed(const char *seed) {
    g_play_seed = seed ? seed : "";
    g_play_seed_req = true;
}
static float g_menu_gap = 0.35f;  // space under each item, in line heights

// The first PLAY ever (settings.menu_tip_seen): a note on opening the LAS Recomp menu in game (it isn't obvious),
// OK starts the game. Returns true once OK is pressed.
static bool g_tip_test;  // (LARECOMP_LAUNCHER_SHOT=<frame>:tip)
// First PLAY ever, before the menu note: authentic 16:9 or the unlocked (fill the window) aspect ratio.
static bool aspect_prompt(float s) {
    bool done = false;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x * 0.8f, 760 * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36 * s, 28 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16 * s);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.03f, 0.04f, 0.05f, 0.94f));
    bool open = ImGui::BeginPopupModal("##aspect", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
        ImGui::SetWindowFontScale(1.25f);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("How would you like to play?");
        ImGui::Spacing();
        ImGui::TextDisabled("You can change this any time in Settings.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 14 * s));
        float bw = 300 * s, gap = 20 * s;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 2 * bw - gap) * 0.5f);
        int pick = -1;
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Authentic 16:9", ImVec2(bw, 44 * s))) pick = ASPECT_ORIGINAL;
        ImGui::SameLine(0, gap);
        if (ImGui::Button("Unlocked (fill the screen)", ImVec2(bw, 44 * s))) pick = ASPECT_UNLOCKED;
        if (pick >= 0) {
            g_settings.aspect = pick;
            g_settings.aspect_asked = true;
            settings_save();
            ImGui::CloseCurrentPopup();
            done = true;
        }
        ImGui::EndPopup();
    }
    return done;
}

static bool menu_tip(float s) {
    bool go = false;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x * 0.8f, 760 * s), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36 * s, 28 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16 * s);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.03f, 0.04f, 0.05f, 0.94f));
    bool open = ImGui::BeginPopupModal("##menutip", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
        ImGui::SetWindowFontScale(1.25f);
        // the controller's home button first (what players reach for: the PS button on a DualSense), the combo second
        static const char *const home[] = {"Home button", "Xbox button", "PlayStation button"};  // (bind_label_family)
        static const char *const pad[] = {"Minus + Plus", "View + Menu", "Create + Options"};
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("LAS Recomp has its own menu for settings, the randomizer, cosmetics and more.");
        ImGui::Spacing();
        int fam = bind_label_family() % 3;
        ImGui::Text("Open it in game with F1 or Esc, or on a controller with the %s (or %s together).", home[fam], pad[fam]);
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 14 * s));
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 180 * s) * 0.5f);
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("OK", ImVec2(180 * s, 44 * s)) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
            g_settings.menu_tip_seen = true;
            settings_save();
            ImGui::CloseCurrentPopup();
            go = true;
        }
        ImGui::EndPopup();
    }
    return go;
}

// The game files page's example: a Finder-style window (the same on every platform) with the folder to choose -
// "Game files", opened: exefs in it opened (its files), romfs closed. All drawn here (shapes and the page's font, sized from the font: sharp at
// any scale), up to w wide, at the cursor; it takes its space with a Dummy.
enum { FOLDER_EXAMPLE_ROWS = 9 };
static float folder_example_height(void) {  // (its window, without the Dummy's gap under it)
    float fs = ImGui::GetFontSize();
    return fs * 1.45f + fs * 0.35f * 2 + FOLDER_EXAMPLE_ROWS * fs * 1.12f;
}
static void draw_folder_example(float w) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImFont *font = ImGui::GetFont();
    float fs = ImGui::GetFontSize(), ts = fs * 0.82f;  // (the rows' text a little under the page's)
    float rh = fs * 1.12f, bar = fs * 1.45f, pad = fs * 0.35f, r = fs * 0.45f;
    struct Item { const char *name; int depth; int kind; };  // kind: 0 file, 1 folder opened, 2 folder closed
    static const Item items[] = {{"Game files", 0, 1}, {"exefs", 1, 1}, {"main", 2, 0}, {"main.npdm", 2, 0},
                                 {"rtld", 2, 0}, {"sdk", 2, 0}, {"subsdk0", 2, 0}, {"subsdk1", 2, 0}, {"romfs", 1, 2}};
    const int n = FOLDER_EXAMPLE_ROWS;
    static_assert(sizeof items / sizeof items[0] == FOLDER_EXAMPLE_ROWS, "one row per item");
    w = fminf(w, fs * 15);
    float h = folder_example_height();
    ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + w, a.y + h);
    const ImU32 body = IM_COL32(24, 28, 27, 255), title = IM_COL32(40, 45, 43, 255), edge = IM_COL32(255, 255, 255, 38),
                text = IM_COL32(220, 216, 205, 255), dim = IM_COL32(150, 152, 144, 255),
                gold = IM_COL32(236, 194, 84, 255), gold_tab = IM_COL32(200, 160, 62, 255),
                paper = IM_COL32(226, 224, 216, 255), fold = IM_COL32(160, 158, 150, 255),
                select = IM_COL32(32, 84, 176, 255), text_sel = IM_COL32(250, 250, 255, 255);
    dl->AddRectFilled(ImVec2(a.x, a.y + fs * 0.12f), ImVec2(b.x, b.y + fs * 0.22f), IM_COL32(0, 0, 0, 70), r);  // shadow
    dl->AddRectFilled(a, b, body, r);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + bar), title, r, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(a.x, a.y + bar), ImVec2(b.x, a.y + bar), IM_COL32(0, 0, 0, 90), 1.0f);
    dl->AddRect(a, b, edge, r, 0, 1.0f);
    // the title bar: three dots on the left, the folder it's in (a typical one) in the middle
    static const ImU32 dots[] = {IM_COL32(255, 95, 87, 255), IM_COL32(254, 188, 46, 255), IM_COL32(40, 200, 64, 255)};
    float dr = fs * 0.21f, cy = a.y + bar * 0.5f;
    for (int i = 0; i < 3; i++) dl->AddCircleFilled(ImVec2(a.x + fs * 0.65f + i * fs * 0.62f, cy), dr, dots[i]);
    const char *t = "Documents";
    ImVec2 tsz = font->CalcTextSizeA(ts, FLT_MAX, 0, t);
    dl->AddText(font, ts, ImVec2(a.x + (w - tsz.x) * 0.5f, cy - tsz.y * 0.5f), text, t);
    // the list: a disclosure triangle on folders, the folder / file glyph, the name
    float g = fs * 0.78f;  // (glyph size)
    for (int i = 0; i < n; i++) {
        const Item &it = items[i];
        float y = a.y + bar + pad + i * rh, my = y + rh * 0.5f;
        if (i == 0)  // the folder to choose: selected, as Finder shows it
            dl->AddRectFilled(ImVec2(a.x + fs * 0.25f, y), ImVec2(b.x - fs * 0.25f, y + rh), select, fs * 0.25f);
        else if (i & 1) dl->AddRectFilled(ImVec2(a.x + 1, y), ImVec2(b.x - 1, y + rh), IM_COL32(255, 255, 255, 9));
        float x = a.x + fs * 0.5f + it.depth * fs * 1.25f;
        if (it.kind) {  // ▼ opened, ▶ closed
            float u = fs * 0.2f, cx = x + u;
            ImU32 tc = i == 0 ? text_sel : dim;
            if (it.kind == 1)
                dl->AddTriangleFilled(ImVec2(cx - u, my - u * 0.6f), ImVec2(cx + u, my - u * 0.6f),
                                      ImVec2(cx, my + u * 0.75f), tc);
            else
                dl->AddTriangleFilled(ImVec2(cx - u * 0.6f, my - u), ImVec2(cx - u * 0.6f, my + u),
                                      ImVec2(cx + u * 0.75f, my), tc);
        }
        x += fs * 0.6f;
        float gx = x, gy = my - g * 0.5f;
        if (it.kind) {  // folder: a tab, then the body over it
            dl->AddRectFilled(ImVec2(gx, gy + g * 0.12f), ImVec2(gx + g * 0.46f, gy + g * 0.42f), gold_tab, g * 0.1f);
            dl->AddRectFilled(ImVec2(gx, gy + g * 0.26f), ImVec2(gx + g, gy + g * 0.9f), gold, g * 0.1f);
        } else {  // file: a page with its top right corner folded
            float l = gx + g * 0.16f, rr = gx + g * 0.84f, tp = gy + g * 0.02f, bt = gy + g * 0.98f, c = g * 0.24f;
            ImVec2 pts[] = {ImVec2(l, tp), ImVec2(rr - c, tp), ImVec2(rr, tp + c), ImVec2(rr, bt), ImVec2(l, bt)};
            dl->AddConvexPolyFilled(pts, 5, paper);
            dl->AddTriangleFilled(ImVec2(rr - c, tp), ImVec2(rr - c, tp + c), ImVec2(rr, tp + c), fold);
        }
        ImVec2 nsz = font->CalcTextSizeA(ts, FLT_MAX, 0, it.name);
        float nx = x + g + fs * 0.4f;
        dl->AddText(font, ts, ImVec2(nx, my - nsz.y * 0.5f), i == 0 ? text_sel : text, it.name);
        if (i == 0) {  // an arrow pointing back at the name, then what to do (smaller when the row is narrow)
            const char *hint = "Choose this one";
            float ax = nx + nsz.x + fs * 0.5f, aw = fs * 1.1f, gap = fs * 0.3f, room = b.x - fs * 0.6f - ax;
            float hs = ts, hw = font->CalcTextSizeA(hs, FLT_MAX, 0, hint).x;
            if (aw + gap + hw > room) hs *= fmaxf(0.7f, (room - aw - gap) / hw);
            float hx = ax + aw + gap, th = fs * 0.09f, hd = fs * 0.26f;
            dl->AddLine(ImVec2(ax + th, my), ImVec2(ax + aw, my), gold, th);
            dl->AddTriangleFilled(ImVec2(ax - th, my), ImVec2(ax + hd, my - hd), ImVec2(ax + hd, my + hd), gold);
            dl->AddText(font, hs, ImVec2(hx, my - font->CalcTextSizeA(hs, FLT_MAX, 0, hint).y * 0.5f), gold, hint);
        }
    }
    ImGui::Dummy(ImVec2(w, h));
}

// Start-screen text: a soft drop shadow, then the text with a top-to-bottom colour gradient (top == bottom: flat)
static void text_fx(ImDrawList *dl, ImFont *font, float size, ImVec2 p, ImU32 top, ImU32 bottom, const char *t,
                    float shadow) {
    if (shadow > 0)
        for (float d : {shadow, shadow * 0.5f}) dl->AddText(font, size, ImVec2(p.x + d, p.y + d), IM_COL32(4, 8, 30, 110), t);
    int v0 = dl->VtxBuffer.Size;
    dl->AddText(font, size, p, top, t);
    if (top == bottom) return;
    float y0 = p.y + size * 0.2f, y1 = p.y + size * 0.85f;
    for (int i = v0; i < dl->VtxBuffer.Size; i++) {
        float f = (dl->VtxBuffer[i].pos.y - y0) / (y1 - y0);
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        ImVec4 a = ImGui::ColorConvertU32ToFloat4(top), b = ImGui::ColorConvertU32ToFloat4(bottom);
        dl->VtxBuffer[i].col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f,
                                                                     a.z + (b.z - a.z) * f, a.w + (b.w - a.w) * f));
    }
}

// A four-point sparkle (PLAY's twinkle): k = 0..1 grows and fades it
static void sparkle(ImDrawList *dl, ImVec2 c, float r, float k, float spin) {
    if (k <= 0) return;
    ImVec2 pts[8];
    for (int i = 0; i < 8; i++) {
        float a = spin + i * 3.14159265f / 4, rr = (i & 1 ? r * 0.22f : r) * k;
        pts[i] = ImVec2(c.x + cosf(a) * rr, c.y + sinf(a) * rr);
    }
    dl->AddCircleFilled(c, r * 0.55f * k, IM_COL32(255, 230, 150, (int)(60 * k)));  // soft glow
    dl->AddConvexPolyFilled(pts, 8, IM_COL32(255, 248, 215, (int)(235 * k)));
}

// split .. split_end: that part of the label in its own colour (PLAY's choice: Vanilla in vanilla cream)
static bool menu_item(const char *label, bool enabled, bool *hovered = NULL, const char *split = NULL, ImU32 col2 = 0,
                      const char *split_end = NULL) {
    ImGui::PushFont(g_menu_small ? g_font_small : g_font_menu);
    ImGui::BeginDisabled(!enabled);
    ImVec2 at = ImGui::GetCursorScreenPos();
    // (the ID from the label's fixed part: PLAY's changes with the seed shown, and a new ID loses the selection)
    ImGui::PushID(label, split ? split : label + strlen(label));
    ImVec2 sz = ImGui::CalcTextSize(label);
    // (EnableNav: otherwise an InvisibleButton can't be reached with the d-pad / arrow keys - ImGui 1.91)
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));  // (the hover band shows the selection, no box)
    bool clicked = ImGui::InvisibleButton("##b", ImVec2(sz.x + 40, sz.y + 6), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    bool hot = ImGui::IsItemFocused() || ImGui::IsItemHovered();
    ImGui::PopID();
    if (hovered) *hovered = hot;
    ImU32 col = !enabled ? IM_COL32(170, 175, 200, 140) : hot ? IM_COL32(255, 214, 64, 255) : IM_COL32(250, 250, 255, 255);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (hot && enabled) {  // a shadow band behind the item (like the version corner's), feathered, with a warm glow
        float x0 = ImGui::GetWindowPos().x, x1 = at.x + sz.x * 0.75f, x2 = at.x + sz.x + sz.y * 3.5f;
        float f = sz.y * 0.22f, y0 = at.y - f * 0.4f, y1 = y0 + f, y3 = at.y + sz.y + f * 0.2f, y2 = y3 - f;
        ImU32 c = IM_COL32(6, 10, 34, 150), g = IM_COL32(255, 190, 80, 34), n = IM_COL32(6, 10, 34, 0), ng = IM_COL32(255, 190, 80, 0);
        for (int pass = 0; pass < 2; pass++) {  // dark band, then the glow over it
            ImU32 a = pass ? g : c, z = pass ? ng : n;
            dl->AddRectFilledMultiColor(ImVec2(x0, y1), ImVec2(x1, y2), a, a, a, a);
            dl->AddRectFilledMultiColor(ImVec2(x1, y1), ImVec2(x2, y2), a, z, z, a);
            dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y1), z, z, a, a);  // (feathered top / bottom)
            dl->AddRectFilledMultiColor(ImVec2(x1, y0), ImVec2(x2, y1), z, z, z, a);
            dl->AddRectFilledMultiColor(ImVec2(x0, y2), ImVec2(x1, y3), a, a, z, z);
            dl->AddRectFilledMultiColor(ImVec2(x1, y2), ImVec2(x2, y3), a, z, z, z);
        }
    }
    ImU32 col_b = hot && enabled ? IM_COL32(255, 160, 50, 255) : col;
    if (split && enabled) {
        std::string head(label, split - label);
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), at, col, col_b, head.c_str(), sz.y * 0.05f);
        std::string mid = split_end ? std::string(split, split_end - split) : std::string(split);
        ImVec2 p2(at.x + ImGui::CalcTextSize(label, split).x, at.y);
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), p2, col2, col2, mid.c_str(), sz.y * 0.05f);
        if (split_end)
            text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + ImGui::CalcTextSize(label, split_end).x, at.y),
                    col, col, split_end, sz.y * 0.05f);
    } else
        text_fx(dl, ImGui::GetFont(), ImGui::GetFontSize(), at, col, col_b, label, enabled ? sz.y * 0.05f : 0);
    if (g_menu_gap >= 0) ImGui::Dummy(ImVec2(0, sz.y * g_menu_gap));  // line spacing
    else ImGui::SetCursorPosY(ImGui::GetCursorPosY() + sz.y * g_menu_gap);  // (tighter than the font's line height)
    ImGui::EndDisabled();
    ImGui::PopFont();
    return clicked;
}

// Controllers, as in the in-game menu (runtime/ui/menu.mm): read straight from SDL, confirm / back as the Confirm
// button setting says (pad_nav.h: the game's layout, or the controller's convention); d-pad / left stick move.
// (ImGui's own SDL gamepad handling is switched off: per the user, it didn't work on the start screen.)

static void launcher_pad_open_all(void) {
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
}

// true when the event was a controller event (handled here)
static bool launcher_pad_event(const SDL_Event &e) {
    ImGuiIO &io = ImGui::GetIO();
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(e.gdevice.which);
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        ImGuiKey k = ui_pad_key(e.gbutton.which, e.gbutton.button, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        float v = e.gaxis.value / 32767.0f;
        // the left stick also works as the d-pad (ImGui moves between items with the d-pad only; the stick scrolls):
        // pressed past half way, released under 0.3
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            static bool held[4];  // left, right, up, down
            bool x = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX;
            int neg = x ? 0 : 2, pos = neg + 1;
            const ImGuiKey keys[4] = {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadDpadUp,
                                      ImGuiKey_GamepadDpadDown};
            for (int i : {neg, pos}) {
                float m = i == neg ? -v : v;
                bool now = held[i] ? m > 0.3f : m > 0.5f;
                if (now != held[i]) { held[i] = now; io.AddKeyEvent(keys[i], now); }
            }
        }
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, v < -0.3f, v < -0.3f ? -v : 0);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, v > 0.3f, v > 0.3f ? v : 0);
        } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, v < -0.3f, v < -0.3f ? -v : 0);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, v > 0.3f, v > 0.3f ? v : 0);
        }
        return true;
    }
    default: return false;
    }
}

// Returns when PLAY is chosen (seed stored in g_settings.rando_seed); QUIT exits.
// Settings' first tab, on the start screen only: the extracted game files (size, delete) and the user's own Switch
// fonts (import / back to the substitute)
// Open a folder in Finder / Explorer (relative paths are in the data root, the working directory)
static void reveal_folder(const std::string &dir) {
    std::string path = dir;
    bool abs = !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
    if (!abs) path = std::string(rt_data_root()) + "/" + path;
    std::string url = "file://";
    if (path[0] != '/') url += "/";  // (Windows: file:///C:/...)
    for (char ch : path) {
        if (ch == '\\') ch = '/';
        if (ch == ' ') url += "%20";
        else if (ch == '%') url += "%25";
        else if (ch == '#') url += "%23";
        else url += ch;
    }
    SDL_OpenURL(url.c_str());
}

static SDL_Window *g_launcher_win;
// The progress overlay in front of the start screen (input waits): the setup, an update's build, or the shader
// preparation (with Skip) - the setup's, or the background one when PLAY was pressed before it finished
static void draw_busy_overlay(ImVec2 ds, const std::string &job, bool test_busy, bool prep_wait = false) {
    float sc = ds.y / 1080.0f;
    ImDrawList *fg = ImGui::GetForegroundDrawList();
    ImVec2 o = ImGui::GetMainViewport()->Pos;
    if (!g_auto_update) fg->AddRectFilled(o, ImVec2(o.x + ds.x, o.y + ds.y), IM_COL32(0, 0, 0, 170));  // (an update: plain)
    float f = test_busy && g_prep_frac < 0 ? 0.42f : prep_wait && g_prep_frac < 0 ? 0.99f : extract_progress();
    // (while the game code is built: what's happening, on whose files - runtime/gamebuild.c)
    bool preparing = g_prep_frac >= 0 || prep_wait, recompiling = g_build_frac >= 0 && !preparing;
    const char *title = preparing ? "Preparing shaders" : g_auto_update ? "Updating the game"
                      : recompiling ? "Recompiling the game" : "Setting up the game files";
    static const char *const notes[] = {"This only needs to be done once and may take a while."};
    int nnotes = recompiling && !g_auto_update ? 1 : 0;
    float tfs = g_font_menu->FontSize * 0.75f, sfs = g_font_small->FontSize * 0.85f;
    ImVec2 ts = g_font_menu->CalcTextSizeA(tfs, FLT_MAX, 0, title);
    float line_h = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, "A").y;
    float pw = fminf(ds.x * 0.6f, 900 * sc), bar_y = 36 * sc + ts.y + 30 * sc;
    for (int i = 0; i < nnotes; i++)  // (wide enough for its text, within the window)
        pw = fmaxf(pw, g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, notes[i]).x + 96 * sc);
    pw = fminf(fmaxf(pw, ts.x + 96 * sc), ds.x - 32 * sc);
    float ph = bar_y + 24 * sc + 26 * sc + line_h + (nnotes ? 14 * sc + nnotes * line_h : 0) + 40 * sc +
               (preparing ? line_h + 28 * sc : recompiling ? 54 * sc : 0);  // (Skip / Cancel)
    ImVec2 a(o.x + (ds.x - pw) * 0.5f, o.y + (ds.y - ph) * 0.5f), b(a.x + pw, a.y + ph);
    fg->AddRectFilled(a, b, IM_COL32(15, 23, 20, 250), 14 * sc);
    fg->AddRect(a, b, IM_COL32(236, 194, 84, 140), 14 * sc, 0, 1.5f * sc);
    fg->AddText(g_font_menu, tfs, ImVec2((a.x + b.x - ts.x) * 0.5f, a.y + 36 * sc), IM_COL32(236, 194, 84, 255), title);
    ImVec2 ba(a.x + 48 * sc, a.y + bar_y), bb(b.x - 48 * sc, a.y + bar_y + 24 * sc);
    fg->AddRectFilled(ba, bb, IM_COL32(255, 255, 255, 25), 12 * sc);
    if (f > 0) fg->AddRectFilled(ba, ImVec2(ba.x + (bb.x - ba.x) * f, bb.y), IM_COL32(236, 194, 84, 255), 12 * sc);
    char line[200];
    if (preparing) snprintf(line, sizeof line, "%d%%", (int)(f * 100));  // (the title says it)
    else snprintf(line, sizeof line, "%s   %d%%", test_busy ? "Setting up the game files..." : job.c_str(),
                  (int)(f * 100));
    ImVec2 ls = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, line);
    fg->AddText(g_font_small, sfs, ImVec2((a.x + b.x - ls.x) * 0.5f, bb.y + 26 * sc), IM_COL32(220, 216, 205, 255), line);
    for (int i = 0; i < nnotes; i++) {
        ImVec2 ns = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, notes[i]);
        fg->AddText(g_font_small, sfs, ImVec2((a.x + b.x - ns.x) * 0.5f, bb.y + 26 * sc + (i + 1) * line_h + 14 * sc),
                    IM_COL32(150, 148, 140, 255), notes[i]);
    }
    if (recompiling) {  // Shader preparation has its own Skip action; cancellation only stops the game-code build.
        ImVec2 ca((a.x + b.x) * 0.5f - 150 * sc, b.y - 66 * sc), cb(ca.x + 300 * sc, ca.y + 44 * sc);
        bool hover = !g_cancel_build && ImGui::IsMouseHoveringRect(ca, cb, false);
        fg->AddRectFilled(ca, cb, hover ? IM_COL32(80, 80, 55, 255) : IM_COL32(45, 55, 45, 255), 8 * sc);
        const char *cancel = g_cancel_build ? "Cancelling..." : "Cancel";  // (B / Circle and Esc too)
        ImVec2 cs = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, cancel);
        fg->AddText(g_font_small, sfs, ImVec2((ca.x + cb.x - cs.x) * 0.5f, (ca.y + cb.y - cs.y) * 0.5f),
                    IM_COL32(236, 194, 84, 255), cancel);
        if ((hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            g_cancel_build = true;
    }
    if (preparing) {  // Skip: the game then compiles what it needs as it goes (back / Esc too)
        const char *sk = g_prep_skip ? "Skipping..." : "Skip";
        ImVec2 ks = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, sk);
        ImVec2 ka((a.x + b.x - ks.x) * 0.5f - 22 * sc, b.y - 40 * sc - line_h - 10 * sc);
        ImVec2 kb(ka.x + ks.x + 44 * sc, ka.y + line_h + 16 * sc);
        bool hover = !g_prep_skip && !g_prep_skip_ask && ImGui::IsMouseHoveringRect(ka, kb, false);
        fg->AddRect(ka, kb, IM_COL32(236, 194, 84, hover ? 255 : 140), 8 * sc, 0, 1.5f * sc);
        fg->AddText(g_font_small, sfs, ImVec2(ka.x + 22 * sc, ka.y + 8 * sc),
                    hover ? IM_COL32(236, 194, 84, 255) : IM_COL32(220, 216, 205, 255), sk);
        if (!g_prep_skip && !g_prep_skip_ask &&
            ((hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ||
             ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            g_prep_skip_ask = true;
    }
    if (preparing && g_prep_skip_ask && !g_prep_skip) {  // what Skip means, before it happens
        const char *q = "Shaders will compile as you play, but you may experience some pop-in.";
        const char *yes = "Yes, skip", *no = "No, wait";
        ImVec2 qs = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, q);
        ImVec2 ys = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, yes), ns = g_font_small->CalcTextSizeA(sfs, FLT_MAX, 0, no);
        float bw = fmaxf(ys.x, ns.x) + 64 * sc, bh = line_h + 20 * sc, gap = 24 * sc;
        float qw = fminf(fmaxf(qs.x, 2 * bw + gap) + 96 * sc, ds.x - 32 * sc), qh = 40 * sc + line_h + 32 * sc + bh + 36 * sc;
        fg->AddRectFilled(a, b, IM_COL32(0, 0, 0, 120), 14 * sc);  // (the bar behind it dimmed)
        ImVec2 qa(o.x + (ds.x - qw) * 0.5f, o.y + (ds.y - qh) * 0.5f), qb(qa.x + qw, qa.y + qh);
        fg->AddRectFilled(qa, qb, IM_COL32(15, 23, 20, 255), 14 * sc);
        fg->AddRect(qa, qb, IM_COL32(236, 194, 84, 200), 14 * sc, 0, 1.5f * sc);
        fg->AddText(g_font_small, sfs, ImVec2((qa.x + qb.x - qs.x) * 0.5f, qa.y + 40 * sc), IM_COL32(236, 232, 220, 255), q);
        float by = qb.y - 36 * sc - bh, bx = (qa.x + qb.x) * 0.5f;
        ImVec2 ya(bx - gap * 0.5f - bw, by), yb(bx - gap * 0.5f, by + bh), na(bx + gap * 0.5f, by), nb(bx + gap * 0.5f + bw, by + bh);
        bool hy = ImGui::IsMouseHoveringRect(ya, yb, false), hn = ImGui::IsMouseHoveringRect(na, nb, false);
        fg->AddRect(ya, yb, IM_COL32(236, 194, 84, hy ? 255 : 140), 8 * sc, 0, 1.5f * sc);
        fg->AddRectFilled(na, nb, IM_COL32(236, 194, 84, hn ? 255 : 210), 8 * sc);  // (waiting: the default)
        fg->AddText(g_font_small, sfs, ImVec2((ya.x + yb.x - ys.x) * 0.5f, ya.y + 10 * sc),
                    hy ? IM_COL32(236, 194, 84, 255) : IM_COL32(220, 216, 205, 255), yes);
        fg->AddText(g_font_small, sfs, ImVec2((na.x + nb.x - ns.x) * 0.5f, na.y + 10 * sc), IM_COL32(15, 23, 20, 255), no);
        bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        // A / Enter: the highlighted default (wait); B / Esc: also wait; Yes only by clicking it or Y / Space
        if ((hy && click) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false))
            g_prep_skip = 1, g_prep_skip_ask = false;
        else if ((hn && click) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            g_prep_skip_ask = false;
    }
}

// ------------------------------------------------------------------ the data folder (runtime/paths.h): another one
// picked (Settings > Prelaunch, the Game Files window), the data moved there or a fresh start, then a restart there
enum { DF_NONE, DF_ASK, DF_MOVING, DF_FAILED };
static int g_df;                  // (main thread)
static std::string g_df_pick;     // the folder picked (g_lock: the dialog calls back on any thread)
static RtDataTarget g_df_t;
static bool g_df_onedrive;        // the folder picked is synced by OneDrive too (Windows)
static std::atomic<int> g_df_step{0}, g_df_result{0};  // result: 0 running, 1 moved, -1 failed
static std::atomic<float> g_df_frac{0};
static char g_df_err[600];
static bool g_df_auto_move, g_df_auto_fresh;  // (test: LARECOMP_LAUNCHER_DATA_PICK=<folder>:move / :fresh)

static void SDLCALL picked_data(void *, const char *const *files, int) {
    if (!files || !files[0]) return;
    pthread_mutex_lock(&g_lock);
    g_df_pick = files[0];
    pthread_mutex_unlock(&g_lock);
}
static void choose_data_folder(void) { SDL_ShowOpenFolderDialog(picked_data, NULL, g_launcher_win, NULL, false); }
static void df_progress(int step, double f) { g_df_step = step, g_df_frac = (float)f; }
static void *df_move_thread(void *) {
    g_df_result = rt_data_move(g_df_t.path, df_progress, g_df_err, sizeof g_df_err) ? 1 : -1;
    return NULL;
}
static void df_restart(void) {
    unsetenv("LARECOMP_LAUNCHER_DATA_PICK");
    rt_paths_restart();
    snprintf(g_df_err, sizeof g_df_err, "Couldn't restart LAS Recomp. Start it again to use the new folder.");
    g_df = DF_FAILED;
}

// The question / the move's progress, in front of everything (called at the start screen's top level)
static void data_folder_popup(float s) {
    float sc = s * 720.0f / 1080.0f;
    pthread_mutex_lock(&g_lock);
    std::string pick;
    pick.swap(g_df_pick);
    bool busy = g_busy;
    pthread_mutex_unlock(&g_lock);
    if (!pick.empty() && g_df == DF_NONE && !busy) {
        rt_data_target(pick.c_str(), &g_df_t);
        g_df_onedrive = !g_df_t.refuse[0] && plat_onedrive_synced(g_df_t.path);
        g_df = DF_ASK;
        ImGui::OpenPopup("##datafolder");
    }
    if (!ImGui::BeginPopupModal("##datafolder", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        return;
    const RtDataTarget &t = g_df_t;
    const ImVec4 red(1, 0.5f, 0.4f, 1);
    ImVec2 bw(240 * sc, 0);
    ImGui::PushFont(g_font_small);
    ImGui::PushFont(g_font_menu);
    ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), g_df == DF_MOVING ? "Moving your data" : "Data folder");
    ImGui::PopFont();
    ImGui::PushTextWrapPos(900 * sc);
    bool close = false, esc = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    auto use_it = [&] {
        stop_background_prep();
        settings_save();
        if (rt_data_use(t.path, g_df_err, sizeof g_df_err)) df_restart();
        else g_df = DF_FAILED;
    };
    if (g_df == DF_ASK) {
        ImGui::TextDisabled("%s", t.path);
        if (g_df_onedrive) ImGui::TextColored(ImVec4(1, 0.72f, 0.4f, 1), "This folder is synced by OneDrive too.");
        ImGui::Spacing();
        if (t.refuse[0]) {
            ImGui::TextColored(red, "%s", t.refuse);
            ImGui::Spacing();
            close = ImGui::Button("OK", bw) || esc;
        } else if (t.has_data) {
            ImGui::Text("This folder already has LAS Recomp data. Use it?");
            ImGui::Spacing();
            if (ImGui::Button("Use it", bw) || g_df_auto_fresh) g_df_auto_fresh = false, use_it();
            ImGui::SameLine();
            close = ImGui::Button("Cancel", bw) || esc;
        } else {
            bool room = t.free >= t.need;
            ImGui::Text("Move your saves, seeds, settings and game files (%.1f GB) there, or start fresh?", t.bytes / 1e9);
            if (!room) ImGui::TextColored(red, "Not enough free space there to move them (%.1f GB free).", t.free / 1e9);
            ImGui::Spacing();
            ImGui::BeginDisabled(!room);
            if ((ImGui::Button("Move", bw) || g_df_auto_move) && room) {
                g_df_auto_move = false;
                stop_background_prep();
                settings_save();
                g_df = DF_MOVING, g_df_step = 0, g_df_frac = 0, g_df_result = 0;
                pthread_t th;
                if (pthread_create(&th, NULL, df_move_thread, NULL) == 0) pthread_detach(th);
                else snprintf(g_df_err, sizeof g_df_err, "Couldn't start the move."), g_df = DF_FAILED;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Start fresh", bw) || g_df_auto_fresh) g_df_auto_fresh = false, use_it();
            ImGui::SameLine();
            ImGui::SetItemDefaultFocus();
            close = ImGui::Button("Cancel", bw) || esc;
        }
    } else if (g_df == DF_MOVING) {
        static const char *const steps[] = {"Copying...", "Checking the copy...", "Removing the old copy..."};
        int r = g_df_result, k = g_df_step;
        ImGui::Text("%s", r == 1 ? "Restarting..." : steps[k < 0 ? 0 : k > 2 ? 2 : k]);
        ImGui::ProgressBar(r == 1 ? 1.0f : g_df_frac.load(), ImVec2(900 * sc, 0));
        ImGui::TextDisabled("%s", t.path);
        if (r == 1) df_restart();
        else if (r == -1) g_df = DF_FAILED;
    } else {
        ImGui::TextColored(red, "%s", g_df_err);
        ImGui::Text("Your data is still in %s", rt_data_root());
        ImGui::Spacing();
        close = ImGui::Button("OK", bw) || esc;
    }
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (close) g_df = DF_NONE, ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static bool g_files_deleted;  // set here: the start screen goes back to setting the game files up
static void prelaunch_tab(float sc) {
    const ImVec4 gold(0.93f, 0.76f, 0.33f, 1);
    auto heading = [&](const char *t) {
        ImGui::PushFont(g_font_menu);
        ImGui::TextColored(gold, "%s", t);
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Spacing();
    };
    heading("Game files");
    static double gb = -1;
    if (gb < 0) gb = rt_game_files_size(g_root.c_str()) / 1e9;
#ifdef _WIN32
    if (ImGui::Button("Show in Explorer")) reveal_folder(g_root);
#elif defined(__linux__)
    if (ImGui::Button("Open folder")) reveal_folder(g_root);
#else
    if (ImGui::Button("Show in Finder")) reveal_folder(g_root);
#endif
    ImGui::SameLine();
    static bool shot_del = getenv("LARECOMP_LAUNCHER_SHOT") && strstr(getenv("LARECOMP_LAUNCHER_SHOT"), ":askdelete");
    if (ImGui::Button("Delete game files") || shot_del) ImGui::OpenPopup("##delfiles"), shot_del = false;  // (test: :askdelete)
    if (ImGui::BeginPopupModal("##delfiles", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(g_font_menu);
        ImGui::TextColored(ImVec4(1, 0.72f, 0.4f, 1), "Are you sure?");
        ImGui::PopFont();
        ImGui::Text("This deletes LAS Recomp's copy of the game files (%.1f GB).", gb);
        ImGui::TextDisabled("Saves, seeds and settings are kept. Your own game files aren't touched.");
        if (rt_dev_mode()) {  // (a source checkout: the files are the project's own extracted/)
            bool abs = !g_root.empty() && (g_root[0] == '/' || (g_root.size() > 1 && g_root[1] == ':'));
            std::string where = abs ? g_root : std::string(rt_data_root()) + "/" + g_root;
            ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Dev checkout: this is %s", where.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(220 * sc, 0))) {
            stop_background_prep();
            bool ok = rt_remove_game_files(g_root.c_str());  // (runtime/paths.c: only what the extraction made)
            game_files_changed();
            set_job(ok ? "Game files deleted." : "Could not delete the game files (not in LAS Recomp's data folder?).", !ok);
            g_files_deleted = true, gb = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("Cancel", ImVec2(220 * sc, 0)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    ImGui::Spacing();
    heading("Data folder");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", rt_data_root());
    ImGui::PopTextWrapPos();
    const char *fixed = rt_data_root_fixed();
    ImGui::BeginDisabled(fixed != NULL);
    if (ImGui::Button("Change...")) choose_data_folder();
    ImGui::EndDisabled();
    if (fixed) ImGui::SameLine(), ImGui::TextDisabled("%s", fixed);
    ImGui::Spacing();
    ImGui::Spacing();
    heading("Game font");
    import_fonts();
    bool fs = have_font(FONT_STD, 4), fl = have_font(FONT_LANG, 8), fe = have_font(FONT_EXT, 2);
    ImGui::Text("Text: %s    Chinese / Korean: %s    Button icons: %s",
                fs ? "Nintendo font (imported)" : "substitute (this OS's, per language)",
                fl ? "Nintendo font (imported)" : "substitute", fe ? "imported" : "substitute");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Fonts aren't included. For the original look, import your own copies of "
                        "nintendo_udsg-r_std_003.bfttf (text), the same-named files for Chinese and Korean "
                        "(nintendo_udsg-r_org_zh-cn_003, nintendo_udsg-r_ext_zh-cn_003, nintendo_udjxh-db_zh-tw_003, "
                        "nintendo_udsg-r_ko_003) and nintendo_ext_003.bfttf (button icons). "
                        ".ttf versions with the same names work too.");
    ImGui::PopTextWrapPos();
    if (!g_font_msg.empty()) ImGui::TextWrapped("%s", g_font_msg.c_str());
    static const SDL_DialogFileFilter font_f[] = {{"Switch fonts", "bfttf;ttf"}};
    if (ImGui::Button("Import fonts")) SDL_ShowOpenFileDialog(picked_fonts, NULL, g_launcher_win, font_f, 1, NULL, true);
    if (fs || fl || fe) {
        ImGui::SameLine();
        if (ImGui::Button("Use substitute font")) remove_fonts();
    }
}

extern "C" void launcher_run(const char *root) {
    laonline_launch_mode(0); // Prelaunch and Vanilla are offline regardless of laonline.ini.
    session_ui_begin();
    g_root = root;
    g_start_screen = 1;
    static bool update_checked;  // (once per launch, not again after a "Title screen" reboot)
    if (!update_checked && !getenv("LARECOMP_NO_UPDATE_CHECK")) {
        update_checked = true;
        pthread_t t;
        // The shared co-op UI checks both release channels once per session.
    }
    ui_window_init();
    SDL_Window *win = ui_window();
    g_launcher_win = win;
#ifdef __APPLE__
    CAMetalLayer *layer = (__bridge CAMetalLayer *)ui_metal_layer();
    id<MTLDevice> dev = (__bridge id<MTLDevice>)ui_metal_device();
    id<MTLCommandQueue> queue = [dev newCommandQueue];
#else
    GpuDevice dev = NULL;
#endif

    IMGUI_CHECKVERSION();
    ImGuiContext *prev_ctx = ImGui::GetCurrentContext();  // the in-game menu's, when back here after a reboot
    ImGuiContext *ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = NULL;
    // fonts belong to this context: back here via "Title screen" (in-place reboot) the previous run's are gone with
    // its context, so load them again (a stale pointer here aborted in PushFont - user, 0.1 RC)
    g_font_small = g_font_menu = g_font_title = NULL;
    // Baloo 2 SemiBold (OFL, shipped: third_party/fonts/Baloo2) - rounded, game-like; it runs small, hence the sizes
    char baloo[1024];
    std::string own = rt_resource("Baloo2-SemiBold.ttf", "third_party/fonts/Baloo2/Baloo2-SemiBold.ttf", baloo, sizeof baloo) ? baloo : "";
    const char *fonts[] = {getenv("LARECOMP_LAUNCHER_FONT") ? getenv("LARECOMP_LAUNCHER_FONT") : "",  // (test)
                           own.c_str(), "fonts/nintendo_udsgr_std_003.ttf", "/System/Library/Fonts/Avenir Next.ttc",
                           "/System/Library/Fonts/Helvetica.ttc", "C:/Windows/Fonts/segoeui.ttf"};
    static const ImWchar ranges[] = {0x20, 0xFF, 0x2039, 0x203A, 0x2192, 0x2192, 0x2713, 0x2713, 0x25B8, 0x25B8, 0};  // + ‹ › → ✓ ▸
    for (const char *p : fonts) {
        if (g_font_menu || !*p || !file_exists(p)) continue;
        bool b2 = p == own.c_str() || getenv("LARECOMP_LAUNCHER_FONT");
        g_font_small = io.Fonts->AddFontFromFileTTF(p, 30 * (b2 ? 1.75f : 1), NULL, ranges);
        g_font_menu = io.Fonts->AddFontFromFileTTF(p, 52 * (b2 ? 1.85f : 1), NULL, ranges);
        g_font_title = io.Fonts->AddFontFromFileTTF(p, 92 * (b2 ? 2.3f : 1), NULL, ranges);
    }
    if (!g_font_menu) g_font_small = g_font_menu = g_font_title = io.Fonts->AddFontDefault();
    tropical_fonts_init();
    ImGuiStyle &st = ImGui::GetStyle();
    st.Colors[ImGuiCol_Header] = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.08f);
    st.Colors[ImGuiCol_HeaderActive] = ImVec4(1, 1, 1, 0.14f);
    st.Colors[ImGuiCol_NavCursor] = ImVec4(0.95f, 0.78f, 0.35f, 1);
    st.FrameRounding = st.WindowRounding = 10;
#ifdef __APPLE__
    ImGui_ImplSDL3_InitForMetal(win);
    ImGui_ImplMetal_Init(dev);
#else
    ImGui_ImplSDL3_InitForVulkan(win);
    vk_imgui_backend_init();
#endif
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, NULL, 0);  // (controllers: launcher_pad_event)
    launcher_pad_open_all();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    // an older version's game data image: the game reads the romfs/ folder itself now - 5.8 GB back
    if (check_copied_files(g_root).empty() && file_exists(g_root + "/romfs.bin") &&
        rt_remove_game_file(g_root.c_str(), "romfs.bin"))
        fprintf(stderr, "[launcher] removed the old game data image (romfs.bin)\n");
    std::string status = check_game_files(g_root);
    if (check_copied_files(g_root).empty()) load_background(dev);  // (also behind an update of the game code)
    std::vector<std::string> seeds = list_seeds();
    std::vector<std::string> choices = {"Vanilla","Co-op","Co-op Randomizer"};
    int choice=g_settings.laonline_play_mode;
    std::string coop_seed=g_settings.laonline_rando_seed[0]?g_settings.laonline_rando_seed:g_settings.rando_seed;
    bool randomizer_context=choice==2,offline_seed_play=false,manual_seed=false;
    char manual_id[128]={0};std::string seed_status;
    std::string cached_seed;LaoSeedRecipe selected_recipe;bool share_open=false;int relay_total=0,relay_ready=0;
    coop_expected={};
    if(!coop_seed.empty()){
        auto proof=std::filesystem::path("rando")/coop_seed/"seed_identity.laoseed";std::error_code proof_error;
        if(std::filesystem::is_regular_file(proof,proof_error)&&std::filesystem::file_size(proof,proof_error)<=2048){
            std::ifstream file(proof,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),{});LaoSeedRecipe saved;
            if(LaoSeedRecipe::decode(bytes,saved)){coop_expected=saved;snprintf(manual_id,sizeof manual_id,"%s",saved.id.c_str());manual_seed=true;
                std::string check=validate_coop_seed(coop_seed);seed_status=check.empty()?"SEED VERIFIED: saved imported world matches its original checksum.":check;}
            else seed_status="Imported identity record is damaged. Import the original seed download again.";
        }
    }

    Page page = status.empty() ? MAIN : SETUP;
    bool play = false;
    bool session_launch_pending=false,rando_room_validated=false;
    auto resolve_recipe=[&](const LaoSeedRecipe &incoming,bool allow_reconstruct){
        LaoSeedRecipe recipe=incoming;
        char error[256];if(!rando_check_seed_id(recipe.id.c_str(),error,sizeof error)){seed_status=error;return;}
        coop_expected=recipe;snprintf(manual_id,sizeof manual_id,"%s",recipe.id.c_str());
        for(const auto &name:list_seeds()){
            LaoSeedRecipe local;if(!lao_seed_recipe(std::filesystem::path("rando")/name,local))continue;
            if(local.id==recipe.id&&local.hash==recipe.hash){coop_seed=name;cached_seed.clear();snprintf(g_settings.laonline_rando_seed,sizeof g_settings.laonline_rando_seed,"%s",coop_seed.c_str());snprintf(g_settings.rando_seed,sizeof g_settings.rando_seed,"%s",coop_seed.c_str());settings_save();seed_status="SEED VERIFIED: canonical ID and full world checksum match. Relay readiness is checked after connection.";return;}
        }
        coop_seed.clear();cached_seed.clear();
        if(!allow_reconstruct){seed_status="Legacy recipe loaded. It contains no world patches or original name. Click Validate Seed ID to explicitly reconstruct this exact ID; download a new seed file to import without generation.";manual_seed=true;return;}
        if(rando_launcher_reconstruct(recipe,error,sizeof error)){
            page=SETTINGS;g_tab_request_ext=3;menu_tabs_focus(false);seed_status="Preparing the host seed with the existing generator and your own game files. Its final checksum must match before starting.";
        }else seed_status=error;
    };
    // LARECOMP_LAUNCHER_SHOT=<frame>[:<page>] (test): save build/launcher.png at that frame and exit; page: settings[:<tab>] / askdelete / setup / extracting / preparing
    long shot_at = getenv("LARECOMP_LAUNCHER_SHOT") ? atol(getenv("LARECOMP_LAUNCHER_SHOT")) : 0, frame = 0;
    if(shot_at&&getenv("LARECOMP_TEST_SEED_IMPORT")){std::lock_guard<std::mutex> lock(coop_pick_lock);coop_picked_seed=getenv("LARECOMP_TEST_SEED_IMPORT");}
    if (shot_at) {
        if(const char *size=getenv("LARECOMP_LAUNCHER_TEST_SIZE")){int w=0,h=0;if(sscanf(size,"%dx%d",&w,&h)==2&&w>=640&&w<=3840&&h>=480&&h<=2160)SDL_SetWindowSize(win,w,h);}

#ifdef __APPLE__
        layer.framebufferOnly = NO;
#endif
        const char *pg = strchr(getenv("LARECOMP_LAUNCHER_SHOT"), ':');
        if (pg && (!strncmp(pg + 1, "settings", 8) || !strcmp(pg + 1, "askdelete"))) page = SETTINGS, menu_tabs_focus(true);
        if(pg && !strcmp(pg+1,"coop-rando")){page=COOP_RANDO;choice=2;randomizer_context=true;}
        if (pg && !strcmp(pg + 1, "main")) page = MAIN;
        if(pg && !strcmp(pg+1,"credits"))page=CREDITS;
        if(pg && !strcmp(pg+1,"lobbies"))page=LOBBY_BROWSER,choice=1;
        if(pg && !strcmp(pg+1,"leaderboards"))page=LEADERBOARDS;
        if(pg && !strcmp(pg+1,"global"))page=GLOBAL_ONLINE;
        if(pg && !strcmp(pg+1,"host"))page=COOP_HOST,choice=1;
        if(pg && !strcmp(pg+1,"private")){page=COOP_HOST;choice=1;session_ui_capture_create();}
        if (pg && !strcmp(pg + 1, "setup")) page = SETUP;
        if (pg && !strcmp(pg + 1, "preparing")) page = SETUP, g_prep_frac = 0.42f, set_job("Preparing shaders");
        if (pg && !strcmp(pg + 1, "skipask")) page = SETUP, g_prep_frac = 0.42f, set_job("Preparing shaders"), g_prep_skip_ask = true;
        if (pg && !strncmp(pg + 1, "fail", 4)) page = SETUP, set_failure(atoi(pg + 5));  // (the failure messages)
        if (pg && !strncmp(pg + 1, "settings", 8) && pg[9] == ':') g_tab_request_ext = atoi(pg + 10);  // settings:<tab>
        if (pg && !strcmp(pg + 1, "delete")) page = SETUP, g_confirm_delete = true;
        if (pg && !strcmp(pg + 1, "tip")) g_settings.menu_tip_seen = false, g_tip_test = true;
        if (pg && !strcmp(pg + 1, "aspect")) g_settings.aspect_asked = g_settings.menu_tip_seen = false, g_tip_test = true;
    }

    static int runs;  // (test: =once - only the first time, so a "Title screen" reboot lands on the start screen)
    const char *ap = getenv("LARECOMP_LAUNCHER_AUTOPLAY");
    if (ap && status.empty() && !(runs && !strcmp(ap, "once"))) play = true;  // (test: PLAY at once)
    if (getenv("LARECOMP_LAUNCHER_PREPARE") && status.empty() && !runs) {  // (test) the setup's shader preparation
        g_busy = true;                                                      // alone, then PLAY with AUTOPLAY
        page = SETUP, play = false;
        pthread_t th;
        pthread_create(&th, NULL, [](void *) -> void * {
            prepare_shaders();
            set_job("Game files ready.");
            pthread_mutex_lock(&g_lock);
            g_busy = false;
            pthread_mutex_unlock(&g_lock);
            return NULL;
        }, NULL);
        pthread_detach(th);
    }
    runs++;
    if (const char *t = getenv("LARECOMP_LAUNCHER_DATA_PICK")) {  // (test) <folder>[:move / :fresh]: picked as the data folder
        std::string p = t;
        size_t c = p.rfind(':');
        if (c != std::string::npos && (p.substr(c) == ":move" || p.substr(c) == ":fresh"))
            g_df_auto_move = p.substr(c) == ":move", g_df_auto_fresh = p.substr(c) == ":fresh", p.resize(c);
        g_df_pick = p;
    }
    if (const char *t = getenv("LARECOMP_LAUNCHER_EXTRACT")) {  // (test) <folder>: set the game files up, log, exit
        g_src = t;
        g_busy = true;
        extract_thread(NULL);
        fprintf(stderr, "[launcher] setup: %s -> %s\n", g_job.c_str(), check_game_files(g_root).empty() ? "READY" : "NOT READY");
        exit(0);
    }
    if (const char *t = getenv("LARECOMP_LAUNCHER_SETUP")) {  // (test) <folder>: Set up started on the start screen
        g_src = t;
        g_busy = true;
        page = SETUP;
        pthread_t th;
        pthread_create(&th, NULL, extract_thread, NULL);
        pthread_detach(th);
    }
    // An update: the game files are there and only the game code is out of date - rebuilt at once, by itself (only
    // the pieces that changed: runtime/gamebuild.c), with the progress bar; then the start screen as usual. A failure
    // stays on the setup page with what went wrong.
    if (!status.empty() && check_copied_files(g_root).empty() && gamebuild_available() &&
        !getenv("LARECOMP_LAUNCHER_DELETE")) {
        fprintf(stderr, "[launcher] game code out of date: updating it\n");
        g_src.clear();
        g_busy = true, g_auto_update = true;
        page = SETUP;
        pthread_t th;
        pthread_create(&th, NULL, extract_thread, NULL);
        pthread_detach(th);
    }
    if (getenv("LARECOMP_LAUNCHER_DELETE")) {  // (test) what DELETE GAME FILES does, logged; then the start screen
        double gb = rt_game_files_size(g_root.c_str()) / 1e9;
        stop_background_prep();
        bool ok = rt_remove_game_files(g_root.c_str());
        game_files_changed();
        status = check_game_files(g_root);
        fprintf(stderr, "[launcher] delete %.1f GB: %s -> %s\n", gb, ok ? "ok" : "refused", status.c_str());
        if (!status.empty()) { g_bg = 0; page = SETUP; }
    }
    if (g_onedrive < 0) {  // (once per launch: a "Title screen" reboot keeps it)
        const char *t = getenv("LARECOMP_TEST_ONEDRIVE");
        g_onedrive = t ? atoi(t) != 0 : plat_onedrive_synced(rt_data_root());
        if (g_onedrive) fprintf(stderr, "[launcher] the data folder is synced by OneDrive%s: %s\n",
                                g_settings.onedrive_kept ? " (kept there)" : "", rt_data_root());
    }
    // the shader preparation of every later start: in the background (first setups and updates do theirs above)
    if (status.empty() && !g_busy && !getenv("LARECOMP_LAUNCHER_PREPARE")) start_background_prep();
    // (test) LARECOMP_LAUNCHER_PLAY_AT=<s>: PLAY that many seconds in; LARECOMP_LAUNCHER_SKIP_AT=<s>: Skip then
    const char *play_at_env = getenv("LARECOMP_LAUNCHER_PLAY_AT"), *skip_at_env = getenv("LARECOMP_LAUNCHER_SKIP_AT");
    uint64_t loop_t0 = SDL_GetTicks(), last_frame = loop_t0, bg_gap = 0, bg_frames = 0;
    bool bg_logged = false;
    for (;;) {
        uint64_t now = SDL_GetTicks();
        if (g_bg_prep) bg_frames++, bg_gap = std::max(bg_gap, now - last_frame);
        else if (!bg_logged && bg_frames) {  // (how the start screen kept up meanwhile)
            bg_logged = true;
            fprintf(stderr, "[launcher] background shader preparation: %llu frames meanwhile, longest %llu ms\n",
                    (unsigned long long)bg_frames, (unsigned long long)bg_gap);
        }
        last_frame = now;
        if (play_at_env && status.empty() && now - loop_t0 >= (uint64_t)(atof(play_at_env) * 1000)) play = true, play_at_env = NULL;
        if (skip_at_env && g_prep_wait && now - loop_t0 >= (uint64_t)(atof(skip_at_env) * 1000)) g_prep_skip = 1, skip_at_env = NULL;
        // PLAY while that still runs: its bar (with Skip) until it's done
        if (play && g_bg_prep) {
            play = false, g_prep_wait = true;
            fprintf(stderr, "[launcher] PLAY: waiting for the shader preparation\n");
        }
        if (g_prep_wait && !g_bg_prep) {
            g_prep_wait = false, play = true;
            fprintf(stderr, "[launcher] shader preparation %s: playing\n", g_prep_skip ? "skipped" : "done");
        }
        if (play && choice!=0) {
            const GameVersion *version=game_version_of(g_root.c_str());
            if(!version || strcmp(version->key,"v1_0_0")) {
                play=false;
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Co-op compatibility",
                    "Co-op native hooks are currently verified for game 1.0.0 only. Vanilla supports the upstream game revisions. Choose Vanilla for these game files.",win);
            }
        }
        /* V14.1: online needs the newest LA-Online - older versions can't play with newer ones (mixed versions broke
         * saves). Offline Vanilla / offline Randomizer still start. */
        if(play && choice!=0 && tropical_update_required()){play=false;g_play_pending=false;page=MAIN;g_need_update_popup=true;tropical_update_required_note(true);}
        if(play && choice==2){std::string error=validate_coop_seed(coop_seed);if(!error.empty()){play=false;page=COOP_RANDO;seed_status=error;}}
        if(page!=SETTINGS&&settings_edit_active())settings_edit_commit();
        if (play) break;
        menu_apply_window_requests(win);  // (the Video tab's Fullscreen)
        rando_coop_context(randomizer_context && choice==2);
        // the seed PLAY would start, kept current (Settings > Gameplay greys out what that seed forces)
        snprintf(g_settings.rando_seed, sizeof g_settings.rando_seed, "%s", choice==2 || offline_seed_play ? coop_seed.c_str() : "");
        if (g_files_deleted) {  // (Settings > Prelaunch deleted them)
            g_files_deleted = false;
            status = check_game_files(g_root);
            if (!status.empty()) { g_bg = 0; bind_capture_cancel(); settings_save(); page = SETUP; }
        }
        if (g_play_seed_req) {  // the Randomizer tab's Play: that seed, then PLAY as if pressed (first-launch questions)
            g_play_seed_req = false;
            seeds = list_seeds();
            choices = {"Vanilla","Co-op","Co-op Randomizer"};
                    bool online_seed=choice!=0;coop_seed=g_play_seed;choice=online_seed?2:0;offline_seed_play=!online_seed;
            bind_capture_cancel();
            settings_save();
            page = online_seed?COOP_RANDO:MAIN;
            g_play_pending = !online_seed;
        }
        {std::string prepared;if(rando_launcher_take_prepared(prepared)){
            /* V13.5 ("seed won't generate"): a newly generated seed is the player's OWN world. It used to be checked
             * against the last host seed this player had downloaded ("Canonical seed ID mismatch"), so a fresh seed
             * looked broken. Forget that host expectation now; joining a room sets it again. */
            coop_expected={};manual_seed=false;manual_id[0]=0;
            coop_seed=prepared;cached_seed.clear();page=COOP_RANDO;std::string error=validate_coop_seed(coop_seed);seed_status=error.empty()?"SEED VALIDATED: generated local world. The relay must confirm the other players.":error;}}
        {std::string picked,export_path,imported;LaoSeedRecipe received;bool pending=false,resolve=false;
         {std::lock_guard<std::mutex> lock(coop_pick_lock);picked.swap(coop_picked_seed);export_path.swap(coop_export_path);imported.swap(coop_imported_seed);
          if(!coop_metadata_status.empty()){seed_status=coop_metadata_status;coop_metadata_status.clear();}
          relay_total=coop_total;relay_ready=coop_ready;
          if(coop_received_pending){received=coop_received;pending=true;resolve=coop_received_resolve;coop_received_pending=false;}}
         if(pending){coop_expected=received;snprintf(manual_id,sizeof manual_id,"%s",received.id.c_str());manual_seed=true;if(resolve){coop_transfer(true,"@lobby","");seed_status="Synchronizing the host world patches. No randomizer generation will run.";}}
         if(!imported.empty()){
          LaoSeedRecipe imported_recipe;if(lao_seed_recipe(std::filesystem::path("rando")/imported,imported_recipe)){
           coop_seed=imported;g_settings.laonline_play_mode=2;coop_expected=imported_recipe;cached_seed.clear();snprintf(manual_id,sizeof manual_id,"%s",imported_recipe.id.c_str());manual_seed=true;
           snprintf(g_settings.laonline_rando_seed,sizeof g_settings.laonline_rando_seed,"%s",coop_seed.c_str());snprintf(g_settings.rando_seed,sizeof g_settings.rando_seed,"%s",coop_seed.c_str());settings_save();
          }}
         if(!export_path.empty()){
          std::filesystem::path destination(export_path);if(destination.extension()!=".laoseed")destination+=".laoseed";
          coop_transfer(false,coop_export_source,destination.string());seed_status="Exporting verified world patches and original name...";}
         if(!picked.empty()){
          std::ifstream signature(picked,std::ios::binary);char magic[2]={0};signature.read(magic,2);signature.close();
          if(magic[0]=='P'&&magic[1]=='K'){coop_transfer(true,picked,"");seed_status="Importing existing named world patches. The randomizer will not run.";}
          else if(std::filesystem::path(picked).extension()==".laoseed"){
           std::error_code ec;auto size=std::filesystem::file_size(picked,ec);LaoSeedRecipe recipe;
           if(ec||size>2048)seed_status="Invalid or oversized seed recipe.";
           else{std::ifstream f(picked,std::ios::binary);std::string text((std::istreambuf_iterator<char>(f)),{});if(LaoSeedRecipe::decode(text,recipe))resolve_recipe(recipe,false);else seed_status="Unsupported or damaged seed recipe. Both PCs need the same HF3 Community 3 generator.";}
          }else{std::string selected,error=import_coop_seed(picked,selected);if(error.empty()){coop_seed=selected;lao_seed_recipe(std::filesystem::path("rando")/selected,coop_expected);cached_seed.clear();seed_status="Seed folder imported. Validate its world checksum before starting.";}else seed_status=error;}
         }}
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input_cursor_event(&e);  // (pad / key: the cursor hides; ImGui's backend leaves it so, below)
            if (page == SETTINGS && bind_capture_event(&e)) continue;  // (waiting for a new binding: controls_ui.mm)
            if (!launcher_pad_event(e)) ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) launcher_quit_requested=true;
        }
        // extraction finished: re-check, load the background
        pthread_mutex_lock(&g_lock);
        bool busy = g_busy;
        pthread_mutex_unlock(&g_lock);
        static bool was_busy;
        if (was_busy && !busy) {
            g_build_cache_changed = true;
            status = check_game_files(g_root);
            if (status.empty()) { if (!g_bg) load_background(dev); page = MAIN; }
            if (status.empty() && ap && getenv("LARECOMP_LAUNCHER_PREPARE")) play = true;
            g_auto_update = false;
        }
        was_busy = busy;

#ifdef __APPLE__
        int w, h;
        SDL_GetWindowSizeInPixels(win, &w, &h);
        layer.drawableSize = CGSizeMake(w, h);
        @autoreleasepool {
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (!drawable) { SDL_Delay(16); continue; }
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = drawable.texture;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0.04, 0.05, 0.06, 1);
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            ImGui_ImplMetal_NewFrame(rp);
#else
        {
            // one frame per refresh of the window's display: the swapchain presents without waiting (mailbox, for the
            // game's own pacing: device.c), so the start screen alone would draw as fast as the GPU goes
            static uint64_t next_frame;
            const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
            uint64_t period = (uint64_t)(1e9 / (dm && dm->refresh_rate > 1 ? dm->refresh_rate : 60.0f)), now = plat_now_ns();
            bool on_time = next_frame > now && next_frame - now <= period;  // (else late, or the rate changed: start over)
            if (on_time) plat_sleep_until_ns(next_frame);
            next_frame = (on_time ? next_frame : now) + period;
            static const float clear_col[4] = {0.04f, 0.05f, 0.06f, 1};
            if (!vk_frame_begin(clear_col)) { SDL_Delay(16); continue; }
            // Regression fixture: exercise backend teardown/recreation in the same launcher process.
            if(shot_at&&getenv("LARECOMP_LAUNCHER_REINIT_TEST")&&(frame==15||frame==30||frame==45)){vk_imgui_backend_shutdown();vk_imgui_backend_init();}
            ImGui_ImplVulkan_NewFrame();
#endif
            if (input_cursor_hidden()) io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
            ImGui_ImplSDL3_NewFrame();
            // the backend (manual mode, no gamepads of its own) clears HasGamepad every frame, and ImGui then drops the
            // controller presses launcher_pad_event fed in and turns gamepad navigation off: set it again while a
            // controller is connected
            if (SDL_HasGamepad()) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
            ImGui::NewFrame();

            ImVec2 ds = io.DisplaySize;
            float s = ds.y / 720.0f;  // layout scale (fonts are built for ~2x)
            ImGui::GetIO().FontGlobalScale = s * 0.5f;
            ImDrawList *bg = ImGui::GetBackgroundDrawList();
            // an update by itself: only its bar, on the start screen's background (gray without it; a failure or
            // Cancel then shows the Game Files page)
            const bool update_only = g_auto_update && busy;
            if (g_bg && (status.empty() || update_only)) {  // cover the window, keep the aspect
                float ia = (float)g_bg_w / g_bg_h, wa = ds.x / ds.y;
                ImVec2 uv0(0, 0), uv1(1, 1);
                if (wa > ia) { float v = ia / wa; uv0.y = (1 - v) / 2; uv1.y = 1 - uv0.y; }
                else { float u = wa / ia; uv0.x = (1 - u) / 2; uv1.x = 1 - uv0.x; }
                bg->AddImage(BG_TEXID, ImVec2(0, 0), ds, uv0, uv1);
            } else if (update_only) {
                bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(40, 40, 40, 255));
            } else {
                bg->AddRectFilledMultiColor(ImVec2(0, 0), ds, IM_COL32(18, 34, 40, 255), IM_COL32(40, 70, 64, 255),
                                            IM_COL32(12, 20, 24, 255), IM_COL32(8, 12, 16, 255));
            }
            if(!update_only && page!=SETUP && page!=SETTINGS){ImTextureID landscape=0;float aspect=1;if(tropical_image("background-beach",&landscape,&aspect)){float target=ds.x/ds.y;ImVec2 uv0(0,0),uv1(1,1);if(target>aspect){float v=aspect/target;uv0.y=(1-v)*.5f;uv1.y=1-uv0.y;}else{float u=target/aspect;uv0.x=(1-u)*.5f;uv1.x=1-uv0.x;}bg->AddImage(landscape,ImVec2(0,0),ds,uv0,uv1);bg->AddRectFilled(ImVec2(0,0),ds,IM_COL32(0,23,50,28));bg->AddRectFilledMultiColor(ImVec2(0,0),ImVec2(ds.x,ds.y*.23f),IM_COL32(0,16,40,150),IM_COL32(0,16,40,150),IM_COL32(0,16,40,0),IM_COL32(0,16,40,0));bg->AddRectFilledMultiColor(ImVec2(0,ds.y*.87f),ds,IM_COL32(0,16,40,0),IM_COL32(0,16,40,0),IM_COL32(0,16,40,205),IM_COL32(0,16,40,205));}}
            if (!update_only && (page==SETUP || page==SETTINGS)) {  // a shadow banner fading in from the left behind the menu, and one from the right behind the version
                const float stops[][2] = {{0.0f, 225}, {0.3f, 215}, {0.44f, 165}, {0.55f, 95}, {0.64f, 35}, {0.74f, 0}};
                for (int i = 0; i + 1 < (int)(sizeof stops / sizeof stops[0]); i++) {
                    ImU32 a = IM_COL32(6, 10, 34, (int)stops[i][1]), b = IM_COL32(6, 10, 34, (int)stops[i + 1][1]);
                    bg->AddRectFilledMultiColor(ImVec2(ds.x * stops[i][0], 0), ImVec2(ds.x * stops[i + 1][0], ds.y), a, b, b, a);
                }
                float y0 = ds.y - 190 * s, y1 = ds.y - 135 * s, y2 = ds.y - 40 * s, y3 = ds.y, x0r = ds.x * 0.5f;
                ImU32 c0 = IM_COL32(6, 10, 34, 0), c1 = IM_COL32(6, 10, 34, 235);
                bg->AddRectFilledMultiColor(ImVec2(x0r, y0), ImVec2(ds.x, y1), c0, c0, c1, c0);  // (feathered top)
                bg->AddRectFilledMultiColor(ImVec2(x0r, y1), ImVec2(ds.x, y2), c0, c1, c1, c0);
                bg->AddRectFilledMultiColor(ImVec2(x0r, y2), ImVec2(ds.x, y3), c0, c1, c0, c0);  // (feathered bottom)
            }

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ds);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::Begin("##launcher", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            const bool waiting = g_prep_wait;  // (the menu waits under the bar)
            ImGui::BeginDisabled(waiting);
            ImGui::SetScrollX(0);  // (the start screen never scrolls; a long seed name or a stray nav move mustn't shift it)
            ImGui::SetScrollY(0);
            float x0 = 90 * s;
            ImGui::SetCursorPos(ImVec2(x0, 120 * s));
            ImGui::PushFont(g_font_title);
            if (false) {  // legacy title retained in source; the live header draws branding, shadowed (not behind the Settings / Game Files windows)
                const char *t = "LAS Recompiled";
                ImVec2 p = ImGui::GetCursorScreenPos();
                text_fx(ImGui::GetWindowDrawList(), ImGui::GetFont(), ImGui::GetFontSize(), p, IM_COL32(255, 232, 120, 255),
                        IM_COL32(255, 138, 48, 255), t, 3 * s);
                ImGui::Dummy(ImGui::CalcTextSize(t));
            }
            ImGui::PopFont();
            ImGui::SetCursorPos(ImVec2(x0, 285 * s));
            // typing in a text field / the on-screen keyboard (osk.h): B / Esc are for it, not "back" (from the frame
            // before too, so the Esc that ends the typing doesn't also leave the page)
            static bool typed_before;
            bool typing = typed_before || ui_text_busy();

            if (page == MAIN) {
                ImGui::PushFont(tropical_body_font());tropical_header(ImGui::GetWindowPos(),ds,s);
                bool pressed=false;
                ImGuiStyle original_style=ImGui::GetStyle();tropical_style();
                float margin=ds.x*.024f,gap=12*s,cw=(ds.x-2*margin-4*gap)/5,ch=ds.y*.317f;  // V12.5: five cards (+ Play Randomizer)
                ImGui::SetCursorPos(ImVec2(margin,ds.y*.234f));
                if(tropical_tile("vanilla","vanilla","Play Vanilla","Original LAS Recompiled experience",ImVec2(cw,ch),IM_COL32(0,205,75,255),0)&&!busy&&status.empty())choice=0,pressed=true;
                ImGui::SameLine(0,gap);
                // V12.5: solo randomizer - the existing LAS Recompiled flow (Settings > Randomizer: generate or pick a
                // seed, then Play), opened straight from the home screen. Offline: no lobby, nothing shared.
                if(tropical_tile("solo-rando","randomizer","Play Randomizer","Solo randomizer - no lobby needed",ImVec2(cw,ch),IM_COL32(255,120,40,255),2)&&!busy&&status.empty()){
                    choice=0;offline_seed_play=false;randomizer_context=false;laonline_launch_mode(0);
                    g_tab_request_ext=3;menu_tabs_focus(false);page=SETTINGS;
                }
                ImGui::SameLine(0,gap);
                if(tropical_tile("browse","lobbies","Browse Lobbies","Find public rooms and see who's playing",ImVec2(cw,ch),IM_COL32(0,125,255,255),1))page=LOBBY_BROWSER,choice=1;
                ImGui::SameLine(0,gap);
                if(tropical_tile("coop","coop-velvet","Play Co-Op","Play online with a friend",ImVec2(cw,ch),IM_COL32(0,205,225,255),1))page=COOP_HOST,choice=1;
                ImGui::SameLine(0,gap);
                if(tropical_tile("randomizer","randomizer","Play Co-Op Randomizer","Co-op adventure with randomized items",ImVec2(cw,ch),IM_COL32(165,40,245,255),2))page=COOP_RANDO,choice=2,randomizer_context=true;
                ImGui::SetCursorPos(ImVec2(margin,ds.y*.563f));cw=(ds.x-2*margin-2*gap)/3;ch=ds.y*.30f;
                if(tropical_tile("global","global","Play Online Global",session_global_count_text(),ImVec2(cw,ch),IM_COL32(245,170,0,255),3))page=GLOBAL_ONLINE;
                ImGui::SameLine(0,gap);
                if(tropical_tile("leaders","leaderboards","Leaderboards","View rankings, records and stats",ImVec2(cw,ch),IM_COL32(245,45,55,255),4))page=LEADERBOARDS;
                ImGui::SameLine(0,gap);
                if(tropical_tile("credits","credits","Credits","Project credits and acknowledgements",ImVec2(cw,ch),IM_COL32(190,207,219,255),5))page=CREDITS;
                // first PLAY ever: the aspect ratio question, then how to reach our menu, then the game
                if (g_play_pending && status.empty()) pressed = true, g_play_pending = false;
                if(pressed && choice==2 && !session_launch_pending){page=COOP_RANDO;randomizer_context=true;pressed=false;}
                if (pressed) {
                    session_launch_pending=false;
                    if (!g_settings.aspect_asked) ImGui::OpenPopup("##aspect");
                    else if (!g_settings.menu_tip_seen) ImGui::OpenPopup("##menutip");
                    else play = true;
                }
                if (g_tip_test) ImGui::OpenPopup(g_settings.aspect_asked ? "##menutip" : "##aspect"), g_tip_test = false;
                if (aspect_prompt(s)) {
                    if (g_settings.menu_tip_seen) play = true;
                    else ImGui::OpenPopup("##menutip");
                }
                if (menu_tip(s)) play = true;
                const float utility_fit=fminf(1.f,(ds.x*.62f-3*ImGui::GetStyle().ItemSpacing.x)/(742*s));  // V12: + What's New?
                ImGui::SetCursorPos(ImVec2(ds.x*.19f,ds.y*.886f));
                if(tropical_utility("Settings","Graphics - Audio - Controls",ImVec2(200*s*utility_fit,58*s*utility_fit),0)){static bool opened;menu_tabs_focus(!opened);opened=true;page=SETTINGS;}
                
                static bool guide_open=false;
                ImGui::SameLine();if(tropical_utility("Help / Guide","How to play - FAQs",ImVec2(172*s*utility_fit,58*s*utility_fit),1))guide_open=true;
                ImGui::SameLine();if(tropical_utility("Quit","Close LAS Recompiled",ImVec2(185*s*utility_fit,58*s*utility_fit),2))launcher_quit_requested=true;
                {   // V12: "What's New?" - opens once by itself after an update, then from this button
                    static bool whats_new_checked;bool open_whats_new=false;
                    if(!whats_new_checked){whats_new_checked=true;std::ifstream seen("laonline-whats-new.txt");std::string v;std::getline(seen,v);if(v!=LAO_RELEASE_LABEL)open_whats_new=true;}
                    ImGui::SameLine();if(tropical_utility("What's New?","Beta 5.4 CO-OP V" LAO_MOD_VERSION,ImVec2(185*s*utility_fit,58*s*utility_fit),1))open_whats_new=true;
                    if(open_whats_new)ImGui::OpenPopup("What's New?");
                    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x*.9f,760*s),fminf(ds.y*.88f,560*s)),ImGuiCond_Appearing);
                    if(ImGui::BeginPopupModal("What's New?",NULL,ImGuiWindowFlags_NoResize)){
                        ImGui::BeginChild("##whats-new",ImVec2(0,-44*s));tropical_whats_new(s);ImGui::EndChild();
                        if(ImGui::Button("Got it",ImVec2(140*s,34*s))){std::ofstream seen("laonline-whats-new.txt");seen<<LAO_RELEASE_LABEL<<"\n";ImGui::CloseCurrentPopup();}
                        ImGui::EndPopup();
                    }
                }
                if(tropical_home_updates(ds,s)||tropical_update_prompt()||g_need_update_popup){g_need_update_popup=false;ImGui::OpenPopup("Co-op Updates");}
                if(shot_at && frame==15 && getenv("LARECOMP_UPDATE_MODAL_TEST"))ImGui::OpenPopup("Co-op Updates");
                ImGui::SetNextWindowSize(ImVec2(fminf(ds.x*.85f,680*s),fminf(ds.y*.92f,620*s)),ImGuiCond_Appearing);
                if(ImGui::BeginPopupModal("Co-op Updates",NULL,ImGuiWindowFlags_NoResize)){tropical_update_draw(s);if(ImGui::Button(lao_updater::busy()?"Hide (keeps updating)":"Later"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
                tropical_guide_draw(&guide_open,s);
                ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));
                const char *coop_credit="Co-op: ThyHeroOfTime  |  GitHub";
                const char *original_credit="LAS Recompiled: peiikai  |  GitHub";
                ImGui::SetCursorPos(ImVec2(ds.x-ImGui::CalcTextSize(coop_credit).x-24*s,ds.y*.93f));
                if(ImGui::SmallButton(coop_credit))SDL_OpenURL("https://github.com/LegendarySavageGamer/Link-s-Awakening-Recomp-Online");
                ImGui::SetCursorPos(ImVec2(ds.x-ImGui::CalcTextSize(original_credit).x-24*s,ds.y*.96f));
                if(ImGui::SmallButton(original_credit))SDL_OpenURL("https://github.com/peiikai/LASRecomp");ImGui::PopStyleColor();
                ImGui::GetStyle()=original_style;ImGui::PopFont();

                g_menu_gap = 0.35f;
            } else if(page==CREDITS || page==LOBBY_BROWSER || page==GLOBAL_ONLINE || page==LEADERBOARDS || page==COOP_HOST){
                ImGui::PushFont(tropical_body_font());tropical_header(ImGui::GetWindowPos(),ds,s);ImGuiStyle previous=ImGui::GetStyle();tropical_style();
                bool room_page=page==COOP_HOST;
                // V12.5: the page panel ends above the Back button (it used to cover it, so Back only reacted in one spot)
                ImGui::SetCursorPos(ImVec2(ds.x*(room_page?.07f:.045f),ds.y*(room_page?.185f:.19f)));
                bool panel_page=page==GLOBAL_ONLINE||page==CREDITS||page==LEADERBOARDS;  // V12.5: rounded translucent panel like the references
                if(panel_page){ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.015f,.08f,.16f,page==GLOBAL_ONLINE?.73f:.82f));ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,18*s);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(14*s,12*s));}
                ImGui::BeginChild("##v10-page",ImVec2(ds.x*(room_page?.86f:.91f),ds.y*(room_page?.70f:.695f)),ImGuiChildFlags_Borders,page==LEADERBOARDS?(ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse):0);
                if(panel_page){ImGui::PopStyleVar(2);ImGui::PopStyleColor();}
                if(page==CREDITS)tropical_credits(s);
                else if(page==LEADERBOARDS){session_leaderboards_draw(s);if(session_leaderboards_take_back())page=MAIN;}
                else if(page==COOP_HOST || page==LOBBY_BROWSER){
                    /* V13 join-in-progress + automatic seed transfer:
                     *  joiner of a Co-op Randomizer room without its exact world: an existing local seed with the same
                     *  canonical ID AND world checksum is reused; otherwise the host's world patches are downloaded from
                     *  the room (no randomizer generation, nothing of the game's own files) and verified.
                     *  host of a Co-op Randomizer room: uploads the selected world to the room once, so joiners can get it. */
                    SessionRoomInfo room_info=session_ui_room_info();
                    static std::string seed_job_key,publish_key;
                    if(cached_seed!=coop_seed){rando_room_validated=false;cached_seed=coop_seed;selected_recipe={};if(!coop_seed.empty())lao_seed_recipe(std::filesystem::path("rando")/coop_seed,selected_recipe);}
                    if(room_info.in_room&&room_info.rando){
                        bool match=!selected_recipe.id.empty()&&selected_recipe.hash==room_info.world&&selected_recipe.id==room_info.seed;
                        if(match&&!rando_room_validated&&validate_seed_root(std::filesystem::path("rando")/coop_seed).empty()){rando_room_validated=true;choice=2;}
                        /* V13.5: a failed download is tried again every 20 s (the host's game re-uploads the seed) */
                        static double seed_job_at=0;static int seed_job_tries=0;double seed_now=SDL_GetTicks()/1000.0;
                        bool seed_retry=!match&&seed_job_key==room_info.code+room_info.world&&seed_now-seed_job_at>=20&&seed_job_tries<45;
                        if(!match&&!room_info.host&&!coop_metadata_busy.load()&&(seed_job_key!=room_info.code+room_info.world||seed_retry)){
                            if(seed_job_key!=room_info.code+room_info.world)seed_job_tries=0;
                            seed_job_key=room_info.code+room_info.world;seed_job_at=seed_now;seed_job_tries++;bool found=false;
                            for(const auto &name:list_seeds()){LaoSeedRecipe local;if(lao_seed_recipe(std::filesystem::path("rando")/name,local)&&local.id==room_info.seed&&local.hash==room_info.world){coop_seed=name;found=true;break;}}
                            if(found)session_ui_set_join_status("Using your copy of this randomizer seed...");
                            else if(coop_room_ini(room_info)){coop_transfer(true,"@room","");session_ui_set_join_status("Downloading randomizer seed...");}
                            else session_ui_set_join_status("Could not prepare the seed download (no write access to the rando folder).");
                        }
                        if(!match&&!coop_metadata_busy.load()&&!seed_status.empty()&&seed_job_key==room_info.code+room_info.world){
                            if(seed_status.find("not on the server yet")!=std::string::npos||seed_status.find("unavailable")!=std::string::npos){
                                char wait[160];int left=20-(int)(SDL_GetTicks()/1000.0-seed_job_at);if(left<0)left=0;
                                snprintf(wait,sizeof wait,"Waiting for the host's seed - the host's game uploads it automatically. Trying again in %d s...",left);
                                session_ui_set_join_status(seed_tries_left(seed_job_tries)?wait:"The host's seed did not arrive. Ask the host to open Play Co-Op once, then press Join again.");
                            }else session_ui_set_join_status(seed_status);
                        }
                        if(match)session_ui_set_join_status("");
                        if(room_info.host&&match&&publish_key!=room_info.code&&!coop_metadata_busy.load()&&coop_room_ini(room_info)){publish_key=room_info.code;coop_transfer(false,"rando/"+coop_seed,"@room");}
                        laonline_host_seed_keepalive();
                    }
                    bool room_rando=room_info.in_room?room_info.rando:choice==2;
                    bool ready=!busy&&status.empty()&&(!room_rando||rando_room_validated);
                    if(session_ui_draw(page==LOBBY_BROWSER,s,room_rando?2:1,room_rando||choice==2?selected_recipe.hash:"",room_rando||choice==2?selected_recipe.id:"",ready)){
                        choice=session_ui_mode();page=MAIN;session_launch_pending=true;g_play_pending=true;
                    }
                    if(session_ui_take_browse())page=LOBBY_BROWSER;
                }else{
                    // V12: Global Vanilla (own save) or Global Randomizer (YOUR seed; nothing is shared in Global)
                    // V14.1: Join Global Randomizer opens a drop-down "Randomizer Seed Options" under the button (the
                    // user's reference picture): type / search a seed, your seeds (with saves marked), Upload Seed,
                    // Use My Current Seed. Saves live WITH their seed (rando/<seed>/save), so picking another seed shows
                    // that seed's saves - the panel says where your saves are so nobody thinks they are lost.
                    static long global_seed_frame=-1;static std::string global_seed;static size_t global_seed_count;
                    static bool global_wait_import=false;static std::string global_pick,global_pick_msg,global_seen_coop;static char global_query[96];
                    if(global_seed_frame<0||frame-global_seed_frame>120){global_seed_frame=frame;global_seed=global_seed_pick(0);global_seed_count=global_seeds().size();}
                    bool global_seed_ok=!global_seed.empty();
                    std::string global_seed_text=global_seed_ok?"Your seed: "+lao_seed_display_name(std::filesystem::path("rando")/global_seed)+" - click for seed options":"Click to upload your seed";
                    ImVec2 gp=ImGui::GetCursorScreenPos(),gsz=ImGui::GetContentRegionAvail();
                    tropical_global_rando_open(ImGui::IsPopupOpen("##global-seed-panel"));
                    int global_choice=tropical_global(s,session_global_count_text(),!busy&&status.empty(),global_seed_text.c_str());
                    float gw=gsz.x,gh=gsz.y,gcx=gp.x+gw*.5f;
                    {   /* "Use any of my seeds (N) >" under the Vanilla door, like the picture */
                        char any[80];snprintf(any,sizeof any,"Use any of my seeds (%zu)  >##global-any",global_seed_count);
                        ImGui::SetCursorScreenPos(ImVec2(gcx-gw*.33f,gp.y+gh*.50f));
                        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(1,1,1,.08f));ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.85f,.95f,1,1));
                        if(ImGui::Button(any))global_choice=2;
                        ImGui::PopStyleColor(3);
                    }
                    auto open_panel=[&](){global_pick=global_seed;global_pick_msg.clear();global_query[0]=0;global_seen_coop=coop_seed;ImGui::OpenPopup("##global-seed-panel");};
                    if(global_choice==1){session_ui_leave();laonline_session_override(nullptr,nullptr,nullptr);laonline_v12_global_variant(0);choice=3;randomizer_context=false;offline_seed_play=false;page=MAIN;g_play_pending=true;}
                    else if(global_choice==2)open_panel();
                    /* a seed file uploaded from the panel: select it as soon as it is imported */
                    if(global_wait_import&&coop_seed!=global_seen_coop&&!coop_seed.empty()){global_wait_import=false;global_pick=coop_seed;global_seed_frame=-1;global_pick_msg="Uploaded: "+lao_seed_display_name(std::filesystem::path("rando")/coop_seed)+" - press Use My Current Seed";if(!ImGui::IsPopupOpen("##global-seed-panel")){std::string keep=global_pick_msg,pick=global_pick;open_panel();global_pick_msg=keep;global_pick=pick;}}
                    if(global_wait_import&&!seed_status.empty()&&seed_status.find("mport")==std::string::npos)global_pick_msg=seed_status;
                    /* the panel: right under the Join Global Randomizer button, right edges lined up */
                    float pw=fmaxf(gw*.36f,430*s),px=gcx+gw*.33f-pw+18*s,py=gp.y+gh*.485f;
                    ImGui::SetNextWindowPos(ImVec2(px,py),ImGuiCond_Always);ImGui::SetNextWindowSize(ImVec2(pw,0),ImGuiCond_Always);
                    ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(.035f,.12f,.25f,.97f));ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(1,.82f,.36f,1));
                    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,16*s);ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize,2.5f*s);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(18*s,14*s));
                    if(ImGui::BeginPopup("##global-seed-panel",ImGuiWindowFlags_NoSavedSettings)){
                        ImGui::PushFont(g_font_menu);ImGui::TextUnformatted("Randomizer Seed Options");ImGui::PopFont();
                        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.82f,.92f,1,1));ImGui::TextWrapped("Upload your own seed file or use your current personal seed. Global is your own world: your seed, your save.");ImGui::PopStyleColor();
                        ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##global-seed-query","Enter seed / personal seed...",global_query,sizeof global_query);
                        auto all=global_seeds();std::string q=global_query;for(auto &ch:q)ch=(char)tolower((unsigned char)ch);
                        auto has_save=[](const std::string &folder){std::error_code ec;for(auto &f:std::filesystem::directory_iterator(std::filesystem::path("rando")/folder/"save"/"save_0",ec)){auto n=f.path().filename().string();if(n.rfind("savedata",0)==0&&n.size()>=13&&n.compare(n.size()-4,4,".bin")==0&&f.file_size(ec)>0)return true;}return false;};
                        std::string saved_in;for(auto &g:all)if(has_save(g.folder)){saved_in=g.label;break;}
                        ImGui::BeginChild("##global-seed-list",ImVec2(-1,fminf(230*s,(float)(all.size()?all.size():1)*30*s+12*s)),ImGuiChildFlags_Borders);
                        int shown=0;
                        for(auto &g:all){
                            std::string id;{LaoSeedRecipe r;if(lao_seed_recipe(std::filesystem::path("rando")/g.folder,r))id=r.id;}
                            std::string hay=g.label+" "+g.folder+" "+id;for(auto &ch:hay)ch=(char)tolower((unsigned char)ch);
                            if(!q.empty()&&hay.find(q)==std::string::npos)continue;
                            shown++;bool saves=has_save(g.folder);
                            std::string row=g.label+(saves?"   - has your saves":"   - no save yet")+(g.folder==global_seed_saved()?"   (current)":"")+"##gs-"+g.folder;
                            if(ImGui::Selectable(row.c_str(),g.folder==global_pick,ImGuiSelectableFlags_AllowDoubleClick)){global_pick=g.folder;global_pick_msg.clear();
                                if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))global_pick_msg="__play__";}
                        }
                        if(!shown)ImGui::TextWrapped(all.empty()?"No seeds on this PC yet - press Upload Seed, or make one in Play Randomizer.":"No seed matches that. Check the name or Seed ID, or press Upload Seed.");
                        ImGui::EndChild();
                        if(!global_pick.empty()&&!has_save(global_pick)&&!saved_in.empty())
                            {ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.84f,.4f,1));ImGui::TextWrapped("This seed has no save yet. Your saves are kept with the seed \"%s\" - pick that one to continue.",saved_in.c_str());ImGui::PopStyleColor();}
                        if(!global_pick_msg.empty()&&global_pick_msg!="__play__")ImGui::TextWrapped("%s",global_pick_msg.c_str());
                        bool play=global_pick_msg=="__play__";
                        float bw=fminf(ImGui::GetContentRegionAvail().x,320*s),bx=(ImGui::GetContentRegionAvail().x-bw)*.5f;
                        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.10f,.36f,.78f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.16f,.46f,.92f,1));ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,12*s);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+bx);ImGui::BeginDisabled(coop_metadata_busy.load());
                        if(ImGui::Button("Upload Seed",ImVec2(bw,38*s))){global_wait_import=true;global_seen_coop=coop_seed;global_pick_msg="Choose your seed file (.laoseed / .zip)...";SDL_ShowOpenFileDialog(picked_coop_seed,NULL,win,coop_seed_filter,1,NULL,false);}
                        ImGui::EndDisabled();
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+bx);ImGui::BeginDisabled(global_pick.empty());
                        if(ImGui::Button("Use My Current Seed",ImVec2(bw,38*s)))play=true;
                        ImGui::EndDisabled();ImGui::PopStyleVar();ImGui::PopStyleColor(2);
                        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.7f,.82f,.95f,1));
                        if(ImGui::SmallButton("Make a new seed")){ImGui::CloseCurrentPopup();coop_expected={};rando_launcher_new_seed();page=SETTINGS;g_tab_request_ext=3;menu_tabs_focus(false);}
                        ImGui::SameLine();if(ImGui::SmallButton("Close"))ImGui::CloseCurrentPopup();
                        ImGui::PopStyleColor();
                        if(play&&!global_pick.empty()){
                            std::string error=validate_seed_root(std::filesystem::path("rando")/global_pick);
                            if(!error.empty())global_pick_msg="This seed can't be used: "+error;
                            else{
                                global_seed_save(global_pick);global_seed=global_pick;coop_seed=global_pick;global_seed_frame=frame;ImGui::CloseCurrentPopup();
                                session_ui_leave();laonline_session_override(nullptr,nullptr,nullptr);laonline_v12_global_variant(1);choice=3;randomizer_context=false;offline_seed_play=true;page=MAIN;g_play_pending=true;
                                fprintf(stderr,"[launcher] Global Randomizer with the player's chosen seed '%s'\n",global_pick.c_str());
                            }
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopStyleVar(3);ImGui::PopStyleColor(2);
                    (void)global_seed_ok;
                }
                ImGui::EndChild();
                {   // V12.5: "< Back" bottom-left in its own space (reference); B / Esc still go back
                    float bh=fminf(ds.y*.068f,62*s),bw=bh*3.1f;ImGui::SetCursorPos(ImVec2(ds.x*.03f,ds.y*.905f));
                    bool can_back=!session_ui_busy();
                    if(settings_look::back_button("##page-back",ImVec2(bw,bh),s,can_back)||(can_back&&!typing&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)&&(ImGui::IsKeyPressed(ImGuiKey_Escape,false)||ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight,false)))){session_ui_leave();page=MAIN;}
                }
                ImGui::GetStyle()=previous;ImGui::PopFont();
            } else if (page == COOP_RANDO) {
                tropical_header(ImGui::GetWindowPos(),ds,s);
                ImGui::SetCursorPos(ImVec2(ds.x*.035f,115*s));ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.025f,.06f,.15f,.95f));ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,14*s);ImGui::BeginChild("##coop-rando",ImVec2(ds.x*.45f,ds.y-145*s),ImGuiChildFlags_Borders);
                ImGui::PushFont(g_font_menu);ImGui::TextUnformatted("CO-OP RANDOMIZER");ImGui::PopFont();
                ImGui::TextWrapped("Download Seed preserves the original name and exact world patches. Import it on the other PC; no new randomizer is generated.");
                 if(ImGui::Button("GENERATE RANDOM SEED",ImVec2(-1,42*s))){coop_expected={};rando_launcher_new_seed();page=SETTINGS;g_tab_request_ext=3;menu_tabs_focus(false);}
                 if(ImGui::Button("BROWSE SEEDS",ImVec2(-1,42*s)))SDL_ShowOpenFileDialog(picked_coop_seed,NULL,win,coop_seed_filter,1,NULL,false);
                 ImGui::BeginDisabled(coop_metadata_busy.load());
                 if(ImGui::Button("UPLOAD SEED FROM FRIEND",ImVec2(-1,42*s)))SDL_ShowOpenFileDialog(picked_coop_seed,NULL,win,coop_seed_filter,1,NULL,false);
                 ImGui::EndDisabled();
                 if(ImGui::Button("ENTER SEED MANUALLY",ImVec2(-1,42*s)))manual_seed=!manual_seed;
                seeds=list_seeds();if(ImGui::BeginCombo("Prepared seed",coop_seed.empty()?"Select a generated seed":lao_seed_display_name(std::filesystem::path("rando")/coop_seed).c_str())){for(const auto &seed:seeds)if(ImGui::Selectable((lao_seed_display_name(std::filesystem::path("rando")/seed)+"##"+seed).c_str(),seed==coop_seed)){coop_seed=seed;coop_expected={};cached_seed.clear();}ImGui::EndCombo();}
                if(ImGui::Button("Back") || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)){page=MAIN;g_refocus=MAIN;}
                ImGui::EndChild();
                ImGui::SetCursorPos(ImVec2(ds.x*.505f,115*s));ImGui::BeginChild("##coop-seed-details",ImVec2(ds.x*.46f,ds.y-145*s),ImGuiChildFlags_Borders);
                ImGui::TextUnformatted("Seed Information");
                ImTextureID banner=0;float banner_aspect=1;if(tropical_image("panorama",&banner,&banner_aspect)){float w=ImGui::GetContentRegionAvail().x;ImGui::Image(banner,ImVec2(w,w/banner_aspect));}

                if(cached_seed!=coop_seed){rando_room_validated=false;cached_seed=coop_seed;selected_recipe={};if(!coop_seed.empty())lao_seed_recipe(std::filesystem::path("rando")/coop_seed,selected_recipe);}
                if(!selected_recipe.id.empty()){
                 ImGui::Text("Prepared Seed: %s",selected_recipe.name.c_str());
                 ImGui::Text("Local folder: %s",coop_seed.c_str());char display[96];snprintf(display,sizeof display,"%s",selected_recipe.id.c_str());
                 coop_text("Canonical Seed ID",display,sizeof display,true,seed_status);
                 if(ImGui::Button("COPY SEED ID")){std::string error=validate_seed_root(std::filesystem::path("rando")/coop_seed);if(error.empty())coop_copy(selected_recipe.id,seed_status);else seed_status=error;}ImGui::SameLine();
                 if(ImGui::Button("SHARE SEED")){std::string error=validate_coop_seed(coop_seed);if(error.empty())share_open=true;else seed_status=error;}ImGui::SameLine();
                 if(ImGui::Button("DOWNLOAD SEED")){std::string error=validate_coop_seed(coop_seed);if(!error.empty())seed_status=error;else if(!coop_export_dialog&&lao_seed_recipe(std::filesystem::path("rando")/coop_seed,coop_export)){coop_export_source="rando/"+coop_seed;coop_export_dialog=true;SDL_ShowSaveFileDialog(saved_coop_seed,NULL,win,coop_export_filter,1,"LA-Online-seed.laoseed");}}
                 ImGui::TextWrapped("World checksum: %s",selected_recipe.hash.c_str());
                }else ImGui::TextUnformatted("NO SEED SELECTED - no randomized world prepared.");
                ImGui::BeginDisabled(coop_metadata_busy.load());
                if(ImGui::Button("Host / Join Selected Seed",ImVec2(-1,42*s))){std::string error=validate_coop_seed(coop_seed);if(error.empty()){rando_room_validated=true;choice=2;page=COOP_HOST;}else seed_status=error;}
                if(ImGui::Button("Get lobby seed ID"))coop_fetch(false);ImGui::SameLine();
                if(ImGui::Button("SYNC SEED"))coop_fetch(true);ImGui::SameLine();
                if(ImGui::Button("RETRY VALIDATION")){cached_seed.clear();std::string error=validate_coop_seed(coop_seed);seed_status=error.empty()?"SEED VALIDATED locally. Online readiness is checked by the relay.":error;}
                if(ImGui::Button("Publish selected world to lobby")){std::string error=validate_coop_seed(coop_seed);if(!error.empty())seed_status=error;else{coop_transfer(false,"rando/"+coop_seed,"@lobby");seed_status="Uploading the existing named world patches...";}}
                ImGui::EndDisabled();
                if(manual_seed){
                 coop_text("Seed ID",manual_id,sizeof manual_id,false,seed_status);
                 if(ImGui::Button("COPY SEED ID##manual")){std::string id=coop_canonical_id(manual_id);char error[256];if(rando_check_seed_id(id.c_str(),error,sizeof error))coop_copy(id,seed_status);else seed_status=error;}ImGui::SameLine();
                 if(ImGui::Button("VALIDATE SEED ID")){
                  std::string id=coop_canonical_id(manual_id);char error[256];
                  if(!rando_check_seed_id(id.c_str(),error,sizeof error))seed_status=error;
                  else if(!coop_expected.hash.empty()&&id!=coop_expected.id)seed_status="Seed ID differs from the host. Use SYNC SEED to receive the host's world.";
                  else if(!coop_expected.hash.empty())resolve_recipe(coop_expected,true);
                  else coop_fetch(true,id);
                 }
                 if(ImGui::Button("Validate and open full settings")){char error[256];std::string id=coop_canonical_id(manual_id);if(rando_launcher_seed_id(id.c_str(),error,sizeof error)){page=SETTINGS;g_tab_request_ext=3;menu_tabs_focus(false);}else seed_status=error;}
                }
                if(coop_metadata_busy.load()){
                 static double phase_at=0;static std::string phase;
                 if(coop_file_job.load()&&ImGui::GetTime()-phase_at>.25){phase_at=ImGui::GetTime();std::ifstream progress("laonline-seed-transfer.log");std::string line;while(std::getline(progress,line))if(line.rfind("Connecting to Host Seed",0)==0||line.rfind("Synchronizing World Data",0)==0||line.rfind("Checking Randomizer Settings",0)==0)phase=line.substr(0,140);}
                 if(!coop_file_job.load())phase.clear();ImGui::TextWrapped("%s",phase.empty()?"VALIDATING SEED - working...":phase.c_str());
                }
                if(relay_total>0)ImGui::Text("Relay snapshot: %d / %d connected players confirmed the world",relay_ready,relay_total);
                if(!seed_status.empty())ImGui::TextWrapped("%s",seed_status.c_str());
                if(share_open){ImGui::OpenPopup("Share Seed");share_open=false;}
                if(ImGui::BeginPopupModal("Share Seed",NULL,ImGuiWindowFlags_AlwaysAutoResize)){
                 ImGui::TextUnformatted("LAS Recomp Beta 5 Hotfix 3 / game 1.0.0");ImGui::TextUnformatted(LaoSeedRecipe::generator);
                 char display[96];snprintf(display,sizeof display,"%s",selected_recipe.id.c_str());coop_text("Seed ID##share",display,sizeof display,true,seed_status);
                 char checksum[65];snprintf(checksum,sizeof checksum,"%s",selected_recipe.hash.c_str());coop_text("World checksum",checksum,sizeof checksum,true,seed_status);
                 ImGui::TextUnformatted("The ID reconstructs settings. The checksum verifies the generated world.");
                 if(ImGui::Button("COPY SEED ID##share"))coop_copy(selected_recipe.id,seed_status);ImGui::SameLine();
                 if(ImGui::Button("DOWNLOAD SEED##share")){std::string error=validate_coop_seed(coop_seed);if(!error.empty())seed_status=error;else if(!coop_export_dialog&&lao_seed_recipe(std::filesystem::path("rando")/coop_seed,coop_export)){coop_export_source="rando/"+coop_seed;coop_export_dialog=true;SDL_ShowSaveFileDialog(saved_coop_seed,NULL,win,coop_export_filter,1,"LA-Online-seed.laoseed");}}
                 if(ImGui::Button("Close"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
                }
                if(!coop_metadata_busy.load()&&ImGui::Button("START CO-OP RANDOMIZER",ImVec2(-1,38*s))){cached_seed.clear();std::string error=validate_coop_seed(coop_seed);if(error.empty()){if(!g_settings.aspect_asked)ImGui::OpenPopup("##aspect");else if(!g_settings.menu_tip_seen)ImGui::OpenPopup("##menutip");else play=true;}else seed_status=error;}
                if(aspect_prompt(s)){if(g_settings.menu_tip_seen)play=true;else ImGui::OpenPopup("##menutip");}
                if(menu_tip(s))play=true;
                ImGui::EndChild();ImGui::PopStyleVar();ImGui::PopStyleColor();
            } else if (page == SETTINGS) {
                // the in-game menu's tabs (runtime/ui/menu.mm), in a window like the in-game one; tabs that need a
                // loaded game say so
                float sc = ds.y / 1080.0f, w = fminf(ds.x * 0.9f, 1500 * sc), h = ds.y - 2 * 18 * sc;  // (all the height: a slim margin)
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ds.x,
                                                          ImGui::GetWindowPos().y + ds.y), IM_COL32(0, 0, 0, 110));
                ImVec2 wp = ImGui::GetWindowPos();
                ImGui::SetCursorScreenPos(ImVec2(wp.x + (ds.x - w) * 0.5f, wp.y + (ds.y - h) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.015f, 0.07f, 0.14f, 0.98f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * sc);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * sc, 24 * sc));
                ImGui::BeginChild("##settings", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::PushFont(g_font_small);
                ImGui::PushFont(g_font_menu);
                ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), "Settings");
                ImGui::PopFont();
                bool back;
                {  // close: a drawn X in the corner (B / Esc too)
                    float bx = 44 * sc;
                    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                    back = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
                    ImGui::PopStyleColor();
                    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                    ImU32 col = hot ? IM_COL32(236, 194, 84, 255) : IM_COL32(220, 216, 205, 255);
                    float m = bx * 0.3f, t = (hot ? 3.0f : 2.4f) * sc * 1.5f;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    if (hot) dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), IM_COL32(255, 255, 255, 20), 8 * sc);
                    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, t);
                    dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, t);
                }
                bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);  // (before the pages run, like the menu)
                menu_tabs_draw(sc, g_font_small, g_font_menu, false, prelaunch_tab);
                back=back||menu_settings_take_close();
                int cap_dev;
                bind_capture_active(&cap_dev, NULL, NULL);
                if (!popup && !typing && cap_dev < 0 && !controls_ui_took_back() && !menu_tabs_took_back() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                    (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    back = true;
                ImGui::PopFont();
                ImGui::EndChild();
                if (back) {
                    bind_capture_cancel();
                    menu_settings_cancel();
                    settings_save();
                    page = MAIN, g_refocus = SETTINGS;
                    // V12.5 crash fix: after Global (choice 3, saved as the play mode) choices[choice] read past the end of
                    // the list and copying that garbage string threw std::length_error ("basic_string::_M_create") -
                    // closing or cancelling Settings on the start screen then crashed the whole game.
                    bool listed = choice >= 0 && choice < (int)choices.size();
                    std::string cur = listed ? choices[choice] : std::string();  // seeds made / renamed / deleted in the Randomizer tab
                    seeds = list_seeds();
                    choices = {"Vanilla","Co-op","Co-op Randomizer"};
                    if (listed) {
                        choice = 0;
                        for (size_t i = 0; i < choices.size(); i++)
                            if (choices[i] == cur) choice = (int)i;
                    }
                }
            } else if (update_only) {
                pthread_mutex_lock(&g_lock);
                std::string job = g_job;
                pthread_mutex_unlock(&g_lock);
                draw_busy_overlay(ds, job, false);
            } else {
                // SETUP: the game files, in a window like Settings (option rows: runtime/ui/option_rows.h) - choose the
                // folder with the game files, set up; once set up, delete. Each row's explanation on the right.
                float sc = ds.y / 1080.0f, w = fminf(ds.x * 0.9f, 1500 * sc), h = ds.y - 2 * 18 * sc;
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ds.x,
                                                          ImGui::GetWindowPos().y + ds.y), IM_COL32(0, 0, 0, 110));
                ImVec2 wp = ImGui::GetWindowPos();
                ImGui::SetCursorScreenPos(ImVec2(wp.x + (ds.x - w) * 0.5f, wp.y + (ds.y - h) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.015f, 0.07f, 0.14f, 0.98f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * sc);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * sc, 24 * sc));
                ImGui::BeginChild("##setup", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::PushFont(g_font_small);
                ImGui::PushFont(g_font_menu);
                ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), "Game Files");
                ImGui::PopFont();
                bool back = false;
                if (!busy) {  // close: the drawn X in the corner, as Settings (B / Esc too)
                    float bx = 44 * sc;
                    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
                    back = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
                    ImGui::PopStyleColor();
                    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                    ImU32 col = hot ? IM_COL32(236, 194, 84, 255) : IM_COL32(220, 216, 205, 255);
                    float m = bx * 0.3f, t = (hot ? 3.0f : 2.4f) * sc * 1.5f;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    if (hot) dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), IM_COL32(255, 255, 255, 20), 8 * sc);
                    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, t);
                    dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, t);
                }
                ImGui::Spacing();

                pthread_mutex_lock(&g_lock);
                std::string src = g_src, job = g_job, job_hint = g_job_hint;
                bool failed = g_job_failed;
                pthread_mutex_unlock(&g_lock);
                bool ready = status.empty() && !busy;  // (set up: the window offers to delete them)
                static double files_gb = -1;
                // chosen: "Selected" with a green check (long paths ran over the label); the path is in the
                // explanation, and choosing again opens the picker again
                std::string src_v = src.empty() ? "Choose..." : "Selected";
                auto check_left_of_value = [&](const std::string &v) {
                    if (v != "Selected") return;
                    ImVec2 rb = ImGui::GetItemRectMax(), ra = ImGui::GetItemRectMin();
                    float fs = ImGui::GetFontSize(), vw = ImGui::CalcTextSize(v.c_str()).x;
                    float x = rb.x - 16 * sc - vw - fs * 0.95f, cy = (ra.y + rb.y) * 0.5f, u = fs * 0.62f;
                    ImU32 g = IM_COL32(120, 220, 120, 255);
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddLine(ImVec2(x, cy), ImVec2(x + u * 0.35f, cy + u * 0.35f), g, fs * 0.11f);
                    dl->AddLine(ImVec2(x + u * 0.35f, cy + u * 0.35f), ImVec2(x + u, cy - u * 0.4f), g, fs * 0.11f);
                };
                std::string src_d = "The folder with your own extracted game files (exefs/romfs) of The Legend of Zelda: Link's "
                                    "Awakening for Nintendo Switch. The game isn't included with LAS Recomp.";
                if (!src.empty()) src_d += "\n\nChosen: " + src;
                // the step that's running / its result, under the rows and in the Set up row's explanation
                const std::string &job_line = job;
                std::string ex_d = "Copies the game files LAS Recomp needs and recompiles the game from them. This only "
                                   "needs to be done once and may take a while. Your own files aren't changed.";
                if (!job.empty()) ex_d += "\n\n" + job + (job_hint.empty() ? "" : ".\n" + job_hint);

                // the controller's cursor starts on the first row (not the X), a few frames while the window lays out
                static int seen_frame = -10, focus_first;  // (not drawn last frame: the window has just opened)
                if (seen_frame != ImGui::GetFrameCount() - 1) focus_first = 4;
                seen_frame = ImGui::GetFrameCount();
                auto first_row = [&] {
                    if (focus_first > 0 && focus_first--) ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
                };
                opt_begin("##files", sc);
                if (!ready) {
                    opt_section("Your copy of the game");
                    if (busy) opt_disable_next();
                    if (opt_action("Game files folder", src_v.c_str(), src_d.c_str(), true))
                        SDL_ShowOpenFolderDialog(picked, NULL, win, NULL, false);
                    check_left_of_value(src_v);
                    first_row();
                    opt_section("Set up");
                    if (busy || (src.empty() && !check_copied_files(g_root).empty())) opt_disable_next();
                    bool resume = !busy && src.empty() && resumable_build();
                    if (opt_action(resume ? "Resume game code" : "Set up game files", busy ? "Working..." : "", ex_d.c_str(), true)) {
                        pthread_mutex_lock(&g_lock);
                        g_busy = true;
                        pthread_mutex_unlock(&g_lock);
                        pthread_t t;
                        pthread_create(&t, NULL, extract_thread, NULL);
                        pthread_detach(t);
                    }
                    files_gb = -1;  // (re-measured after a setup)
                } else {
                    // DELETE: frees LAS Recomp's copy (runtime/paths.c removes only what the setup made, only inside
                    // the data root). Saves, seeds, settings and the user's own files stay.
                    if (files_gb < 0) files_gb = rt_game_files_size(g_root.c_str()) / 1e9;
                    char gb[32];
                    snprintf(gb, sizeof gb, "%.1f GB", files_gb);
                    opt_section("Game files");
                    if (!g_confirm_delete) {
                        if (opt_action("Delete game files", gb, "Frees the space LAS Recomp's copy of the game files takes. "
                                       "Saves, seeds and settings are kept, and your own game files aren't touched: you can "
                                       "set the game files up again from them.", true))
                            g_confirm_delete = true;
                        first_row();
                    } else {
                        if (opt_action("Yes, delete them", gb, "Deletes the extracted game files now.", true)) {
                            stop_background_prep();
                            bool ok = rt_remove_game_files(g_root.c_str());
                            game_files_changed();
                            status = check_game_files(g_root);
                            if (ok) set_job("Game files deleted.");
                            else set_job("Couldn't delete the game files", true, "They aren't in LAS Recomp's data folder, "
                                         "so they were left alone.");
                            if (!status.empty()) g_bg = 0;
                            g_confirm_delete = false, files_gb = -1;
                        }
                        if (opt_action("Cancel", "", "Keeps the game files.", true)) g_confirm_delete = false;
                    }
                }
                {  // where all of it is kept: another folder (a bigger drive)
                    const char *fixed = rt_data_root_fixed();
                    std::string d = std::string("Where LAS Recomp keeps game files, saves, seeds and settings.\n\n") +
                                    rt_data_root() + (fixed ? std::string("\n\n") + fixed : "");
                    opt_section("Storage");
                    if (busy || fixed) opt_disable_next();
                    if (opt_action("Data folder", "Change...", d.c_str(), true)) choose_data_folder();
                }
                if (!busy && has_build_cache()) {
                    opt_section("Build cache");
                    if (opt_action("Clear build cache", "", "Removes the saved game-code build files. The next setup compiles "
                                   "everything again.", true)) {
                        char why[512] = "";
                        g_build_cache_changed = true;
                        if (build_cache_clear(g_root.c_str(), why, sizeof why)) set_job("Build cache cleared.");
                        else set_job("Could not clear the build cache.", true, why);
                    }
                }
                if (!job_line.empty()) {  // the current step / result
                    ImGui::Dummy(ImVec2(0, 14 * sc));
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextColored(failed ? ImVec4(1, 0.5f, 0.4f, 1) : busy ? ImVec4(0.93f, 0.76f, 0.33f, 1)
                                              : ImVec4(0.45f, 0.95f, 0.55f, 1), "%s", job_line.c_str());
                    if (failed && !job_hint.empty())  // (what to do next, in the rows' own text colour)
                        ImGui::TextColored(ImVec4(0.86f, 0.85f, 0.80f, 1), "%s", job_hint.c_str());
                    ImGui::PopTextWrapPos();
                } else if (!busy && !status.empty() && status != "No game files yet") {
                    // (no setup run yet this session: why the game files on disk can't be played - an incomplete copy
                    // opens this window instead of crashing on Play)
                    ImGui::Dummy(ImVec2(0, 14 * sc));
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", status.c_str());
                    ImGui::TextColored(ImVec4(0.86f, 0.85f, 0.80f, 1), "%s", status_hint(status));
                    ImGui::PopTextWrapPos();
                } else if (ready && files_changed(g_root)) {  // (playable, but a damaged or changed file: #391)
                    ImGui::Dummy(ImVec2(0, 14 * sc));
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextColored(ImVec4(1, 0.72f, 0.4f, 1), "%s", CHANGED_HEAD);
                    ImGui::TextColored(ImVec4(0.86f, 0.85f, 0.80f, 1), "%s", CHANGED_HINT);
                    ImGui::PopTextWrapPos();
                }
                if (!ready && g_onedrive > 0 && !g_settings.onedrive_kept) {  // (before the copy is the time to move)
                    ImGui::Dummy(ImVec2(0, 14 * sc));
                    int r = onedrive_notice(1.5f * sc, ImGui::GetContentRegionAvail().x, false);
                    if (r == 1) onedrive_keep();
                    else if (r == 2) choose_data_folder();
                }
                if (ready) opt_end();
                else {  // before the setup: under the description, what the folder to choose looks like, and the hint
                    opt_split();
                    opt_desc_text();
                    float fs = ImGui::GetFontSize(), ew = fminf(ImGui::GetContentRegionAvail().x, fs * 15);
                    ImGui::Dummy(ImVec2(0, fs * 0.5f));  // (a paragraph's space)
                    draw_folder_example(ew);
                    ImGui::Dummy(ImVec2(0, fs * 0.3f));  // (its shadow)
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ew);
                    ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.52f, 1), "%s", PICK_FOLDER_HINT);
                    ImGui::PopTextWrapPos();
                    opt_split_end();
                }
                if (g_confirm_delete && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                                         ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    g_confirm_delete = false;  // (B / Esc: back out of the question first)
                else if (!busy && !typing && g_df == DF_NONE && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                                                                 ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                    back = true;
                ImGui::PopFont();
                ImGui::EndChild();
                if (back && !busy) {
                    settings_save();
                    g_confirm_delete = false;
                    page = MAIN, g_refocus = SETUP;
                }
                static bool test_busy = getenv("LARECOMP_LAUNCHER_SHOT") && (strstr(getenv("LARECOMP_LAUNCHER_SHOT"), ":extracting") ||
                                                                         strstr(getenv("LARECOMP_LAUNCHER_SHOT"), ":preparing") ||
                                                                         strstr(getenv("LARECOMP_LAUNCHER_SHOT"), ":skipask"));
                if (busy || test_busy) draw_busy_overlay(ds, job, test_busy);  // (everything dimmed, a bar in front)

            }

            // status (lower left) and version (lower right) - not under the Settings / Game Files windows
            if (page == MAIN) {
            ImGui::PushFont(tropical_body_font());
            ImGui::SetCursorPos(ImVec2(ds.x*.024f, ds.y*.886f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(0,2*s));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
            ImGui::BeginChild("##home-game-status",ImVec2(ds.x*.235f,ds.y*.114f-5*s),ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);
            ImGui::SetWindowFontScale(.95f);
            const float status_x=ImGui::GetCursorPosX();
            const char *fixture_version=shot_at?getenv("LARECOMP_LAUNCHER_STATUS_FIXTURE"):NULL;
            if (!fixture_version && status.empty() && files_changed(g_root)) {  // (playable, but a damaged or changed file: #391)
                ImGui::TextColored(ImVec4(1, 0.72f, 0.4f, 1), "!  %s", CHANGED_HEAD);
                ImGui::SetCursorPosX(status_x);
                ImGui::TextDisabled("%s", CHANGED_HINT);
            } else if (status.empty() || fixture_version) {
                {  // a drawn check mark (the Switch font has no ✓)
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    float u = ImGui::GetTextLineHeight();
                    ImU32 g = IM_COL32(140, 230, 140, 255);
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + u * 0.1f, p.y + u * 0.55f), ImVec2(p.x + u * 0.35f, p.y + u * 0.8f), g, u * 0.12f);
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + u * 0.35f, p.y + u * 0.8f), ImVec2(p.x + u * 0.85f, p.y + u * 0.2f), g, u * 0.12f);
                    ImGui::SetCursorPosX(status_x + u * 1.3f);
                }
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1), "Game files ready.");
                ImGui::SetCursorPosX(status_x);
                static std::string version_label;  // (the copied files' release: "1.0.1")
                static int label_of = -1;
                if (label_of != g_files_changes.load()) {
                    label_of = g_files_changes.load();
                    char label[32];
                    const char *v = game_release_label((g_root + "/exefs").c_str(), label, sizeof label);
                    version_label = fixture_version ? fixture_version : v ? v : "?";
                }
                ImGui::TextColored(ImVec4(0.66f, 0.76f, 0.98f, 1), "Nintendo Switch  \xc2\xb7  v%s", version_label.c_str());
            } else {
                ImGui::TextColored(ImVec4(1, 0.72f, 0.4f, 1), "!  %s", busy ? "Setting up the game files..." : status.c_str());
                ImGui::SetCursorPosX(status_x);
                ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.84f,.91f,1,1));ImGui::TextWrapped("%s", busy ? "Set up the game files to play." : status_hint(status));ImGui::PopStyleColor();
            }
            tropical_update_notice();
            ImGui::SetWindowFontScale(1);
            ImGui::EndChild();ImGui::PopStyleVar(2);
            if (page==MAIN && g_onedrive > 0 && !g_settings.onedrive_kept) {
                float w = fminf(ds.x * 0.42f, 560 * s);
                ImGui::SetCursorPos(ImVec2(ds.x - w - 60 * s, 60 * s));
                int r = onedrive_notice(s, w, status.empty());
                if (r == 1) onedrive_keep();
                else if (r == 2) choose_data_folder();
            }
            if (const char *f = getenv("LARECOMP_LAUNCHER_FONT")) {  // (test) which typeface this window shows
                const char *b = strrchr(f, '/');
                ImGui::GetWindowDrawList()->AddText(ImVec2(ImGui::GetWindowPos().x + 20 * s, ImGui::GetWindowPos().y + 16 * s),
                                                    IM_COL32(255, 255, 255, 255), b ? b + 1 : f);
            }
            if (false) {  // version, and a newer release when the update check found one (lower right). The background is
               // always the title movie's still: bright clouds here, under a shadow banner from the right
                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImFont *font = ImGui::GetFont();
                float fs = ImGui::GetFontSize(), right = ImGui::GetWindowPos().x + ds.x - 60 * s;
                auto text = [&](ImVec2 p, float size, ImU32 col, const char *t, bool bold) {
                    for (float d : {2.5f, 1.5f, 0.8f}) dl->AddText(font, size, ImVec2(p.x + d * s, p.y + d * s), IM_COL32(4, 8, 30, 150), t);
                    dl->AddText(font, size, p, col, t);
                    if (bold) dl->AddText(font, size, ImVec2(p.x + 0.7f * s, p.y), col, t);
                };
                std::string ver = tag_label(LARECOMP_VERSION, true);
                float vs = fs * 1.4f, y = ImGui::GetWindowPos().y + ds.y - 134 * s;
                ImVec2 vsz = font->CalcTextSizeA(vs, FLT_MAX, 0, ver.c_str());
                text(ImVec2(right - vsz.x, y), vs, IM_COL32(255, 255, 255, 255), ver.c_str(), false);
                pthread_mutex_lock(&g_update_lock);
                std::string upd = g_update_tag;
                pthread_mutex_unlock(&g_update_lock);
                if (!upd.empty()) {
                    const char *arrow = font->FindGlyphNoFallback(0x2192) ? " \xe2\x86\x92" : " \xe2\x96\xb8";
                    std::string pre = "Update available!  ", link = "DOWNLOAD " + tag_label(upd, false) + arrow;
                    float wp = font->CalcTextSizeA(fs, FLT_MAX, 0, pre.c_str()).x;
                    ImVec2 lsz = font->CalcTextSizeA(fs, FLT_MAX, 0, link.c_str());
                    ImVec2 lp(right - lsz.x, y + vsz.y + 2 * s);
                    text(ImVec2(lp.x - wp, lp.y), fs, IM_COL32(200, 214, 250, 255), pre.c_str(), false);
                    ImGui::SetCursorScreenPos(lp);
                    bool click = ImGui::InvisibleButton("update", lsz);
                    bool hover = ImGui::IsItemHovered();
                    // the menu's hover colours (yellow to orange), lighter while the link itself is hovered
                    ImU32 col = hover ? IM_COL32(255, 238, 150, 255) : IM_COL32(255, 214, 64, 255);
                    ImU32 col_b = hover ? IM_COL32(255, 196, 110, 255) : IM_COL32(255, 160, 50, 255);
                    text_fx(dl, font, fs, lp, col, col_b, link.c_str(), 1.5f * s);
                    text_fx(dl, font, fs, ImVec2(lp.x + 0.7f * s, lp.y), col, col_b, link.c_str(), 0);  // (faux bold)
                    if (hover) {
                        dl->AddLine(ImVec2(lp.x, lp.y + lsz.y), ImVec2(lp.x + lsz.x, lp.y + lsz.y), col, 1.5f * s);
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                    if (click) SDL_OpenURL((RELEASE_PAGE + upd).c_str());
                }
            }
            ImGui::PopFont();
            }
            if(launcher_quit_requested){launcher_quit_requested=false;ImGui::OpenPopup("Quit LAS Recompiled?");}
                if(ImGui::BeginPopupModal("Quit LAS Recompiled?",NULL,ImGuiWindowFlags_AlwaysAutoResize)){
                    ImGui::TextUnformatted("Close LAS Recompiled and return to the desktop?");
                    if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::SameLine();
                    if(ImGui::Button("Yes, quit"))exit(0);ImGui::EndPopup();
                }
            ImGui::EndDisabled();
            data_folder_popup(s);  // (another data folder picked: move / start fresh, then restart)
            ImGui::End();
            ImGui::PopStyleColor();
            if (waiting) draw_busy_overlay(ds, "", false, true);
            osk_draw(g_font_menu, s * 0.7f);  // (a text field activated with the controller)
            typed_before = ui_text_busy();

            ImGui::Render();
#ifdef __APPLE__
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc);
            [enc endEncoding];
            id<MTLTexture> shot = nil;
            if (shot_at && ++frame == shot_at) {
                MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:drawable.texture.pixelFormat
                                             width:drawable.texture.width height:drawable.texture.height mipmapped:NO];
                td.storageMode = MTLStorageModeShared;
                shot = [dev newTextureWithDescriptor:td];
                id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
                [bl copyFromTexture:drawable.texture toTexture:shot];
                [bl endEncoding];
            }
            [cb presentDrawable:drawable];
            [cb commit];
            if (shot) {
                [cb waitUntilCompleted];
                save_png(shot, getenv("LARECOMP_SHOT_PATH") ? getenv("LARECOMP_SHOT_PATH") : "build/launcher.png");
                exit(0);
            }
#else
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)vk_frame_cmd());
            bool shot = shot_at && ++frame == shot_at;
            vk_frame_end(shot ? getenv("LARECOMP_SHOT_PATH") ? getenv("LARECOMP_SHOT_PATH") : "build/launcher.png" : NULL);
            if (shot) exit(0);
#endif
        }
    }
    laonline_launch_mode(choice);
    g_settings.laonline_play_mode=choice;
    snprintf(g_settings.laonline_rando_seed,sizeof g_settings.laonline_rando_seed,"%s",coop_seed.c_str());
    if(settings_edit_active())settings_edit_commit();
    // play: remember the choice, hand the window to the game
    snprintf(g_settings.rando_seed, sizeof g_settings.rando_seed, "%s", choice==2 || offline_seed_play ? coop_seed.c_str() : "");
    settings_save();
#ifdef __APPLE__
    ImGui_ImplMetal_Shutdown();
#else
    vk_imgui_backend_shutdown();
#endif
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(prev_ctx);
    g_bg = 0;
    g_start_screen = 0;
}



