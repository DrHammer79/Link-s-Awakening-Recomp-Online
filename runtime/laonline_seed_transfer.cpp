// A Co-op Randomizer room's seed, from the host's game to the others' (runtime/laonline_seed_transfer.h).
//
// LAS Recomp Co-Op's port of LA-Online V15.2's seed transfer. V15.2 sent a seed as patches against each player's own
// game files (a seed then held its changed game files, romfs/). A seed made since LAS Recomp's November 2026 randomizer
// holds none: its record (seed.json: every decision its game files are built from, on each machine, from that player's
// own files) and its code patches (exefs/*.pchtxt) are the world, so the package is simply the seed folder's own files.
// The relay side is V15.2's unchanged: a zip whose manifest.json names its format (LAO-WORLD-PATCH-1, the baseline the
// relay accepts), the seed's ID, its world checksum and its name; uploaded in 0x17 chunks after a 0x16 request carrying
// that checksum, the package's size and SHA-256, ended by 0x18; downloaded as 0x9D (size, SHA-256), 0x9E chunks, 0x9F.
// This package adds "seed_format": "lasrecomp-seed-1" (16.0 plays only with 16.0: no V15.2 game reads it).
#include "laonline_seed_transfer.h"

#include "cache_sha256.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET seed_sock;
#define SEED_BAD INVALID_SOCKET
#define seed_close closesocket
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int seed_sock;
#define SEED_BAD (-1)
#define seed_close close
#endif

extern "C" {
#include "zipfile.h"
int laonline_seed_identity_folder(const char *dir, unsigned char out[32]);
}

namespace fs = std::filesystem;

namespace {

const uint64_t PACKAGE_MAX = 64ull << 20;  // (a seed's own files: well under a megabyte; the relay's limit is 256 MB)

std::string hex32(const unsigned char d[32]) {
    static const char *h = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 32; i++) s += h[d[i] >> 4], s += h[d[i] & 15];
    return s;
}

bool unhex32(const std::string &s, unsigned char d[32]) {
    if (s.size() != 64) return false;
    for (int i = 0; i < 32; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = s[i * 2 + k];
            v = v * 16 + (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1000);
        }
        if (v < 0) return false;
        d[i] = (unsigned char)v;
    }
    return true;
}

bool world_of(const fs::path &dir, std::string &hex) {
    unsigned char d[32];
    if (!laonline_seed_identity_folder(dir.string().c_str(), d)) return false;
    hex = hex32(d);
    return true;
}

// The relay's rules for a seed's ID ([A-Z0-9-]{20,95}) and a name ([A-Za-z0-9 _()-]{1,90})
bool id_ok(const std::string &s) {
    if (s.size() < 20 || s.size() > 95) return false;
    for (char c : s)
        if (!(isdigit((unsigned char)c) || (c >= 'A' && c <= 'Z') || c == '-')) return false;
    return true;
}
std::string clean_name(const std::string &s) {
    std::string out;
    for (char c : s)
        if (isalnum((unsigned char)c) || c == ' ' || c == '_' || c == '(' || c == ')' || c == '-') out += c;
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.size() > 90) out.resize(90);
    return out.empty() ? "Shared seed" : out;
}

// A path inside the package: plain parts only (letters, digits, . _ - ( ) and spaces), no "..", nothing absolute
bool rel_ok(const std::string &r) {
    if (r.empty() || r.size() > 200 || r[0] == '/' || r.back() == '/') return false;
    for (char c : r)
        if (!(isalnum((unsigned char)c) || strchr("._-() /", c))) return false;
    size_t at = 0;
    while (at <= r.size()) {
        size_t end = r.find('/', at);
        std::string part = r.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return true;
}

std::string read_text(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string seed_id_of(const fs::path &dir) {
    std::string id = read_text(dir / "seed_id.txt");
    id.erase(std::find_if(id.begin(), id.end(), [](char c) { return c == '\r' || c == '\n'; }), id.end());
    while (!id.empty() && id.back() == ' ') id.pop_back();
    for (auto &c : id) c = (char)toupper((unsigned char)c);
    return id;
}

// "key": "value" in our own manifest (its values have no escapes: the relay checks them against the rules above)
std::string json_value(const std::string &j, const char *key) {
    std::string k = std::string("\"") + key + "\":";
    size_t at = j.find(k);
    if (at == std::string::npos) return "";
    at += k.size();
    while (at < j.size() && j[at] == ' ') at++;
    if (at >= j.size() || j[at] != '"') return "";
    size_t end = j.find('"', at + 1);
    return end == std::string::npos ? "" : j.substr(at + 1, end - at - 1);
}

// ---- the relay connection: frames are u16 length (the type byte and the body) + type + body
struct Conn {
    seed_sock s = SEED_BAD;
    ~Conn() { if (s != SEED_BAD) seed_close(s); }
    bool open(const std::string &server, std::string &status) {
        std::string host = server, port = "47620";
        size_t colon = host.rfind(':');
        if (colon != std::string::npos) port = host.substr(colon + 1), host.resize(colon);
        addrinfo hint = {}, *found = nullptr;
        hint.ai_family = AF_INET, hint.ai_socktype = SOCK_STREAM;
        if (host.empty() || getaddrinfo(host.c_str(), port.c_str(), &hint, &found) || !found) {
            status = "Cannot reach the relay.";
            return false;
        }
        s = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
        bool ok = s != SEED_BAD && connect(s, found->ai_addr, (int)found->ai_addrlen) == 0;
        freeaddrinfo(found);
        if (!ok) { status = "Cannot connect to the relay."; return false; }
#ifdef _WIN32
        DWORD t = 30000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&t, sizeof t);
#else
        timeval t = {30, 0};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof t);
#endif
        return true;
    }
    bool all(uint8_t *p, size_t n, bool sending) {
        while (n) {
            int k = sending ? send(s, (const char *)p, (int)n, 0) : recv(s, (char *)p, (int)n, 0);
            if (k <= 0) return false;
            p += k, n -= (size_t)k;
        }
        return true;
    }
    bool put(uint8_t type, const uint8_t *body, size_t n) {
        if (n + 1 > 4096) return false;  // (the relay's frame limit)
        uint8_t f[4100] = {(uint8_t)(n + 1), (uint8_t)((n + 1) >> 8), type};
        if (n) memcpy(f + 3, body, n);
        return all(f, n + 3, true);
    }
    bool get(uint8_t &type, std::vector<uint8_t> &body) {
        uint8_t h[2];
        if (!all(h, 2, false)) return false;
        size_t n = h[0] | h[1] << 8;
        if (n < 1) return false;
        body.resize(n);
        if (!all(body.data(), n, false)) return false;
        type = body[0];
        body.erase(body.begin());
        return true;
    }
};

struct Net {  // (Windows: sockets need it once per user)
    Net() {
#ifdef _WIN32
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
#endif
    }
    ~Net() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

void put_str(std::vector<uint8_t> &b, const std::string &s) {
    size_t n = std::min<size_t>(s.size(), 255);
    b.push_back((uint8_t)n);
    b.insert(b.end(), s.begin(), s.begin() + (long)n);
}

std::vector<uint8_t> request(int action, const std::string &name, const std::string &lobby, const std::string &password) {
    std::vector<uint8_t> b = {(uint8_t)action};
    put_str(b, name), put_str(b, lobby), put_str(b, password);
    return b;
}

void digest(const std::vector<uint8_t> &data, unsigned char out[32]) {
    CacheSha256 h;
    h.add(data.data(), data.size());
    unhex32(h.finish(), out);
}

}  // namespace

bool lao_seed_package(const std::string &seed_dir, const std::string &zip_path, std::string &world_hex, std::string &status) {
    fs::path root(seed_dir);
    std::error_code e;
    if (!fs::is_regular_file(root / "seed.json")) {
        status = fs::is_directory(root / "romfs")
                     ? "This seed was made by an older LAS Recomp: make it again on the Randomizer page to share it."
                     : "This folder isn't a finished seed.";
        return false;
    }
    std::string id = seed_id_of(root);
    if (!id_ok(id)) { status = "The seed has no valid seed ID (seed_id.txt)."; return false; }
    if (!world_of(root, world_hex)) { status = "Could not read the seed's files."; return false; }
    std::vector<std::string> files;  // (its own files: not its saves, not the generator's log)
    uint64_t total = 0;
    for (auto it = fs::recursive_directory_iterator(root, e); !e && it != fs::recursive_directory_iterator(); it.increment(e)) {
        std::string rel = it->path().lexically_relative(root).generic_string();
        if (it->is_directory()) {
            if (rel == "save" || rel == "romfs") it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file() || rel == "generate.log") continue;
        if (!rel_ok(rel)) { status = "The seed holds a file with an unusual name: " + rel; return false; }
        files.push_back(rel), total += it->file_size();
    }
    if (e || total > PACKAGE_MAX) { status = e ? "Could not read the seed folder." : "The seed is too large to send."; return false; }
    std::sort(files.begin(), files.end());
    std::string manifest = "{\"format\": \"LAO-WORLD-PATCH-1\", \"baseline\": \"beta_5.4\", \"seed_format\": \"lasrecomp-seed-1\", "
                           "\"seed_id\": \"" + id + "\", \"world_sha256\": \"" + world_hex + "\", \"name\": \"" +
                           clean_name(root.filename().string()) + "\", \"files\": [";
    for (size_t i = 0; i < files.size(); i++) manifest += (i ? ", \"" : "\"") + files[i] + "\"";
    manifest += "]}\n";
    std::string mpath = zip_path + ".manifest.json";
    { std::ofstream m(mpath, std::ios::binary | std::ios::trunc); m << manifest; if (!m) { status = "Could not write the package."; return false; } }
    char why[300] = "";
    ZipWriter *w = zip_create(zip_path.c_str(), why, sizeof why);
    bool ok = w && zip_add_file(w, "manifest.json", mpath.c_str(), true);
    for (auto &f : files) ok = ok && zip_add_file(w, ("seed/" + f).c_str(), (root / f).string().c_str(), true);
    ok = w && zip_finish(w) && ok;
    fs::remove(mpath, e);
    if (!ok) { status = std::string("Could not write the package") + (why[0] ? std::string(": ") + why : "."); return false; }
    status = "packaged " + std::to_string(files.size()) + " file(s) of seed " + id;
    return true;
}

bool lao_seed_unpack(const std::string &zip_path, const std::string &rando_root, std::string &ready, std::string &status) {
    char why[300] = "";
    ZipReader *z = zip_open(zip_path.c_str(), why, sizeof why);
    if (!z) { status = std::string("The seed package can't be read: ") + why; return false; }
    struct Close { ZipReader *z; ~Close() { zip_close(z); } } close{z};
    int mi = zip_find(z, "manifest.json");
    size_t msize = 0;
    char *m = mi >= 0 ? (char *)zip_read(z, mi, &msize) : nullptr;
    std::string manifest = m ? std::string(m, msize) : "";
    free(m);
    if (json_value(manifest, "format") != "LAO-WORLD-PATCH-1" || json_value(manifest, "seed_format") != "lasrecomp-seed-1") {
        status = "The host's game sent a seed this version can't read (a different LA-Online version).";
        return false;
    }
    std::string world = json_value(manifest, "world_sha256"), id = json_value(manifest, "seed_id");
    std::string name = clean_name(json_value(manifest, "name"));
    unsigned char want[32];
    if (!unhex32(world, want) || !id_ok(id)) { status = "The seed package's manifest is invalid."; return false; }
    fs::path rando(rando_root), incoming = rando / ".lao-incoming";
    std::error_code e;
    fs::remove_all(incoming, e);
    fs::create_directories(incoming, e);
    uint64_t total = 0;
    for (int i = 0, n = zip_count(z); i < n; i++) {
        ZipEntry en;
        if (!zip_entry_at(z, i, &en) || en.is_dir || strncmp(en.name, "seed/", 5) != 0) continue;
        std::string rel = en.name + 5;
        if (en.is_symlink || !rel_ok(rel) || (total += en.size) > PACKAGE_MAX) {
            status = "The seed package holds an unsafe entry: " + rel;
            fs::remove_all(incoming, e);
            return false;
        }
        size_t size = 0;
        void *data = zip_read(z, i, &size);
        fs::path out = incoming / fs::path(rel);
        fs::create_directories(out.parent_path(), e);
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (data) f.write((const char *)data, (std::streamsize)size);
        free(data);
        if (!data || !f) { status = "Could not write the seed's files."; fs::remove_all(incoming, e); return false; }
    }
    std::string got;
    if (!world_of(incoming, got) || got != world || seed_id_of(incoming) != id) {
        status = "The seed arrived damaged (its world checksum differs): it is downloaded again.";
        fs::remove_all(incoming, e);
        return false;
    }
    // (into rando/<name>, or the folder already here with this world; another folder of that name: "<name> (2)"...)
    for (int k = 1; k < 100; k++) {
        std::string folder = k == 1 ? name : name + " (" + std::to_string(k) + ")";
        fs::path dest = rando / folder;
        std::string have;
        if (fs::exists(dest)) {
            if (world_of(dest, have) && have == world) {
                fs::remove_all(incoming, e);
                ready = folder, status = "seed " + id + " already here (" + folder + ")";
                return true;
            }
            continue;
        }
        fs::rename(incoming, dest, e);
        if (e) { status = "Could not put the seed in place: " + e.message(); fs::remove_all(incoming, e); return false; }
        ready = folder, status = "seed " + id + " downloaded (" + folder + ")";
        return true;
    }
    fs::remove_all(incoming, e);
    status = "Too many seed folders of that name.";
    return false;
}

bool lao_seed_publish(const std::string &server, const std::string &name, const std::string &lobby,
                      const std::string &password, const std::string &seed_dir, std::string &status) {
    std::string zip = (fs::path(seed_dir).parent_path() / ".lao-seed-upload.zip").string(), world;
    if (!lao_seed_package(seed_dir, zip, world, status)) return false;
    std::ifstream f(zip, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    unsigned char w[32], sha[32];
    unhex32(world, w), digest(data, sha);
    Net net;
    Conn c;
    if (!c.open(server, status)) return false;
    std::vector<uint8_t> req = request(1, name, lobby, password);
    req.insert(req.end(), w, w + 32);
    uint32_t n = (uint32_t)data.size();
    uint8_t size_le[4] = {(uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24)};
    req.insert(req.end(), size_le, size_le + 4);
    req.insert(req.end(), sha, sha + 32);
    uint8_t type = 0;
    std::vector<uint8_t> body;
    if (!c.put(0x16, req.data(), req.size()) || !c.get(type, body) || type != 0x9d || body.empty() || body[0] != 1) {
        status = "The relay didn't take the seed (it accepts two uploads at a time; tried again later).";
        return false;
    }
    for (size_t at = 0; at < data.size(); at += 4000)
        if (!c.put(0x17, data.data() + at, std::min<size_t>(4000, data.size() - at))) { status = "The upload was interrupted."; return false; }
    if (!c.put(0x18, nullptr, 0) || !c.get(type, body) || type != 0x9d || body.empty() || body[0] != 1) {
        status = "The relay refused the seed (the room's world is another one).";
        return false;
    }
    status = "seed uploaded (" + std::to_string(data.size()) + " bytes)";
    return true;
}

bool lao_seed_sync(const std::string &server, const std::string &name, const std::string &lobby,
                   const std::string &password, const std::string &rando_root, std::string &ready, std::string &status) {
    Net net;
    Conn c;
    if (!c.open(server, status)) return false;
    std::vector<uint8_t> req = request(0, name, lobby, password), body;
    uint8_t type = 0;
    if (!c.put(0x16, req.data(), req.size()) || !c.get(type, body) || type != 0x9d || body.empty()) {
        status = "The relay didn't answer.";
        return false;
    }
    if (body[0] != 1 || body.size() != 37) { status = "The host's seed isn't on the relay yet."; return false; }
    uint32_t size = body[1] | body[2] << 8 | body[3] << 16 | (uint32_t)body[4] << 24;
    unsigned char want[32], got[32];
    memcpy(want, body.data() + 5, 32);
    if (size > PACKAGE_MAX) { status = "The room's seed is too large."; return false; }
    std::vector<uint8_t> data;
    while (data.size() < size) {
        if (!c.get(type, body) || type != 0x9e || data.size() + body.size() > size) { status = "The download was interrupted."; return false; }
        data.insert(data.end(), body.begin(), body.end());
    }
    if (!c.get(type, body) || type != 0x9f) { status = "The download was interrupted."; return false; }
    digest(data, got);
    if (memcmp(want, got, 32)) { status = "The seed arrived damaged: it is downloaded again."; return false; }
    std::error_code e;
    fs::create_directories(rando_root, e);
    std::string zip = (fs::path(rando_root) / ".lao-seed-download.zip").string();
    {
        std::ofstream f(zip, std::ios::binary | std::ios::trunc);
        f.write((const char *)data.data(), (std::streamsize)data.size());
        if (!f) { status = "Could not save the seed package."; return false; }
    }
    return lao_seed_unpack(zip, rando_root, ready, status);
}
