// The menu's Online page: LA-Online's sessions on its relay. Host a private session (a room code to share) or a public
// one, join one by its code or from the public list, mark Ready; the host starts it, and online play then runs in that
// session (laonline_session_override + laonline_launch_mode: the relay lobby, password and name the room gives).
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
#include "laonline_seed_share.h"
extern "C" {
#include "../rt.h"  // (LOG)
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
        int mode = seed_describe(world, seed) ? 2 : 1;  // (a randomizer seed played: a Co-op Randomizer session)
        std::vector<uint8_t> b = {1, (uint8_t)action};
        put_str(b, code), put_str(b, token), put_str(b, name), put_str(b, LAO_ROOM_BUILD);
        b.push_back((uint8_t)mode);
        put_str(b, world), put_str(b, seed), put_str(b, title), put_str(b, "");  // (no description)
        b.push_back(ready);
        b.insert(b.end(), colours.begin(), colours.end());
        put_str(b, search);
        b.push_back((uint8_t)filter), b.push_back((uint8_t)sort), b.push_back((uint8_t)page), b.push_back((uint8_t)(page >> 8));
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

// Leaving the room ends online play in it too.
static void leave(const Directory &d) {
    if (!d.rooms.empty()) request(7, d.rooms[0].code);
    if (g_launched) {
        laonline_launch_mode(0);
        laonline_session_override(nullptr, nullptr, nullptr);
        g_launched = false;
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
            g_note = "This session plays another randomizer seed: play the host's seed (Randomizer page) to join in.";
            return;
        }
        if (lobby.size() > 6) lobby.resize(lobby.size() - 6);  // (".rando": laonline_relay_login adds it back)
    }
    std::string name = my_name(d);
    laonline_session_override(name.c_str(), lobby.c_str(), d.password.c_str());
    laonline_launch_mode(room.mode);
    g_launched = true, g_launched_room = room.code, g_note.clear();
    LOG("[lobby] session %s started: online play as '%s' in lobby %s (%s)\n", room.code.c_str(), name.c_str(),
           lobby.c_str(), room.mode == 2 ? "co-op randomizer" : "co-op");
}

// (test) LAONLINE_TEST_LOBBY=host | host-public | join:<code> | browse: what a player would press, once a save is
// loaded - then Ready, and the host starts the session once every player (2 or more) is ready (tools/coop_tests/
// online_lobby.sh). The same requests the page's buttons make.
static void test_drive(const Directory &d) {
    static const char *plan = getenv("LAONLINE_TEST_LOBBY");
    static uint64_t at;
    if (!plan || !game_in_play() || g_busy.load() || now_ms() < at) return;
    at = now_ms() + 1500;
    bool in_room = !d.token.empty() && !d.rooms.empty();
    if (!in_room) {
        if (!strcmp(plan, "host")) request(1);
        else if (!strcmp(plan, "host-public")) request(2);
        else if (!strncmp(plan, "join:", 5)) request(3, plan + 5);
        else if (!strcmp(plan, "browse")) {
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
    else if (host && all && room.players.size() >= 2) request(6, room.code);
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
    test_drive(d);
    bool in_room = !d.token.empty() && !d.rooms.empty();
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
            ImGui::BeginDisabled(busy || !all || room.players.size() < 2 || !d.success);
            if (ImGui::Button(L("Start the session"), ImVec2(220 * s, 0))) request(6, room.code);
            ImGui::EndDisabled();
            ImGui::TextDisabled("%s", room.players.size() < 2 ? L("Waiting for another player to join...")
                                      : !all ? L("Waiting for every player to be ready.")
                                             : L("Everyone is ready: start the session."));
        } else
            ImGui::TextDisabled("%s", ready ? L("Ready: waiting for the host to start.") : L("Press Ready when you are ready to play."));
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
                                        : !seed_ok ? L("This session plays another randomizer seed: play the host's seed to join in.")
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
    if (laonline_launch_choice() && !g_launched)
        ImGui::TextDisabled("%s", L("Online play is on in the lobby laonline.ini names. A session picked here takes its place."));
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
    if (!d.token.empty() && !d.rooms.empty()) room_view(d, scale);
    else if (g_browsing) browser_view(scale);
    else start_view(d, scale);
}
