#include "laonline_death.h"
/* V13 (Velvet's video, Marin & Tarin's house): name tags sat about two Links above the player. A Link is ~1.7 world
 * units tall on screen; the tag is anchored just above the head (the plate adds its own small gap). Was 2.35. */
#define LAO_TAG_HEAD 1.75f
#include "laonline_story.h"

#include "laonline_checks.h"
// LA-Online (PC prototype, phase 2/3): a second player's Link in LAS Recomp.
//
// OFF unless enabled: laonline.ini next to lasrecomp.exe (or env LAONLINE_MODE). Nothing else in the game changes.
//
//   [laonline.ini]
//   mode = echo        ; echo | host | join   (echo: one PC - a puppet replays YOUR path 1.5 s late, 1.5 units aside)
//   name = LunarGamerz
//   port = 47621       ; host: UDP port to listen on
//   peer = 192.168.1.145:47621   ; join: the host's address
//   puppet_type = 0x21E ; actor type used for the remote Link (0x21E = ShadowLink: tools/rando/miniboss_guard.py)
//
// What is verified from LAS Recomp's own source (all used by its working features):
//   - Link's position: doorjump_player(frame) -> PlayerCollision component, x/y/z floats at +0x80 (miniboss.c, escape.c)
//   - Level name: [GA(PLAYER_STATUS_PTR)] -> C string at +0 (pot_glow.c current_level)
//   - Current room: GAME_MGR +0x4a8 scene (SCENE_LOCK) -> +0x228 room holder, room file name at [+0x1a8]+0x74
//     (digs.c room_named; the holder itself is the current room)
//   - Spawning an actor: GA(CREATE_ACTOR)(out shared_ptr, &room, spawn info) with type u16 at info+8 and the
//     transform at info+0x180 (digs.c spawn_into); removing one: GA(ACTOR_KILL)(actor) (pets.c)
//   - An actor's position: [actor+0x80]+0x80 (miniboss.c "[mb-y]")
// NOT verified yet (this prototype exists to find out, see the [laonline] log lines):
//   - that ShadowLink spawned with zero parameters renders as Link and stays put when we move it every frame;
//     he is an enemy: his own AI may still move/attack. LAONLINE_SL_HOLD=1 keeps him in his "appearing" state.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define sock_close closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define sock_close close
#endif
#include <math.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game_addresses.h"
#include "../laonline_build.h"
#include "../hle.h"
#include "../settings.h"
#include "../cosmetic_colour.h"
#include "laonline_session.h"
#include "laonline_location.h"
#include "../ui/laonline_map_state.h"
#include "laonline_native.h"
#include "laonline_map_anchor.h"
#include "laonline_chat.h"
#include "laonline_v12.h"  /* V12: shared progress, consumables, PvP, Global Randomizer */
#include "laonline_diag.h"
#include "../rando.h"
extern void laonline_look_diag(Cpu *c, uint64_t obj);

uint64_t doorjump_player(uint64_t frame);  // runtime/hle/doorjump.c
extern int game_in_play(void);
// Explicit launcher ownership: configuration alone never opts Vanilla online.
static atomic_int g_launch_online,g_launch_epoch;
static int g_launch_requested;
int laonline_launch_choice(void){return g_launch_requested;}
void laonline_resume_launch_mode(void){atomic_store(&g_launch_online,g_launch_requested);}

void laonline_launch_mode(int enabled) {
    g_launch_requested=enabled;atomic_fetch_add(&g_launch_epoch,1);
    atomic_store(&g_launch_online, enabled);
    LOG("[laonline] launch mode=%s\n", enabled ? "co-op" : "offline");
}
/* Boss type IDs 0x62..0x6a are the nine bosses in upstream
 * tools/rando/souls.py. This observes a factory request, never claims HP/death. */
void laonline_boss_spawn_diagnostic(uint32_t type,uint64_t key) {
    if(!atomic_load(&g_launch_online) || type<0x62 || type>0x6a)return;
    laonline_diag("boss.spawn_requested",0xffff,0xffff,key,0,1);
    LOG("[laonline] boss spawn requested type=%#x actor_key=%llx (not a damage/death event)\n",type,(unsigned long long)key);
}


// ---------------------------------------------------------------- protocol (portable: also for the Switch mod later)
#define LAO_MAGIC 0x314f414cu  // "LAO1"
#define LAO_VERSION 1
typedef struct {
    uint32_t magic;
    uint16_t version, flags;
    uint32_t id, seq;
    char name[17];
    char level[31];
    char room[32];
    float x, y, z, yaw;
} LaoState;  // 112 bytes, little-endian on every target
_Static_assert(sizeof(LaoState) == 112, "LaoState layout");

// ---------------------------------------------------------------- config
enum { M_OFF, M_ECHO, M_HOST, M_JOIN, M_SERVER };
static struct {
    int mode, port, type;
    char name[17], peer[64];
    char server[96], lobby[33], password[33];
} g_cfg = {M_OFF, 47621, 0x21E, "Player", "", "192.168.1.145:47620", "default", ""};
// V10 launch room credentials are ephemeral; never overwrite a player's config.
static pthread_mutex_t room_lock=PTHREAD_MUTEX_INITIALIZER;
static char room_name[17],room_lobby[33],room_password[65];static int room_valid;
void laonline_session_override(const char *name,const char *lobby,const char *password){
 pthread_mutex_lock(&room_lock);room_valid=name&&lobby&&password;
 if(room_valid){snprintf(room_name,sizeof room_name,"%s",name);snprintf(room_lobby,sizeof room_lobby,"%s",lobby);snprintf(room_password,sizeof room_password,"%s",password);}pthread_mutex_unlock(&room_lock);
}
static void room_apply_identity(void){if(atomic_load(&g_launch_online)==3)g_cfg.mode=M_SERVER;pthread_mutex_lock(&room_lock);if(room_valid){g_cfg.mode=M_SERVER;snprintf(g_cfg.name,sizeof g_cfg.name,"%s",room_name);}pthread_mutex_unlock(&room_lock);}
static void room_login(char *name,char *lobby,char *password){pthread_mutex_lock(&room_lock);snprintf(name,17,"%s",room_valid?room_name:g_cfg.name);snprintf(lobby,33,"%s",room_valid?room_lobby:g_cfg.lobby);snprintf(password,65,"%s",room_valid?room_password:g_cfg.password);pthread_mutex_unlock(&room_lock);}


static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    char *b = s;
    while (*b == ' ' || *b == '\t') b++;
    memmove(s, b, strlen(b) + 1);
}

static int g_look_link = 1;  /* 0x0C: puppet_look = link (default) | shadow */
static void set_kv(const char *k, const char *v) {
    if (!strcmp(k, "puppet_look")) { g_look_link = strcmp(v, "shadow") != 0; return; }
    if (!strcmp(k, "mode")) g_cfg.mode = !strcmp(v, "echo") ? M_ECHO : !strcmp(v, "host") ? M_HOST : !strcmp(v, "join") ? M_JOIN : !strcmp(v, "server") ? M_SERVER : M_OFF;
    else if (!strcmp(k, "server")) snprintf(g_cfg.server, sizeof g_cfg.server, "%s", v);
    else if (!strcmp(k, "lobby")) snprintf(g_cfg.lobby, sizeof g_cfg.lobby, "%s", v);
    else if (!strcmp(k, "password")) snprintf(g_cfg.password, sizeof g_cfg.password, "%s", v);
    else if (!strcmp(k, "name")) snprintf(g_cfg.name, sizeof g_cfg.name, "%s", v);
    else if (!strcmp(k, "port")) g_cfg.port = atoi(v);
    else if (!strcmp(k, "peer")) snprintf(g_cfg.peer, sizeof g_cfg.peer, "%s", v);
    else if (!strcmp(k, "puppet_type")) g_cfg.type = (int)strtol(v, NULL, 0);
}

static void load_config(void) {
    FILE *f = fopen("laonline.ini", "r");
    if (f) {
        char line[768];
        while (fgets(line, sizeof line, f)) {
            char *sc = strchr(line, ';');
            if (sc) *sc = 0;
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            trim(line);
            trim(eq + 1);
            set_kv(line, eq + 1);
        }
        fclose(f);
    }
    if (ENV("LAONLINE_MODE")) set_kv("mode", ENV("LAONLINE_MODE"));
    if (ENV("LAONLINE_PEER")) set_kv("peer", ENV("LAONLINE_PEER"));
    if (ENV("LAONLINE_PORT")) set_kv("port", ENV("LAONLINE_PORT"));
    // (LAS Recomp Co-Op's port: the relay's address and the name too - two copies on one PC share the data root's
    // laonline.ini; tools/coop_tests/online_relay.sh)
    if (ENV("LAONLINE_SERVER")) set_kv("server", ENV("LAONLINE_SERVER"));
    if (ENV("LAONLINE_NAME")) set_kv("name", ENV("LAONLINE_NAME"));
}

// ---------------------------------------------------------------- network (non-blocking UDP, polled on the game thread)
static sock_t g_sock = BAD_SOCK;
static struct sockaddr_in g_peer;
static int g_have_peer;
static uint32_t g_my_id, g_seq;

static int net_open(void) {
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w)) return 0;
#endif
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == BAD_SOCK) return 0;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)(g_cfg.mode == M_HOST ? g_cfg.port : 0));
    if (bind(g_sock, (struct sockaddr *)&a, sizeof a) != 0) {
        LOG("[laonline] could not open UDP port %d\n", g_cfg.mode == M_HOST ? g_cfg.port : 0);
        sock_close(g_sock), g_sock = BAD_SOCK;
        return 0;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(g_sock, FIONBIO, &nb);
#else
    fcntl(g_sock, F_SETFL, fcntl(g_sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    if (g_cfg.mode == M_JOIN) {
        char host[64];
        int port = g_cfg.port;
        snprintf(host, sizeof host, "%s", g_cfg.peer);
        char *colon = strrchr(host, ':');
        if (colon) *colon = 0, port = atoi(colon + 1);
        memset(&g_peer, 0, sizeof g_peer);
        g_peer.sin_family = AF_INET;
        g_peer.sin_port = htons((uint16_t)port);
        g_have_peer = inet_pton(AF_INET, host, &g_peer.sin_addr) == 1;
        LOG("[laonline] join: sending to %s:%d%s\n", host, port, g_have_peer ? "" : " (bad address - use an IP)");
    } else {
        LOG("[laonline] host: listening on UDP %d\n", g_cfg.port);
    }
    return 1;
}

// ---------------------------------------------------------------- local state
static uint64_t g_room_args, g_transform_args;
static uint64_t g_spawn_args_by_peer[LAO_PLAYERS];
#define g_spawn_args (g_spawn_args_by_peer[g_puppet_slot])
static uint64_t g_spawn_serial;

static bool current_room_name(Cpu *c, char *out, size_t n) {
    out[0] = 0;
    uint64_t mgr = LD64(GA(GAME_MGR));
    if (!mgr) return false;
    if (!g_room_args) g_room_args = (uint64_t)(uintptr_t)hle_alloc(0x40);
    uint64_t pair = g_room_args;
    memset((void *)(uintptr_t)pair, 0, 0x40);
    c->x[0] = pair + 0x10;
    c->x[1] = LD64(mgr + 0x4a8);
    guest_call(c, GA(SCENE_LOCK));
    uint64_t scene = LD64(pair + 0x10), sctrl = LD64(pair + 0x18), holder = scene ? LD64(scene + 0x228) : 0;
    bool ok = false;
    if (holder >= HEAP_BASE && holder + 0x260 <= HEAP_BASE + HEAP_MAX) {
        uint64_t info = LD64(holder + 0x1a8);
        if (info >= HEAP_BASE && info < HEAP_BASE + HEAP_MAX) {
            const char *file = (const char *)(uintptr_t)(info + 0x74), *end = memchr(file, 0, 0x40);
            if (end && end - file > 4) {
                const char *base = end;
                while (base > file && base[-1] != '/') base--;
                size_t len = (size_t)(end - base - 4);  // (".leb")
                if (len >= n) len = n - 1;
                memcpy(out, base, len), out[len] = 0;
                ok = true;
            }
        }
    }
    if (sctrl) { c->x[0] = sctrl; guest_call(c, GA(SP_RELEASE)); }
    return ok;
}

extern int laonline_level_ready(void);

static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

// ---------------------------------------------------------------- the remote player and its puppet
typedef struct {
    LaoState st;           // latest accepted state
    uint64_t consumed_revision,unready_since,recovery_after;
    uint64_t at;           // frame it arrived
    int valid;
    float px, py, pz;      // where the puppet is drawn (smoothed)
    float yaw;
    uint64_t obj, ctrl;    // the puppet actor and our weak reference to it
    uint64_t arrival_at;
    uint32_t popped_id;    // V10: the player he already popped up for (later respawns are quiet)
    int phase;             // V5: 0 init (his own update runs), 1 popping (runs, Pop state), 2 held (update skipped)
    uint64_t phase_at;     // ms the phase began
    uint32_t updates;      // his update calls seen (log)
    uint16_t anim_id;
    uint8_t anim_flags;
    LaoWireState wire;
    uint64_t owner_room, owner_control, candidate_room, candidate_control;
    uint64_t stable_since, last_try;
    int wait_reason;
    char level[31];        // level the puppet was spawned in
    uint16_t map_id, room_id;
} LaoPuppet;
/* One independent lifecycle and native spawn record per remote participant. */
static LaoPuppet g_puppets[LAO_PLAYERS];
static unsigned g_puppet_slot;
#define g_rp (g_puppets[g_puppet_slot])
#define g_rp_map (g_rp.map_id)
#define g_rp_room (g_rp.room_id)

/* A1: use the loaded holder pool already verified and used by digs.c room_named.
 * Protocol v1 sends a folded room id, not its name. Match the file's basename
 * with the same lao_room_id; reject ambiguous hash matches rather than guess.
 * The pair is borrowed only on this game-thread tick, as in digs.c. */
static bool g_puppet_owner_loaded;
static bool remote_room_pair(Cpu *c, const char *level, uint64_t *room, uint64_t *control) {
    *room = *control = 0;g_puppet_owner_loaded=false;
    uint64_t mgr = LD64(GA(GAME_MGR));
    if (!mgr) return false;
    if (!g_room_args) g_room_args = (uint64_t)(uintptr_t)hle_alloc(0x40);
    uint64_t pair = g_room_args;
    memset((void *)(uintptr_t)pair, 0, 0x40);
    c->x[0] = pair + 0x10; c->x[1] = LD64(mgr + 0x4a8);
    guest_call(c, GA(SCENE_LOCK));
    uint64_t scene = LD64(pair + 0x10), sc = LD64(pair + 0x18);
    uint64_t holder = scene ? LD64(scene + 0x228) : 0;
    unsigned matches = 0;
    if (holder) for (int k = 0; k < 64; ++k) {
        uint64_t e = holder + (int64_t)((k & 1) ? -(k + 1) / 2 : k / 2) * 0x260;
        if (e < HEAP_BASE || e + 0x260 > HEAP_BASE + HEAP_MAX ||
            LD64(e + 0x50) != e || (k && LD32(e + 0x240) != 1)) continue;
        uint64_t info = LD64(e + 0x1a8), r = LD64(e + 0x1b8), rc = LD64(e + 0x1c0);
        if (r<HEAP_BASE || r+0x100>HEAP_BASE+HEAP_MAX || rc<HEAP_BASE || rc+0x18>HEAP_BASE+HEAP_MAX ||
            (int64_t)LD64(rc+8)<0 || info < HEAP_BASE || info + 0xb4 > HEAP_BASE + HEAP_MAX) continue;
        const char *file = (const char *)(uintptr_t)(info + 0x74), *end = memchr(file, 0, 0x40);
        if (!end || end - file < 5 || strcmp(end - 4, ".leb")) continue;
        const char *base = end - 4;
        while (base > file && base[-1] != '/') --base;
        size_t n = (size_t)(end - 4 - base);
        char name[64]; memcpy(name, base, n); name[n] = 0;
        if(r==g_rp.owner_room && rc==g_rp.owner_control)g_puppet_owner_loaded=true;
        bool match = g_rp.st.room[0] ? !strcmp(name, g_rp.st.room) :
            (g_rp_room != LAO_UNKNOWN && lao_room_id(level, name) == g_rp_room);
        if (match) { ++matches; *room = r; *control = rc; }
    }
    if (sc) { c->x[0] = sc; guest_call(c, GA(SP_RELEASE)); }
    if (matches != 1) { *room = *control = 0; return false; }
    return true;
}

/* Native update isolation and animation integration, guarded by code fingerprints. */
static uint64_t g_position_set, g_rotation_set;
static bool native_adapter_ready(void) {
    if (!g_ga) return false;
    if (LD32(GA(LAONLINE_SHADOW_UPDATE_SITE)) != 0xd10103ffu ||
        LD32(GA(LAONLINE_ANIMATION_SITE)) != 0xd10183ffu ||
        LD32(GA(LAONLINE_SL_POP)) != 0xf81e0ff3u) return false;
    /* The transform's setters, from the calls Shadow Link's own code makes to them. */
    g_position_set = lao_bl_target(GA(LAONLINE_SL_POSITION_CALL), LD32(GA(LAONLINE_SL_POSITION_CALL)));
    g_rotation_set = lao_bl_target(GA(LAONLINE_SL_ROTATION_CALL), LD32(GA(LAONLINE_SL_ROTATION_CALL)));
    return g_position_set >= MAIN_BASE && g_position_set < SDK_BASE &&
           g_rotation_set >= MAIN_BASE && g_rotation_set < SDK_BASE;
}
static uint16_t g_local_anim = 0x67;
static uint8_t g_local_anim_flags;
static uint64_t g_local_anim_at;
void laonline_native_animation(Cpu *c) {
    if (!g_ga || c->x[2] != 0 || c->x[1] > 0xffffu) return;
    uint64_t mgr = LD64(GA(GAME_MGR)), link = mgr ? LD64(mgr + 0x4b8) : 0;
    if (!link || LD64(link + LAO_LOCAL_ANIMATION_OFF) != c->x[0]) {
        // (LAS Recomp Co-Op's port: or another local co-op player's Link - sent on his own connection, laonline_local.c)
        extern void laonline_local_animation(uint64_t player, uint16_t anim);
        laonline_local_animation(c->x[0], (uint16_t)c->x[1]);
        return;
    }
    g_local_anim = (uint16_t)c->x[1];
    g_local_anim_flags = (g_local_anim_flags ^ LAO_ANIMATION_EVENT) | LAO_ANIMATION_VALID;
    g_local_anim_at = plat_now_ns() / 1000000ull;
}


static bool weak_alive(uint64_t ctrl) { return ctrl && (int64_t)LD64(ctrl + 8) >= 0; }

/* V15 (PvP hit reaction, Z-targeting): the LOCAL Link through the same verified native helpers the puppets use.
 * All return 0 (and do nothing) when the code fingerprints don't match this game version. */
uint16_t laonline_local_anim(uint64_t *at_ms) { if (at_ms) *at_ms = g_local_anim_at; return g_local_anim; }
static uint64_t local_link_actor(void) {
    if (!g_ga) return 0;
    uint64_t mgr = LD64(GA(GAME_MGR)), link = mgr ? LD64(mgr + 0x4b8) : 0;
    return link >= HEAP_BASE && link < HEAP_BASE + HEAP_MAX ? link : 0;
}
uint64_t laonline_local_link(void) { return local_link_actor(); }
int laonline_local_yaw(float *yaw) {  /* Link's facing, as the puppets send it */
    uint64_t link = local_link_actor(), tr = link ? LD64(link + 0x80) : 0;
    if (!(tr >= HEAP_BASE && tr < HEAP_BASE + HEAP_MAX)) return 0;
    *yaw = lao_quaternion_yaw((const float *)(uintptr_t)(tr + 0x90));
    return isfinite(*yaw);
}
int laonline_local_set_yaw(Cpu *c, float yaw) {  /* turn Link (the puppets' rotation setter) */
    if (!c || !isfinite(yaw) || !native_adapter_ready()) return 0;
    uint64_t link = local_link_actor(), tr = link ? LD64(link + 0x80) : 0;
    if (!(tr >= HEAP_BASE && tr < HEAP_BASE + HEAP_MAX)) return 0;
    if (!g_transform_args) g_transform_args = (uint64_t)(uintptr_t)hle_alloc(0x40);
    float *q = (float *)(uintptr_t)(g_transform_args + 16);
    lao_yaw_quaternion(yaw, q);
    Cpu saved = *c;
    c->x[0] = tr; c->x[1] = g_transform_args + 16; guest_call(c, g_rotation_set);
    *c = saved;
    return 1;
}
int laonline_local_play_anim(Cpu *c, uint16_t id) {  /* play one of Link's own animations (the puppets' player) */
    if (!c || !id || !native_adapter_ready()) return 0;
    uint64_t link = local_link_actor(), anim = link ? LD64(link + LAO_LOCAL_ANIMATION_OFF) : 0;
    if (!(anim >= HEAP_BASE && anim < HEAP_BASE + HEAP_MAX)) return 0;
    Cpu saved = *c;
    c->x[0] = anim; c->x[1] = id; c->x[2] = 0; c->v[0] = 0;
    guest_call(c, GA(LAONLINE_ANIMATION_SITE));
    *c = saved;
    return 1;
}

/* The native show routine d14820 also enables CharacterMovementComponent.
 * Pop disables the two attack components, but leaves this physical collider
 * enabled. A network-driven visual must never block a native boss's movement.
 * Use the component's own enable API so its PhysX filters are updated too;
 * clearing an actor byte alone would leave stale collision shapes behind. */
static void puppet_collision_off(Cpu *c) {
    uint64_t set_enabled=GA(LAONLINE_COLLIDER_SET_ENABLED);
    if(!set_enabled || !g_rp.obj || !weak_alive(g_rp.ctrl) ||
       LD64(g_rp.obj)!=GA(SHADOW_LINK_VTABLE))return;
    uint64_t comp=LD64(g_rp.obj+0x1630u);
    if(comp<HEAP_BASE || comp>HEAP_BASE+HEAP_MAX-0x300 ||
       LD64(comp)!=GA(LAONLINE_COLLIDER_VT) ||
       LD64(LD64(comp)+0xa8)!=set_enabled ||
       LD32(set_enabled)!=0x39473009u ||
       LD32(set_enabled+4)!=0x12000028u || !LD8(comp+0x1cc))return;
    Cpu saved=*c;c->x[0]=comp;c->x[1]=0;
    guest_call(c,set_enabled);*c=saved;
    laonline_diag("puppet.collision_disable_readback",g_rp.map_id,g_rp.room_id,g_rp.obj,1,LD8(comp+0x1cc));
    LOG("[laonline] remote Link collision disabled peer=%u native_enabled=%u\n",g_rp.st.id,LD8(comp+0x1cc));
}

/* V12 "ghost" remote Link: still drawn and animated, but nothing of him can touch the world. V10 already switched off
 * his movement collider (+0x1630) and his two body-hit components (+0x1690 / +0x16a0); his sword colliders
 * (SwordAttackColComp, SwordDamageColComp, SwordCharging*, SwordPoke*, SwordRotation* - names from the V12 look
 * diagnostics) stayed live, so a teammate's swing played on his puppet could still reach a boss on this PC.
 * Every component of THIS actor whose "set enabled" virtual (+0xa8) is one of the three verified collider
 * implementations above is found once per incarnation and kept off every frame while v12_ghost_puppets applies
 * (default: the Wind Fish's Egg, where Dethl got stuck). Components the local Link also references are skipped. */
#define GHOST_MAX 24
static uint64_t g_ghost_obj[LAO_PLAYERS],g_ghost_comp[LAO_PLAYERS][GHOST_MAX],g_ghost_fn[LAO_PLAYERS][GHOST_MAX];
static int g_ghost_n[LAO_PLAYERS];
static int ghost_heap(uint64_t p,uint64_t size){return p>=HEAP_BASE&&p+size<HEAP_BASE+HEAP_MAX;}
static void puppet_ghost(Cpu *c,const char *level){
    unsigned slot=g_puppet_slot;
    if(!laonline_v12_ghost_level(level)||!g_ga||!g_rp.obj||!weak_alive(g_rp.ctrl)||
       g_rp.phase!=2||LD64(g_rp.obj)!=GA(SHADOW_LINK_VTABLE))return;
    if(g_ghost_obj[slot]!=g_rp.obj){
        g_ghost_obj[slot]=g_rp.obj;g_ghost_n[slot]=0;
        uint64_t known[3],kvt[3];int nk=0,nv=0;
        for(int k=0;k<3;k++){uint64_t comp=LD64(g_rp.obj+(k==0?0x1630u:k==1?0x1690u:0x16a0u));
            if(ghost_heap(comp,0x300)&&LD64(comp)>=MAIN_BASE&&LD64(comp)<SDK_BASE){uint64_t fn=LD64(LD64(comp)+0xa8);kvt[nv++]=LD64(comp);
                if(fn>=MAIN_BASE&&fn<SDK_BASE){int dup=0;for(int j=0;j<nk;j++)dup|=known[j]==fn;if(!dup)known[nk++]=fn;}}}
        /* If the model (+0x1640) or animation (+0x16e0) component shares one of those virtuals, it is a generic
         * component method, not a collider one: then only components of exactly those collider classes count. */
        int generic=0;
        for(int k=0;k<2;k++){uint64_t comp=LD64(g_rp.obj+(k?0x16e0u:0x1640u));
            if(ghost_heap(comp,0x300)&&LD64(comp)>=MAIN_BASE&&LD64(comp)<SDK_BASE)for(int j=0;j<nk;j++)generic|=LD64(LD64(comp)+0xa8)==known[j];}
        uint64_t model=LD64(g_rp.obj+0x1640u),anim=LD64(g_rp.obj+0x16e0u);
        uint64_t mgr=LD64(GA(GAME_MGR)),local=mgr?LD64(mgr+0x4b8):0;
        for(uint32_t off=0x1000;off<0x1800&&g_ghost_n[slot]<GHOST_MAX;off+=8){
            uint64_t comp=LD64(g_rp.obj+off);
            if(!ghost_heap(comp,0x300)||comp==g_rp.obj||comp==model||comp==anim||off==0x1640u||off==0x16e0u)continue;
            uint64_t vt=LD64(comp);if(vt<MAIN_BASE||vt>=SDK_BASE)continue;
            uint64_t fn=LD64(vt+0xa8);int match=0;
            if(generic){for(int j=0;j<nv;j++)match|=kvt[j]==vt;}else for(int j=0;j<nk;j++)match|=known[j]==fn;
            if(!match)continue;
            int dup=0;for(int j=0;j<g_ghost_n[slot];j++)dup|=g_ghost_comp[slot][j]==comp;if(dup)continue;
            int shared=0;   /* never touch a component the local Link also points at */
            if(local&&ghost_heap(local,0x3000))for(uint32_t lo=0;lo<0x3000&&!shared;lo+=8)shared=LD64(local+lo)==comp;
            if(shared)continue;
            g_ghost_comp[slot][g_ghost_n[slot]]=comp;g_ghost_fn[slot][g_ghost_n[slot]++]=fn;
            LOG("[laonline] V12 ghost: peer %u collider +%#x vtable main+%llx enable main+%llx\n",g_rp.st.id,off,
                (unsigned long long)(vt-MAIN_BASE),(unsigned long long)(fn-MAIN_BASE));
        }
        LOG("[laonline] V12 ghost: peer %u in %s - %d colliders kept off, still visible (match by %s; v12_ghost_puppets)\n",g_rp.st.id,level,g_ghost_n[slot],generic?"class":"virtual");
        laonline_diag("puppet.ghost",g_rp.map_id,g_rp.room_id,g_rp.obj,g_rp.st.id,g_ghost_n[slot]);
    }
    Cpu saved=*c;
    for(int j=0;j<g_ghost_n[slot];j++){
        uint64_t comp=g_ghost_comp[slot][j];
        if(LD64(LD64(comp)+0xa8)!=g_ghost_fn[slot][j])continue;  /* freed / reused: leave it alone */
        c->x[0]=comp;c->x[1]=0;guest_call(c,g_ghost_fn[slot][j]);
    }
    *c=saved;
}

/* V5: his own update runs while he initialises and while he plays his Pop (rising out of the shadow), then is
 * skipped for good: frozen in the Pop state, body shown, attack components off (Pop's own entry turns them off). */
#define SL_SM 0xff0u            /* state machine; current state byte at +0xd, 0xff = none yet */
#define SL_STATE_POP 6          /* states at sm+0x40+0x30*i: #6 enter main+0xd117c0 (Pop), update main+0xd11840 */
#define SL_STATE_CHANGE 0xd0a3f0u
int laonline_native_shadow_update(Cpu *c) {
    for(unsigned id=0;id<LAO_PLAYERS;id++) {
        LaoPuppet *p=&g_puppets[id];
        if(c->x[0] && c->x[0]==p->obj && weak_alive(p->ctrl)) {
            if(p->updates++==0)LOG("[laonline] puppet update peer=%u phase=%d\n",id,p->phase);
            return p->phase>=2;
        }
    }
    return 0; // Every real enemy and the local Link remain native.

}

/* V12b: scoped native resource redirect, implemented separately. */
extern void laonline_look_begin(int enabled, const char *level);
extern void laonline_look_end(void);
static void lao_look_swap(int on, const char *level) {
    if (on) laonline_look_begin(g_look_link, level); else laonline_look_end();
}
static void puppet_drop(Cpu *c, const char *why) {
    lao_look_swap(0, NULL);
    if (!g_rp.ctrl) return;
    if (weak_alive(g_rp.ctrl) && g_rp.obj) {
        c->x[0] = g_rp.obj;
        guest_call(c, GA(ACTOR_KILL));
    }
    c->x[0] = g_rp.ctrl;
    guest_call(c, GA(WEAK_RELEASE));
    laonline_diag("puppet.remove",g_rp.map_id,g_rp.room_id,g_rp.st.id,0,1);
    laonline_remote_draw_reset((int)g_puppet_slot);
    LOG("[laonline] remote Link removed (%s) peer=%u actor=%llx owner=%llx frame=%llu\n", why,g_rp.st.id,(unsigned long long)g_rp.obj,(unsigned long long)g_rp.owner_room,(unsigned long long)g_rp.at);
    laonline_diag(why,g_rp.map_id,g_rp.room_id,g_rp.st.id,0,0);
    g_rp.obj = g_rp.ctrl = 0;
}

static void puppet_spawn(Cpu *c, const char *level, uint64_t target, uint64_t target_control) {
    if (!native_adapter_ready()) return;
    if (!g_spawn_args) g_spawn_args = (uint64_t)(uintptr_t)hle_alloc(0x400);
    uint64_t s = g_spawn_args;
    memset((void *)(uintptr_t)s, 0, 0x400);
    uint64_t info = s + 0x40;
    ST64(s + 0x10, target); ST64(s + 0x18, target_control);
    /* Native actor registry keys are identities, not participant IDs. Never reuse
     * an unloaded incarnation's key: the factory consults its global registry. */
    uint64_t incarnation=0x4c41000000000000ull | ((uint64_t)(g_rp.st.id&0xffu)<<40) | (++g_spawn_serial & 0xffffffffffull);
    ST64(info,incarnation);
    ST16(info + 8, (uint16_t)g_cfg.type);
    ST32(info + 0xc, 0);
    float *tp = (float *)(uintptr_t)(info + 0x180);
    tp[0] = g_rp.st.x, tp[1] = g_rp.st.y, tp[2] = g_rp.st.z;
    tp[7] = 1.0f;  // (rotation: identity quaternion)
    tp[8] = tp[9] = tp[10] = 1.0f;
    c->x[0] = s, c->x[1] = s + 0x10, c->x[2] = info;
    lao_look_swap(1, level);
    guest_call(c, GA(CREATE_ACTOR));
    lao_look_swap(0, NULL);  /* V12b: restore routing even when native creation fails. */
    uint64_t obj = LD64(s), ictrl = LD64(s + 8);
    if (!obj || !ictrl) lao_look_swap(0, NULL);
    if (obj && ictrl) {
        __atomic_fetch_add((int64_t *)(uintptr_t)(ictrl + 0x10), 1, __ATOMIC_ACQ_REL);  // our weak ref
        uint64_t mgr = LD64(GA(GAME_MGR)), local = mgr ? LD64(mgr + 0x4b8) : 0;
        if (obj == local || LD64(obj) != GA(SHADOW_LINK_VTABLE)) {
            LOG("[laonline] rejected actor alias/type from native factory\n");
            if (obj != local) { c->x[0] = obj; guest_call(c, GA(ACTOR_KILL)); }
            c->x[0] = ictrl; guest_call(c, GA(WEAK_RELEASE));
            c->x[0] = ictrl; guest_call(c, GA(SP_RELEASE));
            return;
        }
        g_rp.obj = obj, g_rp.ctrl = ictrl;
        puppet_collision_off(c);
        if (g_look_link) laonline_look_diag(c, g_rp.obj);
        g_rp.owner_room = target; g_rp.owner_control = target_control;
        LOG("[laonline] A1 spawned peer %u room %llx control %llx (remote room id %04x)\n",
            g_rp.st.id, (unsigned long long)target, (unsigned long long)target_control, g_rp_room);
        g_rp.arrival_at = plat_now_ns() / 1000000ull;
        g_rp.anim_id = 0x14c; g_rp.anim_flags = 0;
        /* Pop's visual entry shows the model, disables both body attack
         * components and sword reactions, and starts 0x14c. No AI tick/event. */
        g_rp.phase = 0, g_rp.phase_at = g_rp.arrival_at, g_rp.updates = 0;  /* V5: Pop runs once he is set up */
        c->x[0] = ictrl;
        guest_call(c, GA(SP_RELEASE));
        if(!weak_alive(g_rp.ctrl)) {
            c->x[0]=g_rp.ctrl;guest_call(c,GA(WEAK_RELEASE));g_rp.obj=g_rp.ctrl=0;
            LOG("[laonline] V16 factory incarnation expired; waiting for a live room\n");return;
        }
        laonline_diag("puppet.spawn",g_rp.map_id,g_rp.room_id,incarnation,0,1);
        laonline_remote_draw_reset((int)g_puppet_slot);
        LOG("[laonline] V16 puppet incarnation=%llx room=%llx\n",(unsigned long long)incarnation,(unsigned long long)target);
        g_rp.px = g_rp.st.x, g_rp.py = g_rp.st.y, g_rp.pz = g_rp.st.z;
        snprintf(g_rp.level, sizeof g_rp.level, "%s", level);
        LOG("[laonline] remote Link '%s' spawned: actor %llx type %#x vtable main+%llx at %.2f %.2f %.2f\n",
            g_rp.st.name, (unsigned long long)obj, g_cfg.type, (unsigned long long)(LD64(obj) - 0x7000000000ull),
            g_rp.st.x, g_rp.st.y, g_rp.st.z);
    } else {
        LOG("[laonline] spawning the remote Link (type %#x) failed\n", g_cfg.type);
    }
}

static void puppet_tick(Cpu *c, uint64_t frame, const char *level, const char *room) {
    if (!native_adapter_ready()) {
        static int reported;
        if (!reported && g_rp.valid) {
            LOG("[laonline] peer received; rendering blocked: native adapter fingerprint mismatch\n");
            reported = 1;
        }
        if (g_rp.ctrl) puppet_drop(c, "native adapter unavailable");
        return;
    }
    bool fresh = g_rp.valid && (g_cfg.mode==M_SERVER || frame - g_rp.at < 180);  // (3 s)
    /* V6: the same level is enough (anywhere on the Field, any room of a dungeon): he is drawn wherever he is,
     * as long as the area is loaded around us. LAONLINE_SAME_ROOM=1: the old same-room rule. */
    bool same_room_rule = ENV("LAONLINE_SAME_ROOM") != NULL;
    bool same_level = fresh && level[0] &&
        (g_rp.st.level[0] ? (!strcmp(g_rp.st.level, level) && (!same_room_rule || !strcmp(g_rp.st.room, room))) :
        (g_rp_map != LAO_UNKNOWN && g_rp_map == lao_name_id(level) &&
         (!same_room_rule || (room[0] && g_rp_room != LAO_UNKNOWN && g_rp_room == lao_room_id(level, room)))));
    if (g_rp.ctrl && !weak_alive(g_rp.ctrl)) {  // the game removed it (level / room unload): forget it
        c->x[0] = g_rp.ctrl;
        guest_call(c, GA(WEAK_RELEASE));
        g_rp.obj = g_rp.ctrl = 0;
        g_rp.last_try = 0;
        LOG("[laonline] A1 unloaded: retry on next ready tick\n");
    }
    if(g_cfg.mode==M_SERVER && g_rp.unready_since)return; // Hold only; never spawn/update from stale samples.
    if(laonline_v12_hide_puppet(level)){if(g_rp.ctrl)puppet_drop(c,"V12 final boss: remote Link hidden");return;}
    if (g_rp.ctrl && (!same_level || strcmp(g_rp.level, level))) puppet_drop(c, !fresh ? "no data" : "other area");
    uint64_t target = 0, target_control = 0;
    bool resolved=false;
    if(same_level && laonline_level_ready())resolved=remote_room_pair(c,level,&target,&target_control);
    int reason = !same_level ? 1 : !laonline_level_ready() ? 2 : (!g_rp.ctrl && !resolved) ? 3 : 0;
    if (reason) {
        g_rp.candidate_room = g_rp.candidate_control = 0;
        if (reason != g_rp.wait_reason) LOG("[laonline] A1 waiting: %s\n",
            reason == 1 ? "peer unavailable/other level" : reason == 2 ? "level transition/settle" : "remote room not loaded or ambiguous");
        g_rp.wait_reason = reason;
        /* 0x0B: kept while the remote room resolves; he goes only if his own room unloads */
        return;  // never touch a puppet while the game's level is changing
    }
    uint64_t resolved_room=target,resolved_control=target_control;
    /* Existing live puppets follow network positions even beyond the loaded room list. */
    if (g_rp.ctrl && (!resolved || g_puppet_owner_loaded)) target = g_rp.owner_room, target_control = g_rp.owner_control;
    if (target != g_rp.candidate_room || target_control != g_rp.candidate_control) {
        g_rp.candidate_room = target; g_rp.candidate_control = target_control;
        g_rp.stable_since = frame; g_rp.last_try = 0;
        LOG("[laonline] A1 remote room resolved %llx; waiting 6 stable ticks\n", (unsigned long long)target);
    }
    if (frame - g_rp.stable_since < 6) return;
    if (g_rp.ctrl && (g_rp.owner_room != target || g_rp.owner_control != target_control)) {
        /* An actor belongs to its spawn room's culling/lifetime. A newly loaded
         * owner is adopted by a quiet incarnation; never rewrite shared_ptrs. */
        LOG("[laonline] V16 quiet room handoff peer %u %llx -> %llx\n",g_rp.st.id,
            (unsigned long long)g_rp.owner_room,(unsigned long long)target);
        puppet_drop(c,"V16 loaded room ownership changed");
    }
    // Recover an incarnation that remains alive but stops submitting any draw while
    // demonstrably near/on-screen. Use the verified factory lifecycle, no render flags.
    uint64_t recovery_now=plat_now_ns()/1000000ull;
    if(g_rp.ctrl && g_rp.phase==2 && resolved && recovery_now>=g_rp.recovery_after &&
       laonline_remote_draw_stalled((int)g_puppet_slot,recovery_now)){
        laonline_diag("puppet.recover",g_rp.map_id,g_rp.room_id,g_rp.obj,g_rp.st.id,1);
        puppet_drop(c,"M7 nearby native draw stalled; quiet recovery");
        target=resolved_room;target_control=resolved_control;
        g_rp.candidate_room=target;g_rp.candidate_control=target_control;g_rp.stable_since=frame-6;
        g_rp.recovery_after=recovery_now+15000;g_rp.last_try=0;
    }
    if (g_rp.wait_reason) LOG("[laonline] A1 ready: level settled, remote room loaded\n");
    g_rp.wait_reason = 0;
    if (!g_rp.ctrl) {
        if (g_rp.last_try && frame - g_rp.last_try < 6) return;
        g_rp.last_try = frame;
        puppet_spawn(c, level, target, target_control);
        if (!g_rp.ctrl) return;
    }
    {   /* V7: he is set up by his own code, then popped up once (his Pop entry: body shown, rising animation,
         * attack and body-hit components off) and his own update is skipped from then on. If anything hides
         * him again (+0x1700 = 0: his "shown" mark, set by LAONLINE_SL_SHOW), he is shown again, hits off. */
        uint64_t now = plat_now_ns() / 1000000ull;
        uint8_t st = LD8(g_rp.obj + SL_SM + 0xd);
        if (g_rp.phase == 0 && st != 0xff && now - g_rp.phase_at >= 300) {
            Cpu caller = *c;
            bool joining = g_rp.popped_id != g_rp.st.id;
            if (joining) {  /* he just joined: his rising-out-of-the-shadow entrance */
                c->x[0] = g_rp.obj; guest_call(c, GA(LAONLINE_SL_POP));
                g_rp.popped_id = g_rp.st.id;
            } else {                              /* a door / room reload: just there, no entrance */
                c->x[0] = g_rp.obj; c->x[1] = 1; guest_call(c, GA(LAONLINE_SL_SHOW));
                for (int k = 0; k < 2; k++) {
                    uint64_t comp = LD64(g_rp.obj + (k ? 0x1690u : 0x16a0u));
                    if (comp) { c->x[0] = comp; c->x[1] = 0; guest_call(c, LD64(LD64(comp) + 0xa8)); }
                }
                g_rp.arrival_at = now - 1000;
            }
            *c = caller;
            int quiet = g_rp.arrival_at + 1000 <= now;
            g_rp.phase = 2, g_rp.phase_at = now;
            if (g_look_link) laonline_look_diag(c, g_rp.obj);
            if (!quiet) g_rp.arrival_at = now + 600;  /* (rise, then network animations) */
            /* V8: his brain off on every PC, with or without the update hook: each of his 15 states' "update"
             * (state machine at +0xff0, state i at +0x30*i: update member function at +0x58/+0x60, used when
             * bit i of +0x560 is clear - main+0xd0b040) now points at an empty function (LAONLINE_SL_STATE_NOP: ret).
             * Only this actor's own table is changed; no state can then decide to chase or attack. */
            {
                uint64_t sm = g_rp.obj + SL_SM;
                int n = 0;
                for (int i = 0; i < 15; i++) {
                    if (LD8(sm + 0x560 + (i >> 3)) & (1u << (i & 7))) continue;
                    if (!LD64(sm + 0x30 * i + 0x58)) continue;
                    ST64(sm + 0x30 * i + 0x58, GA(LAONLINE_SL_STATE_NOP)), ST64(sm + 0x30 * i + 0x60, 0), n++;
                }
                LOG("[laonline] v8: %d of his states made empty (no chasing, no attacks)\n", n);
            }
            lao_look_swap(0, NULL);
            LOG("[laonline] A1 puppet initialized peer %u (state %u, %u updates): %s, AI off\n",
                g_rp.st.id, st, g_rp.updates, joining ? "join entrance" : "quiet room respawn");
        } else if (g_rp.phase == 0 && g_rp.phase_at && now - g_rp.phase_at > 3000) {
            LOG("[laonline] v7: puppet not set up after 3 s (state %u, %u updates)\n", st, g_rp.updates);
            puppet_drop(c, "A1 initialization timeout; retry");
            g_rp.last_try = 0;
            return;
        } else if (g_rp.phase == 2 && !LD8(g_rp.obj + 0x1700) && frame % 30 == 0) {
            Cpu caller = *c;
            c->x[0] = g_rp.obj; c->x[1] = 1; guest_call(c, GA(LAONLINE_SL_SHOW));  /* show */
            for (int k = 0; k < 2; k++) {  /* attack / body-hit components off, as his Pop entry does */
                uint64_t comp = LD64(g_rp.obj + (k ? 0x1690u : 0x16a0u));
                if (comp) { c->x[0] = comp; c->x[1] = 0; guest_call(c, LD64(LD64(comp) + 0xa8)); }
            }
            *c = caller;
            LOG("[laonline] v7: he was hidden again (state %u): shown\n", st);
        }
        if (frame % 120 == 0)
            LOG("[laonline] v7: phase %d state %u shown %u updates %u\n", g_rp.phase, st, LD8(g_rp.obj + 0x1700), g_rp.updates);
    }
    /* Pop/quiet show can enable movement collision again. Keep only this
     * registered remote incarnation non-colliding after every visual change. */
    puppet_collision_off(c);
    puppet_ghost(c,level);  /* V12: every collider off, still visible (Wind Fish's Egg by default) */
    // smooth toward the latest network position; jump if it is far (teleport / door)
    float dx = g_rp.st.x - g_rp.px, dy = g_rp.st.y - g_rp.py, dz = g_rp.st.z - g_rp.pz;
    if (dx * dx + dy * dy + dz * dz > 25.0f) g_rp.px = g_rp.st.x, g_rp.py = g_rp.st.y, g_rp.pz = g_rp.st.z;
    else g_rp.px += dx * 0.35f, g_rp.py += dy * 0.35f, g_rp.pz += dz * 0.35f;
    uint64_t tr = LD64(g_rp.obj + 0x80);
    if (tr >= HEAP_BASE && tr < HEAP_BASE + HEAP_MAX) {
        if (!g_transform_args) g_transform_args = (uint64_t)(uintptr_t)hle_alloc(0x40);
        uint64_t transform_args = g_transform_args;
        float *pos = (float *)(uintptr_t)transform_args;
        pos[0] = g_rp.px; pos[1] = g_rp.py; pos[2] = g_rp.pz; pos[3] = 0;
        lao_yaw_quaternion(g_rp.st.yaw, pos + 4);
        Cpu caller = *c;
        c->x[0] = tr; c->x[1] = transform_args; guest_call(c, g_position_set);
        c->x[0] = tr; c->x[1] = transform_args + 16; guest_call(c, g_rotation_set);
        /* Arrival is bounded and runs once per actor. Afterwards only native
         * animation requests received from this participant drive playback. */
        uint64_t clock = plat_now_ns() / 1000000ull;
        uint16_t id = (g_rp.wire.flags & LAO_ANIMATION_VALID) ? g_rp.wire.anim_id : 0x67;
        if (g_rp.phase == 2 && clock >= g_rp.arrival_at && clock - g_rp.arrival_at >= 750 &&
            lao_animation_changed(g_rp.anim_id, g_rp.anim_flags, id, g_rp.wire.flags)) {
            uint64_t animation = LD64(g_rp.obj + LAO_REMOTE_ANIMATION_OFF);
            if (animation) {
                c->x[0] = animation; c->x[1] = id; c->x[2] = 0; c->v[0] = 0;
                guest_call(c, GA(LAONLINE_ANIMATION_SITE));
                if (c->x[0]) { g_rp.anim_id = id; g_rp.anim_flags = g_rp.wire.flags; }
            }
        }
        *c = caller;
    }
    /* Appearance, motion and actions belong to the verified native adapter.
     * Never force enemy state bytes or restore the local player's health. */
    if (frame % 120 == 0) {
        uint64_t tr2 = LD64(g_rp.obj + 0x80);
        const float *q = (tr2 >= HEAP_BASE && tr2 < HEAP_BASE + HEAP_MAX) ? (const float *)(uintptr_t)(tr2 + 0x80) : NULL;
        LOG("[laonline] puppet actual %.2f %.2f %.2f (wanted %.2f %.2f %.2f)\n", q ? q[0] : 0.f, q ? q[1] : 0.f,
            q ? q[2] : 0.f, g_rp.px, g_rp.py, g_rp.pz);
    }
    if (frame % 120 == 0)
        LOG("[laonline] f%llu remote '%s' %s/%s net %.2f %.2f %.2f drawn %.2f %.2f %.2f actor %llx\n",
            (unsigned long long)frame, g_rp.st.name, g_rp.st.level, g_rp.st.room, g_rp.st.x, g_rp.st.y, g_rp.st.z,
            g_rp.px, g_rp.py, g_rp.pz, (unsigned long long)g_rp.obj);
}

/* ---- V10: on-screen info for the overlay (runtime/ui/laonline_overlay.mm): join/leave toasts and the player list */
static pthread_mutex_t g_ov_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { char text[160]; uint64_t at_ms; int item; } g_toast[32];
static int g_toast_count;
static struct { char name[17], location[33]; uint8_t rgb[3], health, max_health; } g_ov_players[LAO_PLAYERS];
static struct { char name[17]; float pos[3]; int active; uint8_t rgb[3], health, max_health; } g_ov_tags[LAO_PLAYERS];
static int g_ov_nplayers, g_ov_connected;
/* Visual-only map player snapshots: no gameplay writes or actor references. */
static LaoMapOverlayPlayer g_ov_map[LAO_PLAYERS];
static int g_ov_nmap;
static void ov_toast_item(const char *fmt, const char *name,int item) {
    pthread_mutex_lock(&g_ov_lock);
    if(g_toast_count<32) {
        snprintf(g_toast[g_toast_count].text,sizeof g_toast[0].text,fmt,name);
        g_toast[g_toast_count].item=item;
        g_toast[g_toast_count++].at_ms=0;
    } else LOG("[laonline] notification queue full\n");
    pthread_mutex_unlock(&g_ov_lock);
    LOG("[laonline] UI event queued: "); LOG(fmt,name);LOG("\n");
}
static void ov_toast(const char *fmt,const char *name){ov_toast_item(fmt,name,-1);}
void laonline_notify(const char *text) {ov_toast("%s",text);}
void laonline_notify_item(const char *text,int item){ov_toast_item("%s",text,item);}
int laonline_toast_with_item(int i,char *out,int cap,uint64_t *age_ms,int *item) {
    if(i<0||i>=2||!out||cap<1||!age_ms)return 0;
    pthread_mutex_lock(&g_ov_lock);
    uint64_t now=plat_now_ns()/1000000ull;
    while(g_toast_count && g_toast[0].at_ms && now-g_toast[0].at_ms>=5000) {
        LOG("[laonline] UI notification expired: %s\n",g_toast[0].text);
        memmove(g_toast,g_toast+1,sizeof g_toast[0]*(--g_toast_count));
    }
    int ok=i<g_toast_count;
    if(ok) {
        if(!g_toast[i].at_ms)g_toast[i].at_ms=now?now:1;
        *age_ms=now>=g_toast[i].at_ms?now-g_toast[i].at_ms:0;
        snprintf(out,(size_t)cap,"%s",g_toast[i].text);if(item)*item=g_toast[i].item;
    }
    pthread_mutex_unlock(&g_ov_lock);return ok;
}
int laonline_toast(int i,char *out,int cap,uint64_t *age){return laonline_toast_with_item(i,out,cap,age,NULL);}
void laonline_report_player(char *out,int cap){if(out && cap>0)snprintf(out,(size_t)cap,"%.16s",g_cfg.name);}
/* For the overlay: the other players (name, 1 = in our area, tunic colour); returns the count, -1 = not online */
int laonline_players(int i, char *name, int cap, char *location, int location_cap, uint8_t rgb[3]) {
    pthread_mutex_lock(&g_ov_lock);
    int n = g_ov_connected ? g_ov_nplayers : -1;
    if (i >= 0 && i < g_ov_nplayers && name) {
        snprintf(name, (size_t)cap, "%s", g_ov_players[i].name);
        snprintf(location, (size_t)location_cap, "%s", g_ov_players[i].location[0] ? g_ov_players[i].location : "Loading");
        memcpy(rgb, g_ov_players[i].rgb, 3);
    }
    pthread_mutex_unlock(&g_ov_lock);
    return n;
}

/* The map overlay uses only copies of the server's verified room identifiers. */
int laonline_map_player(int i, LaoMapOverlayPlayer *out) {
    pthread_mutex_lock(&g_ov_lock);
    int n = g_ov_connected ? g_ov_nmap : -1;
    if (out && i >= 0 && i < n) *out = g_ov_map[i];
    pthread_mutex_unlock(&g_ov_lock);
    return n;
}

/* The only actor tagged is the active remote puppet. The local Link is never
 * exposed to the overlay. Position is copied under the overlay lock. */
int laonline_remote_tag_at(int id,char *name, int cap, float pos[3], uint8_t rgb[3], int *health, int *max_health) {
    int ok = 0;
    pthread_mutex_lock(&g_ov_lock);
    if (g_ov_connected && id >= 0 && id < LAO_PLAYERS && g_ov_tags[id].active) {
        snprintf(name, (size_t)cap, "%s", g_ov_tags[id].name);
        memcpy(pos, g_ov_tags[id].pos, sizeof g_ov_tags[id].pos);
        memcpy(rgb, g_ov_tags[id].rgb, 3);
        *health = g_ov_tags[id].health; *max_health = g_ov_tags[id].max_health;
        ok = 1;
    }
    pthread_mutex_unlock(&g_ov_lock);
    return ok;
}

int laonline_remote_tag(char *name,int cap,float pos[3],uint8_t rgb[3],int *hp,int *max_hp) {
    for(int id=0;id<LAO_PLAYERS;id++)if(laonline_remote_tag_at(id,name,cap,pos,rgb,hp,max_hp))return 1;
    return 0;
}

static void accept_state(const LaoState *s, uint64_t frame) {
    if (s->magic != LAO_MAGIC || s->version != LAO_VERSION || s->id == g_my_id) return;
    if (g_rp.valid && s->id == g_rp.st.id && (int32_t)(s->seq - g_rp.st.seq) <= 0) return;  // (old / repeated)
    if (!g_rp.valid || s->id != g_rp.st.id) LOG("[laonline] player '%.16s' connected\n", s->name);
    g_rp.st = *s;
    g_rp.st.name[16] = 0, g_rp.st.level[30] = 0, g_rp.st.room[31] = 0;
    g_rp.at = frame, g_rp.valid = 1;
}


// ---------------------------------------------------------------- Docker server (LA-Online protocol v1, same as the Switch mod)
// TCP to the user's existing server (server.py): frame = u16 len + u8 type + payload, little-endian.
// Own thread for connect / receive; the game thread only swaps in the latest state under a lock.
#include "laonline_coop_wire.h"
extern void laonline_coop_transport(int);
extern void laonline_coop_peer_left(int);
extern void laonline_coop_message(const uint8_t *,size_t,const char *);
extern int laonline_coop_packet(uint8_t *,int);
extern void laonline_coop_initialize(int,const char *);
extern void laonline_coop_context(uint16_t,uint16_t,const char *);
#define LAO_V1_LOCATION_SUPPORTED 0 /* Existing Docker relay v1 rejects C_LOCATION (0x0A). */
enum { C_HELLO = 0x01, C_STATE = 0x02, C_VITALS = 0x09, C_LOCATION = 0x0A,
       S_WELCOME = 0x81, S_PLAYER_JOIN = 0x82, S_PLAYER_LEAVE = 0x83, S_STATE = 0x84,
       S_PROFILE = 0x8A, S_VITALS = 0x8B, S_LOCATION = 0x8C, S_ERROR = 0x8F };
typedef LaoWireState WireState;
_Static_assert(sizeof(WireState) == 27, "protocol v1 STATE size");
static pthread_mutex_t g_srv_lock = PTHREAD_MUTEX_INITIALIZER;
static WireState g_srv_out;
static int g_srv_out_ready;
static uint64_t g_srv_out_ms;
static uint8_t g_srv_health, g_srv_max_health;
static char g_srv_location[33];
static LaoSession g_srv_session;
static LaoAppearance g_srv_appearance;
static int g_srv_appearance_cap;
static LaoMapAnchor g_anchor_out,g_anchor_peers[LAO_PLAYERS];
static float g_anchor_history[LAO_PLAYERS][3],g_anchor_last_field[3];
static int g_anchor_known[LAO_PLAYERS],g_anchor_have_field,g_anchor_cap;
static atomic_int g_srv_status;
static atomic_int g_retry_connection;
int laonline_connection_status(void) {
    if(!atomic_load(&g_launch_online))return 0;
    int status=atomic_load(&g_srv_status);
    return status==1?2:status==2?3:status==3?4:1;
}
void laonline_connection_retry(void) {atomic_store(&g_retry_connection,1);}

static uint64_t now_ms(void) { return plat_now_ns() / 1000000ull; }
static void srv_clear(void) {
    laonline_coop_transport(0);
    laonline_chat_capability(0);
    char left[LAO_PLAYERS][17] = {{0}};
    pthread_mutex_lock(&g_srv_lock);
    for (int i = 0; i < LAO_PLAYERS; ++i)
        if (g_srv_session.peers[i].joined) snprintf(left[i], sizeof left[i], "%s", g_srv_session.peers[i].name);
    lao_session_reset(&g_srv_session);
    g_srv_out_ready = 0;
    memset(g_anchor_peers,0,sizeof g_anchor_peers);
    memset(g_anchor_known,0,sizeof g_anchor_known);g_anchor_cap=0;
    g_srv_appearance_cap = 0;
    g_srv_health = g_srv_max_health = 0;
    pthread_mutex_unlock(&g_srv_lock);
    for (int i = 0; i < LAO_PLAYERS; ++i) if (left[i][0]) ov_toast("%s left", left[i]);
}

static int send_all(sock_t s, const uint8_t *p, int n) {
    while (n > 0) { if(!atomic_load(&g_launch_online))return 0; int r = (int)send(s, (const char *)p, n, 0); if (r <= 0) return 0; p += r, n -= r; }
    return 1;
}
static char g_srv_refused[200];  /* V14.1: the server's reason for refusing us (launcher shows it) */
const char *laonline_server_refusal(void) { return g_srv_refused; }
static int put_str(uint8_t *b, const char *s) { size_t n = strlen(s); if (n > 255) n = 255; b[0] = (uint8_t)n; memcpy(b + 1, s, n); return (int)n + 1; }
static int get_str(const uint8_t *b, int len, int *o, char *out, size_t cap) {
    if (*o >= len) return 0;
    int n = b[(*o)++];
    if (*o + n > len) return 0;
    size_t k = (size_t)n < cap - 1 ? (size_t)n : cap - 1;
    memcpy(out, b + *o, k), out[k] = 0, *o += n;
    return 1;
}

static void srv_handle(const uint8_t *b, int len) {
    if (len <= 0) return;
    // (LAS Recomp Co-Op's port: a message about one of this PC's other local players - on the relay as players of their
    // own, laonline_local.c - is not about a remote player: no puppet, no name tag, no "joined")
    extern int laonline_local_peer_message(const uint8_t *, int);
    if (laonline_local_peer_message(b, len)) return;
    laonline_v12_message(b,(size_t)len);  /* V12 (before the Global filter: PvP frames are Global's) */
    if(atomic_load(&g_launch_online)==3 && b[0]!=0x81 && b[0]!=0x82 && b[0]!=0x83 && b[0]!=0x84 && b[0]!=0x89 && b[0]!=0x8a && b[0]!=0x8b && b[0]!=0x8c && b[0]!=0x8d && b[0]!=0x8f && b[0]!=LAO_CHAT_RECEIVE && b[0]!=LAO_TYPING_RECEIVE)return;
    pthread_mutex_lock(&g_srv_lock);
    char event_name[17] = "";
    int was_joined=len>=2 && b[1]<LAO_PLAYERS && g_srv_session.peers[b[1]].joined;
    if (b[0] == S_PLAYER_LEAVE && len == 2 && b[1] < LAO_PLAYERS)
        snprintf(event_name, sizeof event_name, "%s", g_srv_session.peers[b[1]].name);
    if (b[0] == 0x89 && len == 5) {
        uint32_t reply; memcpy(&reply, b + 1, 4);
        if(reply==0x4c414f5eu){extern void laonline_coop_world_capability(void);laonline_coop_world_capability();}
        if(reply==LAO_STORY_REPLY)laonline_story_capability();
        if (reply == LAO_CHECK_REPLY) laonline_checks_capability();
        if(reply==LAO_DEATH_REPLY)laonline_death_capability();
        if (reply == LAO_CHAT_REPLY) laonline_chat_capability(1);
        if (reply == LAO_MAP_REPLY){g_anchor_cap=1;LOG("[laonline] V16 relay supports indoor map anchors\n");}
        if (reply == LAO_COOP_REPLY) laonline_coop_transport(1);
        if (reply == 0x4c414f64u) {extern void laonline_coop_advanced_capability(void);laonline_coop_advanced_capability();}
        if (reply == 0x4c414f60u) {extern void laonline_coop_equipment_capability(void);laonline_coop_equipment_capability();}
        if (reply == 0x4c414f5cu) {extern void laonline_coop_rewards_capability(void);laonline_coop_rewards_capability();}
        if (reply == LAO_CAP_REPLY && !g_srv_appearance_cap) {
            g_srv_appearance_cap = 1;
            LOG("[laonline] V13 relay supports live appearance and location updates\n");
        }
    }
    if((b[0]==LAO_CHAT_RECEIVE || b[0]==LAO_TYPING_RECEIVE) && len>=2 && b[1]<LAO_PLAYERS) {
        int sender=b[1];const char *name=sender==g_srv_session.local_id?g_cfg.name:g_srv_session.peers[sender].joined?g_srv_session.peers[sender].name:NULL;
        if(name){laonline_chat_peer_name(sender,name);laonline_chat_receive(b,(size_t)len);}
    }
    if (b[0]==S_PLAYER_LEAVE && len==2) {laonline_chat_peer_left(b[1]);laonline_coop_peer_left(b[1]);}
    if(b[0]==LAO_MAP_RECEIVE && len==2+sizeof(LaoMapAnchor) && b[1]<LAO_PLAYERS && g_srv_session.peers[b[1]].joined) {
        LaoMapAnchor a;memcpy(&a,b+2,sizeof a);
        if(lao_anchor_valid(&a))g_anchor_peers[b[1]]=a;
    }
    if(b[0]>=LAO_COOP_READY && b[0]<=LAO_COOP_SNAPSHOT) {
        const char *name=(len>=2 && b[1]<LAO_PLAYERS && g_srv_session.peers[b[1]].joined)?g_srv_session.peers[b[1]].name:NULL;
        laonline_coop_message(b,(size_t)len,name);
    }
    if(b[0]==0xa0)laonline_death_message(b,(size_t)len);
    if(b[0]==S_PLAYER_LEAVE&&len==2)laonline_death_peer_left(b[1]);
    if(b[0]==0x98||b[0]==0x9b){extern void laonline_coop_world_message(const uint8_t *,size_t);laonline_coop_world_message(b,(size_t)len);}
    if(b[0]==LAO_STORY_RECEIVE)laonline_story_receive(b,(size_t)len);
    if(b[0]==LAO_CHECK_RECEIVE)laonline_checks_receive(b,(size_t)len);
    if(b[0]==0x97){extern void laonline_coop_rewards_message(const uint8_t *,size_t);laonline_coop_rewards_message(b,(size_t)len);}
    int accepted = lao_session_message(&g_srv_session, b, (size_t)len, now_ms());
    if(accepted && b[0]==S_STATE && len>=2 && b[1]<LAO_PLAYERS) {
        const LaoPeer *p=&g_srv_session.peers[b[1]];
        if(p->state.map==lao_name_id("Field")) {
            float pos[3]={p->state.x,p->state.y,p->state.z};
            LaoMapAnchor a={0};a.map=p->state.map;a.flags=1;memcpy(a.world,pos,12);
            if(lao_anchor_valid(&a)){memcpy(g_anchor_history[b[1]],pos,12);g_anchor_known[b[1]]=1;}
        }
    }
    if (accepted && b[0] == S_PLAYER_JOIN && len >= 2 && b[1] < LAO_PLAYERS) {
        snprintf(event_name, sizeof event_name, "%s", g_srv_session.peers[b[1]].name);
        memset(&g_anchor_peers[b[1]],0,sizeof g_anchor_peers[0]);g_anchor_known[b[1]]=0;
    }
    if (b[0] == LAO_APPEARANCE_REPLY && accepted)
        LOG("[laonline] V13 appearance received player #%u mask=%02x meshes=%05x\n", b[1],
            g_srv_session.peers[b[1]].appearance.mask, g_srv_session.peers[b[1]].appearance.meshes);
    if (b[0] == S_VITALS && accepted)
        LOG("[laonline] A2 received player #%u health %u/%u quarter-hearts\n", b[1], b[2], b[3]);
    if (b[0] == S_WELCOME && accepted) {
        g_srv_status = 1; g_srv_refused[0] = 0;
        laonline_chat_identity(g_srv_session.local_id);
        LOG("[laonline] joined lobby as #%d\n", g_srv_session.local_id);
    }
    char refused[200] = "";
    if (b[0] == S_ERROR && len >= 3 && 3 + b[2] <= len) {  /* V14.1: say WHY (e.g. an update is needed to play online) */
        int m = b[2] < (int)sizeof refused - 1 ? b[2] : (int)sizeof refused - 1;
        memcpy(refused, b + 3, (size_t)m); refused[m] = 0;
        snprintf(g_srv_refused, sizeof g_srv_refused, "%s", refused);
    }
    if (b[0] == S_ERROR || (b[0] == S_WELCOME && !accepted)) {
        lao_session_reset(&g_srv_session);
        g_srv_status = 2;
        LOG("[laonline] rejected or malformed server welcome%s%s\n", refused[0] ? ": " : "", refused);
    }
    pthread_mutex_unlock(&g_srv_lock);
    if (refused[0]) ov_toast("%s", refused);
    if (accepted && b[0] == S_WELCOME) ov_toast("%s joined the session",g_cfg.name);
    if (accepted && !was_joined && b[0] == S_PLAYER_JOIN && event_name[0]) ov_toast("%s joined", event_name);
    if (accepted && b[0] == S_PLAYER_LEAVE && event_name[0]) ov_toast("%s left", event_name);
}

/* Local and legacy remote labels use the same verified metadata. */
static void location_name(const char *level, const char *room, char out[33]) {
    lao_location_ids(lao_name_id(level), lao_room_id(level, room), out);
}

static int seed_metadata_request(const char *folder,char *id,size_t idcap,char *world,size_t worldcap,int *total,int *ready,char *display,size_t displaycap,char *error,size_t cap){
 extern int laonline_seed_identity_folder(const char *,unsigned char[32]);
 unsigned char digest[32]={0};char seed_id[96]={0};sock_t sock=BAD_SOCK;int ok=0;
 if(error&&cap)snprintf(error,cap,"Could not contact the updated LA-Online relay.");
 if(folder){char path[1200];snprintf(path,sizeof path,"%s/seed_id.txt",folder);FILE *f=fopen(path,"rb");if(!f)return 0;fgets(seed_id,sizeof seed_id,f);fclose(f);trim(seed_id);for(char *p=seed_id;*p;p++)if(*p>='a'&&*p<='z')*p-=32;if(!laonline_seed_identity_folder(folder,digest))return 0;}
 load_config();char lobby[33],base[27];snprintf(base,sizeof base,"%.26s",g_cfg.lobby);snprintf(lobby,sizeof lobby,"%s.rando",base);
#ifdef _WIN32
 WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))return 0;
#endif
 char host[96],port[16]="47620";snprintf(host,sizeof host,"%s",g_cfg.server);char *colon=strrchr(host,':');if(colon){*colon=0;snprintf(port,sizeof port,"%s",colon+1);}
 struct addrinfo hints={0},*address=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
 if(getaddrinfo(host,port,&hints,&address)||!address)goto done;
 sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(sock==BAD_SOCK){freeaddrinfo(address);goto done;}
#ifdef _WIN32
 u_long nonblocking=1;ioctlsocket(sock,FIONBIO,&nonblocking);
#else
 int saved_flags=fcntl(sock,F_GETFL,0);fcntl(sock,F_SETFL,saved_flags|O_NONBLOCK);
#endif
 int connected=connect(sock,address->ai_addr,(int)address->ai_addrlen);freeaddrinfo(address);
 if(connected){fd_set writefds;FD_ZERO(&writefds);FD_SET(sock,&writefds);struct timeval timeout={4,0};if(select((int)sock+1,NULL,&writefds,NULL,&timeout)<=0)goto done;int failure=0;socklen_t size=sizeof failure;if(getsockopt(sock,SOL_SOCKET,SO_ERROR,(char*)&failure,&size)||failure)goto done;}
#ifdef _WIN32
 nonblocking=0;ioctlsocket(sock,FIONBIO,&nonblocking);DWORD timeout_ms=5000;setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout_ms,sizeof timeout_ms);setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,(const char*)&timeout_ms,sizeof timeout_ms);
#else
 fcntl(sock,F_SETFL,saved_flags);struct timeval timeout={5,0};setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
#endif
 uint8_t packet[512];int n=3;packet[2]=0x15;packet[n++]=folder?(display?4:1):world?(display?3:2):0;n+=put_str(packet+n,g_cfg.name);n+=put_str(packet+n,lobby);n+=put_str(packet+n,g_cfg.password);if(folder){memcpy(packet+n,digest,32);n+=32;n+=put_str(packet+n,seed_id);if(display)n+=put_str(packet+n,display);}packet[0]=(uint8_t)(n-2);packet[1]=(uint8_t)((n-2)>>8);
 for(int at=0;at<n;){int sent=send(sock,(const char*)packet+at,n-at,0);if(sent<=0)goto done;at+=sent;}
 uint8_t header[2];for(int at=0;at<2;){int got=recv(sock,(char*)header+at,2-at,0);if(got<=0)goto done;at+=got;}
 n=header[0]|header[1]<<8;if(n<3||n>(int)sizeof packet)goto done;for(int at=0;at<n;){int got=recv(sock,(char*)packet+at,n-at,0);if(got<=0)goto done;at+=got;}
 if(world){
  if(n<37)goto done;int end=37+packet[34];
  if(packet[0]!=(display?0x9c:0x9a)||packet[1]!=1||n<37||packet[34]>95||n<end)goto done;
  if(display){if(n<=end||packet[end]>90||n!=end+1+packet[end])goto done;snprintf(display,displaycap,"%.*s",packet[end],packet+end+1);}else if(n!=end)goto done;
  if(worldcap<65)goto done;const char *hex="0123456789abcdef";for(int i=0;i<32;i++){world[i*2]=hex[packet[i+2]>>4];world[i*2+1]=hex[packet[i+2]&15];}world[64]=0;
  if(id&&idcap)snprintf(id,idcap,"%.*s",packet[34],packet+35);
  if(total)*total=packet[35+packet[34]];if(ready)*ready=packet[36+packet[34]];ok=1;if(error&&cap)error[0]=0;goto done;
 }
 if(packet[0]!=0x99||packet[1]!=1||packet[2]>95||n!=3+packet[2]){if(error&&cap)snprintf(error,cap,"Seed registration was refused. Check the lobby password, selected world and server update.");goto done;}
 if(id&&idcap)snprintf(id,idcap,"%.*s",packet[2],packet+3);ok=1;if(error&&cap)error[0]=0;
done:if(sock!=BAD_SOCK)sock_close(sock);
#ifdef _WIN32
 WSACleanup();
#endif
 return ok;
}

int laonline_seed_metadata(const char *folder,char *id,size_t idcap,char *error,size_t cap){return seed_metadata_request(folder,id,idcap,NULL,0,NULL,NULL,NULL,0,error,cap);}
int laonline_seed_descriptor(char *id,size_t idcap,char *world,size_t worldcap,int *total,int *ready,char *error,size_t cap){return seed_metadata_request(NULL,id,idcap,world,worldcap,total,ready,NULL,0,error,cap);}

int laonline_seed_metadata_named(const char *folder,const char *name,char *id,size_t idcap,char *error,size_t cap){char display[96];snprintf(display,sizeof display,"%s",name);return seed_metadata_request(folder,id,idcap,NULL,0,NULL,NULL,display,sizeof display,error,cap);}
int laonline_seed_descriptor_named(char *id,size_t idcap,char *world,size_t worldcap,int *total,int *ready,char *display,size_t displaycap,char *error,size_t cap){return seed_metadata_request(NULL,id,idcap,world,worldcap,total,ready,display,displaycap,error,cap);}
// LAS Recomp Co-Op's port: the relay login srv_thread uses - its address, name, lobby (the Randomizer's ".rando" one too)
// and password - for this PC's other local players' own connections (laonline_local.c). 0 unless online play is
// launched in relay mode (not Global).
int laonline_relay_login(char *host, size_t hn, char *port, size_t pn, char *name, char *lobby, char *password) {
    int launch = atomic_load(&g_launch_online);
    if (g_cfg.mode != M_SERVER || launch < 1 || launch > 2) return 0;
    snprintf(host, hn, "%s", g_cfg.server);
    snprintf(port, pn, "47620");
    char *colon = strrchr(host, ':');
    if (colon) *colon = 0, snprintf(port, pn, "%s", colon + 1);
    char login_lobby[33];
    room_login(name, login_lobby, password);
    if (launch == 2) snprintf(lobby, 33, "%.26s.rando", login_lobby);
    else snprintf(lobby, 33, "%s", login_lobby);
    return 1;
}

static void *srv_thread(void *arg) {
    (void)arg;
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    char host[96], port[16] = "47620";
    snprintf(host, sizeof host, "%s", g_cfg.server);
    char *colon = strrchr(host, ':');
    if (colon) *colon = 0, snprintf(port, sizeof port, "%s", colon + 1);
    for (;;) {
        while (!atomic_load(&g_launch_online)) sleep_ms(50);
        int launch_epoch=atomic_load(&g_launch_epoch);
        srv_clear();
        g_srv_status = 0;
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET, hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host, port, &hints, &res) != 0 || !res) {
            g_srv_status=3;
            LOG("[laonline] server: cannot resolve %s - retrying\n", host);
            sleep_ms(5000);
            continue;
        }
        sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(!atomic_load(&g_launch_online)){if(s!=BAD_SOCK)sock_close(s);freeaddrinfo(res);continue;}
        if (s == BAD_SOCK || connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
            g_srv_status=3;
            LOG("[laonline] server: cannot connect to %s:%s - retrying\n", host, port);
            if (s != BAD_SOCK) sock_close(s);
            freeaddrinfo(res);
            sleep_ms(5000);
            continue;
        }
        freeaddrinfo(res);
        if (!atomic_load(&g_launch_online)) { sock_close(s); continue; }
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
        char login_name[17],login_lobby[33],login_password[65];room_login(login_name,login_lobby,login_password);
        uint8_t buf[512];
        int n = 3;
        buf[2] = atomic_load(&g_launch_online)==3 ? 0x31 : C_HELLO;
        buf[n++] = 1, buf[n++] = 0;  // protocol version 1
        n += put_str(buf + n, login_name);
        uint32_t rgb = 0x2fa83c;
        col_target(&g_settings.tunic, COL_SLOT_TUNIC, &rgb);
        buf[n++] = (uint8_t)(rgb >> 16), buf[n++] = (uint8_t)(rgb >> 8), buf[n++] = (uint8_t)rgb;
        char session_lobby[33];if(atomic_load(&g_launch_online)==3)snprintf(session_lobby,sizeof session_lobby,"%s",laonline_v12_global_lobby());else if(atomic_load(&g_launch_online)==2)snprintf(session_lobby,sizeof session_lobby,"%.26s.rando",login_lobby);else snprintf(session_lobby,sizeof session_lobby,"%s",login_lobby);
        n += put_str(buf + n, session_lobby);
        n += put_str(buf + n, atomic_load(&g_launch_online)==3?"":login_password);
        /* V14.1: our LA-Online version after the HELLO fields (older relays ignore trailing bytes). A V14.1+ relay
         * only lets the same or a newer version play, so mixed versions can no longer meet online. */
        buf[n++] = 'L', buf[n++] = 'A', buf[n++] = 'O', buf[n++] = 'V';
        n += put_str(buf + n, LAO_MOD_VERSION);
        buf[0] = (uint8_t)(n - 2), buf[1] = (uint8_t)((n - 2) >> 8);
        if (!send_all(s, buf, n)) { sock_close(s); sleep_ms(3000); continue; }
        LOG("[laonline] server: connected to %s:%s as '%s' (lobby %s, tunic #%06x)\n", host, port, login_name, login_lobby, rgb);
        static uint8_t rb[70000];
        int have = 0;
        uint64_t ping_at = now_ms();
        uint8_t sent_hp = 0xff, sent_max = 0xff;
        int queried = 0, sent_appearance = 0, legacy_notice = 0;
        LaoAppearance last_appearance = {0};
        LaoMapAnchor last_anchor={0};int sent_anchor=0;
        uint8_t last_rgb[3] = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb};
        char sent_location[33] = "";
        int sent_unready = 0;
        while (atomic_load(&g_launch_online) && atomic_load(&g_launch_epoch)==launch_epoch) {
            // send our latest state (20/s)
            WireState out;
            int ready;
            uint64_t sampled_at;
            pthread_mutex_lock(&g_srv_lock);
            ready = g_srv_out_ready, out = g_srv_out, g_srv_out_ready = 0;
            sampled_at = g_srv_out_ms;
            pthread_mutex_unlock(&g_srv_lock);
            uint64_t clock = now_ms();
            int fresh = sampled_at && clock >= sampled_at && clock - sampled_at <= 2000;
            if (fresh) sent_unready = 0;
            if (!fresh && !sent_unready) {
                memset(&out, 0, sizeof out); out.map = out.room = LAO_UNKNOWN;
                ready = 1; sent_unready = 1;
            }
            if (g_srv_status == 1 && !queried && atomic_load(&g_launch_online)==3) {
                uint8_t query[7]={5,0,0x07};uint32_t nonce=LAO_CAP_QUERY;memcpy(query+3,&nonce,4);if(!send_all(s,query,sizeof query))break;nonce=LAO_CHAT_QUERY;memcpy(query+3,&nonce,4);if(!send_all(s,query,sizeof query))break;nonce=LAO_V12_QUERY;memcpy(query+3,&nonce,4);if(!send_all(s,query,sizeof query))break;queried=1;
            }
            if (g_srv_status == 1 && !queried) {
                uint8_t query[7] = {5,0,0x07}; uint32_t nonce = LAO_CAP_QUERY;
                memcpy(query + 3, &nonce, 4);
                if (!send_all(s, query, sizeof query)) break;
                uint32_t sync_nonce=LAO_COOP_QUERY;memcpy(query+3,&sync_nonce,4);
                if(!send_all(s,query,sizeof query)) break;
                uint32_t map_nonce=LAO_MAP_QUERY;memcpy(query+3,&map_nonce,4);
                if(!send_all(s,query,sizeof query)) break;
                uint32_t chat_nonce=LAO_CHAT_QUERY;memcpy(query+3,&chat_nonce,4);
                if(!send_all(s,query,sizeof query))break;
                uint32_t reward_nonce=0x4c414f5bu;memcpy(query+3,&reward_nonce,4);
                if(!send_all(s,query,sizeof query))break;
                uint32_t death_nonce=LAO_DEATH_QUERY;memcpy(query+3,&death_nonce,4);
                if(!send_all(s,query,sizeof query))break;
                uint32_t equipment_nonce=0x4c414f5fu;memcpy(query+3,&equipment_nonce,4);
                if(!send_all(s,query,sizeof query))break;
                uint32_t advanced_nonce=0x4c414f63u;memcpy(query+3,&advanced_nonce,4);
                if(!send_all(s,query,sizeof query))break;
                uint32_t world_nonce=0x4c414f5du;memcpy(query+3,&world_nonce,4);
                if(!send_all(s,query,sizeof query))break;
                uint32_t v12_nonce=LAO_V12_QUERY;memcpy(query+3,&v12_nonce,4);
                if(!send_all(s,query,sizeof query))break;
                queried = 1;
            }
            if (g_srv_status == 1 && clock - ping_at >= 15000) {
                uint8_t ping[7] = {5, 0, 0x07};
                uint32_t stamp = (uint32_t)clock; memcpy(ping + 3, &stamp, 4);
                if (!send_all(s, ping, sizeof ping)) break;
                ping_at = clock;
            }
            extern int laonline_coop_world_ready(void);
            if (ready && g_srv_status == 1 && (atomic_load(&g_launch_online)==3 || laonline_coop_world_ready())) {
                uint8_t f[3 + sizeof out];
                f[0] = 1 + sizeof out, f[1] = 0, f[2] = C_STATE;
                memcpy(f + 3, &out, sizeof out);
                if (!send_all(s, f, sizeof f)) break;
                uint8_t health, max_health;
                pthread_mutex_lock(&g_srv_lock);
                health = g_srv_health; max_health = g_srv_max_health;
                pthread_mutex_unlock(&g_srv_lock);
                if (max_health && (health != sent_hp || max_health != sent_max)) {
                    uint8_t v[5] = {3, 0, C_VITALS, health, max_health};
                    if (!send_all(s, v, sizeof v)) break;
                    sent_hp = health; sent_max = max_health;
                    LOG("[laonline] A2 sent health %u/%u quarter-hearts\n", sent_hp, sent_max);
                }
                LaoAppearance appearance;
                int capable;
                pthread_mutex_lock(&g_srv_lock);
                appearance = g_srv_appearance; capable = g_srv_appearance_cap;
                pthread_mutex_unlock(&g_srv_lock);
                if (lao_appearance_valid(&appearance)) {
                    /* C_PROFILE already exists on v1 relays. Always keep the legacy
                     * tunic/label colour current, even when full appearance is unavailable. */
                    uint8_t current_rgb[3] = {0x2f,0xa8,0x3c};
                    if (appearance.mask & 1) memcpy(current_rgb, appearance.rgb[0], 3);
                    if (memcmp(current_rgb, last_rgb, 3)) {
                        uint8_t pf[6] = {4,0,0x08}; memcpy(pf+3,current_rgb,3);
                        if (!send_all(s,pf,sizeof pf)) break;
                        memcpy(last_rgb,current_rgb,3);
                        LOG("[laonline] V13 live tunic profile sent #%02x%02x%02x\n",pf[3],pf[4],pf[5]);
                    }
                    if (capable && (!sent_appearance || memcmp(&appearance,&last_appearance,sizeof appearance))) {
                        uint8_t af[3+sizeof appearance] = {1+sizeof appearance,0,LAO_APPEARANCE_TYPE};
                        memcpy(af+3,&appearance,sizeof appearance);
                        if (!send_all(s,af,sizeof af)) break;
                        last_appearance=appearance; sent_appearance=1;
                        LOG("[laonline] V13 appearance sent mask=%02x meshes=%05x\n",appearance.mask,appearance.meshes);
                    }
                    if (!capable && queried && !legacy_notice && clock-ping_at>2000) {
                        LOG("[laonline] V13 legacy relay: tunic-only fallback; update relay for full appearance\n");
                        legacy_notice=1;
                    }
                }
                LaoMapAnchor anchor;int map_cap;
                pthread_mutex_lock(&g_srv_lock);anchor=g_anchor_out;map_cap=g_anchor_cap;pthread_mutex_unlock(&g_srv_lock);
                if(map_cap && (!sent_anchor || memcmp(&anchor,&last_anchor,sizeof anchor))) {
                    uint8_t af[3+sizeof anchor]={1+sizeof anchor,0,LAO_MAP_SEND};memcpy(af+3,&anchor,sizeof anchor);
                    if(!send_all(s,af,sizeof af))break;
                    last_anchor=anchor;sent_anchor=1;
                }
                char location[33];
                pthread_mutex_lock(&g_srv_lock);
                snprintf(location, sizeof location, "%s", g_srv_location);
                pthread_mutex_unlock(&g_srv_lock);
                if ((LAO_V1_LOCATION_SUPPORTED || capable) && location[0] && strcmp(location, sent_location)) {
                    uint8_t f[36]; size_t ln = strlen(location);
                    f[0] = (uint8_t)(2 + ln); f[1] = 0; f[2] = C_LOCATION; f[3] = (uint8_t)ln;
                    memcpy(f + 4, location, ln);
                    if (!send_all(s, f, (int)(4 + ln))) break;
                    snprintf(sent_location, sizeof sent_location, "%s", location);
                    LOG("[laonline] location sent: %s\n", location);
                }
            }
            int coop_failed=0;
            for(int budget=0;atomic_load(&g_launch_online)!=3 && budget<16;budget++) {
                uint8_t cf[256];int cn=laonline_coop_packet(cf,sizeof cf);
                if(!cn)cn=laonline_death_packet(cf,sizeof cf);
                if(!cn)break;
                if(!send_all(s,cf,cn)){coop_failed=1;break;}
            }
            for(int budget=0;!coop_failed && g_srv_status==1 && budget<16;budget++){  /* V12 progress / PvP frames */
                uint8_t vf[32];int vn=laonline_v12_packet(vf,sizeof vf,atomic_load(&g_launch_online));
                if(!vn)break;
                if(!send_all(s,vf,vn))coop_failed=1;
            }
            uint8_t chat_frame[614];int chat_n=laonline_chat_packet(chat_frame,sizeof chat_frame);
            if(chat_n && !send_all(s,chat_frame,chat_n))break;
            if(coop_failed)break;
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(s, &rd);
            struct timeval tv = {0, 50000};
            int sel = select((int)s + 1, &rd, NULL, NULL, &tv);
            if (sel < 0) break;
            if (sel == 0) continue;
            if (have == (int)sizeof rb) break;
            int r = (int)recv(s, (char *)rb + have, (int)(sizeof rb - have), 0);
            if (r <= 0) break;
            have += r;
            int o = 0;
            while (have - o >= 2) {
                int fl = rb[o] | rb[o + 1] << 8;
                if (fl == 0) { g_srv_status = 2; break; }
                if (have - o - 2 < fl) break;
                srv_handle(rb + o + 2, fl);
                {   // (LAS Recomp Co-Op's port: this PC's other local players listed in a WELCOME: not remote players)
                    extern void laonline_local_hide(LaoSession *);
                    pthread_mutex_lock(&g_srv_lock);
                    laonline_local_hide(&g_srv_session);
                    pthread_mutex_unlock(&g_srv_lock);
                }
                o += 2 + fl;
            }
            memmove(rb, rb + o, (size_t)(have - o));
            have -= o;
            if (g_srv_status == 2) break;
        }
        sock_close(s);
        LOG("[laonline] server: disconnected%s\n", g_srv_status == 2 ? " (refused)" : " - reconnecting");
        srv_clear();
        while(g_srv_status==2 && atomic_load(&g_launch_online) && !atomic_exchange(&g_retry_connection,0))sleep_ms(50);
        sleep_ms(3000);
    }
    return NULL;
}

// ---------------------------------------------------------------- per frame (game thread: runtime/hle/warp.c)
int laonline_is_puppet(uint64_t o) { for(unsigned id=0;o && id<LAO_PLAYERS;id++)if(o==g_puppets[id].obj && weak_alive(g_puppets[id].ctrl))return 1;return 0; }
int laonline_puppet_active(void) {for(unsigned id=0;id<LAO_PLAYERS;id++)if(g_puppets[id].obj && weak_alive(g_puppets[id].ctrl))return 1;return 0;}

/* Roster membership is independent of scene/actor readiness. Publish atomically. */
static void publish_roster(const LaoSession *session,const LaoState *state,int have_me,
 uint16_t map_id,uint16_t room_id,const char *local_location,const LaoAppearance *own,
 LaoMapAnchor local_anchor,const LaoMapAnchor anchors[LAO_PLAYERS],
 const float histories[LAO_PLAYERS][3],const int anchor_known[LAO_PLAYERS]){
 LaoSession snapshot=*session;LaoState me=*state;LaoAppearance own_avatar=*own;
            pthread_mutex_lock(&g_ov_lock);
            g_ov_connected = snapshot.connected, g_ov_nplayers = 0, g_ov_nmap = 0;
            if (snapshot.connected) {
                LaoMapOverlayPlayer *m = &g_ov_map[g_ov_nmap++];
                memset(m, 0, sizeof *m);
                snprintf(m->name, sizeof m->name, "%s", g_cfg.name);
                snprintf(m->location, sizeof m->location, "%s", local_location);
                m->map = map_id; m->room = room_id; m->local = 1;m->id=(uint8_t)snapshot.local_id;
                if(!have_me)snprintf(m->location,sizeof m->location,"Changing rooms");
                m->world[0]=me.x; m->world[1]=me.y; m->world[2]=me.z; m->position_valid=have_me;m->fresh=have_me;
                memcpy(m->raw,m->world,12);m->interior=map_id!=lao_name_id("Field");
                if(m->interior){
                    if((local_anchor.flags&1)&&local_anchor.map==map_id&&lao_anchor_valid(&local_anchor)){memcpy(m->world,local_anchor.world,12);m->approximate=(local_anchor.flags&4)!=0;}
                    else if(!rando_seed_dir()&&lao_vanilla_anchor(map_id,m->world))m->approximate=1;
                    else m->position_valid=0;
                }
                uint64_t mgr=have_me?LD64(GA(GAME_MGR)):0,actor=mgr?LD64(mgr+0x4b8):0,tr=actor?LD64(actor+0x80):0;
                if(have_me && tr)m->yaw=lao_quaternion_yaw((const float*)(uintptr_t)(tr+0x90));
                uint32_t tint = 0x44aa55u;
                (void)col_target(&g_settings.tunic, COL_SLOT_TUNIC, &tint);
                m->rgb[0] = (uint8_t)(tint >> 16);
                m->rgb[1] = (uint8_t)(tint >> 8);
                m->rgb[2] = (uint8_t)tint;
                static const uint8_t green[3]={0x3f,0xa3,0x4d},gold[3]={0xff,0xdf,0x57};
                memcpy(m->cap_rgb,(own_avatar.mask&1)?own_avatar.rgb[0]:green,3);memcpy(m->hair_rgb,(own_avatar.mask&2)?own_avatar.rgb[1]:gold,3);m->avatar_known=1;
            }
            for (int id = 0; id < LAO_PLAYERS; ++id) {
                const LaoPeer *p = &snapshot.peers[id];
                char remote_location[33];
                lao_location_ids(p->samples ? p->state.map : LAO_UNKNOWN,
                                 p->samples ? p->state.room : LAO_UNKNOWN, remote_location);
                if(p->samples && p->state.map==lao_name_id("Field")) {
                    const char *area=lao_field_area(p->state.x,p->state.z);
                    if(area)snprintf(remote_location,sizeof remote_location,"%s",area);
                }
                int on = snapshot.connected && id != snapshot.local_id && p->joined;
                if (on) {
                    int k = g_ov_nplayers++;
                    snprintf(g_ov_players[k].name, 17, "%s", p->name);
                    memcpy(g_ov_players[k].rgb, p->rgb, 3);
                    snprintf(g_ov_players[k].location, sizeof g_ov_players[k].location, "%s", remote_location);
                    if (g_ov_nmap < LAO_PLAYERS) {
                        LaoMapOverlayPlayer *m = &g_ov_map[g_ov_nmap++];
                        memset(m, 0, sizeof *m);
                        snprintf(m->name, sizeof m->name, "%s", p->name);
                        snprintf(m->location, sizeof m->location, "%s", remote_location);
                        m->map = p->samples ? p->state.map : LAO_UNKNOWN;
                        m->room = p->samples ? p->state.room : LAO_UNKNOWN;
                        m->world[0]=p->state.x; m->world[1]=p->state.y; m->world[2]=p->state.z;
                        memcpy(m->raw,m->world,12);m->yaw=p->state.rot_y;
                        uint64_t stamp=now_ms();
                        m->position_valid=p->samples && stamp>=p->received_ms && stamp-p->received_ms<=LAO_STATE_AGE_MS;
                        m->fresh=m->position_valid;m->id=(uint8_t)id;
                        m->interior=p->state.map!=lao_name_id("Field");
                        if(m->position_valid && m->interior) {
                            LaoMapAnchor anchor=anchors[id];
                            if((anchor.flags&1) && anchor.map==p->state.map && lao_anchor_valid(&anchor)) {
                                memcpy(m->world,anchor.world,12);m->approximate=(anchor.flags&4)!=0;
                            } else if(anchor_known[id])memcpy(m->world,histories[id],12);
                            else if(!rando_seed_dir() && lao_vanilla_anchor(p->state.map,m->world))m->approximate=1;
                            else m->position_valid=0;
                        }
                        memcpy(m->rgb, p->rgb, 3);
                        static const uint8_t green[3]={0x3f,0xa3,0x4d},gold[3]={0xff,0xdf,0x57};
                        memcpy(m->cap_rgb,(p->appearance.mask&1)?p->appearance.rgb[0]:green,3);memcpy(m->hair_rgb,(p->appearance.mask&2)?p->appearance.rgb[1]:gold,3);m->avatar_known=lao_appearance_valid(&p->appearance);
                    }
                    memcpy(g_ov_tags[id].rgb, p->rgb, 3);
                    g_ov_tags[id].health = p->health;
                    g_ov_tags[id].max_health = p->max_health;
                }
            }
            pthread_mutex_unlock(&g_ov_lock);
}

extern bool laonline_ui_paused(void);
/* LAS Recomp's start screen has no online choice (LA-Online's own start screen isn't part of this build): a mode set in
 * laonline.ini or LAONLINE_MODE starts online play as the game does - co-op, or co-op randomizer while a seed is played
 * - as choosing it there would (laonline_launch_mode). Once, at the first game frame. */
static void launch_from_config(void) {
    static int done;
    if (done || atomic_load(&g_launch_online)) return;
    done = 1;
    load_config();
    if (g_cfg.mode == M_OFF) return;
    extern const char *rando_seed_dir(void);
    laonline_launch_mode(rando_seed_dir() ? 2 : 1);
}

// Online play set up at all: laonline.ini or LAONLINE_MODE sets a mode (the config launch_from_config reads). The rest
// of the game calls into online play only behind this (main.c, warp.c, input.c, bindings.c, souls.c, miniboss.c,
// hooks.c, movie.c, fs.c), so without it the game runs as it does without online play: no journal file, no
// Z-targeting, F6 to F8 free to bind (LAS Recomp's review, point 4: play with the feature off stays identical). Worked
// out once, by main.c at boot before the other threads start (after rt_paths_init: the data root's laonline.ini).
// g_laonline_on is the same, for the game code's patches (tools/patches.py: a load, not a call, on paths every actor
// takes - each animation played).
bool g_laonline_on;
bool laonline_configured(void) {
    static int known = -1;
    if (known < 0) {
        load_config();
        known = g_cfg.mode != M_OFF;
        g_laonline_on = known;
    }
    return known;
}

void laonline_tick(Cpu *c, uint64_t frame) {
    launch_from_config();
    if (!atomic_load(&g_launch_online)) return;
    static int init;
    if (!init) {
        init = 1;
        load_config();room_apply_identity();
        laonline_diag_identity(g_cfg.name);
        LOG("[laonline] BUILD HF3-DEATH-NOTICES: native GameOver notices, verified equipment and exact named seeds\n");
        if (g_cfg.type != 0x21E) {
            LOG("[laonline] unsupported puppet type; online rendering disabled\n");
            g_cfg.mode = M_OFF;
        }
        if (g_cfg.mode == M_OFF) return;
        // This PC's player id, from the OS's random source (runtime/platform/*_compat.h). It was rand() (never seeded:
        // the same first value in every process) ^ this variable's address (one per boot on Windows) ^ the frame (1,
        // the first one): two copies started alike - two on one PC, as tools/coop_tests/online_net.sh runs them -
        // got the same id and each dropped the other's states as its own (accept_state).
        do g_my_id = arc4random(); while (!g_my_id);
        if (g_cfg.mode == M_SERVER) {
            pthread_t t;
            if (pthread_create(&t, NULL, srv_thread, NULL) == 0) pthread_detach(t);
            else { LOG("[laonline] network thread creation failed\n"); g_cfg.mode = M_OFF; }
        } else if (g_cfg.mode != M_ECHO && !net_open()) g_cfg.mode = M_OFF;
        LOG("[laonline] on: mode %d, name '%s', puppet type %#x\n", g_cfg.mode, g_cfg.name, g_cfg.type);
    }
    room_apply_identity();
    if (g_cfg.mode == M_OFF) return;
    laonline_coop_initialize(g_cfg.mode==M_SERVER && atomic_load(&g_launch_online)!=3,g_cfg.name);
    if (!game_in_play()) {
        laonline_death_context(0xffff,0xffff,0);
        // Keep network membership current even while no gameplay scene exists.
        LaoSession roster;LaoAppearance avatar;pthread_mutex_lock(&g_srv_lock);roster=g_srv_session;avatar=g_srv_appearance;pthread_mutex_unlock(&g_srv_lock);
        LaoState loading={0};LaoMapAnchor no_anchor={0},anchors[LAO_PLAYERS]={0};float history[LAO_PLAYERS][3]={{0}};int known[LAO_PLAYERS]={0};
        publish_roster(&roster,&loading,0,LAO_UNKNOWN,LAO_UNKNOWN,"Changing rooms",&avatar,no_anchor,anchors,history,known);
        laonline_appearance_clear();
        pthread_mutex_lock(&g_srv_lock);
        g_srv_out_ms = 0;
        pthread_mutex_unlock(&g_srv_lock);
        return;
    }
    Cpu saved = *c;

    const char *level = "";
    uint64_t ps = LD64(GA(PLAYER_STATUS_PTR));
    if (ps) level = (const char *)(uintptr_t)ps;
    uint64_t pc = doorjump_player(frame);

    // ---- our own state
    LaoState me;
    memset(&me, 0, sizeof me);
    me.magic = LAO_MAGIC, me.version = LAO_VERSION, me.id = g_my_id, me.seq = ++g_seq;
    snprintf(me.name, sizeof me.name, "%s", g_cfg.name);
    snprintf(me.level, sizeof me.level, "%s", level);
    char room[32] = "";
    current_room_name(c, room, sizeof room);
    snprintf(me.room, sizeof me.room, "%s", room);
    char local_location[33]; location_name(level, room, local_location);
    laonline_coop_context(lao_name_id(level),lao_room_id(level,room),level);
    if(laonline_level_ready())laonline_death_context(lao_name_id(level),lao_room_id(level,room),!laonline_ui_paused()&&ps&&(int16_t)LD16(ps+0x84)>0);
    else laonline_death_context(0xffff,0xffff,0);
    static uint16_t diag_map=0xffff,diag_room=0xffff;
    uint16_t dm=lao_name_id(level),dr=lao_room_id(level,room);
    { extern void laonline_local_context(uint16_t, uint16_t); laonline_local_context(dm, dr); }  /* (LAS Recomp Co-Op) */
    if(dm!=diag_map || dr!=diag_room){laonline_diag("scene.enter",dm,dr,0,0,1);diag_map=dm;diag_room=dr;}

    bool have_me = pc != 0;
    if (have_me) {
        const float *p = (const float *)(uintptr_t)(pc + 0x80);
        me.x = p[0], me.y = p[1], me.z = p[2];
    }
    if(have_me && !strcmp(level,"Field")) {
        const char *area=lao_field_area(me.x,me.z);if(area)snprintf(local_location,sizeof local_location,"%s",area);
    }
    // ---- network / echo
    if (g_cfg.mode == M_ECHO) {
        enum { DELAY = 90 };
        static LaoState hist[DELAY];
        if (have_me) {
            LaoState e = me;
            e.id = g_my_id ^ 0xffffffffu;
            snprintf(e.name, sizeof e.name, "echo");
            e.x += 1.5f;
            LaoState old = hist[frame % DELAY];
            hist[frame % DELAY] = e;
            if (old.magic) accept_state(&old, frame);
        }
    } else if (g_cfg.mode == M_SERVER) {
        uint16_t map_id = lao_name_id(level), room_id = lao_room_id(level, room);
        if (frame % 3 == 0) {
            WireState w = {0};
            w.map = have_me ? map_id : LAO_UNKNOWN;
            w.room = have_me ? room_id : LAO_UNKNOWN;
            w.x = me.x; w.y = me.y; w.z = me.z;
            uint64_t mgr = LD64(GA(GAME_MGR)), local = mgr ? LD64(mgr + 0x4b8) : 0;
            uint64_t tr = local ? LD64(local + 0x80) : 0;
            if (tr) w.rot_y = lao_quaternion_yaw((const float *)(uintptr_t)(tr + 0x90));
            w.anim_id = g_local_anim; w.flags = g_local_anim_flags;
            int max_health = 0;
            extern int cheats_link_health(int *max);
            int health = cheats_link_health(&max_health);
            /* Elapsed time since the native play request, not a sampled
             * animation-controller frame. Receiver lets native playback run. */
            uint64_t clock = now_ms();
            if (clock >= g_local_anim_at) w.anim_frame = (float)(clock - g_local_anim_at) * .06f;
            LaoAppearance appearance; laonline_appearance_capture(&appearance);
            pthread_mutex_lock(&g_srv_lock);
            g_srv_appearance = appearance;
            float position[3]={me.x,me.y,me.z};
            if(have_me)g_anchor_out=lao_anchor_sample(map_id,room_id,position,g_anchor_last_field,&g_anchor_have_field,rando_seed_dir()==NULL);
            else memset(&g_anchor_out,0,sizeof g_anchor_out);
            g_srv_out = w; g_srv_out_ready = 1; g_srv_out_ms = now_ms();
            g_srv_health = (uint8_t)(health < 0 ? 0 : health > 255 ? 255 : health);
            g_srv_max_health = (uint8_t)(max_health < 1 ? 1 : max_health > 255 ? 255 : max_health);
            snprintf(g_srv_location, sizeof g_srv_location, "%s", local_location);
            pthread_mutex_unlock(&g_srv_lock);
        }
        LaoAppearance own_avatar;LaoSession snapshot;LaoMapAnchor anchors[LAO_PLAYERS],local_anchor;
        float histories[LAO_PLAYERS][3];int anchor_known[LAO_PLAYERS];
        pthread_mutex_lock(&g_srv_lock);
        snapshot = g_srv_session;own_avatar=g_srv_appearance;local_anchor=g_anchor_out;
        memcpy(anchors,g_anchor_peers,sizeof anchors);memcpy(histories,g_anchor_history,sizeof histories);
        memcpy(anchor_known,g_anchor_known,sizeof anchor_known);
        pthread_mutex_unlock(&g_srv_lock);
        publish_roster(&snapshot,&me,have_me,map_id,room_id,local_location,&own_avatar,local_anchor,anchors,histories,anchor_known);
        unsigned prior_slot=g_puppet_slot;
        uint64_t now=now_ms();
        for(unsigned id=0;id<LAO_PLAYERS;id++) {
            g_puppet_slot=id;
            const LaoPeer *p=&snapshot.peers[id];
            if(!p->joined)g_rp.popped_id=0;
            int ready=have_me && lao_peer_ready(&snapshot,(int)id,map_id,room_id,now);
            if(!ready) {
                int connected=snapshot.connected && p->joined && id!=(unsigned)snapshot.local_id;
                int unknown=p->state.map==LAO_UNKNOWN;
                int compatible=p->state.map==map_id || unknown;
                if(g_rp.valid && g_rp.ctrl && connected && compatible) {
                    if(!g_rp.unready_since){g_rp.unready_since=now;
                        LOG("[laonline] M5 actor.wait peer=%u connected=1 revision=%llu age_ms=%llu map=%04x local_map=%04x unknown=%d actor=%llx alive=%d; hold existing incarnation\n",id,(unsigned long long)p->revision,(unsigned long long)(now>=p->received_ms?now-p->received_ms:0),p->state.map,map_id,unknown,(unsigned long long)g_rp.obj,weak_alive(g_rp.ctrl));}
                    // Pause/short stalls are not proof that an actor left its area.
                    // Freeze its last validated transform; no extrapolation or respawn.
                    if(now-g_rp.unready_since<=10000){g_rp.at=frame;continue;}
                }
                if(g_rp.ctrl){LOG("[laonline] M5 actor.retire peer=%u connected=%d revision=%llu age_ms=%llu remote_map=%04x local_map=%04x actor=%llx alive=%d\n",id,connected,(unsigned long long)p->revision,(unsigned long long)(now>=p->received_ms?now-p->received_ms:0),p->state.map,map_id,(unsigned long long)g_rp.obj,weak_alive(g_rp.ctrl));
                    puppet_drop(c,!connected?"peer disconnected":"confirmed other map or movement unavailable 10 seconds");}
                g_rp.valid=0;g_rp.unready_since=0;if(!p->joined)g_rp.popped_id=0;
                continue;
            }
            if(g_rp.unready_since)LOG("[laonline] M5 actor.resume peer=%u revision=%llu same_incarnation=%d\n",id,(unsigned long long)p->revision,g_rp.ctrl!=0);
            g_rp.unready_since=0;
            if(p->revision!=g_rp.consumed_revision || !g_rp.valid) {
                g_rp.consumed_revision=p->revision;
                LaoState in={0};in.magic=LAO_MAGIC;in.version=LAO_VERSION;
                in.id=0x53000000u|id;in.seq=(uint32_t)p->revision;
                snprintf(in.name,sizeof in.name,"%s",p->name);
                in.x=p->state.x;in.y=p->state.y;in.z=p->state.z;in.yaw=p->state.rot_y;
                g_rp_map=p->state.map;g_rp_room=p->state.room;
                accept_state(&in,frame);g_rp.wire=p->state;
            }
        }
        g_puppet_slot=prior_slot;
    } else if (g_sock != BAD_SOCK) {
        LaoState in;
        struct sockaddr_in from;
        for (int k = 0; k < 64; k++) {
#ifdef _WIN32
            int fl = sizeof from;
#else
            socklen_t fl = sizeof from;
#endif
            int n = (int)recvfrom(g_sock, (char *)&in, sizeof in, 0, (struct sockaddr *)&from, &fl);
            if (n != (int)sizeof in) break;
            if (in.magic != LAO_MAGIC) continue;
            if (g_cfg.mode == M_HOST && (!g_have_peer || from.sin_addr.s_addr != g_peer.sin_addr.s_addr ||
                                         from.sin_port != g_peer.sin_port)) {
                g_peer = from, g_have_peer = 1;
                char ip[32];
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
                LOG("[laonline] host: peer is %s:%d\n", ip, ntohs(from.sin_port));
            }
            accept_state(&in, frame);
        }
        if (have_me && g_have_peer && frame % 3 == 0)  // (20 per second)
            sendto(g_sock, (const char *)&me, sizeof me, 0, (struct sockaddr *)&g_peer, sizeof g_peer);
    }

    unsigned prior_slot=g_puppet_slot;
    unsigned first=g_cfg.mode==M_SERVER?0:prior_slot;
    unsigned limit=g_cfg.mode==M_SERVER?LAO_PLAYERS:prior_slot+1;
    pthread_mutex_lock(&g_ov_lock);
    for(unsigned id=0;id<LAO_PLAYERS;id++)g_ov_tags[id].active=0;
    pthread_mutex_unlock(&g_ov_lock);
    for(unsigned id=first;id<limit;id++) {
        g_puppet_slot=id;
        puppet_tick(c,frame,level,room);
        int active=g_rp.obj && weak_alive(g_rp.ctrl) && g_rp.phase==2 && g_rp.valid;
        if(g_cfg.mode==M_SERVER && g_look_link && active) {
            LaoPeer peer;
            pthread_mutex_lock(&g_srv_lock);peer=g_srv_session.peers[id];
            int joined=g_srv_session.connected && peer.joined;
            pthread_mutex_unlock(&g_srv_lock);
            if(joined) {
                LaoAppearance appearance=peer.appearance;
                if(!lao_appearance_valid(&appearance)) {
                    memset(&appearance,0,sizeof appearance);appearance.version=1;appearance.mask=1;
                    memcpy(appearance.rgb[0],peer.rgb,3);
                }
                laonline_appearance_publish(c,(int)id,g_rp.obj,&appearance);
            }else laonline_appearance_forget((int)id);
        }else laonline_appearance_forget((int)id);
        if(active && LD8(g_rp.obj+0x1700)) {
            pthread_mutex_lock(&g_ov_lock);
            g_ov_tags[id].active=1;
            snprintf(g_ov_tags[id].name,17,"%s",g_rp.st.name);
            g_ov_tags[id].pos[0]=g_rp.px;g_ov_tags[id].pos[1]=g_rp.py+LAO_TAG_HEAD;g_ov_tags[id].pos[2]=g_rp.pz;
            pthread_mutex_unlock(&g_ov_lock);
        }
    }
    g_puppet_slot=prior_slot;

    int local_id;
    pthread_mutex_lock(&g_srv_lock);local_id=g_srv_session.local_id;pthread_mutex_unlock(&g_srv_lock);
    float chat_local[3]={me.x,me.y+LAO_TAG_HEAD,me.z};
    laonline_chat_actor(local_id,chat_local,have_me);
    for(int id=0;id<LAO_PLAYERS;id++)if(id!=local_id){
        pthread_mutex_lock(&g_ov_lock);
        laonline_chat_actor(id,g_ov_tags[id].pos,g_ov_tags[id].active);
        pthread_mutex_unlock(&g_ov_lock);
    }
    {float v12_me[3]={me.x,me.y,me.z};laonline_v12_tick(c,frame,level,room,v12_me,have_me);}
    *c = saved;
}

/* V12: a joined peer's last network position (PvP reach test). Network data only, no actor pointers. */
int laonline_v12_peer(int id,float pos[3],uint16_t *map,int *local_id){
    if(id<0||id>=LAO_PLAYERS)return 0;
    pthread_mutex_lock(&g_srv_lock);
    const LaoPeer *p=&g_srv_session.peers[id];
    int joined=g_srv_session.connected&&p->joined&&p->samples>0;
    pos[0]=p->state.x;pos[1]=p->state.y;pos[2]=p->state.z;*map=p->state.map;*local_id=g_srv_session.local_id;
    pthread_mutex_unlock(&g_srv_lock);
    return joined;
}

RT_ON_RESET(laonline_reset) {
    /* Called after guest threads stop and before guest memory is wiped. */
    atomic_store(&g_launch_online,0);atomic_fetch_add(&g_launch_epoch,1);
    pthread_mutex_lock(&g_ov_lock);g_ov_connected=0;g_ov_nplayers=g_ov_nmap=0;pthread_mutex_unlock(&g_ov_lock);
    for(int id=0;id<LAO_PLAYERS;id++)laonline_remote_draw_reset(id);
    laonline_appearance_clear();
    memset(g_puppets, 0, sizeof g_puppets);
    memset(g_ghost_obj,0,sizeof g_ghost_obj);memset(g_ghost_n,0,sizeof g_ghost_n);
    g_puppet_slot = 0;
    g_room_args = g_transform_args = 0;memset(g_spawn_args_by_peer,0,sizeof g_spawn_args_by_peer);
    g_rp_map = g_rp_room = LAO_UNKNOWN;
    g_anchor_have_field=0;memset(&g_anchor_out,0,sizeof g_anchor_out);
    g_local_anim = 0x67; g_local_anim_flags = 0; g_local_anim_at = 0;
    pthread_mutex_lock(&g_srv_lock);
    memset(g_anchor_known,0,sizeof g_anchor_known);memset(g_anchor_peers,0,sizeof g_anchor_peers);
    for (int id = 0; id < LAO_PLAYERS; ++id) g_srv_session.peers[id].samples = 0;
    g_srv_out_ms = 0; g_srv_out_ready = 0;
    pthread_mutex_unlock(&g_srv_lock);
}
