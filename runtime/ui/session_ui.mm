#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using UiSocket=SOCKET;
#define UI_BAD_SOCKET INVALID_SOCKET
#define ui_close closesocket
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
using UiSocket=int;
#define UI_BAD_SOCKET (-1)
#define ui_close close
#endif
#include "session_ui.h"
#include "tropical_ui.h"
#include "lao_icons.h"
#include "settings_look.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <fstream>
#include <algorithm>
#include <math.h>
#include <stdexcept>
#include <string.h>
#include <stdlib.h>
extern "C" {
#include "../settings.h"
#include "../laonline_build.h"  // V12: room build tag
#include "../cosmetic_colour.h"
}
extern "C" void laonline_session_override(const char *,const char *,const char *);
extern bool laonline_avatar_icon(const uint8_t *,const uint8_t *,ImTextureID *,float *);
struct RoomMember{std::string name;uint8_t rgb[3],hair[3];bool ready,host;};
struct UiRoom{std::string code,title,description,host,build,world,seed;int mode=1,capacity=4;bool public_room=false,started=false;std::vector<RoomMember> players;};
struct DirectoryState{std::string self_name,token,password,lobby,message="Choose a session or enter a room code.";std::vector<UiRoom> rooms;int total=0,page=0;bool success=false;};
static DirectoryState listing,membership;static std::mutex response_lock;static std::atomic<bool> busy(false);static unsigned generation;static double last_poll;static bool started_once;
static char username[17],room_code[7],session_title[49],description[81],search[49];static std::string endpoint;static int filter_mode,sort_mode;static int current_mode=1;static std::string current_world,current_seed;
static bool browse_requested;static char edited_name[17];static double last_copy=-100;static int session_filter;static bool search_dirty;static double search_changed;
static std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==std::string::npos?"":s.substr(a,b-a+1);}
static void str(std::vector<uint8_t>& b,const std::string &s){if(s.size()>255)throw std::runtime_error("Text too long.");b.push_back((uint8_t)s.size());b.insert(b.end(),s.begin(),s.end());}
struct WireReader{const std::vector<uint8_t>& b;size_t i=0;int byte(){if(i>=b.size())throw std::runtime_error("Incomplete relay response.");return b[i++];}int word(){int a=byte();return a|(byte()<<8);}std::string string(){int n=byte();if(i+n>b.size())throw std::runtime_error("Incomplete relay response.");std::string s((const char*)b.data()+i,n);i+=n;return s;}};
static bool transfer(UiSocket sock,uint8_t *data,int n,bool sending){while(n){int k=sending?send(sock,(const char*)data,n,0):recv(sock,(char*)data,n,0);if(k<=0)return false;data+=k;n-=k;}return true;}
static std::vector<uint8_t> exchange(const std::string &address,std::vector<uint8_t> request,uint8_t opcode=0x30){
#ifdef _WIN32
 WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w))throw std::runtime_error("Cannot initialize networking.");
 struct Cleanup{~Cleanup(){WSACleanup();}} cleanup;
#endif
 std::string host=address,port="47620";auto split=host.rfind(':');if(split!=std::string::npos){port=host.substr(split+1);host.resize(split);}if(host.empty())throw std::runtime_error("No relay configured. Run the player installer to set your server.");
 addrinfo hint={},*found=nullptr;hint.ai_family=AF_INET;hint.ai_socktype=SOCK_STREAM;if(getaddrinfo(host.c_str(),port.c_str(),&hint,&found))throw std::runtime_error("Cannot reach the configured relay.");
 UiSocket socket_fd=socket(found->ai_family,found->ai_socktype,found->ai_protocol);
 if(socket_fd==UI_BAD_SOCKET){freeaddrinfo(found);throw std::runtime_error("Cannot open a connection.");}
 struct SocketGuard{UiSocket socket_fd;~SocketGuard(){ui_close(socket_fd);}} guard{socket_fd};
#ifdef _WIN32
 DWORD timeout=3000;setsockopt(socket_fd,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof timeout);setsockopt(socket_fd,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof timeout);u_long nonblock=1;ioctlsocket(socket_fd,FIONBIO,&nonblock);
#else
 timeval timeout={3,0};setsockopt(socket_fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(socket_fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
#endif
 int connected=connect(socket_fd,found->ai_addr,(int)found->ai_addrlen);freeaddrinfo(found);
#ifdef _WIN32
 if(connected && WSAGetLastError()==WSAEWOULDBLOCK){fd_set writable;FD_ZERO(&writable);FD_SET(socket_fd,&writable);timeval wait={3,0};if(select(0,nullptr,&writable,nullptr,&wait)>0){int error=0,len=sizeof error;getsockopt(socket_fd,SOL_SOCKET,SO_ERROR,(char*)&error,&len);connected=error?1:0;}}nonblock=0;ioctlsocket(socket_fd,FIONBIO,&nonblock);
#endif
 if(connected)throw std::runtime_error("Cannot connect. Check your connection and try Refresh.");
 std::vector<uint8_t> packet={(uint8_t)(request.size()+1),(uint8_t)((request.size()+1)>>8),opcode};packet.insert(packet.end(),request.begin(),request.end());if(!transfer(socket_fd,packet.data(),(int)packet.size(),true))throw std::runtime_error("Connection interrupted.");
 uint8_t header[3];if(!transfer(socket_fd,header,3,false))throw std::runtime_error("Session service unavailable. The host needs the matching V12 relay update.");int n=header[0]|(header[1]<<8);if(header[2]!=(uint8_t)(opcode+0x80)||n<2||n>16384)throw std::runtime_error("Session service unavailable. The host needs the matching V12 relay update.");std::vector<uint8_t> response(n-1);if(!transfer(socket_fd,response.data(),n-1,false))throw std::runtime_error("Incomplete relay response.");return response;
}
static DirectoryState decode(const std::vector<uint8_t>& bytes){DirectoryState state;WireReader r{bytes};state.success=r.byte()==1;state.message=r.string();if(!state.success)return state;state.token=r.string();state.password=r.string();state.lobby=r.string();state.total=r.word();state.page=r.word();int count=r.byte();if(count>8)throw std::runtime_error("Invalid session count.");for(int i=0;i<count;i++){UiRoom room;room.code=r.string();room.title=r.string();room.description=r.string();room.host=r.string();room.mode=r.byte();room.public_room=r.byte()!=0;room.started=r.byte()!=0;room.capacity=r.byte();room.build=r.string();room.world=r.string();room.seed=r.string();int players=r.byte();if(players>4||room.capacity!=4)throw std::runtime_error("Unsupported room capacity.");for(int p=0;p<players;p++){RoomMember member;member.name=r.string();for(auto &c:member.rgb)c=(uint8_t)r.byte();for(auto &c:member.hair)c=(uint8_t)r.byte();member.ready=r.byte()!=0;member.host=r.byte()!=0;room.players.push_back(member);}state.rooms.push_back(room);}if(r.i!=bytes.size())throw std::runtime_error("Unexpected relay response.");return state;}
/* V12.5: the room view polls every 2 s (action 4). V12 disabled Back / Ready Up / Start for every poll, so they
 * flickered (Velvet_Charm's report) and a press during a poll was lost. Polls are now background-only: controls only
 * grey out for the player's own action, and a press during a poll is queued and sent right after it. */
static std::atomic<bool> acting(false);
static struct PendingAction{bool set=false;int action=0;std::string code;bool ready=false;int page=0;std::string name;bool named=false;} pending_action;
/* V13 join-in-progress: the membership token of the last joined room is kept in laonline-rejoin.txt, so a player whose
 * game closed can take their reserved slot back for 2 minutes (relay: same token = same slot, never just the name). */
static void rejoin_save(const std::string &code,const std::string &token){std::ofstream f("laonline-rejoin.txt",std::ios::trunc);if(f&&!code.empty())f<<code<<"\n"<<token<<"\n";}
static std::string rejoin_token(const std::string &code){std::ifstream f("laonline-rejoin.txt");std::string c,t;if(f&&std::getline(f,c)&&std::getline(f,t)&&c==code&&t.size()==32)return t;return "";}
static std::string join_status;static std::mutex join_status_lock;
void session_ui_set_join_status(const std::string &text){std::lock_guard<std::mutex> lock(join_status_lock);join_status=text;}
static void request(int action,const std::string &code="",bool ready=false,int page=0,const char *name_override=nullptr){
 if(busy.load()&&action!=4&&action!=0){std::lock_guard<std::mutex> lock(response_lock);pending_action.set=true;pending_action.action=action;pending_action.code=code;pending_action.ready=ready;pending_action.page=page;pending_action.named=name_override!=nullptr;pending_action.name=name_override?name_override:"";acting=true;return;}
 if(busy.exchange(true))return;if(action!=4&&action!=0)acting=true;std::string token;{std::lock_guard<std::mutex> lock(response_lock);token=membership.token;}if(action==3&&token.empty())token=rejoin_token(code);unsigned epoch=generation;std::vector<uint8_t> b={1,(uint8_t)action};
 str(b,code);str(b,token);std::string request_name=name_override?name_override:(username[0]?username:"Player");str(b,request_name);str(b,LAO_ROOM_BUILD);b.push_back((uint8_t)current_mode);str(b,current_world);str(b,current_seed);str(b,session_title);str(b,description);b.push_back(ready);uint32_t rgb=0x3fa34d;col_target(&g_settings.tunic,COL_SLOT_TUNIC,&rgb);b.insert(b.end(),{(uint8_t)(rgb>>16),(uint8_t)(rgb>>8),(uint8_t)rgb});uint32_t hair=0xffdf57;col_target(&g_settings.hair,COL_SLOT_HAIR,&hair);b.insert(b.end(),{(uint8_t)(hair>>16),(uint8_t)(hair>>8),(uint8_t)hair});str(b,search);b.push_back((uint8_t)filter_mode);b.push_back((uint8_t)sort_mode);b.push_back((uint8_t)page);b.push_back((uint8_t)(page>>8));std::string address=endpoint;
 std::thread([b,address,action,epoch,request_name](){DirectoryState state;try{state=decode(exchange(address,b));}catch(const std::exception &e){state.message=e.what();}std::lock_guard<std::mutex> lock(response_lock);if(epoch==generation){if(action==0)listing=state;else if(action==7){membership={};membership.message=state.success?"Left session.":state.message;rejoin_save("","");}else if(state.success){if(action==9)state.self_name=request_name;membership=state;if(action!=4&&!state.rooms.empty())rejoin_save(state.rooms[0].code,state.token);}else if(action==4&&membership.success){/* one failed background poll: keep the room on screen, retry next poll */}else {membership.message=state.message;membership.success=false;}}if(action!=4&&action!=0)acting=false;busy=false;}).detach();
}
void session_ui_begin(void){std::lock_guard<std::mutex> lock(response_lock);generation++;listing={};membership={};started_once=false;room_code[0]=0;endpoint.clear();username[0]=0;  /* V12.5: empty until set - the field shows "Enter your username..." */std::ifstream f("laonline.ini");std::string line;while(std::getline(f,line)){auto comment=line.find(';');if(comment!=std::string::npos)line.resize(comment);auto eq=line.find('=');if(eq==std::string::npos)continue;auto key=trim(line.substr(0,eq)),value=trim(line.substr(eq+1));if(key=="name")snprintf(username,sizeof username,"%.16s",value.c_str());if(key=="server")endpoint=value;}laonline_session_override(nullptr,nullptr,nullptr);last_poll=-10;}
bool session_ui_active(void){std::lock_guard<std::mutex> lock(response_lock);return !membership.token.empty();}
bool session_ui_busy(void){return acting.load();}
int session_ui_mode(void){return current_mode;}
SessionRoomInfo session_ui_room_info(void){SessionRoomInfo info;std::lock_guard<std::mutex> lock(response_lock);
 if(membership.token.empty()||membership.rooms.empty())return info;const UiRoom &room=membership.rooms[0];
 info.in_room=true;info.started=room.started;info.rando=room.mode==2;info.code=room.code;info.lobby=membership.lobby;info.password=membership.password;info.world=room.world;info.seed=room.seed;info.server=endpoint;
 std::string me=membership.self_name.empty()?(username[0]?username:"Player"):membership.self_name;info.name=me;for(auto &m:room.players)if(m.name==me)info.host=m.host;return info;}
bool session_ui_take_browse(void){bool value=browse_requested;browse_requested=false;return value;}
void session_ui_leave(void){DirectoryState state;{std::lock_guard<std::mutex> lock(response_lock);state=membership;}if(!state.rooms.empty())request(7,state.rooms[0].code);}
void session_ui_capture_create(void){if(getenv("LARECOMP_LAUNCHER_SHOT")&&getenv("LARECOMP_SESSION_CAPTURE")){const char *code=getenv("LARECOMP_SESSION_CAPTURE_JOIN");if(code&&strlen(code)==6)request(3,code);else request(1);}}
static void member_portrait(const RoomMember &member,float size){ImTextureID tex=0;float aspect=1;if(laonline_avatar_icon(member.rgb,member.hair,&tex,&aspect))ImGui::Image(tex,ImVec2(size,size));else ImGui::Dummy(ImVec2(size,size));}
static bool host_card(bool public_room,ImVec2 size,float scale){
 ImGui::PushID(public_room?"public":"private");ImVec2 p=ImGui::GetCursorScreenPos();bool click=ImGui::InvisibleButton("host",size,ImGuiButtonFlags_EnableNav);auto *d=ImGui::GetWindowDrawList();bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused();ImU32 accent=public_room?IM_COL32(20,200,245,255):IM_COL32(40,225,90,255);ImVec2 end(p.x+size.x,p.y+size.y);d->AddRectFilled(p,end,IM_COL32(0,29,57,245),13*scale);d->AddRect(p,end,accent,13*scale,0,(hot?4:2)*scale);
 if(hot)d->AddRect(ImVec2(p.x-4*scale,p.y-4*scale),ImVec2(end.x+4*scale,end.y+4*scale),(accent&0xffffff)|0x70000000,15*scale,0,3*scale);
 ImVec2 c(p.x+size.x*.5f,p.y+size.y*.27f);for(int i=-1;i<=1;i++){float x=c.x+i*25*scale,y=c.y+(i?5:0)*scale;d->AddCircleFilled(ImVec2(x,y),12*scale,IM_COL32_WHITE);d->AddRectFilled(ImVec2(x-15*scale,y+16*scale),ImVec2(x+15*scale,y+43*scale),IM_COL32_WHITE,8*scale);}
 ImVec2 symbol(c.x+63*scale,c.y+30*scale);if(public_room){d->AddCircle(symbol,22*scale,IM_COL32_WHITE,24,3*scale);d->AddEllipse(symbol,ImVec2(11*scale,22*scale),IM_COL32_WHITE,0,24,2*scale);d->AddLine(ImVec2(symbol.x-22*scale,symbol.y),ImVec2(symbol.x+22*scale,symbol.y),IM_COL32_WHITE,2*scale);}else{d->AddRectFilled(ImVec2(symbol.x-16*scale,symbol.y-3*scale),ImVec2(symbol.x+16*scale,symbol.y+25*scale),IM_COL32_WHITE,5*scale);d->AddCircle(ImVec2(symbol.x,symbol.y-3*scale),11*scale,IM_COL32_WHITE,20,5*scale);}
 auto center=[&](ImFont *font,const char *text,float y,float fs){float w=font->CalcTextSizeA(fs,1e6f,0,text).x;if(w>size.x-24*scale)fs*=(size.x-24*scale)/w;w=font->CalcTextSizeA(fs,1e6f,0,text).x;d->AddText(font,fs,ImVec2(p.x+(size.x-w)*.5f,p.y+y),IM_COL32_WHITE,text);};
 center(tropical_heading_font(),public_room?"Host a Public Session":"Host a Private Session",size.y*.51f,27*scale);center(tropical_body_font(),public_room?"Allow anyone to find and join your session.":"Play with family or friends using a room code.",size.y*.66f,17*scale);
 {float by0=p.y+size.y*.78f,by1=end.y-16*scale,bfs=fminf(30*scale,(by1-by0)*.55f);ImU32 top=(accent&0x00ffffff)|0xff000000;  // V12.5: rounded button, label centred
  d->AddRectFilled(ImVec2(p.x+16*scale,by0),ImVec2(end.x-16*scale,by1),accent,(by1-by0)*.45f);d->AddRectFilled(ImVec2(p.x+22*scale,by0+3*scale),ImVec2(end.x-22*scale,(by0+by1)*.5f),IM_COL32(255,255,255,40),(by1-by0)*.3f);
  d->AddRect(ImVec2(p.x+16*scale,by0),ImVec2(end.x-16*scale,by1),IM_COL32(255,255,255,hot?230:110),(by1-by0)*.45f,0,1.5f*scale);
  center(tropical_heading_font(),public_room?"Host Public":"Host Private",(by0+by1)*.5f-p.y-bfs*.55f,bfs);}ImGui::PopID();return click;
}
bool session_ui_draw(bool browser,float scale,int mode,const std::string &world,const std::string &seed,bool can_start){
 DirectoryState state,list;{std::lock_guard<std::mutex> lock(response_lock);state=membership;list=listing;}bool in_room=!state.token.empty()&&!state.rooms.empty();current_mode=mode;current_world=world;current_seed=seed;
 if(!in_room&&browser){ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Your Username");ImGui::SameLine();ImGui::SetNextItemWidth(260*scale);ImGui::InputTextWithHint("##browse-username","Enter your username...",username,sizeof username);ImGui::Separator();}

 if(in_room&&!username[0])snprintf(username,sizeof username,"Player");  // (the name sent when the field was left empty)
 if(in_room){const UiRoom &room=state.rooms[0];current_mode=room.mode;if(!state.self_name.empty())snprintf(username,sizeof username,"%s",state.self_name.c_str());

  {PendingAction run;{std::lock_guard<std::mutex> lock(response_lock);if(!busy.load()&&pending_action.set){run=pending_action;pending_action=PendingAction();}}
   if(run.set){acting=false;request(run.action,run.code,run.ready,run.page,run.named?run.name.c_str():nullptr);}}
  bool user_busy=acting.load();
  ImGui::BeginDisabled(user_busy);if(ImGui::Button("<",ImVec2(36*scale,36*scale)))session_ui_leave();ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginGroup();ImGui::PushFont(tropical_heading_font());ImGui::TextUnformatted(room.mode==2?(room.public_room?"Co-Op Randomizer > Public Session":"Co-Op Randomizer > Private Session"):(room.public_room?"Play Co-Op > Public Session":"Play Co-Op > Private Session"));ImGui::PopFont();ImGui::TextWrapped("%s",room.description.empty()?"Share the room code with your family or friends to join.":room.description.c_str());ImGui::EndGroup();ImGui::Spacing();ImGui::Separator();float w=ImGui::GetContentRegionAvail().x;
  float room_h=fmaxf(300*scale,ImGui::GetContentRegionAvail().y-ImGui::GetTextLineHeightWithSpacing()*2.5f);  // V12.5: fits the panel
  bool own_host=false,own_ready=false;for(auto &m:room.players)if(m.name==username)own_host=m.host,own_ready=m.ready;
  {/* V12.5: "<name> connected." / "<name> left." when the room's player list changes */
   static std::string last_room;static std::vector<std::string> last_names;static std::string notice;static double notice_at=-100;
   std::vector<std::string> names;for(auto &m:room.players)names.push_back(m.name);
   if(last_room==room.code){for(auto &n:names)if(std::find(last_names.begin(),last_names.end(),n)==last_names.end()&&n!=username){notice=n+" connected.";notice_at=ImGui::GetTime();}
    for(auto &n:last_names)if(std::find(names.begin(),names.end(),n)==names.end()){notice=n+" left the session.";notice_at=ImGui::GetTime();}}
   last_room=room.code;last_names=names;
   if(ImGui::GetTime()-notice_at<6){ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.05f,.35f,.18f,.95f));ImGui::BeginChild("join-notice",ImVec2(-1,34*scale),ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar);ImGui::TextUnformatted(notice.c_str());ImGui::EndChild();ImGui::PopStyleColor();}}
  ImGui::BeginChild("session-info",ImVec2(w*.41f,room_h),ImGuiChildFlags_Borders);ImGui::SetWindowFontScale(1.2f);ImGui::Spacing();ImGui::PushFont(tropical_heading_font());ImGui::TextUnformatted("Your Username");ImGui::PopFont();ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-40*scale);ImGui::InputText("##saved-username",username,sizeof username,ImGuiInputTextFlags_ReadOnly);ImGui::SameLine();ImGui::BeginDisabled(user_busy||own_ready||room.started);if(ImGui::Button("Edit")){snprintf(edited_name,sizeof edited_name,"%s",username);ImGui::OpenPopup("Edit your session name");}ImGui::EndDisabled();
  if(ImGui::BeginPopup("Edit your session name")){ImGui::TextUnformatted("Your name in this session");ImGui::InputText("Name",edited_name,sizeof edited_name);if(ImGui::Button("Apply")){request(9,room.code,false,0,edited_name);ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
  ImGui::Spacing();ImGui::Separator();ImGui::Spacing();ImGui::PushFont(tropical_heading_font());ImGui::TextUnformatted(room.public_room?"Session Status":"Room Code");ImGui::PopFont();
  if(room.public_room){ImGui::SetWindowFontScale(2.1f);ImGui::TextColored(state.success?ImVec4(.1f,1,.5f,1):ImVec4(1,.7f,.3f,1),"%s",!state.success?"Reconnecting":room.started?"In Progress":"Open");ImGui::SetWindowFontScale(1.2f);}
  else{ImGui::BeginChild("room-code-box",ImVec2(ImGui::GetContentRegionAvail().x-42*scale,70*scale),ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);ImGui::SetWindowFontScale(2.7f);ImGui::TextUnformatted(room.code.c_str());ImGui::EndChild();ImGui::SameLine();ImGui::BeginDisabled(user_busy||!own_host||room.players.size()!=1||room.started);if(ImGui::Button("New",ImVec2(36*scale,70*scale)))request(8,room.code);ImGui::EndDisabled();if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Generate a new code before other players join.");}
  ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,.6f,.25f,1));if(ImGui::Button("Share Room Code",ImVec2(-1,42*scale))){SDL_SetClipboardText(room.code.c_str());last_copy=ImGui::GetTime();}ImGui::PopStyleColor();if(ImGui::GetTime()-last_copy<3)ImGui::TextColored(ImVec4(.2f,1,.5f,1),"Room code copied.");ImGui::Spacing();ImGui::TextWrapped(room.public_room?"Your session is publicly listed and can be joined by anyone with a compatible game.":"Share this code with your friends to join your session.");ImGui::EndChild();ImGui::SameLine();
  ImGui::BeginChild("session-members",ImVec2(0,room_h),ImGuiChildFlags_Borders);ImGui::SetWindowFontScale(1.2f);ImGui::PushFont(tropical_heading_font());ImGui::Text("Player Slots (%d/4)",(int)room.players.size());ImGui::PopFont();ImGui::Separator();bool host=false,me_ready=false,all_ready=true;
  for(int i=0;i<4;i++){ImGui::PushID(i);ImGui::BeginChild("slot",ImVec2(-1,52*scale),ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar);ImGui::SetWindowFontScale(1.2f);ImGui::Text("%d",i+1);ImGui::SameLine(0,14*scale);
   if(i<(int)room.players.size()){auto &member=room.players[i];member_portrait(member,34*scale);ImGui::SameLine(0,12*scale);ImGui::BeginGroup();ImGui::TextUnformatted(member.name.c_str());ImGui::TextColored(member.ready?ImVec4(.1f,1,.5f,1):ImVec4(.75f,.82f,.9f,1),"%s%s",member.host?"Host - ":"",member.ready?"Ready":"Not ready");ImGui::EndGroup();all_ready&=member.ready;if(member.name==username)host=member.host,me_ready=member.ready;}
   else{ImVec2 at=ImGui::GetCursorScreenPos();auto *draw=ImGui::GetWindowDrawList();draw->AddCircle(ImVec2(at.x+17*scale,at.y+17*scale),17*scale,IM_COL32(145,170,190,255),24,2*scale);draw->AddCircleFilled(ImVec2(at.x+17*scale,at.y+12*scale),6*scale,IM_COL32(145,170,190,255));draw->AddRectFilled(ImVec2(at.x+8*scale,at.y+21*scale),ImVec2(at.x+26*scale,at.y+28*scale),IM_COL32(145,170,190,255),5*scale);ImGui::Dummy(ImVec2(34*scale,34*scale));ImGui::SameLine(0,12*scale);ImGui::TextDisabled("Waiting for players...");}
   ImGui::EndChild();ImGui::PopID();}
  bool seed_ok=room.mode!=2||(current_world==room.world&&current_seed==room.seed);
  if(room.started){
   /* V13 join-in-progress: the adventure is already running - no Ready / Start; the game starts as soon as this PC
    * has the room's exact world (downloaded automatically for Co-op Randomizer) and the game files are ready. */
   ImGui::Spacing();ImGui::TextColored(ImVec4(1,.85f,.3f,1),"%s","Adventure in progress");
   std::string js;{std::lock_guard<std::mutex> lock(join_status_lock);js=join_status;}
   if(!seed_ok)ImGui::TextWrapped("%s",js.empty()?"Downloading randomizer seed...":js.c_str());
   else if(!can_start)ImGui::TextWrapped("%s",js.empty()?"Preparing the randomized world...":js.c_str());
   else ImGui::TextColored(ImVec4(.3f,1,.6f,1),"%s","Adventure ready! Joining...");
   ImGui::TextDisabled("Shared story progress is sent when you arrive.");
  }else{
  ImGui::BeginDisabled(user_busy||room.started||(!can_start&&!me_ready)||!state.success);if(ImGui::Button(me_ready?"Cancel Ready":"Ready Up",ImVec2(-1,38*scale)))request(5,room.code,!me_ready);ImGui::EndDisabled();
  if(host){
   /* V12.5: the host stays in control - Start lights up (pulsing gold) only when everyone is ready; it never starts by itself. */
   bool start_ready=all_ready&&can_start&&!room.started&&state.success&&room.players.size()>=2;
   ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.95f,.68f,.05f,1));ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.12f,.10f,.02f,1));ImGui::BeginDisabled(user_busy||!all_ready||!can_start||room.started||!state.success);if(ImGui::Button("Start Adventure",ImVec2(-1,38*scale)))request(6,room.code);ImGui::EndDisabled();ImGui::PopStyleColor(2);
   if(start_ready){ImVec2 a=ImGui::GetItemRectMin(),b2=ImGui::GetItemRectMax();float pulse=.5f+.5f*sinf((float)ImGui::GetTime()*4);ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x-3*scale,a.y-3*scale),ImVec2(b2.x+3*scale,b2.y+3*scale),IM_COL32(255,236,140,(int)(120+135*pulse)),8*scale,0,3*scale);ImGui::TextColored(ImVec4(1,.85f,.3f,1),"All players are ready! Press Start Adventure.");}
   else if(room.players.size()<2)ImGui::TextColored(ImVec4(.55f,.85f,1,1),"%s",room.mode==2?"Waiting for another player to join this seed...":"Waiting for another player to join...");
   else if(!all_ready)ImGui::TextWrapped("Waiting for every player to mark Ready.");}else if(me_ready)ImGui::TextColored(ImVec4(.1f,1,.5f,1),"Ready - waiting for host to start.");else ImGui::TextWrapped("Press Ready Up when you are ready to join the adventure.");  }
  ImGui::EndChild();
  if(!can_start)ImGui::TextWrapped("Game files and the matching validated seed must be ready before starting.");ImGui::TextWrapped("%s",state.message.c_str());
  if(!busy&&ImGui::GetTime()-last_poll>2){last_poll=ImGui::GetTime();request(4,room.code);}
  if(room.started&&state.success&&!started_once&&can_start&&seed_ok){started_once=true;std::string lobby=state.lobby;if(room.mode==2&&lobby.size()>6)lobby.resize(lobby.size()-6);laonline_session_override(username,lobby.c_str(),state.password.c_str());return true;}
  return false;
 }
 ImGui::BeginDisabled(busy);
 if(!browser){
  /* V12.5 Play Co-Op (reference picture): Your Username, the two host cards, then Browse Public Sessions /
   * Private Room Code / Join by Code on one row. Everything is a normal focusable control, so D-pad / arrows / Tab
   * move between them and A / Enter presses. */
  ImVec2 avail=ImGui::GetContentRegionAvail();float s=scale,fs=ImGui::GetFontSize(),gap=16*s;auto *d=ImGui::GetWindowDrawList();
  ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.02f,.10f,.22f,1));ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.55f,.72f,.92f,.9f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,10*s);ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1.5f*s);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(42*s,9*s));
  // Your Username [pencil  name] - and the optional public session name beside it
  ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Your Username");ImGui::SameLine(0,gap);
  ImGui::SetNextItemWidth(fminf(avail.x*.32f,340*s));ImGui::InputTextWithHint("##username","Enter your username...",username,sizeof username);
  {ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();float cy=(a.y+b.y)*.5f,x=a.x+20*s,r=8*s;ImU32 ink=IM_COL32(235,242,255,255);  // pencil
   d->AddLine(ImVec2(x-r*.7f,cy+r*.7f),ImVec2(x+r*.6f,cy-r*.6f),ink,3.2f*s);d->AddTriangleFilled(ImVec2(x-r*.95f,cy+r*.95f),ImVec2(x-r*.85f,cy+r*.45f),ImVec2(x-r*.45f,cy+r*.85f),ink);}
  if(ImGui::IsItemHovered())ImGui::SetTooltip("The name other players see. Leave empty to play as \"Player\".");
  ImGui::SameLine(0,gap*1.5f);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(12*s,9*s));ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::InputTextWithHint("##session-title","Public session name (optional)",session_title,sizeof session_title);ImGui::PopStyleVar();
  if(ImGui::IsItemHovered())ImGui::SetTooltip("Shown in Browse Lobbies when you host a public session.");
  ImGui::Dummy(ImVec2(1,6*s));ImVec2 lp=ImGui::GetCursorScreenPos();d->AddLine(lp,ImVec2(lp.x+avail.x,lp.y),IM_COL32(120,170,230,120),1.2f*s);ImGui::Dummy(ImVec2(1,10*s));
  // the two cards fill the space left above the bottom row
  float row_h=fminf(52*s,fs*1.9f),card_h=fmaxf(180*s,ImGui::GetContentRegionAvail().y-row_h-40*s-fs*1.4f),width=(avail.x-gap)*.5f;
  if(host_card(false,ImVec2(width,card_h),scale))request(1);ImGui::SameLine(0,gap);if(host_card(true,ImVec2(width,card_h),scale))request(2);
  ImGui::Dummy(ImVec2(1,8*s));lp=ImGui::GetCursorScreenPos();d->AddLine(lp,ImVec2(lp.x+avail.x,lp.y),IM_COL32(120,170,230,120),1.2f*s);ImGui::Dummy(ImVec2(1,10*s));
  // Browse Public Sessions | Private Room Code [......] [Join by Code]
  if(settings_look::wide_button("Browse Public Sessions##browse",ImVec2(avail.x*.3f,row_h),0,s))browse_requested=true;
  ImGui::SameLine(0,gap);ImVec2 sp=ImGui::GetCursorScreenPos();d->AddLine(ImVec2(sp.x,sp.y+row_h*.15f),ImVec2(sp.x,sp.y+row_h*.85f),IM_COL32(150,190,235,160),1.5f*s);ImGui::Dummy(ImVec2(1,row_h));
  ImGui::SameLine(0,gap);ImGui::SetCursorPosY(ImGui::GetCursorPosY()+(row_h-fs)*.5f-2*s);ImGui::TextUnformatted("Private Room Code");ImGui::SameLine(0,gap*.75f);
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x,sp.y+(row_h-(fs+18*s))*.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(14*s,9*s));float code_w=ImGui::GetContentRegionAvail().x-avail.x*.2f-gap;ImGui::SetNextItemWidth(fmaxf(120*s,code_w));
  bool enter=ImGui::InputTextWithHint("##room-code","Enter a room code...",room_code,sizeof room_code,ImGuiInputTextFlags_CharsUppercase|ImGuiInputTextFlags_CharsNoBlank|ImGuiInputTextFlags_EnterReturnsTrue);ImGui::PopStyleVar();
  ImGui::SameLine(0,gap);ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x,sp.y));
  if((settings_look::wide_button("Join by Code##join",ImVec2(ImGui::GetContentRegionAvail().x,row_h),1,s,room_code[0]!=0)||(enter&&room_code[0])))request(3,room_code);
  ImGui::PopStyleVar(3);ImGui::PopStyleColor(2);
 }else{
  ImGui::EndDisabled(); // Search/filter input stays responsive while a request is in flight.
  ImGui::PushFont(tropical_heading_font());ImGui::TextUnformatted("Browse Lobbies");ImGui::PopFont();ImGui::TextUnformatted("Find and join eligible co-op sessions.");ImGui::SameLine();if(ImGui::Button(busy?"Refreshing...":"Refresh"))request(0);ImGui::Separator();
  float width=ImGui::GetContentRegionAvail().x,sidebar=200*scale;float list_h=fmaxf(220*scale,ImGui::GetContentRegionAvail().y-ImGui::GetFrameHeightWithSpacing()-ImGui::GetTextLineHeightWithSpacing()*2.2f);  // V12.5: fits the panel (the page row and message used to spill under Back)
  ImGui::BeginChild("directory-filters",ImVec2(sidebar,list_h),ImGuiChildFlags_Borders);ImGui::SetWindowFontScale(1.1f);ImGui::SetNextItemWidth(-1);if(ImGui::InputTextWithHint("##host-search","Search by host name...",search,sizeof search)){search_dirty=true;search_changed=ImGui::GetTime();}
  ImGui::Spacing();ImGui::TextUnformatted("Game Mode");for(int i=0;i<3;i++){const char *names[]={"All Modes","Regular Co-Op","Co-Op Randomizer"};if(ImGui::Selectable(names[i],filter_mode==i)){filter_mode=i;request(0);}}
  ImGui::Spacing();ImGui::TextUnformatted("Session Type");const char *types[]={"All Sessions","Public Only","Private Only"};for(int i=0;i<3;i++)if(ImGui::Selectable(types[i],session_filter==i))session_filter=i;
  ImGui::Spacing();ImGui::TextUnformatted("Sort By");const char *sorts[]={"Players (High to Low)","Players (Low to High)","Recently Created","Session Name"};ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##sort",&sort_mode,sorts,4))request(0);
  ImGui::Spacing();ImGui::Separator();ImGui::TextUnformatted("Private Room Code");ImGui::SetNextItemWidth(-1);ImGui::InputText("##private-code",room_code,sizeof room_code,ImGuiInputTextFlags_CharsUppercase|ImGuiInputTextFlags_CharsNoBlank);ImGui::BeginDisabled(busy);if(ImGui::Button("Join by Code",ImVec2(-1,34*scale)))request(3,room_code);ImGui::EndDisabled();ImGui::TextWrapped("Private rooms are only available through a valid room code.");ImGui::EndChild();ImGui::SameLine();
  ImGui::BeginChild("directory-results",ImVec2(0,list_h),ImGuiChildFlags_Borders);ImGui::SetWindowFontScale(1.1f);
  if(session_filter==2)ImGui::TextWrapped("Private sessions are not publicly discoverable. Enter your invitation's room code to join.");
  else if(list.rooms.empty())ImGui::TextWrapped("%s",list.success?"No public sessions match your filters.":list.message.c_str());
  else if(ImGui::BeginTable("rooms",7,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY,ImVec2(0,list_h-50*scale))){
   const char *columns[]={"Session Name","Host","Players","Game Mode","Type","Status","Join"};float widths[]={220,140,65,125,60,85,70};for(int c=0;c<7;c++)ImGui::TableSetupColumn(columns[c],ImGuiTableColumnFlags_WidthFixed,widths[c]*scale);ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
   for(auto &room:list.rooms){ImGui::PushID(room.code.c_str());ImGui::TableNextRow(0,55*scale);ImGui::TableNextColumn();ImTextureID art=0;float ratio=1;if(tropical_image(room.mode==2?"randomizer":"coop",&art,&ratio)){ImGui::Image(art,ImVec2(65*scale,36*scale));ImGui::SameLine();}ImGui::BeginGroup();ImGui::TextUnformatted(room.title.c_str());ImGui::TextDisabled("%s",room.description.c_str());ImGui::EndGroup();
   ImGui::TableNextColumn();for(auto &member:room.players)if(member.host){member_portrait(member,24*scale);ImGui::SameLine();break;}ImGui::TextUnformatted(room.host.c_str());ImGui::TableNextColumn();ImGui::Text("%d/4",(int)room.players.size());ImGui::TableNextColumn();ImGui::TextUnformatted(room.mode==2?"Co-Op Randomizer":"Regular Co-Op");ImGui::TableNextColumn();ImGui::TextUnformatted("Public");ImGui::TableNextColumn();ImGui::TextColored(room.started?ImVec4(1,.8f,.1f,1):room.players.size()==4?ImVec4(.65f,.7f,.75f,1):ImVec4(.1f,1,.5f,1),"%s",room.started?"In Progress":room.players.size()==4?"Full":"Open");if(room.started&&room.players.size()<4&&ImGui::IsItemHovered())ImGui::SetTooltip("In progress - you can still join.");ImGui::TableNextColumn();/* V13: a running adventure stays joinable while a slot is free; the room's mode and seed are adopted (a missing
     * randomizer seed is downloaded after joining). Only a full room or another LA-Online version blocks Join. */
    bool same_version=room.build==LAO_ROOM_BUILD,full=room.players.size()>=4,have_seed=room.mode!=2||(room.world==world&&room.seed==seed);
    ImGui::BeginDisabled(busy||!same_version||full);
    if(ImGui::Button("Join##join",ImVec2(-1,32*scale))){
     int m=current_mode;std::string w=current_world,sd=current_seed;current_mode=room.mode;if(!have_seed){current_world.clear();current_seed.clear();}
     request(3,room.code);current_mode=m;current_world=w;current_seed=sd;}
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",full?"Session full: all four player slots are taken.":!same_version?"This adventure needs a different version of LA-Online.":!have_seed?"This adventure's randomizer seed is downloaded automatically after you join.":room.started?"The adventure is already running - you join where your save is.":"Join this session.");ImGui::PopID();
   }ImGui::EndTable();
  }
  ImGui::EndChild();ImGui::BeginDisabled(busy||list.page==0);if(ImGui::Button("Previous"))request(0,"",false,list.page-1);ImGui::EndDisabled();ImGui::SameLine();ImGui::Text("Page %d / %d - %d sessions",list.page+1,std::max(1,(list.total+7)/8),list.total);ImGui::SameLine();ImGui::BeginDisabled(busy||(list.page+1)*8>=list.total);if(ImGui::Button("Next"))request(0,"",false,list.page+1);ImGui::EndDisabled();
  if(!busy&&search_dirty&&ImGui::GetTime()-search_changed>.4){search_dirty=false;last_poll=ImGui::GetTime();request(0);}else if(!busy&&ImGui::GetTime()-last_poll>10){last_poll=ImGui::GetTime();request(0,"",false,list.page);}
  ImGui::BeginDisabled(busy); // Balance the shared footer below.
 }
 ImGui::EndDisabled();
 // (Play Co-Op: only real news - "Contacting..." or an error - not the default hint, which the layout already shows)
 if(browser||busy||state.message!="Choose a session or enter a room code.")ImGui::TextWrapped("%s",busy?"Contacting session service...":state.message.c_str());return false;
}

/* V12.5 Leaderboards - laid out like the reference design: Sort By / Game Mode / Players filters on the left, a ranked
 * table (50 runs per page, up to 100 pages) and page buttons at the bottom. Every row is a real, player-reported run
 * from the relay; nothing is filled in when the board is empty. Score is computed by the relay from the run itself:
 * 100,000 - clear-time seconds - 250 per death + rupees (minimum 0). An older (V12) relay only answers 8 runs per page
 * without filters; the page then says so and still shows those real runs. */
struct LeaderRun{std::string players;unsigned mode,count,seconds,rupees,deaths,score;};
static std::mutex leader_mutex;static std::atomic<bool> leader_busy(false);static std::vector<LeaderRun> leader_rows;static std::string leader_status="Loading completed runs...";static int leader_total,leader_page,leader_mode=3,leader_sort,leader_count,leader_page_size=50;static bool leader_legacy,leader_back,leader_loaded;static std::atomic<bool> leader_refresh_due(false);
static unsigned leader_names(const std::string &s){unsigned n=1;for(char c:s)if(c==',')n++;return n>4?4:n;}
static unsigned leader_score(unsigned seconds,unsigned deaths,unsigned rupees){long long v=100000LL-seconds-250LL*deaths+rupees;return v<0?0:(unsigned)v;}
static void leader_request(bool submit,const char *players="",int mode=0,int seconds=0,int rupees=0,int deaths=0){
 if(leader_busy.exchange(true))return;auto word=[](std::vector<uint8_t> &b,unsigned v,int n){for(int i=0;i<n;i++)b.push_back((uint8_t)(v>>(i*8)));};
 std::vector<uint8_t> b={1,(uint8_t)(submit?1:0)},v2={2,0},v1={1,0};
 if(submit){char id[65];snprintf(id,sizeof id,"run-%llu-%u",(unsigned long long)SDL_GetTicksNS(),(unsigned)SDL_rand(2147483647));str(b,id);str(b,players);b.push_back((uint8_t)mode);word(b,seconds,4);word(b,rupees,4);word(b,deaths,4);b.push_back(1);}
 else{v2.push_back((uint8_t)leader_mode);v2.push_back((uint8_t)leader_sort);v2.push_back((uint8_t)leader_count);word(v2,leader_page,2);
  // V12 relay fallback: sort 0 time / 1 rupees / 2 fewest deaths, no score or player filter
  v1.push_back((uint8_t)leader_mode);v1.push_back((uint8_t)(leader_sort==2?1:0));word(v1,leader_page,2);}
 std::string address=endpoint;
 std::thread([b,v1,v2,submit,address](){std::vector<LeaderRun> rows;std::string message;int total=0;bool legacy=false;
  auto number=[](WireReader &r){unsigned v=0;for(int i=0;i<4;i++)v|=(unsigned)r.byte()<<(i*8);return v;};
  try{
   if(submit){auto bytes=exchange(address,b,0x32);WireReader r{bytes};bool ok=r.byte()==1;message=r.string();if(ok){message="Run submitted. "+message;leader_refresh_due=true;}}
   else{auto bytes=exchange(address,v2,0x32);WireReader r{bytes};bool ok=r.byte()==1;message=r.string();
    if(ok){total=r.word();int count=r.byte();if(count>50)throw std::runtime_error("Invalid ranking count.");for(int i=0;i<count;i++){LeaderRun run;run.players=r.string();run.mode=r.byte();run.count=r.byte();run.seconds=number(r);run.rupees=number(r);run.deaths=number(r);run.score=number(r);rows.push_back(run);}if(r.i!=bytes.size())throw std::runtime_error("Incomplete ranking response.");}
    else{legacy=true;auto old=exchange(address,v1,0x32);WireReader o{old};ok=o.byte()==1;message=o.string();
     if(ok){total=o.word();int count=o.byte();if(count>8)throw std::runtime_error("Invalid ranking count.");for(int i=0;i<count;i++){LeaderRun run;run.players=o.string();run.mode=o.byte();run.seconds=number(o);run.rupees=number(o);run.deaths=number(o);o.byte();run.count=leader_names(run.players);run.score=leader_score(run.seconds,run.deaths,run.rupees);rows.push_back(run);}if(o.i!=old.size())throw std::runtime_error("Incomplete ranking response.");
      message="This relay is older than V12.5: 8 runs per page, no player-count filter. Update the relay for the full leaderboard.";}}}
  }catch(const std::exception &e){message=e.what();}
  std::lock_guard<std::mutex> guard(leader_mutex);if(!submit){leader_rows=rows;leader_total=total;leader_legacy=legacy;leader_page_size=legacy?8:50;leader_loaded=true;}leader_status=message;leader_busy=false;
 }).detach();
}
bool session_leaderboards_take_back(void){bool v=leader_back;leader_back=false;return v;}
// One filter row on the left panel (icon + label, blue when selected). Returns true when picked.
static bool leader_option(const char *id,const char *label,bool selected,int icon,float w,float h,float s){
 ImVec2 p=ImGui::GetCursorScreenPos();bool click=ImGui::InvisibleButton(id,ImVec2(w,h),ImGuiButtonFlags_EnableNav);bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused();auto *d=ImGui::GetWindowDrawList();
 ImVec2 e(p.x+w,p.y+h);if(selected){d->AddRectFilled(p,e,IM_COL32(28,118,236,255),7*s);d->AddRect(p,e,IM_COL32(120,190,255,255),7*s,0,1.2f*s);}
 else{d->AddRectFilled(p,e,hot?IM_COL32(30,62,105,255):IM_COL32(14,34,62,235),7*s);d->AddRect(p,e,IM_COL32(48,82,125,200),7*s,0,1*s);}
 if(hot&&!selected)d->AddRect(p,e,IM_COL32(150,210,255,255),7*s,0,1.6f*s);
 ImVec2 c(p.x+20*s,p.y+h*.5f);float r=9*s;ImU32 ink=IM_COL32_WHITE;using namespace lao_icon;
 switch(icon){case 0:clock(d,c,r,ink);break;case 1:trophy(d,c,r,IM_COL32(255,214,64,255));break;case 2:rupee(d,c,r,IM_COL32(40,210,90,255));break;case 3:skull(d,c,r,ink);break;
  case 4:grid(d,c,r,ink);break;case 5:sword(d,c,r,ink);break;case 6:people(d,c,r,2,ink);break;case 7:die(d,c,r,ink);break;default:people(d,ImVec2(c.x+2*s,c.y),icon-10>=3?r*.62f:r,icon-10,ink);break;}
 d->AddText(tropical_body_font(),15*s,ImVec2(p.x+44*s,p.y+h*.5f-9*s),IM_COL32_WHITE,label);
 return click;
}
static void leader_section(const char *title,float s){ImVec2 p=ImGui::GetCursorScreenPos();ImGui::GetWindowDrawList()->AddText(tropical_heading_font(),17*s,ImVec2(p.x,p.y+4*s),IM_COL32(235,242,255,255),title);ImGui::Dummy(ImVec2(1,22*s));}
static bool leader_page_button(const char *label,bool selected,bool enabled,float w,float h,float s){
 ImGui::BeginDisabled(!enabled);ImVec2 p=ImGui::GetCursorScreenPos();bool click=ImGui::InvisibleButton(label,ImVec2(w,h),ImGuiButtonFlags_EnableNav);bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused();ImGui::EndDisabled();
 auto *d=ImGui::GetWindowDrawList();ImVec2 e(p.x+w,p.y+h);if(selected)d->AddRectFilled(p,e,IM_COL32(28,118,236,255),6*s);else if(hot)d->AddRectFilled(p,e,IM_COL32(40,80,130,255),6*s);
 if(hot)d->AddRect(p,e,IM_COL32(150,210,255,255),6*s,0,1.4f*s);
 ImU32 ink=enabled?IM_COL32_WHITE:IM_COL32(120,135,155,255);ImVec2 c(p.x+w*.5f,p.y+h*.5f);
 if(!strcmp(label,"##first")||!strcmp(label,"##last")){int dir=label[2]=='f'?0:1;lao_icon::chevron(d,ImVec2(c.x-4*s,c.y),6*s,dir,ink,2.4f*s);lao_icon::chevron(d,ImVec2(c.x+4*s,c.y),6*s,dir,ink,2.4f*s);}
 else if(!strcmp(label,"##prev")||!strcmp(label,"##next"))lao_icon::chevron(d,c,6*s,label[2]=='p'?0:1,ink,2.4f*s);
 else{const char *t=label;const char *hash=strstr(label,"##");std::string shown=hash?std::string(label,hash-label):std::string(t);ImVec2 ts=tropical_body_font()->CalcTextSizeA(15*s,1e6f,0,shown.c_str());d->AddText(tropical_body_font(),15*s,ImVec2(c.x-ts.x*.5f,c.y-ts.y*.5f),ink,shown.c_str());}
 return click&&enabled;
}
void session_leaderboards_draw(float scale){
 const float s=scale;using namespace lao_icon;
 static bool requested=false;if(!requested){requested=true;leader_request(false);}
 if(!leader_busy&&leader_refresh_due.exchange(false))leader_request(false);
 static bool want_submit=false;
 static char players[81];static int mode=0,seconds=3600,rupees=0,deaths=0;static bool completed=false;
 auto *d=ImGui::GetWindowDrawList();ImFont *hf=tropical_heading_font(),*bf=tropical_body_font();
 ImVec2 origin=ImGui::GetCursorScreenPos(),avail=ImGui::GetContentRegionAvail();float W=avail.x,H=avail.y;
 bool busy_now=leader_busy.load();auto refresh=[&](){leader_page=0;leader_request(false);};
 // ---- header: back arrow, trophy badge, title, subtitle, Global Leaderboards selector ----
 float hh=64*s;{ImVec2 c(origin.x+28*s,origin.y+hh*.5f);ImGui::SetCursorScreenPos(ImVec2(c.x-24*s,c.y-24*s));if(ImGui::InvisibleButton("##leader-back",ImVec2(48*s,48*s),ImGuiButtonFlags_EnableNav))leader_back=true;bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused();
  d->AddCircleFilled(c,23*s,hot?IM_COL32(50,95,150,255):IM_COL32(18,40,70,255));d->AddCircle(c,23*s,IM_COL32(120,170,220,255),32,1.5f*s);arrow_left(d,c,11*s,IM_COL32_WHITE);
  ImVec2 b0(origin.x+64*s,origin.y+hh*.5f-26*s),b1(b0.x+52*s,b0.y+52*s);d->AddRectFilled(b0,b1,IM_COL32(215,35,40,255),9*s);d->AddRect(b0,b1,IM_COL32(255,150,140,255),9*s,0,1.5f*s);trophy(d,ImVec2((b0.x+b1.x)*.5f,(b0.y+b1.y)*.5f+2*s),17*s,IM_COL32_WHITE);
  d->AddText(hf,27*s,ImVec2(b1.x+14*s,origin.y+hh*.5f-26*s),IM_COL32_WHITE,"Leaderboards");
  d->AddText(bf,16*s,ImVec2(b1.x+14*s,origin.y+hh*.5f+5*s),IM_COL32(205,222,245,255),"View the fastest clear times and highest scores from around the world.");
  // right: Global Leaderboards pill (opens a small menu with Refresh / Submit Completed Run)
  float pw=240*s,ph=40*s;ImVec2 pp(origin.x+W-pw-4*s,origin.y+hh*.5f-ph*.5f);ImGui::SetCursorScreenPos(pp);bool open=ImGui::InvisibleButton("##leader-scope",ImVec2(pw,ph),ImGuiButtonFlags_EnableNav);bool phot=ImGui::IsItemHovered()||ImGui::IsItemFocused();
  d->AddRectFilled(pp,ImVec2(pp.x+pw,pp.y+ph),phot?IM_COL32(40,140,255,255):IM_COL32(25,112,235,255),8*s);d->AddRect(pp,ImVec2(pp.x+pw,pp.y+ph),IM_COL32(140,200,255,255),8*s,0,1.3f*s);
  globe(d,ImVec2(pp.x+22*s,pp.y+ph*.5f),10*s,IM_COL32_WHITE);d->AddText(bf,16*s,ImVec2(pp.x+42*s,pp.y+ph*.5f-10*s),IM_COL32_WHITE,"Global Leaderboards");chevron(d,ImVec2(pp.x+pw-20*s,pp.y+ph*.5f),6*s,2,IM_COL32_WHITE,2.2f*s);
  if(open)ImGui::OpenPopup("##leader-scope-menu");ImGui::SetNextWindowPos(ImVec2(pp.x,pp.y+ph+4*s));
  if(ImGui::BeginPopup("##leader-scope-menu")){ImGui::MenuItem("Global Leaderboards",nullptr,true,false);ImGui::Separator();if(ImGui::MenuItem("Refresh",nullptr,false,!busy_now))leader_request(false);if(ImGui::MenuItem("Submit Completed Run...",nullptr,false,!busy_now)){snprintf(players,sizeof players,"%s",username);want_submit=true;ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
 }
 // ---- left filter panel ----
 float top=origin.y+hh+10*s,lw=fmaxf(205*s,W*.19f),bottom=origin.y+H;ImVec2 l0(origin.x,top),l1(origin.x+lw,bottom);
 d->AddRectFilled(l0,l1,IM_COL32(8,24,48,235),10*s);d->AddRect(l0,l1,IM_COL32(60,110,170,200),10*s,0,1.2f*s);
 ImGui::SetCursorScreenPos(ImVec2(l0.x+8*s,l0.y+6*s));ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(4*s,4*s));ImGui::BeginGroup();float ow=lw-16*s,oh=fminf(32*s,(bottom-top-3*30*s-20*s)/13-4*s);
 ImGui::BeginDisabled(busy_now);
 leader_section("Sort By",s);const char *sorts[]={"Best Time (Any%)","Highest Score","Most Rupees","Most Deaths (Challenge)"};
 for(int i=0;i<4;i++){char id[24];snprintf(id,sizeof id,"##sort%d",i);ImGui::PushID(id);if(leader_option(id,sorts[i],leader_sort==i,i,ow,oh,s)&&leader_sort!=i){leader_sort=i;refresh();}ImGui::PopID();if(i==1&&ImGui::IsItemHovered())ImGui::SetTooltip("Score = 100,000 - clear time in seconds - 250 per death + rupees (minimum 0).");}
 leader_section("Game Mode",s);const char *modes[]={"All Modes","Vanilla","Regular Co-Op","Co-Op Randomizer"};const int mode_ids[]={3,0,1,2};
 for(int i=0;i<4;i++){char id[24];snprintf(id,sizeof id,"##mode%d",i);ImGui::PushID(id);if(leader_option(id,modes[i],leader_mode==mode_ids[i],4+i,ow,oh,s)&&leader_mode!=mode_ids[i]){leader_mode=mode_ids[i];refresh();}ImGui::PopID();}
 leader_section("Players",s);const char *counts[]={"All Player Counts","Solo (1 Player)","Duo (2 Players)","Trio (3 Players)","Quad (4 Players)"};
 for(int i=0;i<5;i++){char id[24];snprintf(id,sizeof id,"##count%d",i);ImGui::PushID(id);if(leader_option(id,counts[i],leader_count==i,i?10+i:12,ow,oh,s)&&leader_count!=i){leader_count=i;refresh();}ImGui::PopID();}
 ImGui::EndDisabled();ImGui::EndGroup();ImGui::PopStyleVar();
 // ---- table ----
 float tx=l1.x+12*s,tw=origin.x+W-tx,footer=46*s;ImVec2 t0(tx,top),t1(origin.x+W,bottom-footer-6*s);
 d->AddRectFilled(t0,t1,IM_COL32(8,24,48,235),10*s);d->AddRect(t0,t1,IM_COL32(60,110,170,200),10*s,0,1.2f*s);
 const float colw[]={.06f,.08f,.27f,.16f,.115f,.115f,.11f,.09f};const char *heads[]={"#","Players","Player(s)","Game Mode","Clear Time","Score","Rupees","Deaths"};float colx[9];colx[0]=tx+8*s;for(int i=0;i<8;i++)colx[i+1]=colx[i]+colw[i]*(tw-16*s);
 for(int i=0;i<8;i++){ImVec2 ts=bf->CalcTextSizeA(15*s,1e6f,0,heads[i]);float cx=i==2?colx[i]+(colx[i+1]-colx[i]-ts.x)*.5f:colx[i]+(colx[i+1]-colx[i]-ts.x)*.5f;d->AddText(bf,15*s,ImVec2(cx,top+10*s),IM_COL32(220,232,250,255),heads[i]);}
 ImGui::SetCursorScreenPos(ImVec2(tx+4*s,top+36*s));
 std::vector<LeaderRun> rows;std::string status;int total,page_size;bool legacy,loaded;{std::lock_guard<std::mutex> guard(leader_mutex);rows=leader_rows;status=leader_status;total=leader_total;page_size=leader_page_size;legacy=leader_legacy;loaded=leader_loaded;}
 ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(0,0,0,0));ImGui::BeginChild("##leader-rows",ImVec2(tw-8*s,t1.y-top-42*s),ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);
 {auto *rd=ImGui::GetWindowDrawList();float rh=fmaxf(38*s,(t1.y-top-42*s)/10.f-ImGui::GetStyle().ItemSpacing.y-2*s),rw=ImGui::GetContentRegionAvail().x;float ox=ImGui::GetCursorScreenPos().x-(tx+4*s);(void)ox;
  if(rows.empty()){const char *msg=busy_now?"Loading completed runs...":!loaded?"Loading completed runs...":"No completed runs match these filters yet.";ImVec2 ts=bf->CalcTextSizeA(18*s,1e6f,0,msg);ImVec2 c=ImGui::GetCursorScreenPos();rd->AddText(bf,18*s,ImVec2(c.x+(rw-ts.x)*.5f,c.y+40*s),IM_COL32(220,232,250,255),msg);
   const char *hint="Finish a run, then use Global Leaderboards > Submit Completed Run. Results are player-reported.";ts=bf->CalcTextSizeA(14*s,1e6f,0,hint);rd->AddText(bf,14*s,ImVec2(c.x+(rw-ts.x)*.5f,c.y+70*s),IM_COL32(170,190,215,255),hint);ImGui::Dummy(ImVec2(rw,120*s));}
  for(size_t i=0;i<rows.size();i++){const LeaderRun &r=rows[i];int rank=leader_page*page_size+(int)i+1;ImVec2 p=ImGui::GetCursorScreenPos();ImGui::PushID((int)i);ImGui::InvisibleButton("##row",ImVec2(rw,rh),ImGuiButtonFlags_EnableNav);bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused();
   if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",r.players.c_str());ImGui::PopID();ImVec2 e(p.x+rw,p.y+rh);float cy=p.y+rh*.5f;
   if(rank==1){rd->AddRectFilledMultiColor(p,e,IM_COL32(120,90,20,235),IM_COL32(60,52,30,235),IM_COL32(60,52,30,235),IM_COL32(120,90,20,235));rd->AddRect(p,e,IM_COL32(255,205,60,255),7*s,0,2.2f*s);}
   else{rd->AddRectFilled(p,e,i%2?IM_COL32(16,38,68,230):IM_COL32(12,30,56,230),7*s);rd->AddRect(p,e,IM_COL32(45,80,125,180),7*s,0,1*s);}
   if(hot)rd->AddRect(p,e,IM_COL32(150,210,255,255),7*s,0,1.8f*s);
   auto col=[&](int k){return colx[k]-(tx+8*s)+p.x;};auto centre_text=[&](int k,ImFont *f,float fs,const char *t,ImU32 ink,float dx=0){ImVec2 ts=f->CalcTextSizeA(fs,1e6f,0,t);rd->AddText(f,fs,ImVec2(col(k)+(col(k+1)-col(k)-ts.x)*.5f+dx,cy-ts.y*.5f),ink,t);};
   // rank badge
   {float bs=fminf(rh-10*s,34*s);ImVec2 b0(col(0)+(col(1)-col(0)-bs)*.5f,cy-bs*.5f),b1(b0.x+bs,b0.y+bs);ImU32 fill=rank==1?IM_COL32(230,170,30,255):rank==2?IM_COL32(110,130,160,255):rank==3?IM_COL32(150,90,55,255):IM_COL32(20,45,80,255);
    rd->AddRectFilled(b0,b1,fill,6*s);rd->AddRect(b0,b1,IM_COL32(150,180,215,140),6*s,0,1*s);char t[8];snprintf(t,sizeof t,"%d",rank);ImVec2 ts=hf->CalcTextSizeA(17*s,1e6f,0,t);rd->AddText(hf,17*s,ImVec2((b0.x+b1.x-ts.x)*.5f,cy-ts.y*.5f),IM_COL32_WHITE,t);}
   // player count
   {char t[8];snprintf(t,sizeof t,"%uP",r.count);people(rd,ImVec2(col(1)+16*s,cy),8*s,r.count>1?2:1,IM_COL32_WHITE);rd->AddText(bf,15*s,ImVec2(col(1)+30*s,cy-9*s),IM_COL32_WHITE,t);}
   // avatars + names
   {std::vector<std::string> names;std::string cur;for(char c:r.players){if(c==','){names.push_back(trim(cur));cur.clear();}else cur+=c;}names.push_back(trim(cur));
    float ar=fminf(rh*.36f,17*s),ax=col(2)+ar+4*s;int shown=(int)fminf(4,(float)names.size());for(int k=0;k<shown;k++)avatar(rd,bf,ImVec2(ax+k*ar*1.45f,cy),ar,names[k],IM_COL32(230,240,255,255));
    float nx=ax+(shown-1)*ar*1.45f+ar+10*s,nw=col(3)-nx-6*s;rd->PushClipRect(ImVec2(nx,p.y),ImVec2(nx+nw,e.y),true);
    if(names.size()>1){rd->AddText(hf,15*s,ImVec2(nx,cy-17*s),IM_COL32_WHITE,names[0].c_str());rd->AddText(bf,12*s,ImVec2(nx,cy+1*s),IM_COL32(185,205,230,255),r.players.c_str());}
    else rd->AddText(hf,15*s,ImVec2(nx,cy-9*s),IM_COL32_WHITE,names[0].c_str());rd->PopClipRect();}
   // mode
   {const char *m=r.mode==2?"Co-Op Randomizer":r.mode==1?"Regular Co-Op":"Vanilla";ImVec2 ic(col(3)+16*s,cy);if(r.mode==2)die(rd,ic,9*s,IM_COL32_WHITE);else if(r.mode==1)people(rd,ic,9*s,2,IM_COL32_WHITE);else sword(rd,ic,9*s,IM_COL32_WHITE);
    rd->PushClipRect(ImVec2(col(3),p.y),ImVec2(col(4),e.y),true);rd->AddText(bf,14*s,ImVec2(col(3)+32*s,cy-8*s),IM_COL32_WHITE,m);rd->PopClipRect();}
   // clear time, score, rupees, deaths
   {char t[24];snprintf(t,sizeof t,"%u:%02u:%02u",r.seconds/3600,r.seconds/60%60,r.seconds%60);clock(rd,ImVec2(col(4)+18*s,cy),9*s,IM_COL32_WHITE);rd->AddText(bf,16*s,ImVec2(col(4)+34*s,cy-10*s),IM_COL32_WHITE,t);
    trophy(rd,ImVec2(col(5)+18*s,cy),9*s,IM_COL32(255,205,50,255));rd->AddText(bf,16*s,ImVec2(col(5)+34*s,cy-10*s),IM_COL32_WHITE,thousands(r.score).c_str());
    rupee(rd,ImVec2(col(6)+18*s,cy),9*s,IM_COL32(40,210,90,255));rd->AddText(bf,16*s,ImVec2(col(6)+34*s,cy-10*s),IM_COL32_WHITE,thousands(r.rupees).c_str());
    skull(rd,ImVec2(col(7)+18*s,cy),9*s,IM_COL32_WHITE);rd->AddText(bf,16*s,ImVec2(col(7)+34*s,cy-10*s),IM_COL32_WHITE,thousands(r.deaths).c_str());}
   ImGui::Dummy(ImVec2(1,2*s));}
 }
 ImGui::EndChild();ImGui::PopStyleColor();
 // ---- footer: pages + "Showing a - b of N entries" ----
 int pages=total>0?(total+page_size-1)/page_size:1;if(pages>100)pages=100;if(leader_page>=pages)leader_page=pages-1;
 {float bh=34*s,bw=34*s;std::vector<int> nums;if(pages<=7){for(int i=0;i<pages;i++)nums.push_back(i);}else{nums.push_back(0);int a=leader_page-1,b=leader_page+1;if(a<1){a=1;b=3;}if(b>pages-2){b=pages-2;a=b-2;}if(a>1)nums.push_back(-1);for(int i=a;i<=b;i++)nums.push_back(i);if(b<pages-2)nums.push_back(-1);nums.push_back(pages-1);}
  float bar=(4+nums.size())*(bw+4*s)+16*s;ImVec2 f0(tx+(tw-bar)*.5f,bottom-footer),f1(f0.x+bar,bottom);d->AddRectFilled(f0,f1,IM_COL32(8,24,48,235),10*s);d->AddRect(f0,f1,IM_COL32(60,110,170,200),10*s,0,1.2f*s);
  ImGui::SetCursorScreenPos(ImVec2(f0.x+8*s,f0.y+(footer-bh)*.5f));ImGui::BeginDisabled(busy_now);int go=-1;
  if(leader_page_button("##first",false,leader_page>0,bw,bh,s))go=0;ImGui::SameLine(0,4*s);if(leader_page_button("##prev",false,leader_page>0,bw,bh,s))go=leader_page-1;
  for(int n:nums){ImGui::SameLine(0,4*s);char label[24];if(n<0){snprintf(label,sizeof label,"...##gap%d",(int)ImGui::GetCursorScreenPos().x);leader_page_button(label,false,false,bw,bh,s);}else{snprintf(label,sizeof label,"%d##p%d",n+1,n);if(leader_page_button(label,n==leader_page,true,bw,bh,s))go=n;}}
  ImGui::SameLine(0,4*s);if(leader_page_button("##next",false,leader_page+1<pages,bw,bh,s))go=leader_page+1;ImGui::SameLine(0,4*s);if(leader_page_button("##last",false,leader_page+1<pages,bw,bh,s))go=pages-1;
  ImGui::EndDisabled();if(go>=0&&go!=leader_page){leader_page=go;leader_request(false);}
  char showing[96];int first=total?leader_page*page_size+1:0,last=leader_page*page_size+(int)rows.size();snprintf(showing,sizeof showing,"Showing %s - %s of %s entries",thousands(first).c_str(),thousands(last).c_str(),thousands(total).c_str());
  ImVec2 ts=bf->CalcTextSizeA(15*s,1e6f,0,showing);d->AddText(bf,15*s,ImVec2(origin.x+W-ts.x-6*s,bottom-footer*.5f-ts.y*.5f),IM_COL32(220,232,250,255),showing);
  // status (errors, old relay, player-reported note) on the left of the footer
  if(!status.empty()){float sw=f0.x-tx-12*s;if(sw>80*s){d->AddText(bf,13*s,ImVec2(tx+4*s,bottom-footer+6*s),legacy?IM_COL32(255,205,100,255):IM_COL32(175,195,220,255),status.c_str(),nullptr,sw);}else if(ImGui::IsMouseHoveringRect(f0,f1))ImGui::SetTooltip("%s",status.c_str());}
 }
 ImGui::SetCursorScreenPos(ImVec2(origin.x,bottom));ImGui::Dummy(ImVec2(1,0));
 if(want_submit){want_submit=false;ImGui::OpenPopup("Completed Run");}
 if(ImGui::BeginPopupModal("Completed Run",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("Submit your real completed run. These results are player-reported, not independently verified.");ImGui::InputText("Player(s)",players,sizeof players);ImGui::TextDisabled("Separate co-op players with commas, e.g. Velvet, Sanic");ImGui::Combo("Game Mode",&mode,"Vanilla\0Regular Co-Op\0Co-Op Randomizer\0");ImGui::InputInt("Clear time in seconds",&seconds);ImGui::InputInt("Rupees",&rupees);ImGui::InputInt("Deaths",&deaths);ImGui::Checkbox("I completed this run and these results are accurate",&completed);ImGui::BeginDisabled(!completed||leader_busy||!players[0]||seconds<1||rupees<0||deaths<0);if(ImGui::Button("Submit")){leader_request(true,players,mode,seconds,rupees,deaths);ImGui::CloseCurrentPopup();completed=false;}ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
}

static std::atomic<bool> global_status_busy(false);static int global_count=-1;static unsigned global_status_epoch;static double global_poll=-10;static bool global_status_failed;
const char *session_global_count_text(void){
 static char label[96];double now=ImGui::GetTime();unsigned epoch;{std::lock_guard<std::mutex> lock(response_lock);epoch=generation;if(global_status_epoch!=epoch){global_count=-1;global_status_failed=false;global_poll=-10;global_status_epoch=epoch;}}
 if(now-global_poll>=2 && !global_status_busy.exchange(true)){global_poll=now;std::string address=endpoint;std::thread([address,epoch](){int count=-1;try{auto data=exchange(address,{1},0x33);WireReader r{data};if(r.byte()!=1)throw std::runtime_error("Global unavailable");count=r.word();int capacity=r.word();if(count>capacity||capacity!=100||r.i!=data.size())throw std::runtime_error("Invalid Global status");}catch(const std::exception &){count=-1;}std::lock_guard<std::mutex> guard(response_lock);if(epoch==generation){global_count=count;global_status_failed=count<0;}global_status_busy=false;}).detach();}
 {std::lock_guard<std::mutex> lock(response_lock);if(global_count>=0)snprintf(label,sizeof label,"%d %s currently in Global",global_count,global_count==1?"player":"players");else snprintf(label,sizeof label,"%s",global_status_failed?"Global player count unavailable":"Checking Global player count...");}return label;
}
