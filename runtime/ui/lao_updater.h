#pragma once
// V13 Update Now (see lao_updater.mm).
#include <string>
#include "coop_version.h"
namespace lao_updater {
enum { IDLE, DOWNLOADING, PREPARING, READY, INSTALLING, FAILED };
void start(const std::string &url, long long size, const std::string &tag, int rank);  // download + unpack in the background
int state();
float progress();          // 0..1
std::string message();
bool busy();
bool install_and_restart(); // READY: starts the installer step; the caller then closes the game
std::string just_updated(); // version installed by the last Update Now ("" if none), read once
void draw_notes(const std::string &markdown, float scale);
}
