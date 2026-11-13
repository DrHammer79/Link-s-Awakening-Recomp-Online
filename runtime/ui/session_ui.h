#pragma once
#include <string>
// Return true once, only after the real server starts this room.
bool session_ui_draw(bool browser,float scale,int mode,const std::string &world,const std::string &seed,bool can_start);
void session_ui_begin(void);
void session_ui_leave(void);
bool session_ui_active(void);
bool session_ui_busy(void);
int session_ui_mode(void);
bool session_ui_take_browse(void);
void session_ui_capture_create(void);

// V13 join-in-progress: the room this player is in (for the launcher's automatic seed download / upload)
struct SessionRoomInfo{bool in_room=false,host=false,started=false,rando=false;std::string code,lobby,password,world,seed,server,name;};
SessionRoomInfo session_ui_room_info(void);
void session_ui_set_join_status(const std::string &text);  // shown in the room while joining ("Downloading randomizer seed...")

void session_leaderboards_draw(float scale);
bool session_leaderboards_take_back(void);  // V12.5: header back arrow pressed

const char *session_global_count_text(void);
