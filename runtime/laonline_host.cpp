// LAS Recomp Co-Op's port of LA-Online: a relay hosted by the game (runtime/laonline_host.h). What a relay does is
// LA-Online's (docs/laonline-protocol.md), its rules his relay's, as the stand-in the tests use (tools/laonline_relay.py,
// whose structure this follows - its log lines too, so the same tests run against this one: LAO_NATIVE_RELAY). Lobbies
// by name; ids; the roster; each player's state, hearts, look and map marker passed to the others; shared progress
// (each flag kept, the highest value per item key, both replayed to late joiners); pickups, death notices, chat; the
// co-op seed handshake and its rewards, checks, story and inventory; PvP; the session service (rooms of 4, a name
// once per room, a member not seen for 10 s not Ready and for 2 minutes gone; a room's lobby only for its members,
// once it started); Global and its count; leaderboards (kept while it runs); a Co-op Randomizer room's seed. Everything
// it is sent comes from other PCs: every read is checked (a short or malformed frame drops that connection).
//
// One thread: non-blocking sockets and select(). Also answers the network's search (UDP, the relay's port number:
// "LAOFIND1" -> "LAOHOST1", u16 port, u8 players, str name).
#ifdef _WIN32
#define FD_SETSIZE 512
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
typedef SOCKET HostSocket;
#define HOST_BAD_SOCKET INVALID_SOCKET
static void host_close(HostSocket s) { closesocket(s); }
static bool host_would_block(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static void host_nonblocking(HostSocket s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int HostSocket;
#define HOST_BAD_SOCKET (-1)
static void host_close(HostSocket s) { close(s); }
static bool host_would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
static void host_nonblocking(HostSocket s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

#include "laonline_host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>

#include "cache_sha256.h"

typedef std::string Bytes;  // (bytes on the wire)

namespace {

const uint32_t CAP_REPLIES[] = {0x4c414f51, 0x4c414f53, 0x4c414f55, 0x4c414f57, 0x4c414f59, 0x4c414f5b,
                                0x4c414f5d, 0x4c414f5f, 0x4c414f61, 0x4c414f63, 0x4c414f67, 0x4c414f70};
const uint32_t V12_EXTRAS[] = {0x4c414f72, 0x4c414f73, 0x4c414f74, 0x4c414f75};
const int INV_SIZE = 67;
const size_t SEED_MAX = 64u << 20;   // (a seed package: a few hundred KB)
const size_t CONNECTIONS_MAX = 256;
const size_t OUT_MAX = 8u << 20;     // (a connection that reads nothing: dropped)
const uint16_t SEARCH_PORT = 47620;  // (UDP: the network's search, whatever port a relay is on)

struct Bad : std::runtime_error {
    Bad() : std::runtime_error("malformed") {}
};

// A frame's body, read as the stand-in reads it: a field past its end is an error (the connection dropped); a slice
// past it is what is there.
struct In {
    const Bytes &b;
    size_t o = 0;
    explicit In(const Bytes &body, size_t at = 0) : b(body), o(at) {}
    uint8_t at(size_t i) const {
        if (i >= b.size()) throw Bad();
        return (uint8_t)b[i];
    }
    uint8_t u8() { return at(o++); }
    uint16_t u16() { uint16_t v = at(o) | at(o + 1) << 8; o += 2; return v; }
    uint32_t u32() { uint32_t v = at(o) | at(o + 1) << 8 | at(o + 2) << 16 | (uint32_t)at(o + 3) << 24; o += 4; return v; }
    Bytes take(size_t n) {  // (exactly n)
        if (o + n > b.size()) throw Bad();
        Bytes v = b.substr(o, n);
        o += n;
        return v;
    }
    Bytes str() { size_t n = u8(); return take(n); }
};
Bytes slice(const Bytes &b, size_t from, size_t to) { return from >= b.size() ? Bytes() : b.substr(from, std::min(to, b.size()) - from); }

void put8(Bytes &o, unsigned v) { o.push_back((char)(uint8_t)v); }
void put16(Bytes &o, unsigned v) { put8(o, v), put8(o, v >> 8); }
void put32(Bytes &o, uint32_t v) { put16(o, v & 0xffff), put16(o, v >> 16); }
Bytes put_str(const Bytes &s) { Bytes o; put8(o, (unsigned)std::min<size_t>(s.size(), 255)); return o + s.substr(0, 255); }
Bytes name17(const Bytes &n) { Bytes o = n.substr(0, 16); o.resize(17, '\0'); return o; }
Bytes one(unsigned v) { return Bytes(1, (char)(uint8_t)v); }
std::string lower(Bytes s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; }
std::string printable(const Bytes &s) { std::string o; for (char c : s) o += (unsigned char)c >= 32 && c != 127 ? c : '?'; return o; }

double now_s(void) {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
Bytes token_hex(int bytes) {
    static std::random_device rd;
    static const char *hex = "0123456789abcdef";
    Bytes o;
    for (int i = 0; i < bytes; i++) {
        unsigned v = rd() & 0xff;
        o += hex[v >> 4], o += hex[v & 15];
    }
    return o;
}

struct Lobby;
struct Conn {
    HostSocket s = HOST_BAD_SOCKET;
    double heard = now_s();  // (its last frame: none for 15 s at first, 60 s in a lobby - dropped, as his relay)
    Bytes in, out;
    bool closing = false;     // (close once out is sent)
    // as a player
    int id = -1;
    Lobby *lobby = nullptr;
    Bytes name, rgb = Bytes(3, '\0');
    std::map<int, Bytes> last;  // 0x8B / 0x8C / 0x8D / 0x93 / 0xA2 -> the last body passed on (late joiners)
    Bytes digest;
    bool v12 = false, pvp = false;
    std::map<int, double> hit_at;
    double hurt_at = -99;
    // as a seed upload: its lobby, password, size and checksum; what came so far
    bool uploading = false;
    Bytes up_name, up_lobby, up_password, up_digest, up_data;
    uint32_t up_size = 0;
    void send(const Bytes &body) {
        put16(out, (unsigned)body.size());
        out += body;
    }
};

struct Reward { Bytes digest; uint16_t item; int16_t index; uint16_t map, room; Bytes who; };
struct Lobby {
    Bytes name, password;
    std::map<int, Conn *> players;
    std::vector<int> order;  // (ids in the order they came: the roster's)
    std::set<uint32_t> flags;
    std::map<uint16_t, int16_t> items;
    Bytes digest;
    std::vector<Reward> rewards;
    std::set<uint16_t> checks;
    std::map<Bytes, uint8_t> story;
    uint8_t inventory[INV_SIZE] = {};
};

struct Member {
    Bytes token, name, rgb, hair;
    bool ready = false, host = false;
    double seen;
};
struct Room {
    Bytes code, title, host, build, world, seed, password;
    int mode = 1;
    bool pub = false, started = false;
    std::vector<Member> members;
    int created = 0;
    Bytes lobby() const { return "V10-" + code + (mode == 2 ? ".rando" : ""); }
    Bytes wire() const {
        Bytes b = put_str(code) + put_str(title) + put_str("") + put_str(host);
        put8(b, mode), put8(b, pub), put8(b, started), put8(b, 4);
        b += put_str(build) + put_str(world) + put_str(seed);
        put8(b, (unsigned)members.size());
        for (const Member &m : members) {
            b += put_str(m.name) + m.rgb + m.hair;
            put8(b, m.ready), put8(b, m.host);
        }
        return b;
    }
};
struct Run { Bytes players; int mode, count; uint32_t seconds, rupees, deaths, score; };
struct Seed { Bytes password, data, digest; };

class Relay {
public:
    int max_players = 4;
    void (*print)(const char *text) = nullptr;  // (--relay: its lines there - stdout; in the game: its log)
    std::atomic<int> players{0};
    Bytes host_name;
    int port = 0;

    void log(const char *fmt, ...) {
        char line[514];
        va_list a;
        va_start(a, fmt);
        vsnprintf(line, 512, fmt, a);  // (room for its newline)
        va_end(a);
        if (print) strcat(line, "\n"), print(line);
        else fprintf(stderr, "[relay] %s\n", line);  // (the game's log: rt_log's stderr)
    }

    // ---------------------------------------------------------------- the lobbies
    std::map<Bytes, std::unique_ptr<Lobby>> lobbies;
    std::vector<Conn *> others(Conn *p) {
        std::vector<Conn *> v;
        for (int id : p->lobby->order) if (p->lobby->players[id] != p) v.push_back(p->lobby->players[id]);
        return v;
    }
    void to_others(Conn *p, const Bytes &body, int remember = -1) {
        if (remember >= 0) p->last[remember] = body;
        for (Conn *q : others(p)) q->send(body);
    }
    void to_all(Conn *p, const Bytes &body) {
        for (int id : p->lobby->order) p->lobby->players[id]->send(body);
    }

    bool refuse(Conn *p, int code, const std::string &msg, const Bytes &name, const Bytes &lobby) {
        p->send(one(0x8F) + one(code) + put_str(msg));
        log("refused %s: %s (lobby %s)", printable(name).c_str(), msg.c_str(), printable(lobby).c_str());
        return false;
    }

    bool hello(Conn *p, int t, const Bytes &b) {
        In r(b, 1);
        r.u16();  // (version)
        Bytes name = r.str(), rgb = r.take(3), lobby = r.str(), password = r.str(), version;
        if (slice(b, r.o, r.o + 4) == "LAOV") {
            In v(b, r.o + 4);
            size_t n = v.u8();
            version = slice(b, r.o + 5, r.o + 5 + n);
        }
        if (t == 0x31) {  // (Global: its own two lobbies, no password)
            password.clear();
            lobby = lobby == "V10-GLOBAL-RANDO" ? "V10-GLOBAL-RANDO" : "V10-GLOBAL";
        } else if (lobby.compare(0, 10, "V10-GLOBAL") == 0)
            return refuse(p, 6, "Global requires the social-only client handshake.", name, lobby);
        for (auto &it : rooms) {  // (a session room's lobby: its members only, once it started)
            Room &room = *it.second;
            if (room.lobby() != lobby) continue;
            prune();
            if (!room.started) return refuse(p, 5, "The host has not started this session.", name, lobby);
            bool member = false;
            for (const Member &m : room.members) member |= m.name == name;
            if (password != room.password || !member)
                return refuse(p, 5, "Join this session through its room code or public listing first.", name, lobby);
            break;
        }
        auto &slot = lobbies[lobby];
        if (!slot) slot.reset(new Lobby()), slot->name = lobby, slot->password = password;
        Lobby *L = slot.get();
        if (L->password != password) return refuse(p, 5, "Wrong lobby password.", name, lobby);
        for (auto &q : L->players)
            if (lower(q.second->name) == lower(name))
                return refuse(p, 3, "Someone in this lobby is already called " + printable(name) + ".", name, lobby);
        int places = t == 0x31 ? 100 : max_players, pid = -1;
        for (int i = 0; i < places && pid < 0; i++) if (!L->players.count(i)) pid = i;
        if (pid < 0) return refuse(p, 4, "Lobby is full.", name, lobby);
        p->id = pid, p->lobby = L, p->name = name.empty() ? Bytes("Player") : name.substr(0, 16), p->rgb = rgb;
        Bytes w = one(0x81) + one(pid) + one((unsigned)L->players.size());
        for (int id : L->order) {
            Conn *q = L->players[id];
            w += one(q->id) + one((unsigned)q->name.size()) + q->name + q->rgb;
        }
        put32(w, (uint32_t)L->flags.size());
        for (uint32_t f : L->flags) put32(w, f);
        put16(w, (unsigned)L->items.size());
        for (auto &it : L->items) put16(w, it.first), put16(w, (uint16_t)it.second);
        p->send(w);
        for (int id : L->order) {
            Conn *q = L->players[id];
            q->send(one(0x82) + one(pid) + one((unsigned)p->name.size()) + p->name + p->rgb);
            for (auto &body : q->last) p->send(body.second);  // (what the newcomer would otherwise never get)
        }
        L->players[pid] = p, L->order.push_back(pid);
        players++;
        log("join %d %s lobby %s v%s (%d in, %d flags, %d items replayed)", pid, printable(p->name).c_str(),
            printable(lobby).c_str(), printable(version).c_str(), (int)L->players.size(), (int)L->flags.size(),
            (int)L->items.size());
        return true;
    }

    void leave(Conn *p) {
        Lobby *L = p->lobby;
        if (!L || !L->players.count(p->id) || L->players[p->id] != p) return;
        L->players.erase(p->id);
        L->order.erase(std::find(L->order.begin(), L->order.end(), p->id));
        players--;
        for (int id : L->order) L->players[id]->send(one(0x83) + one(p->id));
        log("leave %d %s (%d in)", p->id, printable(p->name).c_str(), (int)L->players.size());
        p->lobby = nullptr;
    }

    bool handle(Conn *p, const Bytes &b) {  // (false: a protocol error - the connection dropped)
        In r(b);
        int t = r.u8();
        Lobby *L = p->lobby;
        if (t == 0x07) {  // PING
            uint32_t v = r.u32();
            bool cap = std::find(std::begin(CAP_REPLIES), std::end(CAP_REPLIES), v) != std::end(CAP_REPLIES);
            Bytes o = one(0x89);
            put32(o, cap ? v + 1 : v);
            p->send(o);
            if (v == 0x4c414f70) {
                p->v12 = true;
                for (uint32_t x : V12_EXTRAS) { Bytes e = one(0x89); put32(e, x); p->send(e); }
            }
            return true;
        }
        if (!L) return true;
        Bytes pid = one(p->id);
        switch (t) {
        case 0x02: to_others(p, one(0x84) + pid + slice(b, 1, 28)); break;  // STATE
        case 0x08: p->rgb = slice(b, 1, 4); to_others(p, one(0x8A) + pid + p->rgb, 0x8A); break;  // PROFILE
        case 0x09: to_others(p, one(0x8B) + pid + slice(b, 1, 3), 0x8B); break;   // VITALS
        case 0x0A: to_others(p, one(0x8C) + pid + slice(b, 1, b.size()), 0x8C); break;  // LOCATION
        case 0x0B: to_others(p, one(0x8D) + pid + slice(b, 1, 19), 0x8D); break;  // APPEARANCE
        case 0x0F: to_others(p, one(0x93) + pid + slice(b, 1, 18), 0x93); break;  // MAP_ANCHOR
        case 0x03: {  // FLAG_SET: kept, new ones passed on
            uint32_t f = r.u32();
            if (L->flags.insert(f).second) {
                log("flag %u from %d", f, p->id);
                Bytes o = one(0x85) + pid;
                put32(o, f);
                to_others(p, o);
            }
            break;
        }
        case 0x04: {  // ITEM_GET: the highest value per key kept, a higher one passed on
            uint16_t k = r.u16();
            int16_t v = (int16_t)r.u16();
            auto it = L->items.find(k);
            if (v > (it == L->items.end() ? 0 : it->second)) {
                L->items[k] = v;
                log("item %#x = %d from %d", k, v, p->id);
                Bytes o = one(0x86) + pid;
                put16(o, k), put16(o, (uint16_t)v);
                to_others(p, o);
            }
            break;
        }
        case 0x0C: {  // COOP_SETUP -> COOP_READY
            Bytes d = slice(b, 1, 33);
            p->digest = d;
            if (L->digest.empty()) L->digest = d;
            bool ok = d == L->digest;
            log("coop setup %d: %s", p->id, ok ? "ready" : "seed differs");
            p->send(one(0x90) + one(ok) + d);
            break;
        }
        case 0x14: {  // WORLD -> WORLD_RESULT
            Bytes d = slice(b, 2, 34);
            p->digest = d;
            if (L->digest.empty()) L->digest = d;
            p->send(one(0x98) + one(d == L->digest ? 1 : 4) + d);
            break;
        }
        case 0x0D: {  // PICKUP
            In q(b, 9);
            unsigned item = q.u16(), count = q.u16();
            int index = (int16_t)q.u16();
            log("pickup %u x%u (index %d) from %d", item, count, index, p->id);
            to_others(p, one(0x91) + pid + slice(b, 1, 19));
            break;
        }
        case 0x0E: {  // INVENTORY: ORed, the lobby's snapshot to everyone
            for (int i = 0; i < INV_SIZE; i++) L->inventory[i] |= r.at(1 + i);
            to_all(p, one(0x92) + Bytes((const char *)L->inventory, INV_SIZE));
            break;
        }
        case 0x10: to_all(p, one(0x94) + pid + slice(b, 1, b.size())); break;  // CHAT: to everyone, the sender too
        case 0x11: to_others(p, one(0x95) + pid + slice(b, 1, 2)); break;       // TYPING
        case 0x12: {  // CHECK
            int kind = r.u8();
            if (kind == 0) {
                std::vector<uint16_t> fl(L->checks.begin(), L->checks.end());
                for (size_t i = 0; i < std::max<size_t>(fl.size(), 1); i += 255) {
                    size_t n = std::min<size_t>(255, fl.size() - std::min(i, fl.size()));
                    Bytes o = one(0x96) + one((unsigned)n);
                    for (size_t j = 0; j < n; j++) put16(o, fl[i + j]);
                    p->send(o);
                }
            } else if (kind == 1) {
                uint16_t f = r.u16();
                if (L->checks.insert(f).second) {
                    Bytes o = one(0x96) + one(1);
                    put16(o, f);
                    to_others(p, o);
                }
            }
            break;
        }
        case 0x13: {  // REWARD
            int kind = r.u8();
            if (kind == 1 && !p->digest.empty()) {
                uint16_t item = r.u16();
                int16_t index = (int16_t)r.u16();
                uint16_t m = r.u16(), rm = r.u16();
                bool known = false;
                for (const Reward &w : L->rewards) known |= w.digest == p->digest && w.item == item && w.index == index;
                if (!known) {
                    L->rewards.push_back({p->digest, item, index, m, rm, p->name});
                    log("reward %u (index %d) from %d", item, index, p->id);
                }
                Bytes o = one(0x97) + p->digest;
                put16(o, item), put16(o, (uint16_t)index), put16(o, m), put16(o, rm);
                o += name17(p->name);
                for (Conn *q : others(p)) q->send(o);
            } else if (kind == 0 && !p->digest.empty()) {
                for (const Reward &w : L->rewards) {
                    if (w.digest != p->digest) continue;
                    Bytes o = one(0x97) + w.digest;
                    put16(o, w.item), put16(o, (uint16_t)w.index), put16(o, w.map), put16(o, w.room);
                    p->send(o + name17(w.who));
                }
            }
            break;
        }
        case 0x1A: {  // STORY: ORed per seed, to everyone
            Bytes d = slice(b, 2, 34);
            uint8_t mask = r.at(34);
            L->story[d] |= mask;
            to_all(p, one(0xA1) + one(1) + d + one(L->story[d]) + one(0));
            break;
        }
        case 0x19:  // DEATH: to everyone, its sender too
            log("death of %d", p->id);
            to_all(p, one(0xA0) + pid + slice(b, 1, 14) + name17(p->name));
            break;
        case 0x1C: {  // PVP (only after the V12 query: else the connection is dropped)
            if (!p->v12) {
                log("protocol error from %d: PvP before the V12 query (dropped)", p->id);
                return false;
            }
            int kind = r.u8();
            if (kind == 0) {
                p->pvp = b.size() > 2 && b[2] != 0;
                to_others(p, one(0xA2) + one(0) + pid + one(p->pvp), 0xA2);
                log("pvp %s %d", p->pvp ? "on" : "off", p->id);
            } else if (kind == 1 && b.size() >= 3) {
                int target = (uint8_t)b[2];
                int quarters = b.size() > 3 ? std::min(8, std::max(1, (int)(uint8_t)b[3])) : 2;
                auto qi = L->players.find(target);
                Conn *q = qi == L->players.end() ? nullptr : qi->second;
                double now = now_s();
                auto hit = p->hit_at.find(target);
                double last = hit == p->hit_at.end() ? -99 : hit->second;
                if (q && q != p && p->pvp && q->pvp && now - last >= 0.45 && now - q->hurt_at >= 0.8) {
                    p->hit_at[target] = now, q->hurt_at = now;
                    q->send(one(0xA2) + one(1) + pid + one(quarters));
                    for (int id : L->order) L->players[id]->send(one(0xA2) + one(4) + pid + one(target) + one(quarters));
                    log("pvp hit %d -> %d (%d quarters)", p->id, target, quarters);
                }
            } else if (kind == 2 && b.size() == 19) {
                to_others(p, one(0xA2) + one(2) + pid + slice(b, 2, 19));
            } else if (kind == 3 && b.size() >= 3) {
                log("pvp knockout %d by %d", p->id, (uint8_t)b[2]);
            }
            break;
        }
        default: log("ignored type %#x from %d", t, p->id);
        }
        return true;
    }

    // ---------------------------------------------------------------- the session service (0x30)
    std::map<Bytes, std::unique_ptr<Room>> rooms;
    int room_serial = 0;

    void prune() {
        double now = now_s();
        for (auto it = rooms.begin(); it != rooms.end();) {
            Room &r = *it->second;
            std::set<Bytes> playing;
            auto L = lobbies.find(r.lobby());
            if (L != lobbies.end()) for (auto &q : L->second->players) playing.insert(q.second->name);
            for (auto m = r.members.begin(); m != r.members.end();) {
                if (playing.count(m->name)) m->seen = now;
                else if (now - m->seen > 120) {
                    log("session gone %s %s (not seen for 2 minutes)", printable(r.code).c_str(), printable(m->name).c_str());
                    m = r.members.erase(m);
                    continue;
                } else if (now - m->seen > 10) m->ready = false;
                ++m;
            }
            if (r.members.empty()) { it = rooms.erase(it); continue; }
            bool hosted = false;
            for (const Member &m : r.members) hosted |= m.host;
            if (!hosted) r.members[0].host = true, r.host = r.members[0].name;
            ++it;
        }
    }
    Bytes new_code() {
        static const char *letters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        static std::random_device rd;
        for (;;) {
            Bytes c;
            for (int i = 0; i < 6; i++) c += letters[rd() % 32];
            if (!rooms.count(c)) return c;
        }
    }
    static Bytes fail(const std::string &msg) { return one(0) + put_str(msg); }
    static Bytes ok(const std::string &msg, const Bytes &tok = "", const Room *room = nullptr,
                    const std::vector<const Room *> *listed = nullptr, unsigned total = 0, unsigned page = 0) {
        Bytes o = one(1) + put_str(msg) + put_str(tok) + put_str(room ? room->password : "") + put_str(room ? room->lobby() : "");
        std::vector<const Room *> shown = listed ? *listed : room ? std::vector<const Room *>{room} : std::vector<const Room *>{};
        put16(o, total), put16(o, page), put8(o, (unsigned)shown.size());
        for (const Room *r : shown) o += r->wire();
        return o;
    }

    Bytes session(const Bytes &b) {  // (b: the request after its opcode)
        In r(b, 1);
        int action = r.u8();
        Bytes code = r.str(), token = r.str(), name = r.str(), build = r.str();
        int mode = r.u8();
        Bytes world = r.str(), seed = r.str(), title = r.str();
        r.str();  // (description)
        bool ready = r.u8() != 0;
        Bytes rgb = r.take(3), hair = r.take(3), search = r.str();
        int filt = r.u8(), sort = r.u8();
        unsigned page = r.u16();
        name = name.empty() ? Bytes("Player") : name.substr(0, 16);
        prune();
        if (mode != 1 && mode != 2) return fail("This room needs a compatible client and game mode.");
        if (action == 0) {
            std::vector<const Room *> listed;
            std::string s = lower(search);
            for (auto &it : rooms) {
                const Room &x = *it.second;
                if (x.pub && (filt == 0 || x.mode == filt) &&
                    (lower(x.title).find(s) != std::string::npos || lower(x.host).find(s) != std::string::npos))
                    listed.push_back(&x);
            }
            std::stable_sort(listed.begin(), listed.end(), [&](const Room *a, const Room *c) {
                switch (sort < 4 ? sort : 0) {
                case 1: return a->members.size() < c->members.size();
                case 2: return a->created > c->created;
                case 3: return lower(a->title) < lower(c->title);
                default: return a->members.size() > c->members.size();
                }
            });
            std::vector<const Room *> shown;
            for (size_t i = (size_t)page * 8; i < listed.size() && i < (size_t)page * 8 + 8; i++) shown.push_back(listed[i]);
            return ok("", "", nullptr, &shown, (unsigned)listed.size(), page);
        }
        if (action == 1 || action == 2) {
            if (rooms.size() >= 256) return fail("The room directory is busy. Please try again later.");
            if (!token.empty())
                for (auto &it : rooms)
                    for (const Member &m : it.second->members)
                        if (m.token == token) return fail("Leave your current session before creating another.");
            std::unique_ptr<Room> room(new Room());
            room->code = new_code(), room->title = title.empty() ? name + "'s adventure" : title, room->host = name;
            room->mode = mode, room->pub = action == 2, room->build = build, room->world = world, room->seed = seed;
            room->password = token_hex(8), room->created = ++room_serial;
            Member m{token_hex(16), name, rgb, hair, false, true, now_s()};
            room->members.push_back(m);
            Room *x = room.get();
            rooms[x->code] = std::move(room);
            log("session host %s by %s (%s, %s)", printable(x->code).c_str(), printable(name).c_str(),
                x->pub ? "public" : "private", x->mode == 2 ? "co-op randomizer" : "co-op");
            return ok("Session created.", m.token, x);
        }
        auto found = rooms.find(code);
        Room *x = found == rooms.end() ? nullptr : found->second.get();
        if (action == 3) {
            if (!x) return fail("Room not found or expired. Check the six-character code.");
            if (x->build != build) return fail("This adventure needs a different version of LA-Online. Update the game to join.");
            for (Member &m : x->members)
                if (!token.empty() && m.token == token) {  // (his place again: the token he kept)
                    m.name = name, m.rgb = rgb, m.hair = hair, m.seen = now_s();
                    return ok("Welcome back.", m.token, x);
                }
            if (x->members.size() >= 4) return fail("Session full: all four player slots are taken.");
            for (const Member &m : x->members)
                if (lower(m.name) == lower(name))
                    return fail("That player name is already in this adventure. If it is you reconnecting, wait up to 2 "
                                "minutes or pick another name.");
            Member m{token_hex(16), name, rgb, hair, x->started, false, now_s()};
            x->members.push_back(m);
            log("session join %s %s (%d in)", printable(x->code).c_str(), printable(name).c_str(), (int)x->members.size());
            return ok("Joined the session.", m.token, x);
        }
        Member *m = nullptr;
        if (x) for (Member &y : x->members) if (!token.empty() && y.token == token) m = &y;
        if (!m) return fail("Your room membership expired. Join again.");
        m->seen = now_s();
        switch (action) {
        case 4: return ok("", m->token, x);
        case 5:
            if (x->started) return fail("The adventure has already started.");
            m->ready = ready;
            log("session ready %s %s %d", printable(x->code).c_str(), printable(m->name).c_str(), (int)ready);
            return ok("", m->token, x);
        case 6:
            if (!m->host) return fail("Only the host can start the adventure.");
            for (const Member &y : x->members) if (!y.ready) return fail("Every player must be ready before starting.");
            x->started = true;
            log("session start %s lobby %s (%d players)", printable(x->code).c_str(), printable(x->lobby()).c_str(),
                (int)x->members.size());
            return ok("Session started.", m->token, x);
        case 7: {
            bool was_host = m->host;
            Bytes room_code = x->code;
            log("session leave %s %s", printable(x->code).c_str(), printable(m->name).c_str());
            x->members.erase(x->members.begin() + (m - x->members.data()));
            if (x->members.empty()) rooms.erase(room_code);
            else if (was_host) x->members[0].host = true, x->host = x->members[0].name;
            return ok("Left the session.");
        }
        case 8: {
            if (!m->host || x->members.size() != 1 || x->started)
                return fail("Only the host can change the code, before another player joins.");
            std::unique_ptr<Room> keep = std::move(found->second);
            rooms.erase(found);
            keep->code = new_code(), keep->password = token_hex(8);
            Bytes tok = keep->members[0].token;
            rooms[keep->code] = std::move(keep);
            return ok("New room code.", tok, x);
        }
        case 9:
            if (m->ready || x->started) return fail("Mark Not Ready before changing your name.");
            for (const Member &y : x->members)
                if (&y != m && lower(y.name) == lower(name)) return fail("That player name is already in this room.");
            m->name = name;
            if (m->host) x->host = name;
            return ok("Name changed.", m->token, x);
        }
        return fail("Unknown request.");
    }

    // ---------------------------------------------------------------- Global's count (0x33) and the leaderboards (0x32)
    Bytes global_count() {
        unsigned n = 0;
        for (auto &it : lobbies) if (it.first.compare(0, 10, "V10-GLOBAL") == 0) n += (unsigned)it.second->players.size();
        Bytes o = one(1);
        put16(o, std::min(n, 100u)), put16(o, 100);
        return o;
    }
    std::vector<Run> runs;
    Bytes board(const Bytes &b) {
        In r(b);
        int version = r.u8(), action = r.u8();
        if (version == 1 && action == 1) {  // a completed run
            r.str();
            Bytes players = r.str();
            int mode = r.u8();
            uint32_t seconds = r.u32(), rupees = r.u32(), deaths = r.u32();
            int count = std::min(4, (int)std::count(players.begin(), players.end(), ',') + 1);
            long score = std::max(0L, 100000L - (long)seconds - 250L * (long)deaths + (long)rupees);
            runs.push_back({players, mode, count, seconds, rupees, deaths, (uint32_t)score});
            log("board run %s mode %d %us", printable(players).c_str(), mode, seconds);
            return one(1) + put_str("Thank you.");
        }
        if (version == 2 && action == 0) {  // a page
            int mode = r.u8(), sort = r.u8(), count = r.u8();
            unsigned page = r.u16();
            std::vector<const Run *> rows;
            for (const Run &x : runs) if ((mode == 3 || x.mode == mode) && (count == 0 || x.count == count)) rows.push_back(&x);
            std::stable_sort(rows.begin(), rows.end(), [&](const Run *a, const Run *c) {
                switch (sort < 4 ? sort : 0) {
                case 1: return a->score > c->score;
                case 2: return a->rupees > c->rupees;
                case 3: return a->deaths > c->deaths;
                default: return a->seconds < c->seconds;
                }
            });
            Bytes o = one(1) + put_str("");
            put16(o, (unsigned)rows.size());
            size_t from = (size_t)page * 50, to = std::min(rows.size(), from + 50);
            put8(o, (unsigned)(to > from ? to - from : 0));
            for (size_t i = from; i < to; i++) {
                const Run *x = rows[i];
                o += put_str(x->players) + one(x->mode) + one(x->count);
                put32(o, x->seconds), put32(o, x->rupees), put32(o, x->deaths), put32(o, x->score);
            }
            return o;
        }
        return one(0) + put_str("Unknown request.");
    }

    // ---------------------------------------------------------------- a Co-op Randomizer room's seed (0x16 / 0x17 / 0x18)
    std::map<Bytes, Seed> seeds;  // lobby -> its seed, given only with the uploader's password
    bool seed_transfer(Conn *c, const Bytes &b) {
        In r(b, 1);
        int kind = r.u8();
        Bytes name = r.str(), lobby = r.str(), password = r.str();
        if (kind == 1) {  // upload: world checksum, u32 size, sha256; then chunks (0x17), then the end (0x18)
            r.take(32);
            uint32_t size = r.u32();
            Bytes digest = r.take(32);
            if (size > SEED_MAX) return false;
            c->uploading = true, c->up_name = name, c->up_lobby = lobby, c->up_password = password;
            c->up_size = size, c->up_digest = digest;
            c->send(one(0x9d) + one(1));
            return true;
        }
        auto got = seeds.find(lobby);
        if (got == seeds.end() || got->second.password != password) {
            c->send(one(0x9d) + one(0));
            log("seed download %s by %s: none yet", printable(lobby).c_str(), printable(name).c_str());
        } else {
            const Bytes &data = got->second.data;
            Bytes head = one(0x9d) + one(1);
            put32(head, (uint32_t)data.size());
            c->send(head + got->second.digest);
            for (size_t i = 0; i < data.size(); i += 4000) c->send(one(0x9e) + data.substr(i, 4000));
            c->send(one(0x9f));
            log("seed download %s by %s: %d bytes", printable(lobby).c_str(), printable(name).c_str(), (int)data.size());
        }
        c->closing = true;
        return true;
    }
    bool seed_chunk(Conn *c, const Bytes &b) {
        if (b[0] == 0x17) {
            c->up_data.append(b, 1, Bytes::npos);
            return c->up_data.size() <= SEED_MAX;
        }
        if (b[0] != 0x18) return true;
        CacheSha256 h;
        h.add(c->up_data.data(), c->up_data.size());
        std::string sent;
        for (unsigned char x : c->up_digest) { char t[3]; snprintf(t, sizeof t, "%02x", x); sent += t; }
        bool good = c->up_data.size() == c->up_size && h.finish() == sent;
        if (good) seeds[c->up_lobby] = Seed{c->up_password, c->up_data, c->up_digest};
        log("seed upload %s by %s: %u bytes%s", printable(c->up_lobby).c_str(), printable(c->up_name).c_str(), c->up_size,
            good ? "" : " REJECTED");
        c->send(one(0x9d) + one(good));
        c->uploading = false, c->up_data.clear(), c->closing = true;
        return true;
    }

    // ---------------------------------------------------------------- a frame from a connection (false: drop it)
    bool frame(Conn *c, const Bytes &b) {
        if (c->uploading) return seed_chunk(c, b);
        int t = (uint8_t)b[0];
        if (!c->lobby && t == 0x16) return seed_transfer(c, b);
        if (!c->lobby && (t == 0x30 || t == 0x32 || t == 0x33)) {  // (one answer, then the connection closes)
            Bytes rest = b.substr(1);
            c->send(one(t + 0x80) + (t == 0x30 ? session(rest) : t == 0x32 ? board(rest) : global_count()));
            c->closing = true;
            return true;
        }
        if (!c->lobby && (t == 0x01 || t == 0x31)) {
            if (!hello(c, t, b)) c->closing = true;
            return true;
        }
        if (!handle(c, b)) c->closing = true;
        return true;
    }

    // ---------------------------------------------------------------- the network
    HostSocket listener = HOST_BAD_SOCKET, finder = HOST_BAD_SOCKET;
    std::vector<std::unique_ptr<Conn>> conns;
    std::atomic<bool> stop{false};

    // Every network of this PC's - or LAONLINE_HOST_BIND's address alone (the tests: 127.0.0.1, this PC only, so that
    // no firewall asks)
    std::string bound = "0.0.0.0";
    bool open(int p, std::string &error) {
        port = p;
        const char *only = getenv("LAONLINE_HOST_BIND");
        sockaddr_in a = {};
        a.sin_family = AF_INET, a.sin_port = htons((uint16_t)p), a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (only && *only) {
            if (inet_pton(AF_INET, only, &a.sin_addr) != 1) { error = std::string("LAONLINE_HOST_BIND: not an address: ") + only; return false; }
            bound = only;
        }
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == HOST_BAD_SOCKET) { error = "Cannot open a socket."; return false; }
#ifndef _WIN32
        int on = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#endif
        if (bind(listener, (sockaddr *)&a, sizeof a) || listen(listener, 32)) {
            host_close(listener), listener = HOST_BAD_SOCKET;
            error = "Port " + std::to_string(p) + " is in use (another relay or program on this PC?).";
            return false;
        }
        host_nonblocking(listener);
        finder = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);  // (the network's search: not having it is no failure)
        if (finder != HOST_BAD_SOCKET) {
            int on = 1;
            setsockopt(finder, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
            a.sin_port = htons(SEARCH_PORT);  // (one port for every relay's: the search's)
            if (bind(finder, (sockaddr *)&a, sizeof a)) host_close(finder), finder = HOST_BAD_SOCKET;
            else host_nonblocking(finder);
        }
        return true;
    }

    void answer_search(void) {
        char buf[64];
        sockaddr_in from = {};
        socklen_t n = sizeof from;
        int k = (int)recvfrom(finder, buf, sizeof buf, 0, (sockaddr *)&from, &n);
        if (k != 8 || memcmp(buf, "LAOFIND1", 8)) return;
        Bytes o = "LAOHOST1";
        put16(o, (unsigned)port), put8(o, (unsigned)std::min(players.load(), 255));
        o += put_str(host_name.substr(0, 32));
        sendto(finder, o.data(), (int)o.size(), 0, (sockaddr *)&from, n);
    }

    void drop(Conn *c) {
        leave(c);
        host_close(c->s);
        c->s = HOST_BAD_SOCKET;
    }

    void run(void) {
        while (!stop.load()) {
            fd_set rd, wr;
            FD_ZERO(&rd);
            FD_ZERO(&wr);
            HostSocket top = listener;
            FD_SET(listener, &rd);
            if (finder != HOST_BAD_SOCKET) {
                FD_SET(finder, &rd);
                top = std::max(top, finder);
            }
            for (auto &c : conns) {
                if (!c->closing) FD_SET(c->s, &rd);
                if (!c->out.empty()) FD_SET(c->s, &wr);
                top = std::max(top, c->s);
            }
            timeval wait = {0, 200000};
            if (select((int)top + 1, &rd, &wr, nullptr, &wait) < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            if (FD_ISSET(listener, &rd)) {
                HostSocket s = accept(listener, nullptr, nullptr);
                if (s != HOST_BAD_SOCKET) {
                    if (conns.size() >= CONNECTIONS_MAX) host_close(s);
                    else {
                        host_nonblocking(s);
                        int on = 1;
                        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
                        conns.emplace_back(new Conn());
                        conns.back()->s = s;
                    }
                }
            }
            if (finder != HOST_BAD_SOCKET && FD_ISSET(finder, &rd)) answer_search();
            for (size_t i = 0; i < conns.size(); i++) {
                Conn *c = conns[i].get();
                if (c->s == HOST_BAD_SOCKET) continue;
                bool alive = true;
                if (FD_ISSET(c->s, &rd)) {
                    char buf[16384];
                    int k = (int)recv(c->s, buf, sizeof buf, 0);
                    if (k > 0) {
                        c->in.append(buf, k);
                        try {
                            while (alive && !c->closing && c->in.size() >= 2) {
                                size_t n = (uint8_t)c->in[0] | (uint8_t)c->in[1] << 8;
                                if (c->in.size() < 2 + n) break;
                                Bytes body = c->in.substr(2, n);
                                c->in.erase(0, 2 + n);
                                c->heard = now_s();
                                if (n) alive = frame(c, body);
                            }
                        } catch (const Bad &) {
                            alive = false;
                        }
                    } else if (k == 0 || !host_would_block()) alive = false;
                }
                if (alive && !c->out.empty()) {
                    int k = (int)::send(c->s, c->out.data(), (int)std::min<size_t>(c->out.size(), 1 << 20), 0);
                    if (k > 0) c->out.erase(0, k);
                    else if (k < 0 && !host_would_block()) alive = false;
                }
                if (c->out.size() > OUT_MAX || (c->closing && c->out.empty())) alive = false;
                if (now_s() - c->heard > (c->lobby || c->uploading ? 60 : 15)) alive = false;  // (silent: gone)
                if (!alive) drop(c);
            }
            // (sends to the others queued by this pass: their writes go out on the next pass's select)
            conns.erase(std::remove_if(conns.begin(), conns.end(), [](const std::unique_ptr<Conn> &c) { return c->s == HOST_BAD_SOCKET; }),
                        conns.end());
        }
        for (auto &c : conns) drop(c.get());
        conns.clear();
        if (finder != HOST_BAD_SOCKET) host_close(finder), finder = HOST_BAD_SOCKET;
        host_close(listener), listener = HOST_BAD_SOCKET;
    }
};

#ifdef _WIN32
struct Winsock {
    bool ok;
    Winsock() { WSADATA w; ok = !WSAStartup(MAKEWORD(2, 2), &w); }
    ~Winsock() { if (ok) WSACleanup(); }
};
#endif

std::mutex g_host_lock;  // g_host, g_thread
std::unique_ptr<Relay> g_host;
std::thread g_thread;

}  // namespace

bool lao_host_start(int port, const std::string &name, std::string &error) {
    std::lock_guard<std::mutex> lock(g_host_lock);
    if (g_host) return true;
    if (port < 1 || port > 65535) { error = "Choose a port from 1 to 65535."; return false; }
#ifdef _WIN32
    static Winsock winsock;  // (for the process: the relay's thread, the network's search)
#endif
    std::unique_ptr<Relay> r(new Relay());
    r->host_name = name;
    if (!r->open(port, error)) return false;
    r->log("relay listening on %s:%d", r->bound.c_str(), port);
    g_host = std::move(r);
    Relay *x = g_host.get();
    g_thread = std::thread([x] { x->run(); });
    return true;
}

void lao_host_stop(void) {
    std::lock_guard<std::mutex> lock(g_host_lock);
    if (!g_host) return;
    g_host->stop = true;
    g_thread.join();
    g_host->log("relay stopped");
    g_host.reset();
}

bool lao_host_running(void) { std::lock_guard<std::mutex> lock(g_host_lock); return g_host != nullptr; }
int lao_host_port(void) { std::lock_guard<std::mutex> lock(g_host_lock); return g_host ? g_host->port : 0; }
int lao_host_players(void) { std::lock_guard<std::mutex> lock(g_host_lock); return g_host ? g_host->players.load() : 0; }

// ---------------------------------------------------------------- this PC's networks
namespace {
struct Net { uint32_t address, broadcast; };  // (host order)
std::vector<Net> networks(void) {
    std::vector<Net> v;
#ifdef _WIN32
    ULONG size = 16384;
    std::vector<uint8_t> buf;
    ULONG got = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && got == ERROR_BUFFER_OVERFLOW; tries++) {
        buf.resize(size);
        got = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                   (IP_ADAPTER_ADDRESSES *)buf.data(), &size);
    }
    if (got != NO_ERROR) return v;
    for (auto *a = (IP_ADAPTER_ADDRESSES *)buf.data(); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto *u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            uint32_t ip = ntohl(((sockaddr_in *)u->Address.lpSockaddr)->sin_addr.s_addr);
            unsigned bits = u->OnLinkPrefixLength;
            uint32_t mask = bits >= 32 ? 0xffffffffu : bits ? ~(0xffffffffu >> bits) : 0;
            v.push_back({ip, ip | ~mask});
        }
    }
#else
    ifaddrs *list = nullptr;
    if (getifaddrs(&list)) return v;
    for (ifaddrs *a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        uint32_t ip = ntohl(((sockaddr_in *)a->ifa_addr)->sin_addr.s_addr);
        uint32_t mask = a->ifa_netmask ? ntohl(((sockaddr_in *)a->ifa_netmask)->sin_addr.s_addr) : 0xffffff00u;
        v.push_back({ip, ip | ~mask});
    }
    freeifaddrs(list);
#endif
    v.erase(std::remove_if(v.begin(), v.end(), [](const Net &n) { return (n.address >> 24) == 127 || (n.address >> 16) == 0xa9fe; }),
            v.end());  // (loopback; 169.254: no network's address)
    return v;
}
std::string dotted(uint32_t ip) {
    char t[20];
    snprintf(t, sizeof t, "%u.%u.%u.%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255);
    return t;
}
}  // namespace

std::vector<std::string> lao_host_addresses(void) {
    std::vector<std::string> v;
    for (const Net &n : networks()) v.push_back(dotted(n.address));
    return v;
}

std::vector<LaoLanRelay> lao_lan_find(int wait_ms) {
    std::vector<LaoLanRelay> found;
#ifdef _WIN32
    static Winsock winsock;
#endif
    HostSocket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == HOST_BAD_SOCKET) return found;
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof on);
    host_nonblocking(s);
    std::set<uint32_t> targets = {0xffffffffu, 0x7f000001u};  // (everywhere; and this PC: a relay it hosts itself)
    for (const Net &n : networks()) targets.insert(n.broadcast);
    for (int port : {(int)SEARCH_PORT}) {
        for (uint32_t t : targets) {
            sockaddr_in to = {};
            to.sin_family = AF_INET, to.sin_port = htons((uint16_t)port), to.sin_addr.s_addr = htonl(t);
            sendto(s, "LAOFIND1", 8, 0, (sockaddr *)&to, sizeof to);
        }
    }
    double until = now_s() + wait_ms / 1000.0;
    std::set<std::string> seen;
    std::vector<Net> mine = networks();
    while (now_s() < until) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        timeval wait = {0, 50000};
        if (select((int)s + 1, &rd, nullptr, nullptr, &wait) <= 0) continue;
        char buf[128];
        sockaddr_in from = {};
        socklen_t n = sizeof from;
        int k = (int)recvfrom(s, buf, sizeof buf, 0, (sockaddr *)&from, &n);
        if (k < 12 || memcmp(buf, "LAOHOST1", 8)) continue;
        Bytes b(buf, k);
        try {
            In r(b, 8);
            unsigned port = r.u16();
            int count = r.u8();
            Bytes name = r.str();
            uint32_t ip = ntohl(from.sin_addr.s_addr);
            for (const Net &x : mine) if (x.address == ip) ip = 0x7f000001u;  // (this PC's own: as it reaches itself)
            std::string address = dotted(ip) + ":" + std::to_string(port);
            if (seen.insert(address).second) found.push_back({address, printable(name), count});
        } catch (const Bad &) {
        }
    }
    host_close(s);
    return found;
}

int lao_host_main(int port, int max_players, void (*print)(const char *text)) {
    std::string error;
#ifdef _WIN32
    static Winsock winsock;
#endif
    Relay r;
    r.print = print, r.max_players = max_players > 0 ? max_players : 4, r.host_name = "Relay";
    if (!r.open(port, error)) {
        print(("relay: " + error + "\n").c_str());
        return 1;
    }
    r.log("relay listening on %s:%d", r.bound.c_str(), port);
    r.run();
    return 0;
}

// laonline_configured, at boot ("Host whenever the game starts"): as the Relay tab's Start hosting
extern "C" bool lao_host_start_c(int port, const char *name) {
    std::string error;
    if (lao_host_start(port, name && *name ? name : "LAS Recomp", error)) return true;
    fprintf(stderr, "[relay] cannot host on port %d: %s\n", port, error.c_str());
    return false;
}
