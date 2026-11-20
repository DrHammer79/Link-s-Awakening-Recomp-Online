// LAS Recomp Co-Op's port of LA-Online: a relay hosted by the game itself (runtime/laonline_host.cpp) - the Online
// page's "Host a relay on this PC", and `--relay [port]` (a relay alone, no game). Its rules are LA-Online's relay's
// (as tools/laonline_relay.py, the stand-in the tests use). And finding the relays hosted on this network (LAN).
#pragma once
#ifdef __cplusplus
#include <string>
#include <vector>

// Hosting: one thread, until stopped. name: what the network's search shows (the host's player name).
bool lao_host_start(int port, const std::string &name, std::string &error);
void lao_host_stop(void);
bool lao_host_running(void);
int lao_host_port(void);
int lao_host_players(void);  // (connected to one of its lobbies)
// This PC's addresses on its networks (IPv4, not loopback): what to give the others.
std::vector<std::string> lao_host_addresses(void);

// The relays hosted on this PC's networks: those that answer within wait_ms.
struct LaoLanRelay {
    std::string address;  // host:port
    std::string name;
    int players;
};
std::vector<LaoLanRelay> lao_lan_find(int wait_ms);

extern "C" {
#endif
// `--relay [port] [max players]`: a relay alone, its log lines on stdout (as the stand-in's), until killed. Returns the
// exit status.
int lao_host_main(int port, int max_players, void (*print)(const char *text));
bool lao_host_start_c(int port, const char *name);  // (laonline_configured: hosting as the game starts)
#ifdef __cplusplus
}
#endif
