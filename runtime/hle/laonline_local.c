// Online play with local co-op: this PC's other local players (2 to 8) on the relay as players of their own.
//
// LA-Online (runtime/hle/laonline.c) sends one player per PC: Player 1 (GAME_MGR+0x4b8). Its protocol has one player
// per relay connection, so each other local player who is in the game (runtime/hle/coop.c) gets a connection of his
// own: the same relay, lobby and password as Player 1's (laonline_relay_login), his name "<Player 1's> P<n>", his
// colour, and his Link's position, facing, animation and hearts - what the other PCs need to show him as a remote
// Link with a name tag, and his own PvP (laonline_v12.c). Shared progress, pickups, deaths and chat stay Player 1's
// connection's (one save per PC). What comes in on it is read and dropped, but for its WELCOME (the id the relay gave
// him) and the PvP hits on him. No change to the protocol or the relay; the main connection leaves these ids out
// (laonline_local_peer_message, laonline_local_hide: this PC doesn't show its own players as remote ones).
//
// What LA-Online's relay (server.py, V15.2) asks of each connection, as of Player 1's: it opens once Player 1's is in
// the same lobby (laonline_relay_on: so his is the lobby's first - its host, whose world the lobby takes); in a session
// room's lobby only a member of the room gets in (the room's places: laonline_lobby_seat_ok,
// runtime/ui/laonline_lobby.mm); PvP frames only after the V12 capability query (else the relay drops the connection);
// in a Co-op Randomizer lobby a STATE counts only once his world - Player 1's (laonline_coop_world_frame) - is
// registered.
//
// Relay mode only (laonline.ini mode = server). Game thread: laonline_local_tick (from warp.c with online play's ticks)
// samples every 3rd frame, as Player 1's; laonline_local_animation (the animation hook, laonline.c) keeps each one's
// last animation. Its own thread does the network, 20 times a second.
#include "../game_addresses.h"
#include "../hle.h"
#include "../platform.h"
#include "laonline_native.h"
#include "laonline_session.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define sock_close closesocket
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define sock_close close
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "../laonline_build.h"  // (LAO_MOD_VERSION: sent in HELLO, as Player 1's connection sends it - the relay checks it)

#define LOCAL_MAX 8  // (players 2..8: index 1..7, as coop.c's)

extern uint64_t coop_player(int i);
extern uint32_t coop_joined(void);
extern uint32_t coop_colour_rgb(int player);
typedef struct { int player, hp, max, down; float revive; uint32_t rgb; } CoopHudRow;  // (runtime/ui/coop_hud.mm)
extern int coop_hud_rows(CoopHudRow *rows, int cap);
extern int game_in_play(void);
extern int laonline_relay_login(char *host, size_t hn, char *port, size_t pn, char *name, char *lobby, char *password);
extern int laonline_relay_on(const char *lobby);  // (laonline.c: Player 1 is in this lobby)
extern int laonline_lobby_seat_ok(int i);  // (runtime/ui/laonline_lobby.mm)
extern int laonline_coop_world_frame(uint8_t *out, int capacity);

// A local player's name online: Player 1's, then " P<n>" (16 at most, the relay's limit) - his relay connection's and
// his place in a session room's (they must be the same: the relay lets a room's members in by name).
void laonline_local_name(const char *first, int i, char out[17]) { snprintf(out, 17, "%.13s P%d", first, i + 1); }

typedef struct {
    int in;                       // in the game, his Link found, sampled this time round
    LaoWireState st;              // what goes out as his STATE
    uint8_t hp, max;              // quarter hearts
    uint32_t rgb;
    uint16_t anim;                // his last animation (laonline_local_animation)
    uint8_t anim_flags;
    uint64_t anim_at, sampled_at; // ms
} Sample;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Sample g_s[LOCAL_MAX];
static uint64_t g_anim_comp[LOCAL_MAX];   // (game thread: each Link's animation component, for the hook)
static uint16_t g_map = LAO_UNKNOWN, g_room = LAO_UNKNOWN;
static _Atomic int g_id[LOCAL_MAX];       // the relay's id + 1 for each one connected (0: none - zero-initialized,
                                          // so no id is taken for one of ours before the thread runs: 0 is a real id)
static int g_started;

static uint64_t ms_now(void) { return plat_now_ns() / 1000000ull; }

// ---- game thread

void laonline_local_context(uint16_t map, uint16_t room) { g_map = map, g_room = room; }

void laonline_local_animation(uint64_t comp, uint16_t anim) {
    for (int i = 1; i < LOCAL_MAX; i++)
        if (comp && g_anim_comp[i] == comp) {
            pthread_mutex_lock(&g_lock);
            g_s[i].anim = anim;
            g_s[i].anim_flags = (uint8_t)((g_s[i].anim_flags ^ LAO_ANIMATION_EVENT) | LAO_ANIMATION_VALID);
            g_s[i].anim_at = ms_now();
            pthread_mutex_unlock(&g_lock);
            return;
        }
}

static void *net_thread(void *);

void laonline_local_tick(Cpu *c, uint64_t frame) {
    (void)c;
    if (frame % 3) return;
    char host[96], port[16], name[17], lobby[33], password[65];
    if (!laonline_relay_login(host, sizeof host, port, sizeof port, name, lobby, password)) return;
    if (!g_started) {
        g_started = 1;
        pthread_t t;
        if (pthread_create(&t, NULL, net_thread, NULL) == 0) pthread_detach(t);
    }
    CoopHudRow rows[LOCAL_MAX];
    int nrows = coop_hud_rows(rows, LOCAL_MAX);
    uint32_t joined = coop_joined();
    uint64_t now = ms_now();
    for (int i = 1; i < LOCAL_MAX; i++) {
        uint64_t link = (joined >> i & 1) && game_in_play() ? coop_player(i) : 0;
        uint64_t col = link ? LD64(link + 0x80) : 0;  // (his PlayerCollision: position +0x80, rotation +0x90)
        int ok = col && LD64(col) == GA(PLAYER_COLLISION_VT);
        g_anim_comp[i] = ok ? LD64(link + LAO_LOCAL_ANIMATION_OFF) : 0;
        pthread_mutex_lock(&g_lock);
        Sample *s = &g_s[i];
        s->in = ok;
        if (ok) {
            const float *p = (const float *)(uintptr_t)(col + 0x80);
            s->st.map = g_map, s->st.room = g_room;
            s->st.x = p[0], s->st.y = p[1], s->st.z = p[2];
            s->st.rot_y = lao_quaternion_yaw((const float *)(uintptr_t)(col + 0x90));
            s->st.anim_id = s->anim_flags ? s->anim : 0x67;  // (0x67: his default, laonline.c g_local_anim)
            s->st.anim_frame = s->anim_at && now >= s->anim_at ? (float)(now - s->anim_at) * .06f : 0;
            s->st.flags = s->anim_flags;
            s->rgb = coop_colour_rgb(i);
            for (int r = 0; r < nrows; r++)
                if (rows[r].player == i) s->hp = (uint8_t)(rows[r].hp < 0 ? 0 : rows[r].hp > 255 ? 255 : rows[r].hp),
                                         s->max = (uint8_t)(rows[r].max < 1 ? 1 : rows[r].max > 255 ? 255 : rows[r].max);
            s->sampled_at = now;
        }
        pthread_mutex_unlock(&g_lock);
    }
}

// ---- the main connection: these ids are this PC's own players

int laonline_local_peer_message(const uint8_t *b, int len) {
    if (len < 2) return 0;
    switch (b[0]) {
    case 0x82: case 0x83: case 0x84: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x93: case 0x95: break;
    default: return 0;
    }
    for (int i = 1; i < LOCAL_MAX; i++)
        if (atomic_load(&g_id[i]) == b[1] + 1) return 1;
    return 0;
}

void laonline_local_hide(LaoSession *s) {
    for (int i = 1; i < LOCAL_MAX; i++) {
        int id = atomic_load(&g_id[i]) - 1;
        if (id >= 0 && id < LAO_PLAYERS && s->peers[id].joined) s->peers[id].joined = 0, s->peers[id].samples = 0;
    }
}

// ---- network thread: one relay connection per local player in the game

typedef struct {
    sock_t s;
    uint8_t rb[4096];
    int have, welcomed, launch, queried, world_sent;
    uint64_t ping_at;
    uint8_t sent_hp, sent_max;
    uint32_t sent_rgb;
    uint64_t sent_sample;
    char login[200];  // the session it joined (relay_login's key): another one, or none, closes it
} Conn;

// Player 1's relay login (laonline_relay_login) and a key for it: the relay, the lobby, the password and the name.
// 0: online play isn't on through the relay.
static int relay_login(char host[96], char port[16], char name[17], char lobby[33], char password[65], char *key, size_t cap) {
    int launch = laonline_relay_login(host, 96, port, 16, name, lobby, password);
    if (!launch) { if (cap) key[0] = 0; return 0; }
    snprintf(key, cap, "%d %s:%s %s %s %s", launch, host, port, lobby, password, name);
    return launch;  // (3: Global - its HELLO is 0x31)
}

static int send_frame(sock_t s, const uint8_t *body, int n) {
    uint8_t f[600];
    if (n + 2 > (int)sizeof f) return 0;
    f[0] = (uint8_t)n, f[1] = (uint8_t)(n >> 8);
    memcpy(f + 2, body, (size_t)n);
    for (int o = 0; o < n + 2;) {
        int r = (int)send(s, (const char *)f + o, n + 2 - o, 0);
        if (r <= 0) return 0;
        o += r;
    }
    return 1;
}

// PvP (laonline_v12.c, each local player's own attacks and his PvP switch): frames to go out on his connection, in
// order, after his STATE. Game thread pushes, the network thread sends; dropped with the connection.
typedef struct { uint8_t b[32]; int n; } OutFrame;
static OutFrame g_out[LOCAL_MAX][16];
static int g_out_n[LOCAL_MAX];
int laonline_local_push(int i, const uint8_t *frame, int n) {  // frame: u16 length + body, as push_frame's
    if (i < 1 || i >= LOCAL_MAX || n < 3 || n - 2 > (int)sizeof g_out[0][0].b || atomic_load(&g_id[i]) <= 0) return 0;
    pthread_mutex_lock(&g_lock);
    int ok = g_out_n[i] < 16;
    if (ok) g_out[i][g_out_n[i]].n = n - 2, memcpy(g_out[i][g_out_n[i]].b, frame + 2, (size_t)(n - 2)), g_out_n[i]++;
    pthread_mutex_unlock(&g_lock);
    return ok;
}
int laonline_local_relay_id(int i) { return i >= 1 && i < LOCAL_MAX ? atomic_load(&g_id[i]) - 1 : -1; }

static void conn_close(Conn *k, int i) {
    pthread_mutex_lock(&g_lock);
    g_out_n[i] = 0;
    pthread_mutex_unlock(&g_lock);
    if (k->s != BAD_SOCK) sock_close(k->s);
    memset(k, 0, sizeof *k);
    k->s = BAD_SOCK;
    if (atomic_exchange(&g_id[i], 0) > 0) LOG("[laonline] local player %d: off the relay\n", i + 1);
}

static int conn_open(Conn *k, int i, uint32_t rgb) {
    char host[96], port[16], name[17], lobby[33], password[65], key[200];
    int launch = relay_login(host, port, name, lobby, password, key, sizeof key);
    if (!launch || !laonline_relay_on(lobby) || !laonline_lobby_seat_ok(i)) return 0;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET, hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res) return 0;
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCK || connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        if (s != BAD_SOCK) sock_close(s);
        freeaddrinfo(res);
        return 0;
    }
    freeaddrinfo(res);
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    char me[17];
    laonline_local_name(name, i, me);
    uint8_t b[300];
    int n = 0;
    b[n++] = launch == 3 ? 0x31 : 0x01;  // HELLO (Story Co-op / Co-op Randomizer: the lobby says which; Global: 0x31)
    b[n++] = 1, b[n++] = 0;  // protocol version 1
    size_t l = strlen(me); b[n++] = (uint8_t)l; memcpy(b + n, me, l); n += (int)l;
    b[n++] = (uint8_t)(rgb >> 16), b[n++] = (uint8_t)(rgb >> 8), b[n++] = (uint8_t)rgb;
    l = strlen(lobby); b[n++] = (uint8_t)l; memcpy(b + n, lobby, l); n += (int)l;
    l = strlen(password); b[n++] = (uint8_t)l; memcpy(b + n, password, l); n += (int)l;
    memcpy(b + n, "LAOV", 4); n += 4;
    l = strlen(LAO_MOD_VERSION); b[n++] = (uint8_t)l; memcpy(b + n, LAO_MOD_VERSION, l); n += (int)l;
    k->s = s;
    if (!send_frame(s, b, n)) { conn_close(k, i); return 0; }
    k->ping_at = ms_now(), k->sent_hp = k->sent_max = 0xff, k->sent_rgb = rgb, k->launch = launch;
    snprintf(k->login, sizeof k->login, "%s", key);
    LOG("[laonline] local player %d: on the relay %s:%s as '%s' (lobby %s)\n", i + 1, host, port, me, lobby);
    return 1;
}

// What came in: its WELCOME (the id), an ERROR (closed), the rest dropped. 0: the connection is gone.
static int conn_read(Conn *k, int i) {
    for (;;) {
        int r = (int)recv(k->s, (char *)k->rb + k->have, (int)(sizeof k->rb - (size_t)k->have), 0);
        if (r == 0) return 0;
        if (r < 0) {
#ifdef _WIN32
            return WSAGetLastError() == WSAEWOULDBLOCK;
#else
            return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
        }
        k->have += r;
        int o = 0;
        while (k->have - o >= 2) {
            int fl = k->rb[o] | k->rb[o + 1] << 8;
            if (fl == 0) return 0;
            if (k->have - o - 2 < fl) break;
            const uint8_t *f = k->rb + o + 2;
            if (f[0] == 0x81 && fl >= 2 && !k->welcomed) {
                k->welcomed = 1;
                atomic_store(&g_id[i], f[1] + 1);
                LOG("[laonline] local player %d: relay id %d\n", i + 1, f[1]);
            } else if (f[0] == 0x8F) {
                LOG("[laonline] local player %d: refused by the relay\n", i + 1);
                return 0;
            } else if (f[0] == 0xA2 && fl == 4 && f[1] == 1) {  // (a PvP hit on him: S_PVP kind 1, attacker, quarters)
                extern void laonline_v12_hit_for(int self, int from, int quarters);
                laonline_v12_hit_for(i, f[2], f[3]);
            }
            o += 2 + fl;
        }
        if (o) memmove(k->rb, k->rb + o, (size_t)(k->have - o)), k->have -= o;
        if (k->have >= (int)sizeof k->rb) return 0;
    }
}

static void *net_thread(void *arg) {
    (void)arg;
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    static Conn conn[LOCAL_MAX];
    uint64_t retry_at[LOCAL_MAX] = {0};
    for (int i = 0; i < LOCAL_MAX; i++) conn[i].s = BAD_SOCK;
    for (;;) {
        plat_sleep_until_ns(plat_now_ns() + 50000000ull);
        uint64_t now = ms_now();
        // (the session Player 1 is in now: a connection opened for another one - online play stopped, or another session
        // picked on the Online page - leaves at once, and joins this one)
        char host[96], port[16], name[17], lobby[33], password[65], login[200];
        relay_login(host, port, name, lobby, password, login, sizeof login);
        for (int i = 1; i < LOCAL_MAX; i++) {
            Conn *k = &conn[i];
            Sample s;
            pthread_mutex_lock(&g_lock);
            s = g_s[i];
            pthread_mutex_unlock(&g_lock);
            if (k->s != BAD_SOCK && strcmp(k->login, login)) conn_close(k, i);
            int wanted = login[0] && s.in && s.sampled_at && now - s.sampled_at < 2000;
            if (!wanted) {
                if (k->s != BAD_SOCK) conn_close(k, i);
                continue;
            }
            if (k->s == BAD_SOCK) {
                if (now < retry_at[i]) continue;
                if (!conn_open(k, i, s.rgb)) { retry_at[i] = now + 5000; continue; }
            }
            if (!conn_read(k, i)) { conn_close(k, i); retry_at[i] = now + 3000; continue; }
            if (!k->welcomed) continue;
            int ok = 1;
            if (!k->queried) {  // (PvP: the relay takes his PvP frames only after this - Player 1's connection asks too)
                uint8_t q[5] = {0x07, 0x70, 0x4f, 0x41, 0x4c};  // PING LAO_V12_QUERY
                ok = send_frame(k->s, q, 5), k->queried = 1;
            }
            if (ok && k->launch != 3 && !k->world_sent) {  // (his world, once Player 1's is registered: not in Global)
                uint8_t w[140];
                int n = laonline_coop_world_frame(w, sizeof w);
                if (n) {
                    uint8_t q[5] = {0x07, 0x5d, 0x4f, 0x41, 0x4c};  // PING world capability, then WORLD
                    ok = send_frame(k->s, q, 5) && send_frame(k->s, w + 2, n - 2), k->world_sent = 1;
                }
            }
            if (ok && s.sampled_at != k->sent_sample) {  // STATE, then what changed
                uint8_t b[1 + sizeof(LaoWireState)];
                b[0] = 0x02;
                memcpy(b + 1, &s.st, sizeof s.st);
                ok = send_frame(k->s, b, (int)sizeof b);
                k->sent_sample = s.sampled_at;
                if (ok && s.max && (s.hp != k->sent_hp || s.max != k->sent_max)) {
                    uint8_t v[3] = {0x09, s.hp, s.max};
                    ok = send_frame(k->s, v, 3), k->sent_hp = s.hp, k->sent_max = s.max;
                }
                if (ok && s.rgb != k->sent_rgb) {
                    uint8_t pr[4] = {0x08, (uint8_t)(s.rgb >> 16), (uint8_t)(s.rgb >> 8), (uint8_t)s.rgb};
                    ok = send_frame(k->s, pr, 4), k->sent_rgb = s.rgb;
                }
            }
            while (ok) {  // (his PvP's frames: laonline_local_push)
                uint8_t body[32];
                int n = 0;
                pthread_mutex_lock(&g_lock);
                if (g_out_n[i]) {
                    n = g_out[i][0].n, memcpy(body, g_out[i][0].b, (size_t)n);
                    memmove(&g_out[i][0], &g_out[i][1], sizeof g_out[i][0] * (size_t)--g_out_n[i]);
                }
                pthread_mutex_unlock(&g_lock);
                if (!n) break;
                ok = send_frame(k->s, body, n);
            }
            if (ok && now - k->ping_at >= 15000) {  // (keepalive, as Player 1's)
                uint32_t t = (uint32_t)now;
                uint8_t p[5] = {0x07, (uint8_t)t, (uint8_t)(t >> 8), (uint8_t)(t >> 16), (uint8_t)(t >> 24)};
                ok = send_frame(k->s, p, 5), k->ping_at = now;
            }
            if (!ok) { conn_close(k, i); retry_at[i] = now + 3000; }
        }
    }
    return NULL;
}
