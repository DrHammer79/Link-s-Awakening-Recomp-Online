// V15.1 "Logs for the developer" (developer: players kept hunting for each log file to send one by one).
// One button - Settings > Report / Logs > SEND LOGS TO DEVELOPER - copies every log into the folder
// "LOGS FOR DEVELOPER" next to the game, packs it into one zip and sends that zip through the co-op server to the
// developer's logs webhook (relay: opcode 0x35; the webhook lives only on the server). If the server can't be reached,
// the zip is shown in Explorer to drag into Discord. The folder is also refreshed at every start, so after a crash
// the logs of the run that crashed are already in it.
// Left out on purpose: laonline.ini / larecomp.ini (server address, password), saves, seeds and game files.
#include "imgui.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "../paths.h"
#include "../laonline_build.h"
#ifdef _WIN32
#include <windows.h>
#endif
namespace fs = std::filesystem;
extern "C" void laonline_report_player(char *, int);
extern "C" void laonline_diag_log_code(char *, size_t);
extern "C" int laonline_logs_upload(const char *zip, const char *player, const char *code, char *msg, size_t cap);  // laonline_report_send.cpp

static std::atomic<int> g_state(0);  // 0 idle, 1 working, 2 done, -1 failed
static std::mutex g_mx;
static std::string g_msg, g_zip;
static void set_msg(const std::string &m) { std::lock_guard<std::mutex> l(g_mx); g_msg = m; }

// Every log the game writes (developer: "every single log ... LA online world, LA online rejoin, LA online PVP, all of
// them"): lasrecomp.log + the backups of the last runs, dev-events-log*, every laonline-*.txt / .log / .jsonl,
// BUILD-LOG.txt, crash notes and unsent reports. Not laonline.ini / larecomp.ini (passwords) or saves.
static bool is_log(const std::string &name) {
    auto ends = [&](const char *x) { size_t n = strlen(x); return name.size() >= n && !name.compare(name.size() - n, n, x); };
    if (name.rfind("lasrecomp", 0) == 0 && ends(".log")) return true;                     // lasrecomp.log, backups
    if (name.rfind("dev-events-log", 0) == 0) return true;                                 // + rotated .1 .. .4
    if (name.rfind("laonline-", 0) == 0 && (ends(".txt") || ends(".log") || ends(".jsonl")))
        return name.rfind("laonline-report-", 0) != 0 || ends(".json");                    // (all laonline-* logs)
    return name == "BUILD-LOG.txt" || name == "crash.txt" || ends(".dmp");
}
static fs::path data_root() { const char *r = rt_data_root(); return fs::path(r && r[0] ? r : "."); }
/* V15.2: logs sent from the start screen said "Player" (the online name is only known once you play online) - the
 * name in laonline.ini is used then. */
static void player_name(char *out, int cap) {
    laonline_report_player(out, cap);
    if (out[0] && strcmp(out, "Player")) return;
    std::ifstream ini(data_root() / "laonline.ini"); std::string line;
    while (std::getline(ini, line)) {
        size_t k = line.find_first_not_of(" \t");
        if (k == std::string::npos || line.compare(k, 4, "name") || line.find('=') == std::string::npos) continue;
        std::string key = line.substr(k, line.find('=') - k); while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        if (key != "name") continue;
        std::string v = line.substr(line.find('=') + 1); v = v.substr(0, v.find(';'));
        while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) v.pop_back();
        if (!v.empty()) snprintf(out, (size_t)cap, "%s", v.c_str());
        return;
    }
}
static std::string safe_name(const char *s) {
    std::string o;
    for (const char *p = s; *p && o.size() < 16; p++) o += (isalnum((unsigned char)*p) || *p == '-' || *p == '_') ? *p : '_';
    return o.empty() ? "Player" : o;
}
// copy the logs into <data>/LOGS FOR DEVELOPER (old copies there are replaced); returns how many files.
// small: for a second try when the zip is over Discord's limit - only the newest 2 backups, and big files keep their
// last 1.5 MB (the end is where the problem is).
static int collect(const fs::path &root, const fs::path &dir, std::string &list, bool small = false) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (auto &e : fs::directory_iterator(dir, ec)) fs::remove_all(e.path(), ec);
    int n = 0;
    auto take = [&](const fs::path &src, const std::string &as) {
        std::error_code e2;
        if (!fs::is_regular_file(src, e2)) return;
        uintmax_t size = fs::file_size(src, e2), keep = 1536 * 1024;
        if (small && size > keep) {  // tail only
            std::ifstream in(src, std::ios::binary); in.seekg((std::streamoff)(size - keep));
            std::ofstream out(dir / as, std::ios::binary | std::ios::trunc);
            out << "[...first " << (size - keep) / 1024 << " KB left out to fit Discord...]\n" << in.rdbuf();
            n++; list += "  " + as + " (last 1.5 MB)\n"; return;
        }
        fs::copy_file(src, dir / as, fs::copy_options::overwrite_existing, e2);
        if (!e2) { n++; list += "  " + as + "\n"; }
    };
    std::vector<fs::path> backups;
    for (auto &e : fs::directory_iterator(root, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string name = e.path().filename().string();
        if (!is_log(name)) continue;
        if (name.rfind("lasrecomp-backup-", 0) == 0) { backups.push_back(e.path()); continue; }
        if (small && name.rfind("dev-events-log.txt.", 0) == 0) continue;  // rotated older events
        take(e.path(), name);
    }
    std::sort(backups.begin(), backups.end(), [](const fs::path &a, const fs::path &b) {
        std::error_code e1, e2; return fs::last_write_time(a, e1) > fs::last_write_time(b, e2); });
    for (size_t i = 0; i < backups.size() && i < (small ? 2u : 6u); i++) take(backups[i], backups[i].filename().string());
    if (fs::is_directory(root / "reports-pending", ec))
        for (auto &e : fs::directory_iterator(root / "reports-pending", ec)) take(e.path(), "report-" + e.path().filename().string());
    char player[17] = {0}; player_name(player, sizeof player);
    char code[10] = {0}; laonline_diag_log_code(code, sizeof code);
    time_t now = time(NULL); char stamp[64]; strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));
    std::ofstream info(dir / "INFO.txt", std::ios::trunc);
    info << "LA-Online logs\nPlayer: " << player << "\nVersion: " LAO_RELEASE_LABEL "\nLog code: " << code << "\nCollected: " << stamp
         << "\n\nFiles:\n" << list;
    std::ofstream how(dir / "HOW TO SEND.txt", std::ios::trunc);
    how << "Send these logs to the developer:\n"
           "In the game: Settings > Report / Logs > SEND LOGS TO DEVELOPER. They go straight to the developer's Discord.\n\n"
           "If that can't reach the server, the game makes one zip file (LA-Online-Logs-....zip) next to this folder\n"
           "and shows it to you - drag it into Discord.\n"
           "No game? Right-click this folder > Send to > Compressed (zipped) folder, and drag that zip into Discord.\n"
           "No passwords or saves are in here.\n";
    return n;
}

extern "C" void laonline_logs_refresh_async(void) {  // at start: the folder holds the previous runs' logs
    std::thread([] { std::string list; std::error_code ec; fs::path root = data_root(); collect(root, root / "LOGS FOR DEVELOPER", list); }).detach();
}

#ifdef _WIN32
static bool run_hidden(std::wstring cmd, DWORD wait_ms) {
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
    DWORD w = WaitForSingleObject(pi.hProcess, wait_ms), code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return w == WAIT_OBJECT_0 && code == 0;
}
#endif

#ifdef _WIN32
static bool compress(const fs::path &dir, const fs::path &zip) {
    auto ps_quote = [](std::wstring w) { std::wstring o; for (wchar_t c : w) { o += c; if (c == L'\'') o += L'\''; } return o; };  // ' -> ''
    std::wstring src = ps_quote((dir / "*").wstring()), dst = ps_quote(zip.wstring());
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Compress-Archive -Path '" + src +
                       L"' -DestinationPath '" + dst + L"' -CompressionLevel Optimal -Force\"";
    std::error_code ec;
    return run_hidden(cmd, 180000) && fs::exists(zip, ec);
}
#endif
static const uintmax_t k_discord_max = (uintmax_t)(9.5 * 1024 * 1024);
static void make_zip() {
    fs::path root = data_root(), dir = root / "LOGS FOR DEVELOPER";
    std::string list; int n = collect(root, dir, list);
    if (!n) { set_msg("No log files were found next to the game."); g_state = -1; return; }
    char player[17] = {0}; player_name(player, sizeof player);
    char code[10] = {0}; laonline_diag_log_code(code, sizeof code);
    time_t now = time(NULL); char stamp[32]; strftime(stamp, sizeof stamp, "%Y-%m-%d_%H%M", localtime(&now));
    fs::path zip = root / ("LA-Online-Logs-" + safe_name(player) + "-" + stamp + ".zip");
    std::error_code ec;
    for (auto &e : fs::directory_iterator(root, ec)) {  // keep only the newest zip (old ones would pile up)
        std::string name = e.path().filename().string();
        if (name.rfind("LA-Online-Logs-", 0) == 0 && e.path().extension() == ".zip") fs::remove(e.path(), ec);
    }
#ifdef _WIN32
    set_msg("Packing your logs...");
    bool zipped = compress(dir, zip);
    if (zipped && fs::file_size(zip, ec) > k_discord_max) {  // too big for Discord: newest logs, their ends only
        fs::remove(zip, ec); list.clear(); n = collect(root, dir, list, true); zipped = compress(dir, zip);
    }
    if (!zipped) {
        run_hidden(L"explorer.exe \"" + dir.wstring() + L"\"", 0);
        set_msg("The zip couldn't be made, so the folder \"LOGS FOR DEVELOPER\" was opened. Right-click it > Send to > "
                "Compressed (zipped) folder, then drag that zip into Discord.");
        g_state = -1; return;
    }
    char mb[32]; snprintf(mb, sizeof mb, "%.1f MB", (double)fs::file_size(zip, ec) / 1048576.0);
    { std::lock_guard<std::mutex> l(g_mx); g_zip = zip.filename().string(); }
    set_msg(std::to_string(n) + " log files packed (" + mb + "). Sending them to the developer...");
    char why[400] = {0};
    int sent = laonline_logs_upload(zip.string().c_str(), player, code, why, sizeof why);
    if (sent == 1) {
        set_msg(std::string("Sent! ") + std::to_string(n) + " log files (" + mb + ") went to the developer's Discord." +
                (code[0] ? std::string(" Your log code: ") + code + "." : std::string()));
        g_state = 2; return;
    }
    run_hidden(L"explorer.exe /select,\"" + zip.wstring() + L"\"", 0);
    set_msg(std::string(why[0] ? why : "The logs couldn't be sent.") + " The zip " + zip.filename().string() +
            " is shown in Explorer - drag it into Discord instead.");
    g_state = -1; return;
#else
    (void)stamp;
    set_msg(std::to_string(n) + " log files are in the folder \"LOGS FOR DEVELOPER\" next to the game. Zip that folder and "
            "drag the zip into Discord.");
    g_state = 2; return;
#endif
}

// Settings > Report / Logs (laonline_report.mm): the button and its status line
extern "C" void laonline_logs_draw(float width, float height) {
    int s = g_state.load();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.13f, .45f, .28f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.18f, .58f, .36f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.10f, .36f, .22f, 1));
    ImGui::BeginDisabled(s == 1);
    if (ImGui::Button(s == 1 ? "Sending your logs...##devlogs" : "SEND LOGS TO DEVELOPER##devlogs", ImVec2(width, height))) {
        g_state = 1; set_msg("Packing your logs...");
        std::thread(make_zip).detach();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Sends every log straight to the developer's Discord (through your co-op server).\n"
                          "If that doesn't work, you get one zip to drag into Discord. No passwords or saves are included.");
    std::string m; { std::lock_guard<std::mutex> l(g_mx); m = g_msg; }
    if (!m.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, s == -1 ? ImVec4(1, .8f, .4f, 1) : ImVec4(.55f, .95f, .65f, 1));
        ImGui::SetWindowFontScale(.8f); ImGui::TextWrapped("%s", m.c_str()); ImGui::SetWindowFontScale(1);
        ImGui::PopStyleColor();
    }
}
