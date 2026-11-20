// The menu's Online page: LA-Online's sessions on its relay. Host a private session (a room code to share) or a public
// one, join one by its code or from the public list, mark Ready; the host starts it, and online play then runs in that
// session (laonline_session_override + laonline_launch_mode: the relay lobby, password and name the room gives). Its
// Global tab joins Global (everyone, each in his own world); its Leaderboards tab lists and takes completed runs.
//
// From LA-Online V15.2's runtime/ui/session_ui.mm (MIT (c) ThyHeroOfTime, LICENSE-LA-Online): the session service's
// protocol (relay opcode 0x30, actions 0..9: docs/laonline-protocol.md) and its rules - the 2 s room poll, a press
// made during a poll sent after it, the rejoin token kept in laonline-rejoin.txt - as he wrote them. What changed: it is
// a page of the game's menu instead of his start screen's (drawn with the menu's own widgets, not his artwork), and the
// room is polled every frame the game draws (laonline_lobby_frame), so a session the host starts begins with the menu
// closed too. Shown only while online play is set up (laonline_configured: laonline.ini names a relay).
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using LobbySocket = SOCKET;
#define LOBBY_BAD_SOCKET INVALID_SOCKET
#define lobby_close closesocket
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using LobbySocket = int;
#define LOBBY_BAD_SOCKET (-1)
#define lobby_close close
#endif
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "imgui.h"
#include "i18n.h"
#include "plastic.h"  // (the menu's tab colours: the tab row)
#include "laonline_seed_share.h"
#include "../laonline_seed_transfer.h"
extern "C" {
#include "../rt.h"  // (LOG, rt_request_reboot, g_romfs_dir)
#include "../paths.h"  // (rt_resource: the seed helper)
#include "../settings.h"
#include "../laonline_build.h"
#include "../cosmetic_colour.h"
#include "../rando.h"
}

extern "C" void laonline_session_override(const char *name, const char *lobby, const char *password);  // hle/laonline.c
extern "C" void laonline_launch_mode(int mode);
extern "C" int laonline_launch_choice(void);
extern "C" bool laonline_configured(void);
extern "C" void laonline_lobby_config(char *name, size_t name_cap, char *server, size_t server_cap);
extern "C" int game_in_play(void);  // hle/warp.c
extern bool laonline_avatar_icon(const uint8_t *cap, const uint8_t *hair, ImTextureID *texture, float *aspect);

namespace {
struct Member { std::string name; uint8_t rgb[3], hair[3]; bool ready, host; };
struct Room {
    std::string code, title, description, host, build, world, seed;
    int mode = 1, capacity = 4;  // mode 1 co-op, 2 co-op randomizer
    bool public_room = false, started = false;
    std::vector<Member> players;
};
struct Directory {  // one answer of the session service: a page of public rooms (action 0), or this player's room
    std::string self_name, token, password, lobby, message;
    std::vector<Room> rooms;
    int total = 0, page = 0;
    bool success = false;
};
struct Pending { bool set = false; int action = 0; std::string code; bool ready = false; int page = 0; std::string name; bool named = false; };
struct Wire {  // reads an answer: u8 / u16 little-endian / u8-length strings
    const std::vector<uint8_t> &b;
    size_t i = 0;
    int byte() { if (i >= b.size()) throw std::runtime_error("Incomplete relay response."); return b[i++]; }
    int word() { int a = byte(); return a | (byte() << 8); }
    std::string string() {
        int n = byte();
        if (i + n > b.size()) throw std::runtime_error("Incomplete relay response.");
        std::string s((const char *)b.data() + i, n);
        i += n;
        return s;
    }
};
}  // namespace

static std::mutex g_lock;  // g_listing, g_member, g_pending, g_seed_*
static Directory g_listing, g_member;
static Pending g_pending;
static std::atomic<bool> g_busy(false), g_acting(false);  // a request in flight; one of the player's own (greys out)
static unsigned g_generation;
static std::string g_endpoint;  // the relay: laonline.ini's server
static char g_name[17], g_code[7], g_title[49], g_search[49];
static int g_filter_mode, g_session_filter, g_sort, g_list_page;
static bool g_inited, g_browsing, g_launched;  // g_launched: online play started for the room this player is in
static std::string g_launched_room, g_note;     // g_note: why the started room can't begin here
static uint64_t g_poll_at, g_list_at;

static uint64_t now_ms(void) { return SDL_GetTicksNS() / 1000000ull; }

static void init_once(void) {
    if (g_inited) return;
    g_inited = true;
    char name[17] = "", server[96] = "";
    laonline_lobby_config(name, sizeof name, server, sizeof server);
    snprintf(g_name, sizeof g_name, "%s", strcmp(name, "Player") ? name : "");
    g_endpoint = server;
}

// This PC's seed as the session service names it: the seed's ID and its world's checksum (LaoSeedRecipe, as his
// launcher's rooms did). Worked out once per seed folder - it reads the whole world - on the request thread.
static std::string g_seed_dir, g_seed_world, g_seed_id;
static bool seed_describe(std::string &world, std::string &id) {
    const char *dir = rando_seed_dir();
    std::lock_guard<std::mutex> lock(g_lock);
    if (!dir) { world.clear(), id.clear(); return false; }
    if (g_seed_dir != dir) {
        LaoSeedRecipe recipe;
        g_seed_dir = dir;
        if (lao_seed_recipe(std::filesystem::path(dir), recipe)) g_seed_world = recipe.hash, g_seed_id = recipe.id;
        else g_seed_world.clear(), g_seed_id.clear();
    }
    world = g_seed_world, id = g_seed_id;
    return !id.empty();
}
static bool seed_cached(std::string &world, std::string &id) {  // (the render thread: never hashes)
    const char *dir = rando_seed_dir();
    std::lock_guard<std::mutex> lock(g_lock);
    if (!dir || g_seed_dir != dir) return false;
    world = g_seed_world, id = g_seed_id;
    return !id.empty();
}

// ---------------------------------------------------------------- the session service (relay opcode 0x30)
static bool transfer(LobbySocket s, uint8_t *data, int n, bool sending) {
    while (n) {
        int k = sending ? send(s, (const char *)data, n, 0) : recv(s, (char *)data, n, 0);
        if (k <= 0) return false;
        data += k, n -= k;
    }
    return true;
}
// One request on a connection of its own, as his: u16 length, opcode, request; the answer's opcode is opcode + 0x80.
static std::vector<uint8_t> exchange(const std::string &address, const std::vector<uint8_t> &request, uint8_t opcode) {
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w)) throw std::runtime_error("Cannot initialize networking.");
    struct Cleanup { ~Cleanup() { WSACleanup(); } } cleanup;
#endif
    std::string host = address, port = "47620";
    size_t split = host.rfind(':');
    if (split != std::string::npos) port = host.substr(split + 1), host.resize(split);
    if (host.empty()) throw std::runtime_error("No relay set up: laonline.ini's server.");
    addrinfo hint = {}, *found = nullptr;
    hint.ai_family = AF_INET, hint.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hint, &found) || !found) throw std::runtime_error("Cannot reach the relay.");
    LobbySocket s = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (s == LOBBY_BAD_SOCKET) { freeaddrinfo(found); throw std::runtime_error("Cannot open a connection."); }
    struct Guard { LobbySocket s; ~Guard() { lobby_close(s); } } guard{s};
#ifdef _WIN32
    DWORD timeout = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof timeout);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof timeout);
    u_long nonblock = 1;  // (a connect that can't take long: 3 s at most)
    ioctlsocket(s, FIONBIO, &nonblock);
    int failed = connect(s, found->ai_addr, (int)found->ai_addrlen);
    if (failed && WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(s, &writable);
        timeval wait = {3, 0};
        failed = 1;
        if (select(0, nullptr, &writable, nullptr, &wait) > 0) {
            int error = 0, len = sizeof error;
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&error, &len);
            failed = error != 0;
        }
    }
    nonblock = 0;
    ioctlsocket(s, FIONBIO, &nonblock);
#else
    timeval timeout = {3, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    int failed = connect(s, found->ai_addr, (socklen_t)found->ai_addrlen);
#endif
    freeaddrinfo(found);
    if (failed) throw std::runtime_error("Cannot connect to the relay. Check your connection and try again.");
    std::vector<uint8_t> packet = {(uint8_t)(request.size() + 1), (uint8_t)((request.size() + 1) >> 8), opcode};
    packet.insert(packet.end(), request.begin(), request.end());
    if (!transfer(s, packet.data(), (int)packet.size(), true)) throw std::runtime_error("Connection interrupted.");
    uint8_t head[3];
    if (!transfer(s, head, 3, false)) throw std::runtime_error("The relay has no session service.");
    int n = head[0] | (head[1] << 8);
    if (head[2] != (uint8_t)(opcode + 0x80) || n < 2 || n > 16384) throw std::runtime_error("The relay has no session service.");
    std::vector<uint8_t> answer(n - 1);
    if (!transfer(s, answer.data(), n - 1, false)) throw std::runtime_error("Incomplete relay response.");
    return answer;
}

static Directory decode(const std::vector<uint8_t> &bytes) {
    Directory d;
    Wire r{bytes};
    d.success = r.byte() == 1;
    d.message = r.string();
    if (!d.success) return d;
    d.token = r.string(), d.password = r.string(), d.lobby = r.string();
    d.total = r.word(), d.page = r.word();
    int count = r.byte();
    if (count > 8) throw std::runtime_error("Invalid session count.");
    for (int i = 0; i < count; i++) {
        Room room;
        room.code = r.string(), room.title = r.string(), room.description = r.string(), room.host = r.string();
        room.mode = r.byte();
        room.public_room = r.byte() != 0, room.started = r.byte() != 0;
        room.capacity = r.byte();
        room.build = r.string(), room.world = r.string(), room.seed = r.string();
        int players = r.byte();
        if (players > 4 || room.capacity != 4) throw std::runtime_error("Unsupported room capacity.");
        for (int p = 0; p < players; p++) {
            Member m;
            m.name = r.string();
            for (auto &c : m.rgb) c = (uint8_t)r.byte();
            for (auto &c : m.hair) c = (uint8_t)r.byte();
            m.ready = r.byte() != 0, m.host = r.byte() != 0;
            room.players.push_back(m);
        }
        d.rooms.push_back(room);
    }
    if (r.i != bytes.size()) throw std::runtime_error("Unexpected relay response.");
    return d;
}

// The rejoin token: a player whose game closed takes their slot back (2 minutes, relay rule) with it - his V13. Kept
// with the name it was for, and given back only to that name: the file is the data folder's, and another copy of the
// game started from it (another player) joining the same room was handed the first one's place (online_lobby.sh).
static void rejoin_save(const std::string &code, const std::string &token, const std::string &name) {
    std::ofstream f("laonline-rejoin.txt", std::ios::trunc);
    if (f && !code.empty()) f << code << "\n" << token << "\n" << name << "\n";
}
static std::string rejoin_token(const std::string &code, const std::string &name) {
    std::ifstream f("laonline-rejoin.txt");
    std::string c, t, n;
    if (f && std::getline(f, c) && std::getline(f, t) && std::getline(f, n) && c == code && t.size() == 32 && n == name)
        return t;
    return "";
}

static void put_str(std::vector<uint8_t> &b, const std::string &s) {
    size_t n = std::min<size_t>(s.size(), 255);
    b.push_back((uint8_t)n);
    b.insert(b.end(), s.begin(), s.begin() + n);
}
static void put_rgb(std::vector<uint8_t> &b, const CosColour *c, int slot, uint32_t fallback) {
    uint32_t rgb = fallback;
    col_target(c, slot, &rgb);
    b.insert(b.end(), {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb});
}

// A request's body: u8 version 1, u8 action, then its fields in the relay's order (docs/laonline-protocol.md 6a).
static std::vector<uint8_t> session_body(int action, const std::string &code, const std::string &token, const std::string &name,
                                         const std::string &world, const std::string &seed, const std::string &title, bool ready,
                                         const std::vector<uint8_t> &colours, const std::string &search, int filter, int sort, int page) {
    std::vector<uint8_t> b = {1, (uint8_t)action};
    put_str(b, code), put_str(b, token), put_str(b, name), put_str(b, LAO_ROOM_BUILD);
    b.push_back(seed.empty() ? 1 : 2);  // (a randomizer seed played: a Co-op Randomizer session)
    put_str(b, world), put_str(b, seed), put_str(b, title), put_str(b, "");  // (no description)
    b.push_back(ready);
    b.insert(b.end(), colours.begin(), colours.end());
    put_str(b, search);
    b.push_back((uint8_t)filter), b.push_back((uint8_t)sort), b.push_back((uint8_t)page), b.push_back((uint8_t)(page >> 8));
    return b;
}

// Actions: 0 list, 1 host private, 2 host public, 3 join (code), 4 poll, 5 ready, 6 start, 7 leave, 8 new code, 9 rename.
static void request(int action, const std::string &code = "", bool ready = false, int page = 0, const char *rename = nullptr) {
    bool own = action != 0 && action != 4;
    if (g_busy.load() && own) {  // (a press during a poll: sent right after it - laonline_lobby_frame)
        std::lock_guard<std::mutex> lock(g_lock);
        g_pending = {true, action, code, ready, page, rename ? rename : "", rename != nullptr};
        g_acting = true;
        return;
    }
    if (g_busy.exchange(true)) return;
    if (own) g_acting = true;
    std::string token;
    unsigned epoch;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        token = g_member.token, epoch = g_generation;
    }
    std::string name = rename ? rename : g_name[0] ? g_name : "Player";
    if (action == 3 && token.empty()) token = rejoin_token(code, name);
    std::string title = g_title, search = g_search, address = g_endpoint;
    int filter = g_filter_mode, sort = g_sort;
    // (the tunic and hair colours, for the players' portraits: the Cosmetics page's)
    std::vector<uint8_t> colours;
    put_rgb(colours, &g_settings.tunic, COL_SLOT_TUNIC, 0x3fa34d);
    put_rgb(colours, &g_settings.hair, COL_SLOT_HAIR, 0xffdf57);
    std::thread([=]() {
        std::string world, seed;
        seed_describe(world, seed);
        std::vector<uint8_t> b = session_body(action, code, token, name, world, seed, title, ready, colours, search, filter, sort, page);
        Directory d;
        try { d = decode(exchange(address, b, 0x30)); }
        catch (const std::exception &e) { d.message = e.what(); }
        std::lock_guard<std::mutex> lock(g_lock);
        if (epoch == g_generation) {
            if (action == 0) g_listing = d;
            else if (action == 7) { g_member = {}; g_member.message = d.success ? "Left the session." : d.message; rejoin_save("", "", ""); }
            else if (d.success) {
                if (action == 9) d.self_name = name;
                else if (!g_member.self_name.empty()) d.self_name = g_member.self_name;
                g_member = d;
                if (action != 4 && !d.rooms.empty()) rejoin_save(d.rooms[0].code, d.token, name);
            } else if (action == 4 && g_member.success) {  // (one failed poll: the room stays, the next poll retries)
            } else g_member.message = d.message, g_member.success = false;
        }
        if (own) g_acting = false;
        g_busy = false;
    }).detach();
}

static std::string my_name(const Directory &d) { return !d.self_name.empty() ? d.self_name : g_name[0] ? g_name : "Player"; }

// ---------------------------------------------------------------- this PC's other players' places in the room
// LA-Online's relay lets a player into a session room's lobby only if he is one of the room's members (by name:
// server.py's Directory.admit), and a room has 4 places. So each of this PC's other players who is in the game
// (coop_joined: players 2..8) takes a place of his own: he joins the room by its code with his online name
// (laonline_local_name - his relay connection's) and his colour, is Ready when Player 1 is, keeps his place (a poll
// every 5 s: the relay takes Ready from a member it hasn't seen for 10 s, and the place after 2 minutes) and gives it
// back when he leaves the game or Player 1 leaves the room. His relay connection (laonline_local.c) opens only once he
// has it (laonline_lobby_seat_ok). A full room: he plays on this PC only (a note on the page). His place's token is
// kept in laonline-rejoin-local.txt (a game started again takes it back, as Player 1's laonline-rejoin.txt).
extern "C" uint32_t coop_joined(void);           // hle/coop.c
extern "C" uint32_t coop_colour_rgb(int player);
extern "C" void laonline_local_name(const char *first, int i, char out[17]);  // hle/laonline_local.c
struct Seat { std::string token, code, name, note; bool busy = false; uint64_t at = 0, poll_at = 0; };
static Seat g_seat[8];  // (g_lock) players 2..8: 1..7
static std::atomic<bool> g_session_on(false);  // online play started in a room (g_launched, for laonline_local.c)

static void seats_save(void) {  // (g_lock held) "<code> <token> <name>" per place
    std::ofstream f("laonline-rejoin-local.txt", std::ios::trunc);
    for (int i = 1; i < 8; i++)
        if (f && !g_seat[i].token.empty()) f << g_seat[i].code << " " << g_seat[i].token << " " << g_seat[i].name << "\n";
}
static std::string seat_rejoin_token(const std::string &code, const std::string &name) {
    std::ifstream f("laonline-rejoin-local.txt");
    std::string line;
    while (f && std::getline(f, line)) {
        size_t a = line.find(' '), b = a == std::string::npos ? a : line.find(' ', a + 1);
        if (b != std::string::npos && line.compare(0, a, code) == 0 && b - a - 1 == 32 && line.substr(b + 1) == name)
            return line.substr(a + 1, 32);
    }
    return "";
}

static void seat_request(int i, int action, const std::string &code, const std::string &token, const std::string &name, bool ready = false) {
    { std::lock_guard<std::mutex> lock(g_lock); g_seat[i].busy = true; }
    uint32_t rgb = coop_colour_rgb(i);
    std::vector<uint8_t> colours = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb};
    put_rgb(colours, &g_settings.hair, COL_SLOT_HAIR, 0xffdf57);
    std::string address = g_endpoint;
    std::thread([=]() {
        std::string world, seed;
        seed_describe(world, seed);
        std::vector<uint8_t> b = session_body(action, code, token, name, world, seed, "", ready, colours, "", 0, 0, 0);
        Directory d;
        bool answered = false;
        try { d = decode(exchange(address, b, 0x30)), answered = true; }
        catch (const std::exception &e) { d.message = e.what(); }
        std::lock_guard<std::mutex> lock(g_lock);
        Seat &s = g_seat[i];
        s.busy = false;
        if (action == 7) {
            if (s.token == token) s.token.clear(), s.code.clear();
            LOG("[lobby] player %d left session %s\n", i + 1, code.c_str());
        } else if (d.success) {
            if (action == 3) {
                s.token = d.token, s.code = d.rooms.empty() ? code : d.rooms[0].code, s.name = name, s.note.clear();
                LOG("[lobby] player %d has a place in session %s as '%s'\n", i + 1, s.code.c_str(), name.c_str());
            }
            if (action == 5) s.at = now_ms() + 2500;  // (Player 1's next poll shows it)
        } else if (answered && action == 3) {  // (the relay said no: the room is full, say)
            if (s.note != d.message) LOG("[lobby] player %d gets no place in session %s: %s\n", i + 1, code.c_str(), d.message.c_str());
            s.note = d.message, s.at = now_ms() + 10000;
        } else if (answered && action == 4) {  // (a poll refused: his place is gone - he joins again)
            LOG("[lobby] player %d lost his place in session %s: %s\n", i + 1, code.c_str(), d.message.c_str());
            s.token.clear(), s.code.clear();
        } else s.at = now_ms() + (answered ? 2500 : 3000);  // (Ready refused - the room just started, say - or no
                                                            // answer: Player 1's next poll shows the room; tried again)
        seats_save();
    }).detach();
}

// Each frame (laonline_lobby_frame): what each of this PC's other players' places needs next - one request at a time each.
static void seats_tick(const Directory &d, bool in_room) {
    uint32_t joined = coop_joined();
    std::string me = my_name(d);
    const Room *room = in_room ? &d.rooms[0] : nullptr;
    bool ready = false;
    if (room)
        for (auto &m : room->players) if (m.name == me) ready = m.ready;
    uint64_t now = now_ms();
    for (int i = 1; i < 8; i++) {
        Seat s;
        { std::lock_guard<std::mutex> lock(g_lock); s = g_seat[i]; }
        if (s.busy) continue;
        char name[17];
        laonline_local_name(me.c_str(), i, name);
        bool wanted = room && (joined >> i & 1);
        if (!s.token.empty() && (!wanted || s.code != room->code || s.name != name)) { seat_request(i, 7, s.code, s.token, s.name); continue; }
        if (!wanted) {
            if (!s.note.empty()) { std::lock_guard<std::mutex> lock(g_lock); g_seat[i].note.clear(), g_seat[i].at = 0; }
            continue;
        }
        if (now < s.at) continue;
        if (s.token.empty()) { seat_request(i, 3, room->code, seat_rejoin_token(room->code, name), name); continue; }
        const Member *m = nullptr;
        for (auto &x : room->players) if (x.name == s.name) m = &x;
        if (!room->started && m && m->ready != ready) { seat_request(i, 5, s.code, s.token, s.name, ready); continue; }
        if (now >= s.poll_at) {
            { std::lock_guard<std::mutex> lock(g_lock); g_seat[i].poll_at = now + 5000; }
            seat_request(i, 4, s.code, s.token, s.name);
        }
    }
}

// The room's members from other PCs (not this player, nor this PC's other players): the host starts once there is one.
static int others_in(const Room &room, const std::string &me) {
    int n = 0;
    for (auto &m : room.players) {
        bool ours = m.name == me;
        for (int i = 1; i < 8 && !ours; i++) {
            char name[17];
            laonline_local_name(me.c_str(), i, name);
            ours = m.name == name;
        }
        n += !ours;
    }
    return n;
}

// (hle/laonline_local.c, its network thread) May local player i (1..7) go on the relay? In a session room's lobby only
// with his place in the room; anywhere else (Global, laonline.ini's lobby) anyone may.
extern "C" int laonline_lobby_seat_ok(int i) {
    if (!g_session_on.load()) return 1;
    std::lock_guard<std::mutex> lock(g_lock);
    return i > 0 && i < 8 && !g_seat[i].token.empty();
}

// A room being left: no longer this player's from the moment he leaves - not only once the relay's answer comes, a
// few frames later: in between, the room still listed as started (and this player not in it any more) began the
// session again, so he stayed online in its lobby (online_flows.sh).
static std::string g_leaving;  // (render thread) its code, until the answer clears it
static bool in_room_of(const Directory &d) { return !d.token.empty() && !d.rooms.empty() && d.rooms[0].code != g_leaving; }

// Leaving the room ends online play in it too.
static void leave(const Directory &d) {
    if (!d.rooms.empty()) g_leaving = d.rooms[0].code, request(7, d.rooms[0].code);
    if (g_launched) {
        laonline_launch_mode(0);
        laonline_session_override(nullptr, nullptr, nullptr);
        g_launched = false, g_session_on = false;
        LOG("[lobby] left session %s: online play off\n", g_launched_room.c_str());
    }
    g_note.clear();
}

// The room started: online play in its lobby (a Co-op Randomizer room: only with the room's own seed played here).
static void try_launch(const Directory &d, const Room &room) {
    std::string lobby = d.lobby;
    if (room.mode == 2) {
        std::string world, seed;  // (worked out by the room's polls, on their thread)
        if (!seed_cached(world, seed) || world != room.world || seed != room.seed) {
            g_note = "Getting the host's randomizer seed: the game switches to it when it is here.";
            return;
        }
        if (lobby.size() > 6) lobby.resize(lobby.size() - 6);  // (".rando": laonline_relay_login adds it back)
    }
    std::string name = my_name(d);
    laonline_session_override(name.c_str(), lobby.c_str(), d.password.c_str());
    laonline_launch_mode(room.mode);
    g_launched = true, g_session_on = true, g_launched_room = room.code, g_note.clear();
    LOG("[lobby] session %s started: online play as '%s' in lobby %s (%s)\n", room.code.c_str(), name.c_str(),
           lobby.c_str(), room.mode == 2 ? "co-op randomizer" : "co-op");
}

static void global_join(void);
static void board_test(void);
// (test) LAONLINE_TEST_LOBBY=<word>[,<word>...]: what a player would press, once a save is loaded. host | host-public |
// join:<code> | browse: into a session - then Ready, and the host starts it once every player (2 or more) is ready;
// global: Join Global; board: a run submitted, then the leaderboard read; then-global: 15 s after the session began,
// Join Global (leaving the session). tools/coop_tests/online_lobby.sh, online_global.sh, online_flows.sh. The same
// requests the page's buttons make.
static const char *plan_word(const char *plan, const char *word) {  // (the word in the list, or its "<word>:" form)
    size_t n = strlen(word);
    for (const char *p = plan; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL)
        if (!strncmp(p, word, n) && (p[n] == ',' || p[n] == 0 || (word[n - 1] == ':'))) return p + n;
    return NULL;
}
static void test_drive(const Directory &d) {
    static const char *plan = getenv("LAONLINE_TEST_LOBBY");
    static uint64_t at, began_at;
    bool in_room = in_room_of(d);
    // (a save loaded before the first press; in a room Ready and Start go on from the title too - a joiner switched
    // to the host's seed is back at its title, as a player would be, who then presses Ready there or in his save)
    if (!plan || (!in_room && !game_in_play()) || g_busy.load() || now_ms() < at) return;
    at = now_ms() + 1500;
    if (plan_word(plan, "global") || plan_word(plan, "board")) {  // (either, or both: "global,board")
        if (plan_word(plan, "global") && laonline_launch_choice() != 3) global_join();
        if (plan_word(plan, "board")) board_test();
        return;
    }
    if (laonline_launch_choice() == 3) return;  // (in Global: done)
    if (plan_word(plan, "then-global") && g_launched) {
        if (!began_at) began_at = now_ms();
        else if (now_ms() - began_at > 15000) global_join();
        return;
    }
    if (!in_room) {
        const char *code = plan_word(plan, "join:");
        if (plan_word(plan, "host")) request(1);
        else if (plan_word(plan, "host-public")) request(2);
        else if (code) request(3, std::string(code, strcspn(code, ",")));
        else if (plan_word(plan, "browse")) {
            Directory list;
            { std::lock_guard<std::mutex> lock(g_lock); list = g_listing; }
            if (list.rooms.empty()) request(0);
            else request(3, list.rooms[0].code);
        }
        return;
    }
    const Room &room = d.rooms[0];
    std::string me = my_name(d);
    bool host = false, ready = false, all = true;
    for (auto &m : room.players) {
        if (m.name == me) host = m.host, ready = m.ready;
        all &= m.ready;
    }
    if (room.started) return;
    if (!ready) request(5, room.code, true);
    else if (host && all && others_in(room, me) >= 1) request(6, room.code);
}

// ---------------------------------------------------------------- a Co-op Randomizer room's seed (his V13 / V15.2)
// Everyone in a Co-op Randomizer room plays the host's seed. The host's game uploads it to the relay while he hosts the
// room (15 s after the room is known, then every minute for 10 minutes, then every 5 - his keepalive: an upload lost
// on the relay comes back); a joiner without it downloads it (every 20 s until it is there, 45 tries) and the game
// switches to it - rando_select + the in-place reboot, as the Randomizer page switches seeds; the room stays joined
// (this page's state lives through the reboot) and the session begins once the seed matches. The transfer is
// runtime/laonline_seed_transfer.cpp (his tools/rando/lao_seed_transfer.py's, native: LAS Recomp has no Python seed
// helper any more): the seed folder's own files - its record and code patches, no game files - checked against the
// room's world checksum when they arrive; an exact world already on this PC is reused. One job at a time, on its own
// thread.
static std::atomic<bool> g_seed_busy(false);
static std::string g_seed_status, g_seed_ready;  // (g_lock) what the job last said; a downloaded seed's folder
static void seed_job(bool upload, const std::string &seed, const Directory &d) {  // upload: rando/<seed>; else the room's
    if (g_seed_busy.exchange(true)) return;
    std::string server = g_endpoint, name = my_name(d), lobby = d.lobby, password = d.password;
    LOG("[lobby] seed %s: %s\n", upload ? "upload" : "download", upload ? seed.c_str() : "the host's");
    std::thread([=]() {
        std::string status, ready;
        bool ok = upload ? lao_seed_publish(server, name, lobby, password, "rando/" + seed, status)
                         : lao_seed_sync(server, name, lobby, password, "rando", ready, status);
        if (ok && !upload && (ready.find_first_of("/\:") != std::string::npos || !rando_seed_there(ready.c_str()))) ready.clear();
        LOG("[lobby] seed %s %s: %s\n", upload ? "upload" : "download", ok ? "done" : "failed", status.c_str());
        std::lock_guard<std::mutex> lock(g_lock);
        g_seed_status = status;
        if (!ready.empty()) g_seed_ready = ready;
        g_seed_busy = false;
    }).detach();
}
static void seed_tick(const Directory &d, const Room &room) {
    static std::string room_key;
    static uint64_t next_at;
    static int tries;
    if (room.mode != 2) { room_key.clear(); return; }
    std::string me = my_name(d);
    bool host = false;
    for (auto &m : room.players) if (m.name == me) host = m.host;
    std::string world, seed;
    bool have = seed_cached(world, seed) && world == room.world && seed == room.seed;
    if (room_key != room.code + room.world) room_key = room.code + room.world, tries = 0, next_at = now_ms() + (host ? 15000 : 0);
    if (host) {  // (his keepalive: the room's seed is the host's own, the one he plays)
        if (!have || now_ms() < next_at || g_seed_busy.load()) return;
        next_at = now_ms() + (tries++ < 10 ? 60000 : 300000);
        const char *dir = rando_seed_dir();
        seed_job(true, std::filesystem::path(dir).filename().string(), d);
        return;
    }
    if (have) return;
    std::string ready;
    { std::lock_guard<std::mutex> lock(g_lock); ready = g_seed_ready; g_seed_ready.clear(); }
    if (!ready.empty()) {  // the host's seed is here: the game switches to it
        LOG("[lobby] the host's seed is here (%s): switching to it\n", ready.c_str());
        rando_select(ready.c_str());
        rt_request_reboot(RT_REBOOT_GAME);
        return;
    }
    if (now_ms() < next_at || g_seed_busy.load() || tries >= 45) return;
    next_at = now_ms() + 20000, tries++;
    seed_job(false, "", d);
}

// Each frame the game draws (menu.mm menu_render): a press made during a poll, the room's poll every 2 s, and the
// session's start - with the menu open or not.
extern "C" void laonline_lobby_frame(void) {
    if (!laonline_configured()) return;
    init_once();
    Directory d;
    Pending run;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        d = g_member;
        if (!g_busy.load() && g_pending.set) run = g_pending, g_pending = Pending();
    }
    if (run.set) {
        g_acting = false;
        request(run.action, run.code, run.ready, run.page, run.named ? run.name.c_str() : nullptr);
    }
    if (!g_leaving.empty() && (d.token.empty() || d.rooms.empty() || d.rooms[0].code != g_leaving)) g_leaving.clear();
    test_drive(d);
    bool in_room = in_room_of(d);
    seats_tick(d, in_room);
    if (!in_room) return;
    const Room &room = d.rooms[0];
    {   // (the test's log: each change of the room)
        static std::string last;
        char line[160];
        snprintf(line, sizeof line, "%s %d %d", room.code.c_str(), (int)room.players.size(), room.started);
        if (last != line) {
            last = line;
            LOG("[lobby] in session %s: %d player(s)%s\n", room.code.c_str(), (int)room.players.size(),
                   room.started ? ", started" : "");
        }
    }
    if (!g_busy.load() && now_ms() >= g_poll_at) g_poll_at = now_ms() + 2000, request(4, room.code);
    if (room.started && d.success && !(g_launched && g_launched_room == room.code)) try_launch(d, room);
    seed_tick(d, room);
}

// ---------------------------------------------------------------- the page
static void portrait(const Member &m, float size) {
    ImTextureID tex = 0;
    float aspect = 1;
    if (laonline_avatar_icon(m.rgb, m.hair, &tex, &aspect)) ImGui::Image(tex, ImVec2(size, size));
    else ImGui::Dummy(ImVec2(size, size));
}

static void room_view(const Directory &d, float s) {
    const Room &room = d.rooms[0];
    std::string me = my_name(d);
    bool busy = g_acting.load(), host = false, ready = false, all = true;
    for (auto &m : room.players) {
        if (m.name == me) host = m.host, ready = m.ready;
        all &= m.ready;
    }
    const char *kind = room.mode == 2 ? (room.public_room ? "Co-op Randomizer: public session" : "Co-op Randomizer: private session")
                                      : (room.public_room ? "Co-op: public session" : "Co-op: private session");
    ImGui::SeparatorText(L(kind));
    if (!room.public_room) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(L("Room code"));
        ImGui::SameLine();
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted(room.code.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        static uint64_t copied_at;
        if (ImGui::Button(L("Copy"))) SDL_SetClipboardText(room.code.c_str()), copied_at = now_ms();
        ImGui::SameLine();
        ImGui::BeginDisabled(busy || !host || room.players.size() != 1 || room.started);
        if (ImGui::Button(L("New code"))) request(8, room.code);
        ImGui::EndDisabled();
        if (now_ms() - copied_at < 3000) ImGui::SameLine(), ImGui::TextDisabled("%s", L("Copied."));
        ImGui::TextDisabled("%s", L("Share the code with your friends to join your session."));
    } else
        ImGui::TextDisabled("%s", L("Listed publicly: anyone with the same version can join."));
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s %s", L("Your name:"), me.c_str());
    ImGui::SameLine();
    static char edited[17];
    ImGui::BeginDisabled(busy || ready || room.started);
    if (ImGui::Button(L("Change"))) snprintf(edited, sizeof edited, "%s", me.c_str()), ImGui::OpenPopup("##rename");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("##rename")) {
        ImGui::InputText(L("Name"), edited, sizeof edited);
        if (ImGui::Button(L("Apply")) && edited[0]) request(9, room.code, false, 0, edited), ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button(L("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SeparatorText(L("Players"));
    for (int i = 0; i < 4; i++) {
        ImGui::PushID(i);
        ImGui::Text("%d", i + 1);
        ImGui::SameLine(0, 12 * s);
        if (i < (int)room.players.size()) {
            const Member &m = room.players[i];
            portrait(m, ImGui::GetFrameHeight());
            ImGui::SameLine(0, 10 * s);
            ImGui::TextUnformatted(m.name.c_str());
            ImGui::SameLine(0, 16 * s);
            ImGui::TextColored(m.ready ? ImVec4(.2f, .75f, .35f, 1) : ImVec4(.55f, .55f, .6f, 1), "%s%s",
                               m.host ? L("Host - ") : "", m.ready ? L("Ready") : L("Not ready"));
        } else
            ImGui::TextDisabled("%s", L("Waiting for a player..."));
        ImGui::PopID();
    }
    for (int i = 1; i < 8; i++) {  // (this PC's other players without a place: the room is full, say)
        std::string note;
        { std::lock_guard<std::mutex> lock(g_lock); note = g_seat[i].note; }
        if (!note.empty()) ImGui::TextDisabled("%s %d %s %s", L("Player"), i + 1, L("plays on this PC only:"), note.c_str());
    }
    ImGui::Spacing();

    if (room.started) {
        if (g_launched && g_launched_room == room.code) ImGui::TextColored(ImVec4(.2f, .75f, .35f, 1), "%s", L("Playing online in this session."));
        else if (!g_note.empty()) ImGui::TextWrapped("%s", L(g_note.c_str()));
        else ImGui::TextUnformatted(L("Joining the session..."));
    } else {
        ImGui::BeginDisabled(busy || !d.success);
        if (ImGui::Button(ready ? L("Cancel Ready") : L("Ready"), ImVec2(220 * s, 0))) request(5, room.code, !ready);
        ImGui::EndDisabled();
        if (host) {
            ImGui::SameLine();
            int others = others_in(room, me);  // (this PC's own players don't count: another PC's)
            ImGui::BeginDisabled(busy || !all || others < 1 || !d.success);
            if (ImGui::Button(L("Start the session"), ImVec2(220 * s, 0))) request(6, room.code);
            ImGui::EndDisabled();
            ImGui::TextDisabled("%s", others < 1 ? L("Waiting for another player to join...")
                                      : !all ? L("Waiting for every player to be ready.")
                                             : L("Everyone is ready: start the session."));
        } else
            ImGui::TextDisabled("%s", ready ? L("Ready: waiting for the host to start.") : L("Press Ready when you are ready to play."));
    }
    if (room.mode == 2) {  // (the room's seed: the host's upload, a joiner's download)
        std::string st;
        { std::lock_guard<std::mutex> lock(g_lock); st = g_seed_status; }
        if (g_seed_busy.load()) ImGui::TextDisabled("%s", host ? L("Sending your seed to the relay...") : L("Getting the host's seed..."));
        else if (!st.empty()) ImGui::TextDisabled("%s", st.c_str());
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(L("Leave the session"))) leave(d);
    ImGui::EndDisabled();
    if (!d.success && !d.message.empty()) ImGui::TextWrapped("%s", d.message.c_str());
}

static void browser_view(float s) {
    Directory list;
    { std::lock_guard<std::mutex> lock(g_lock); list = g_listing; }
    bool busy = g_busy.load();
    if (ImGui::Button(L("Back"))) g_browsing = false;
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(busy ? L("Refreshing...") : L("Refresh"))) request(0, "", false, g_list_page), g_list_at = now_ms();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260 * s);
    static bool search_dirty;
    static uint64_t search_at;
    if (ImGui::InputTextWithHint("##search", L("Search by host name..."), g_search, sizeof g_search)) search_dirty = true, search_at = now_ms();
    static const char *modes[] = {"All modes", "Co-op", "Co-op Randomizer"}, *sorts[] = {"Players (most first)", "Players (fewest first)", "Newest", "Session name"};
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::BeginCombo("##mode", L(modes[g_filter_mode]))) {
        for (int i = 0; i < 3; i++) if (ImGui::Selectable(L(modes[i]), g_filter_mode == i)) g_filter_mode = i, g_list_page = 0, request(0);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220 * s);
    if (ImGui::BeginCombo("##sort", L(sorts[g_sort]))) {
        for (int i = 0; i < 4; i++) if (ImGui::Selectable(L(sorts[i]), g_sort == i)) g_sort = i, g_list_page = 0, request(0);
        ImGui::EndCombo();
    }
    std::string world, seed;
    seed_cached(world, seed);
    if (list.rooms.empty())
        ImGui::TextWrapped("%s", busy && !list.success ? L("Contacting the relay...")
                                 : list.success      ? L("No public sessions match.") : list.message.c_str());
    else if (ImGui::BeginTable("##rooms", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(L("Session"), 0, 3);
        ImGui::TableSetupColumn(L("Host"), 0, 2);
        ImGui::TableSetupColumn(L("Players"), 0, 1);
        ImGui::TableSetupColumn(L("Mode"), 0, 2);
        ImGui::TableSetupColumn("", 0, 1.2f);
        ImGui::TableHeadersRow();
        for (auto &room : list.rooms) {
            ImGui::PushID(room.code.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(room.title.empty() ? room.code.c_str() : room.title.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(room.host.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d/4%s", (int)room.players.size(), room.started ? L(", playing") : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(room.mode == 2 ? L("Co-op Randomizer") : L("Co-op"));
            ImGui::TableNextColumn();
            bool same = room.build == LAO_ROOM_BUILD, full = room.players.size() >= 4;
            bool seed_ok = room.mode != 2 || (room.world == world && room.seed == seed);
            ImGui::BeginDisabled(busy || !same || full);
            if (ImGui::Button(L("Join"), ImVec2(-1, 0))) request(3, room.code), g_browsing = false;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", full ? L("All four places are taken.")
                                        : !same ? L("This session needs another version of LA-Online.")
                                        : !seed_ok ? L("This session plays another randomizer seed: the host's comes to you when you join.")
                                                   : L("Join this session."));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    int pages = std::max(1, (list.total + 7) / 8);
    ImGui::BeginDisabled(busy || list.page == 0);
    if (ImGui::Button(L("Previous"))) request(0, "", false, g_list_page = list.page - 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("%d / %d", list.page + 1, pages);
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || list.page + 1 >= pages);
    if (ImGui::Button(L("Next"))) request(0, "", false, g_list_page = list.page + 1);
    ImGui::EndDisabled();
    // (his: the search sent 0.4 s after the last key, the list refreshed every 10 s)
    if (!busy && search_dirty && now_ms() - search_at > 400) search_dirty = false, g_list_page = 0, request(0), g_list_at = now_ms();
    else if (!busy && now_ms() - g_list_at > 10000) g_list_at = now_ms(), request(0, "", false, g_list_page);
}

static void start_view(const Directory &d, float s) {
    bool busy = g_busy.load();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L("Your name"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260 * s);
    ImGui::InputTextWithHint("##name", L("The name the others see"), g_name, sizeof g_name);
    bool rando = rando_seed_dir() != nullptr;
    ImGui::TextDisabled("%s", rando ? L("A randomizer seed is being played: you host a Co-op Randomizer session, on this seed.")
                                    : L("You host a Co-op session (story progress, items and keys shared)."));
    ImGui::SeparatorText(L("Host"));
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(L("Host a private session"), ImVec2(320 * s, 0))) request(1);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", L("With a room code to give your friends."));
    ImGui::SetNextItemWidth(320 * s);
    ImGui::InputTextWithHint("##title", L("Public session name (optional)"), g_title, sizeof g_title);
    ImGui::SameLine();
    if (ImGui::Button(L("Host a public session"))) request(2);
    ImGui::SeparatorText(L("Join"));
    ImGui::SetNextItemWidth(160 * s);
    bool enter = ImGui::InputTextWithHint("##code", L("Room code"), g_code, sizeof g_code,
                                          ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank |
                                              ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button(L("Join by code")) || enter) && g_code[0]) request(3, g_code);
    if (ImGui::Button(L("Browse public sessions"), ImVec2(320 * s, 0))) g_browsing = true, g_list_page = 0, request(0), g_list_at = now_ms();
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (busy) ImGui::TextUnformatted(L("Contacting the relay..."));
    else if (!d.message.empty()) ImGui::TextWrapped("%s", L(d.message.c_str()));
    int choice = laonline_launch_choice();
    if ((choice == 1 || choice == 2) && !g_launched)
        ImGui::TextDisabled("%s", L("Online play is on in the lobby laonline.ini names. A session picked here takes its place."));
}

// ---------------------------------------------------------------- Global (relay opcode 0x33: how many are in it)
// Global (V15.2's start screen's Play Online Global): everyone playing online at once, each in his own world - his
// own save, his own seed (Global Randomizer while a seed is played) - seen together, with PvP and the kill feed; no
// shared progress. Online play's launch mode 3 (laonline.c: HELLO 0x31, the Global lobby); this PC's other local
// players join it too (laonline_local.c).
extern "C" void laonline_v12_global_variant(int randomizer);
static std::atomic<bool> g_global_busy(false);
static int g_global_count = -1;  // (g_lock) -1: not known
static bool g_global_failed;
static uint64_t g_global_at;
static void global_poll(void) {  // the count, every 2 s while the Global tab is shown (his)
    if (now_ms() < g_global_at || g_global_busy.exchange(true)) return;
    g_global_at = now_ms() + 2000;
    std::string address = g_endpoint;
    std::thread([address]() {
        int count = -1;
        try {
            auto d = exchange(address, {1}, 0x33);
            Wire r{d};
            if (r.byte() != 1) throw std::runtime_error("Global unavailable");
            count = r.word();
            int capacity = r.word();
            if (count > capacity || capacity != 100 || r.i != d.size()) throw std::runtime_error("Invalid Global status");
        } catch (const std::exception &) { count = -1; }
        { std::lock_guard<std::mutex> lock(g_lock); g_global_count = count, g_global_failed = count < 0; }
        g_global_busy = false;
    }).detach();
}
static void global_join(void) {
    Directory d;
    { std::lock_guard<std::mutex> lock(g_lock); d = g_member; }
    if (!d.token.empty()) leave(d);  // (a session this player is in: left first)
    int rando = rando_seed_dir() != nullptr;
    std::string name = g_name[0] ? g_name : "Player";
    laonline_session_override(name.c_str(), "", "");  // (the name; Global's lobby is its own, with no password)
    laonline_v12_global_variant(rando);
    laonline_launch_mode(3);
    g_launched = false, g_session_on = false, g_note.clear();
    LOG("[lobby] Global%s: online play as '%s'\n", rando ? " Randomizer" : "", name.c_str());
}
static void global_leave(void) {
    laonline_launch_mode(0);
    laonline_session_override(nullptr, nullptr, nullptr);
    LOG("[lobby] left Global: online play off\n");
}
static void global_view(float s) {
    global_poll();
    int count; bool failed;
    { std::lock_guard<std::mutex> lock(g_lock); count = g_global_count, failed = g_global_failed; }
    ImGui::TextWrapped("%s", L("Everyone playing online, each in their own world - your own save, and your own seed while you play one - "
                               "seen together, with PvP and the kill feed. Nothing is shared between worlds."));
    if (count >= 0) ImGui::Text(count == 1 ? L("%d player in Global") : L("%d players in Global"), count);
    else ImGui::TextDisabled("%s", failed ? L("Global player count unavailable") : L("Checking Global player count..."));
    ImGui::Spacing();
    bool rando = rando_seed_dir() != nullptr;
    if (laonline_launch_choice() == 3) {
        ImGui::TextColored(ImVec4(.2f, .75f, .35f, 1), "%s", L("You are in Global."));
        if (ImGui::Button(L("Leave Global"), ImVec2(320 * s, 0))) global_leave();
        return;
    }
    if (ImGui::Button(rando ? L("Join Global Randomizer (your seed)") : L("Join Global"), ImVec2(320 * s, 0))) global_join();
    Directory d;
    { std::lock_guard<std::mutex> lock(g_lock); d = g_member; }
    if (!d.token.empty()) ImGui::TextDisabled("%s", L("Joining Global leaves your session."));
}

// ---------------------------------------------------------------- Leaderboards (relay opcode 0x32)
// V12.5's leaderboards: completed runs the players reported (not checked by anyone), ranked by the relay. 50 a page,
// filters by mode and player count; an older (V12) relay answers 8 a page without them (his fallback, kept).
// Score = 100,000 - clear time in seconds - 250 per death + rupees (the relay's; at least 0).
struct LeaderRun { std::string players; unsigned mode, count, seconds, rupees, deaths, score; };
static std::mutex g_board_lock;
static std::atomic<bool> g_board_busy(false);
static std::vector<LeaderRun> g_board;
static std::string g_board_status;
static int g_board_total, g_board_page, g_board_mode = 3, g_board_sort, g_board_count, g_board_page_size = 50;
static bool g_board_loaded, g_board_legacy;
static unsigned board_score(unsigned seconds, unsigned deaths, unsigned rupees) {
    long long v = 100000LL - seconds - 250LL * deaths + rupees;
    return v < 0 ? 0 : (unsigned)v;
}
static void board_request(bool submit, const std::string &players = "", int mode = 0, int seconds = 0, int rupees = 0, int deaths = 0) {
    if (g_board_busy.exchange(true)) return;
    auto word = [](std::vector<uint8_t> &b, unsigned v, int n) { for (int i = 0; i < n; i++) b.push_back((uint8_t)(v >> (i * 8))); };
    std::vector<uint8_t> b = {1, (uint8_t)(submit ? 1 : 0)}, v2 = {2, 0}, v1 = {1, 0};
    if (submit) {
        char id[65];
        snprintf(id, sizeof id, "run-%llu-%u", (unsigned long long)SDL_GetTicksNS(), (unsigned)SDL_rand(2147483647));
        put_str(b, id), put_str(b, players);
        b.push_back((uint8_t)mode);
        word(b, (unsigned)seconds, 4), word(b, (unsigned)rupees, 4), word(b, (unsigned)deaths, 4);
        b.push_back(1);
    } else {
        v2.push_back((uint8_t)g_board_mode), v2.push_back((uint8_t)g_board_sort), v2.push_back((uint8_t)g_board_count);
        word(v2, (unsigned)g_board_page, 2);
        v1.push_back((uint8_t)g_board_mode), v1.push_back((uint8_t)(g_board_sort == 2 ? 1 : 0));  // (V12: time / rupees)
        word(v1, (unsigned)g_board_page, 2);
    }
    std::string address = g_endpoint;
    std::thread([b, v1, v2, submit, address]() {
        std::vector<LeaderRun> rows;
        std::string message;
        int total = 0;
        bool legacy = false;
        auto number = [](Wire &r) { unsigned v = 0; for (int i = 0; i < 4; i++) v |= (unsigned)r.byte() << (i * 8); return v; };
        try {
            if (submit) {
                auto d = exchange(address, b, 0x32);
                Wire r{d};
                bool ok = r.byte() == 1;
                message = r.string();
                if (ok) message = "Run submitted. " + message;
            } else {
                auto d = exchange(address, v2, 0x32);
                Wire r{d};
                bool ok = r.byte() == 1;
                message = r.string();
                if (ok) {
                    total = r.word();
                    int count = r.byte();
                    if (count > 50) throw std::runtime_error("Invalid ranking count.");
                    for (int i = 0; i < count; i++) {
                        LeaderRun run;
                        run.players = r.string(), run.mode = r.byte(), run.count = r.byte();
                        run.seconds = number(r), run.rupees = number(r), run.deaths = number(r), run.score = number(r);
                        rows.push_back(run);
                    }
                    if (r.i != d.size()) throw std::runtime_error("Incomplete ranking response.");
                } else {  // (an older relay: its 8-a-page answer)
                    legacy = true;
                    auto o = exchange(address, v1, 0x32);
                    Wire w{o};
                    ok = w.byte() == 1;
                    message = w.string();
                    if (ok) {
                        total = w.word();
                        int count = w.byte();
                        if (count > 8) throw std::runtime_error("Invalid ranking count.");
                        for (int i = 0; i < count; i++) {
                            LeaderRun run;
                            run.players = w.string(), run.mode = w.byte();
                            run.seconds = number(w), run.rupees = number(w), run.deaths = number(w);
                            w.byte();
                            run.count = 1;
                            for (char ch : run.players) run.count += ch == ',';
                            if (run.count > 4) run.count = 4;
                            run.score = board_score(run.seconds, run.deaths, run.rupees);
                            rows.push_back(run);
                        }
                        if (w.i != o.size()) throw std::runtime_error("Incomplete ranking response.");
                        message = "This relay is older than V12.5: 8 runs a page, no player-count filter.";
                    }
                }
            }
        } catch (const std::exception &e) { message = e.what(); }
        std::lock_guard<std::mutex> lock(g_board_lock);
        if (!submit) g_board = rows, g_board_total = total, g_board_legacy = legacy, g_board_page_size = legacy ? 8 : 50, g_board_loaded = true;
        g_board_status = message;
        if (!submit) LOG("[lobby] leaderboard: %d run(s)%s%s\n", total, rows.empty() ? "" : ", first: ", rows.empty() ? "" : rows[0].players.c_str());
        else LOG("[lobby] leaderboard: %s\n", message.c_str());
        g_board_busy = false;
    }).detach();
}
static void board_test(void) {  // (LAONLINE_TEST_LOBBY=board: one run submitted, then the board read)
    static int step;
    if (g_board_busy.load()) return;
    if (step == 0) { step = 1; board_request(true, g_name[0] ? g_name : "Player", 1, 3725, 120, 2); }
    else if (step == 1) { step = 2; g_board_page = 0; board_request(false); }
}
static std::string clock_text(unsigned s) { char t[24]; snprintf(t, sizeof t, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60); return t; }
static void board_view(float s) {
    static bool asked;
    if (!asked) asked = true, board_request(false);
    bool busy = g_board_busy.load();
    std::vector<LeaderRun> rows; std::string status; int total, size; bool loaded;
    { std::lock_guard<std::mutex> lock(g_board_lock); rows = g_board, status = g_board_status, total = g_board_total, size = g_board_page_size, loaded = g_board_loaded; }
    static const char *sorts[] = {"Best time", "Highest score", "Most rupees", "Most deaths"};
    static const char *modes[] = {"Vanilla", "Co-op", "Co-op Randomizer", "All modes"};  // (the relay's: 0, 1, 2; 3 all)
    static const char *counts[] = {"Any number of players", "1 player", "2 players", "3 players", "4 players"};
    bool changed = false;
    auto widest = [](const char *const *names, int n) {  // (a combo as wide as its longest choice, and its arrow)
        float w = 0;
        for (int i = 0; i < n; i++) w = fmaxf(w, ImGui::CalcTextSize(L(names[i])).x);
        return w + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight();
    };
    ImGui::BeginDisabled(busy);
    ImGui::SetNextItemWidth(widest(sorts, 4));
    if (ImGui::BeginCombo("##sort", L(sorts[g_board_sort]))) {
        for (int i = 0; i < 4; i++) if (ImGui::Selectable(L(sorts[i]), g_board_sort == i)) changed |= g_board_sort != i, g_board_sort = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(widest(modes, 4));
    if (ImGui::BeginCombo("##mode", L(modes[g_board_mode]))) {
        for (int i : {3, 0, 1, 2}) if (ImGui::Selectable(L(modes[i]), g_board_mode == i)) changed |= g_board_mode != i, g_board_mode = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(widest(counts, 5));
    if (ImGui::BeginCombo("##count", L(counts[g_board_count]))) {
        for (int i = 0; i < 5; i++) if (ImGui::Selectable(L(counts[i]), g_board_count == i)) changed |= g_board_count != i, g_board_count = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Refresh"))) changed = true;
    ImGui::SameLine();
    if (ImGui::Button(L("Submit a run..."))) ImGui::OpenPopup("##submit");
    ImGui::EndDisabled();
    if (changed) g_board_page = 0, board_request(false);
    if (rows.empty()) ImGui::TextWrapped("%s", busy || !loaded ? L("Loading completed runs...") : L("No completed runs match these filters yet."));
    else if (ImGui::BeginTable("##board", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", 0, .5f);
        ImGui::TableSetupColumn(L("Players"), 0, 3);
        ImGui::TableSetupColumn(L("Mode"), 0, 2);
        ImGui::TableSetupColumn(L("Clear time"), 0, 1.4f);
        ImGui::TableSetupColumn(L("Score"), 0, 1.2f);
        ImGui::TableSetupColumn(L("Rupees"), 0, 1);
        ImGui::TableSetupColumn(L("Deaths"), 0, 1);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < rows.size(); i++) {
            const LeaderRun &r = rows[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", g_board_page * size + (int)i + 1);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.players.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(L(modes[r.mode < 3 ? r.mode : 0]));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(clock_text(r.seconds).c_str());
            ImGui::TableNextColumn(); ImGui::Text("%u", r.score);
            ImGui::TableNextColumn(); ImGui::Text("%u", r.rupees);
            ImGui::TableNextColumn(); ImGui::Text("%u", r.deaths);
        }
        ImGui::EndTable();
    }
    int pages = total > 0 ? (total + size - 1) / size : 1;
    if (pages > 100) pages = 100;
    ImGui::BeginDisabled(busy || g_board_page == 0);
    if (ImGui::Button(L("Previous"))) g_board_page--, board_request(false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("%d / %d - %d %s", g_board_page + 1, pages, total, L("runs"));
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || g_board_page + 1 >= pages);
    if (ImGui::Button(L("Next"))) g_board_page++, board_request(false);
    ImGui::EndDisabled();
    if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
    // a completed run, reported by the player (his: "player-reported, not independently verified")
    static char players[81]; static int mode = 0, h = 1, m = 0, sec = 0, rupees = 0, deaths = 0; static bool honest;
    if (ImGui::BeginPopup("##submit")) {
        if (!players[0]) snprintf(players, sizeof players, "%s", g_name[0] ? g_name : "Player");
        ImGui::TextWrapped("%s", L("Submit your real completed run. Runs are reported by the players, not checked."));
        ImGui::InputText(L("Player(s)"), players, sizeof players);
        ImGui::TextDisabled("%s", L("Co-op players separated by commas."));
        ImGui::Combo(L("Mode"), &mode, "Vanilla\0Co-op\0Co-op Randomizer\0");
        ImGui::SetNextItemWidth(80 * s); ImGui::InputInt(L("h"), &h, 0); ImGui::SameLine();
        ImGui::SetNextItemWidth(80 * s); ImGui::InputInt(L("min"), &m, 0); ImGui::SameLine();
        ImGui::SetNextItemWidth(80 * s); ImGui::InputInt(L("s"), &sec, 0);
        ImGui::InputInt(L("Rupees"), &rupees);
        ImGui::InputInt(L("Deaths"), &deaths);
        ImGui::Checkbox(L("I completed this run and these results are accurate"), &honest);
        int seconds = h * 3600 + m * 60 + sec;
        ImGui::BeginDisabled(!honest || busy || !players[0] || seconds < 1 || h < 0 || m < 0 || sec < 0 || rupees < 0 || deaths < 0);
        if (ImGui::Button(L("Submit"))) board_request(true, players, mode, seconds, rupees, deaths), honest = false, ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(L("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// The page (menu.mm tabs_body, the bar's Online entry).
extern "C" void laonline_lobby_draw(float scale) {
    init_once();
    if (g_endpoint.empty()) {
        ImGui::TextWrapped("%s", L("Online sessions need a relay: set server = <address> in laonline.ini, next to the game."));
        return;
    }
    Directory d;
    { std::lock_guard<std::mutex> lock(g_lock); d = g_member; }
    if (in_room_of(d)) { room_view(d, scale); return; }  // (in a session: its room alone)
    static const char *test_tab = getenv("LAONLINE_TEST_TAB");  // (test: global | board - the page opens on that tab)
    static bool tab_once;
    auto first = [&](const char *t) { return !tab_once && test_tab && !strcmp(test_tab, t) ? ImGuiTabItemFlags_SetSelected : 0; };
    const PlasticTheme &pt = plastic_theme();  // (the bar's and Settings' tab colours)
    ImGui::PushStyleColor(ImGuiCol_Tab, pt.tab);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, pt.tab_hot);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, pt.tab_sel);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, pt.tab_sel);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 8 * scale);
    if (ImGui::BeginTabBar("##online")) {
        if (ImGui::BeginTabItem(L("Sessions"))) {
            if (g_browsing) browser_view(scale);
            else start_view(d, scale);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(L("Global"), NULL, first("global"))) { global_view(scale); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(L("Leaderboards"), NULL, first("board"))) { board_view(scale); ImGui::EndTabItem(); }
        tab_once = true;
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}
