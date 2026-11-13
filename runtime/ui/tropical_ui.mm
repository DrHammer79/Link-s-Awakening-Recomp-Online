#include "tropical_ui.h"
#include "update_tags.h"
#include "coop_version.h"
#include <vector>
#include <SDL3/SDL.h>
#include <map>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <stdio.h>
#include <math.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif
#ifdef __APPLE__
#import <Metal/Metal.h>
#endif
extern "C" {
#include "../paths.h"
#include "../platform.h"
void *ui_metal_device(void);
SDL_Window *ui_window(void);
uint64_t vk_imgui_texture_rgba(const void *,int,int);
uint64_t vk_imgui_texture_generation(void);
}
static const char *coop_repo="https://github.com/LegendarySavageGamer/Link-s-Awakening-Recomp-Online";
struct Art{ImTextureID id=0;float aspect=1;bool tried=false;};
struct ArtContext {uint64_t generation=0;std::map<std::string,Art> images;};
static std::map<ImGuiContext*,ArtContext> art_contexts;
bool tropical_image(const char *name,ImTextureID *out_id,float *aspect){
 ArtContext &context=art_contexts[ImGui::GetCurrentContext()];
#ifndef __APPLE__
 uint64_t generation=vk_imgui_texture_generation();if(!generation){*out_id=0;return false;}
 if(context.generation!=generation){context.images.clear();context.generation=generation;}
#endif
 Art &a=context.images[name];if(!a.tried){a.tried=true;char relative[256],path[1200];snprintf(relative,sizeof relative,"laonline-ui/%s.png",name);
 if(rt_resource(relative,NULL,path,sizeof path)){
  SDL_Surface *raw=SDL_LoadPNG(path);if(raw){SDL_Surface *rgba=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(raw);
  if(rgba&&rgba->w>0&&rgba->h>0&&rgba->w<=4096&&rgba->h<=4096){
   std::string pixels((size_t)rgba->w*rgba->h*4,0);for(int y=0;y<rgba->h;y++)memcpy(pixels.data()+(size_t)y*rgba->w*4,(char*)rgba->pixels+(size_t)y*rgba->pitch,(size_t)rgba->w*4);
#ifdef __APPLE__
   id<MTLDevice> dev=(__bridge id<MTLDevice>)ui_metal_device();if(dev){MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:rgba->w height:rgba->h mipmapped:NO];id<MTLTexture> tex=[dev newTextureWithDescriptor:d];[tex replaceRegion:MTLRegionMake2D(0,0,rgba->w,rgba->h) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:rgba->w*4];a.id=(ImTextureID)(__bridge_retained void*)tex;}
#else
   // Supplied monochrome controller masks use a light tint directly, with their original alpha.
   if(!strcmp(name,"dpad-playstation")||!strcmp(name,"dpad-xbox"))for(size_t i=0;i<pixels.size();i+=4){pixels[i]=(char)235;pixels[i+1]=(char)245;pixels[i+2]=(char)255;}
   a.id=vk_imgui_texture_rgba(pixels.data(),rgba->w,rgba->h);
#endif
   a.aspect=(float)rgba->w/rgba->h;
  }if(rgba)SDL_DestroySurface(rgba);}
 }}*out_id=a.id;*aspect=a.aspect;return a.id!=0;
}
void tropical_style(void){
 auto &s=ImGui::GetStyle();s.WindowRounding=12;s.ChildRounding=12;s.FrameRounding=7;s.PopupRounding=8;s.ScrollbarSize=12;
 s.Colors[ImGuiCol_WindowBg]=ImVec4(.012f,.07f,.14f,.98f);s.Colors[ImGuiCol_ChildBg]=ImVec4(.015f,.08f,.15f,.96f);
 s.Colors[ImGuiCol_PopupBg]=ImVec4(.012f,.07f,.14f,.99f);s.Colors[ImGuiCol_TitleBgActive]=ImVec4(.025f,.20f,.35f,1);
 s.Colors[ImGuiCol_Border]=ImVec4(.04f,.46f,.7f,.9f);s.Colors[ImGuiCol_Text]=ImVec4(.97f,.98f,1,1);
 s.Colors[ImGuiCol_Button]=ImVec4(.015f,.23f,.45f,1);s.Colors[ImGuiCol_ButtonHovered]=ImVec4(.02f,.47f,.75f,1);s.Colors[ImGuiCol_ButtonActive]=ImVec4(0,.65f,.4f,1);
 s.Colors[ImGuiCol_Header]=ImVec4(0,.4f,.5f,1);s.Colors[ImGuiCol_HeaderHovered]=ImVec4(0,.55f,.6f,1);s.Colors[ImGuiCol_HeaderActive]=ImVec4(0,.7f,.35f,1);
 s.Colors[ImGuiCol_FrameBg]=ImVec4(.015f,.12f,.23f,1);s.Colors[ImGuiCol_CheckMark]=ImVec4(.1f,1,.5f,1);s.Colors[ImGuiCol_ScrollbarGrab]=ImVec4(.75f,.9f,.92f,1);
}
void tropical_header(ImVec2 p,ImVec2 size,float scale){
 ImDrawList *dl=ImGui::GetWindowDrawList();ImTextureID logo=0;float aspect=1;
 float h=120*scale,w=h*4.6f;if(tropical_image("logo",&logo,&aspect)){w=h*aspect;float maxw=size.x-fminf(430*scale,size.x*.45f)-65*scale;if(w>maxw){w=maxw;h=w/aspect;}dl->AddImage(logo,ImVec2(p.x+30*scale,p.y+16*scale),ImVec2(p.x+30*scale+w,p.y+16*scale+h));}
 else dl->AddText(ImVec2(p.x+18*scale,p.y+12*scale),IM_COL32_WHITE,"LAS Recompiled");
 std::string las_label="LAS Recompiled "+tag_label(LAO_LAS_BASELINE,true);const char *mod_label="LA-Online CO-OP BETA-V" LAO_MOD_VERSION;
 dl->AddText(ImVec2(p.x+size.x-ImGui::CalcTextSize(las_label.c_str()).x-150*scale,p.y+10*scale),IM_COL32_WHITE,las_label.c_str());
 dl->AddText(ImVec2(p.x+size.x-ImGui::CalcTextSize(mod_label).x-150*scale,p.y+27*scale),IM_COL32(205,232,255,255),mod_label);
 ImVec2 saved=ImGui::GetCursorPos();ImGui::SetCursorScreenPos(ImVec2(p.x+size.x-135*scale,p.y+8*scale));
 if(ImGui::Button("-##minimize",ImVec2(35*scale,27*scale)))SDL_MinimizeWindow(ui_window());ImGui::SameLine(0,3*scale);
 if(ImGui::Button("[]##maximize",ImVec2(35*scale,27*scale))){if(SDL_GetWindowFlags(ui_window())&SDL_WINDOW_MAXIMIZED)SDL_RestoreWindow(ui_window());else SDL_MaximizeWindow(ui_window());}ImGui::SameLine(0,3*scale);
 if(ImGui::Button("X##close",ImVec2(35*scale,27*scale))){SDL_Event event={};event.type=SDL_EVENT_QUIT;SDL_PushEvent(&event);}ImGui::SetCursorPos(saved);
}
bool tropical_card(const char *id,const char *art,const char *title,const char *credit,const char *desc,ImVec2 size,ImU32 accent){
 ImGui::PushID(id);ImVec2 p=ImGui::GetCursorScreenPos();bool click=ImGui::InvisibleButton("card",size,ImGuiButtonFlags_EnableNav);bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused();auto *dl=ImGui::GetWindowDrawList();
 float r=15,edge=hot?4:2;dl->AddRectFilled(p,ImVec2(p.x+size.x,p.y+size.y),IM_COL32(0,25,55,248),r);dl->AddRect(p,ImVec2(p.x+size.x,p.y+size.y),accent,r,0,edge);
 if(hot)dl->AddRect(ImVec2(p.x-3,p.y-3),ImVec2(p.x+size.x+3,p.y+size.y+3),accent,r+3,0,2);
 float pad=size.x*.045f,ah=size.y*.43f;ImTextureID tex=0;float aspect=1;if(tropical_image(art,&tex,&aspect))dl->AddImageRounded(tex,ImVec2(p.x+pad,p.y+pad),ImVec2(p.x+size.x-pad,p.y+ah),ImVec2(0,0),ImVec2(1,1),IM_COL32_WHITE,r);
 auto center=[&](const char *text,float y,float fs,ImU32 col){float fit=fs;ImVec2 ts=ImGui::GetFont()->CalcTextSizeA(fit,1e6f,0,text);if(ts.x>size.x-2*pad)fit*= (size.x-2*pad)/ts.x;ts=ImGui::GetFont()->CalcTextSizeA(fit,1e6f,0,text);dl->AddText(ImGui::GetFont(),fit,ImVec2(p.x+(size.x-ts.x)*.5f,p.y+y),col,text);};
 center(title,size.y*.51f,size.y*.085f,IM_COL32_WHITE);center(credit,size.y*.615f,size.y*.045f,IM_COL32_WHITE);
 dl->AddText(ImGui::GetFont(),size.y*.042f,ImVec2(p.x+pad,p.y+size.y*.69f),IM_COL32(195,222,245,255),desc,NULL,size.x-2*pad);
 ImVec2 b(p.x+pad,p.y+size.y*.83f),e(p.x+size.x-pad,p.y+size.y-pad);dl->AddRectFilled(b,e,accent,8);center(title,size.y*.853f,size.y*.06f,IM_COL32_WHITE);
 ImGui::PopID();return click;
}
#include "release_updates.h"
#include "lao_updater.h"
#include <fstream>
#include <sstream>
struct UpdateState {
 lao_updates::Release coop,las;
 std::string coop_status="Not checked yet.",las_status="Not checked yet.",verified_coop;
 bool preview=false;
};
static std::mutex update_mx;
static std::atomic<bool> checking(false),update_started(false);
static UpdateState updates;
static bool github_get(const wchar_t *path,std::string &body,std::string &error){
#ifdef _WIN32
 HINTERNET h=WinHttpOpen(L"LA-Online-BETA-V12",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!h){error="Cannot initialize HTTPS.";return false;}WinHttpSetTimeouts(h,8000,8000,8000,15000);
 HINTERNET c=WinHttpConnect(h,L"api.github.com",443,0),q=c?WinHttpOpenRequest(c,L"GET",path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):NULL;
 bool ok=q&&WinHttpSendRequest(q,L"Accept: application/vnd.github+json\r\n",(DWORD)-1L,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(q,NULL);DWORD status=0,len=sizeof status;if(ok)ok=WinHttpQueryHeaders(q,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&len,WINHTTP_NO_HEADER_INDEX)&&status==200;
 if(!ok){char text[100];snprintf(text,sizeof text,"GitHub check failed (HTTP %lu, network error %lu).",(unsigned long)status,(unsigned long)GetLastError());error=text;}
 while(ok){DWORD available=0,got=0;if(!WinHttpQueryDataAvailable(q,&available)){ok=false;error="Could not read release information.";break;}if(!available)break;if(body.size()+available>2*1024*1024){ok=false;error="Release information exceeds the supported size.";break;}std::string buf(available,0);if(!WinHttpReadData(q,buf.data(),available,&got)){ok=false;error="Release download interrupted.";break;}body.append(buf.data(),got);}
 if(q)WinHttpCloseHandle(q);if(c)WinHttpCloseHandle(c);WinHttpCloseHandle(h);return ok;
#else
 error="Release checking is unavailable here. Open GitHub releases.";return false;
#endif
}

static bool newer_las(const UpdateState &u){return u.las.rank>tag_rank(LAO_LAS_BASELINE);}
static bool newer_coop(const UpdateState &u){return u.coop.rank>LAO_COOP_RANK;}
static UpdateState update_snapshot(){std::lock_guard<std::mutex> lock(update_mx);return updates;}
static void start_update_check(){
 if(checking.exchange(true))return;
 // V12.5: an exception escaping this background thread used to end the whole game ("terminate called after
 // throwing ... basic_string::_M_create" right after the start screen, lasrecomp.log 2026-10-31). Contained now.
 std::thread([]{try{
  UpdateState u;std::string coop_body,las_body,error;bool co=false,la=false;
  if(const char *prefix=getenv("LARECOMP_UPDATE_FIXTURE")){
   u.preview=true;std::ifstream a(std::string(prefix)+"-coop.json"),b(std::string(prefix)+"-las.json");
   if(a){std::ostringstream out;out<<a.rdbuf();coop_body=out.str();co=true;}if(b){std::ostringstream out;out<<b.rdbuf();las_body=out.str();la=true;}
  }else{
   co=github_get(L"/repos/LegendarySavageGamer/Link-s-Awakening-Recomp-Online/releases?per_page=100",coop_body,error);if(!co)u.coop_status=error;
   error.clear();la=github_get(L"/repos/peiikai/LASRecomp/releases?per_page=100",las_body,error);if(!la)u.las_status=error;
  }
  if(co){u.coop=lao_updates::newest(coop_body,true);
   u.coop_status=u.coop.rank<0?"No recognized published Co-op release found.":newer_coop(u)?"A newer Co-op release is available.":u.coop.rank==LAO_COOP_RANK?"Your Co-op version is up to date.":"This development build is newer than the published Co-op releases.";
  }
  if(la){u.las=lao_updates::newest(las_body,false);
   u.las_status=u.las.rank<0?"The latest LAS release could not be identified.":newer_las(u)?"A newer LAS Recompiled release is available.":"No newer LAS Recompiled release found.";
  }
  if(co)u.verified_coop=lao_updates::verified_coop_for(coop_body,newer_las(u)?u.las.tag:LAO_LAS_BASELINE);
  {std::lock_guard<std::mutex> lock(update_mx);updates=u;}checking=false;
 }catch(...){UpdateState u;u.coop_status=u.las_status="Update check unavailable right now.";{std::lock_guard<std::mutex> lock(update_mx);updates=u;}checking=false;}
 }).detach();
}
static void check_updates_once(){if(!update_started.exchange(true)&&(!getenv("LARECOMP_NO_UPDATE_CHECK")||getenv("LARECOMP_UPDATE_FIXTURE")))start_update_check();}
bool tropical_home_updates(ImVec2 ds,float scale){
 check_updates_once();UpdateState u=update_snapshot();bool open=false;
 float width=fminf(430*scale,ds.x*.45f),right=ds.x-24*scale,x=right-width;
 ImGui::SetCursorPos(ImVec2(right-205*scale,45*scale));
 if(ImGui::Button(checking?"Checking updates...":"Check for updates",ImVec2(205*scale,23*scale))){start_update_check();open=true;}
 if(!newer_las(u)&&!newer_coop(u))return open;
 const bool upstream=newer_las(u),compatible=upstream?!u.verified_coop.empty():u.verified_coop==u.coop.tag;
 const std::string from=upstream?tag_label(LAO_LAS_BASELINE,true):"Co-op V" LAO_MOD_VERSION;
 const std::string to=upstream?tag_label(u.las.tag,true):"Co-op "+u.coop.tag;
 std::string versions=from+"  \xe2\x86\x92  "+to;
 std::string message=compatible?"A verified Co-op release is available. Update LAS and the matching Co-op build together.":"Multiplayer support is unverified. Stay on "+tag_label(LAO_LAS_BASELINE,true)+" until a compatible Co-op release is verified.";
 const bool coop_only=!upstream&&newer_coop(u);  /* V13: a new LA-Online release for the same LAS version - just update */
 if(coop_only)message=lao_updater::busy()?"Updating... click to see progress.":!u.coop.asset.url.empty()?"Click to see what's new and press UPDATE NOW. Your name, lobby and saves stay the same.":"Click to see what's new.";
 ImGui::SetCursorPos(ImVec2(x,72*scale));ImVec2 p=ImGui::GetCursorScreenPos(),size(width,91*scale);
 bool clicked=ImGui::InvisibleButton("##update-notification",size,ImGuiButtonFlags_EnableNav);auto *d=ImGui::GetWindowDrawList();
 d->AddRectFilled(p,ImVec2(p.x+size.x,p.y+size.y),IM_COL32(0,31,64,248),9*scale);d->AddRect(p,ImVec2(p.x+size.x,p.y+size.y),IM_COL32(63,176,229,255),9*scale,0,1.2f*scale);
 ImFont *font=tropical_body_font();float tx=p.x+11*scale,ty=p.y+7*scale;
 d->AddRectFilled(ImVec2(tx,ty),ImVec2(tx+51*scale,ty+15*scale),IM_COL32(20,183,163,255),4*scale);
 d->AddText(font,10*scale,ImVec2(tx+6*scale,ty+1*scale),IM_COL32_WHITE,u.preview?"PREVIEW":"NEW");
 d->AddText(font,11*scale,ImVec2(tx+61*scale,ty),IM_COL32(198,235,255,255),"UPDATE AVAILABLE");
 d->AddText(font,15*scale,ImVec2(tx,ty+17*scale),IM_COL32_WHITE,from.c_str());
 float arrow_x=tx+font->CalcTextSizeA(15*scale,1e6f,0,from.c_str()).x+9*scale,arrow_y=ty+26*scale;
 d->AddLine(ImVec2(arrow_x,arrow_y),ImVec2(arrow_x+17*scale,arrow_y),IM_COL32(150,220,255,255),1.4f*scale);d->AddLine(ImVec2(arrow_x+12*scale,arrow_y-4*scale),ImVec2(arrow_x+17*scale,arrow_y),IM_COL32(150,220,255,255),1.4f*scale);d->AddLine(ImVec2(arrow_x+12*scale,arrow_y+4*scale),ImVec2(arrow_x+17*scale,arrow_y),IM_COL32(150,220,255,255),1.4f*scale);
 d->AddText(font,15*scale,ImVec2(arrow_x+27*scale,ty+17*scale),IM_COL32_WHITE,to.c_str());
 d->AddText(font,11*scale,ImVec2(tx,ty+37*scale),(compatible||coop_only)?IM_COL32(112,245,177,255):IM_COL32(255,215,126,255),coop_only?"New LA-Online update ready":compatible?"Verified multiplayer release available":"Multiplayer Compatibility Notice");
 d->AddText(font,11.5f*scale,ImVec2(tx,ty+52*scale),IM_COL32(222,236,248,255),message.c_str(),NULL,width-22*scale);
 if(ImGui::IsItemHovered())ImGui::SetTooltip(coop_only?"See what's new and update":"Open update details and compatibility notes");
 return open||clicked;
}
/* V13: open the update window by itself once per launch when a newer LA-Online release (with a player package) is
 * published. "Later" just closes it; the home card stays. */
/* V14.1: is a newer LA-Online release out? Used in-game (the small update notice) and by the launcher (online play
 * needs the newest version). Checks GitHub again every 20 minutes while the game runs. Cheap: no copies. */
extern "C" int lao_update_newer(char *tag,int cap){
 static double last=-1;double now=SDL_GetTicks()/1000.0;
 if(!getenv("LARECOMP_NO_UPDATE_CHECK")||getenv("LARECOMP_UPDATE_FIXTURE")){
  if(last<0){last=now;check_updates_once();}
  else if(now-last>1200&&!checking.load()){last=now;start_update_check();}
 }
 std::lock_guard<std::mutex> lock(update_mx);
 if(!newer_coop(updates)||newer_las(updates))return 0;
 if(tag&&cap>0)snprintf(tag,(size_t)cap,"%s",updates.coop.tag.c_str());
 return 1;
}
bool tropical_update_required(){return lao_update_newer(nullptr,0)!=0;}
static bool g_update_required_note;
void tropical_update_required_note(bool on){g_update_required_note=on;}
bool tropical_update_prompt(){
 static bool shown=false;if(shown||checking)return false;UpdateState u=update_snapshot();
 if(!newer_coop(u)||newer_las(u)||u.coop.asset.url.empty())return false;shown=true;return true;
}
static void update_now_panel(const UpdateState &u,float scale){
 const std::string to=u.coop.tag;
 ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.44f,.96f,.69f,1));ImGui::SetWindowFontScale(1.25f);
 ImGui::TextWrapped("LA-Online %s is here!",to.c_str());ImGui::SetWindowFontScale(1);ImGui::PopStyleColor();
 ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("You have V" LAO_MOD_VERSION ". Your name, lobby, room and saves stay the same.");ImGui::PopStyleColor();
 ImGui::Spacing();
 int s=lao_updater::state();float pr=lao_updater::progress();
 if(s==lao_updater::IDLE||s==lao_updater::FAILED){
  if(s==lao_updater::FAILED){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.55f,.45f,1));ImGui::TextWrapped("%s",lao_updater::message().c_str());ImGui::PopStyleColor();}
  if(u.coop.asset.url.empty()){
   ImGui::TextWrapped("This release has no player package attached yet, so it can't install by itself. Ask the host, or open the release page.");
  }else{
   ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.08f,.66f,.36f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.12f,.78f,.45f,1));ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.05f,.55f,.3f,1));
   char label[64];snprintf(label,sizeof label,"%s  (%.0f MB)",s==lao_updater::FAILED?"TRY AGAIN":"UPDATE NOW",u.coop.asset.size/1048576.0);
   if(ImGui::Button(label,ImVec2(-1,fmaxf(44*scale,ImGui::GetFrameHeight()*1.3f))))lao_updater::start(u.coop.asset.url,u.coop.asset.size,u.coop.tag,(int)u.coop.rank);
   ImGui::PopStyleColor(3);
  }
 }else{
  char label[32];snprintf(label,sizeof label,"%d%%",(int)(pr*100+.5f));
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImVec4(.1f,.75f,.42f,1));ImGui::ProgressBar(pr,ImVec2(-1,30*scale),label);ImGui::PopStyleColor();
  ImGui::TextWrapped("%s",lao_updater::message().c_str());
  if(s==lao_updater::READY){
   static double ready_at=0;if(ready_at==0)ready_at=ImGui::GetTime();
   int left=3-(int)(ImGui::GetTime()-ready_at);if(left<0)left=0;
   ImGui::TextWrapped("Restarting in %d...",left);
   if(ImGui::GetTime()-ready_at>=3.0)lao_updater::install_and_restart();
  }
  if(s==lao_updater::INSTALLING){
   static double inst_at=0;if(inst_at==0)inst_at=ImGui::GetTime();
   if(ImGui::GetTime()-inst_at>=1.2)exit(0);  /* the installer window waits for this, installs 96-100%, reopens the game */
  }
 }
 ImGui::Spacing();
 ImGui::BeginChild("##patch-notes",ImVec2(0,fmaxf(90*scale,ImGui::GetContentRegionAvail().y-(2*ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ItemSpacing.y))),true);
 if(u.coop.body.empty())ImGui::TextDisabled("No patch notes were posted with this release.");else lao_updater::draw_notes(u.coop.body,scale);
 ImGui::EndChild();
}
void tropical_update_notice(){
 check_updates_once();auto u=update_snapshot();if(newer_las(u)||newer_coop(u)){
  ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.82f,.35f,1));ImGui::TextWrapped("Update available. Review Co-op compatibility before changing your LAS installation.");ImGui::PopStyleColor();
 }
}
void tropical_update_draw(float scale){
 check_updates_once();auto u=update_snapshot();
 if(g_update_required_note&&newer_coop(u)){  /* V14.1: tried to play online on an older version */
  ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.84f,.4f,1));
  ImGui::TextWrapped("To keep playing online, update first. Everyone online has to be on the same LA-Online version - older versions can't join. Vanilla (offline) still works.");
  ImGui::PopStyleColor();ImGui::Separator();
 }
 if(!(newer_coop(u)&&!newer_las(u))){
 ImGui::TextColored(ImVec4(.7f,.88f,1,1),"LAS Recompiled %s",tag_label(LAO_LAS_BASELINE,true).c_str());
 ImGui::TextUnformatted("LA-Online CO-OP BETA-V" LAO_MOD_VERSION);ImGui::Separator();}
 if(u.preview)ImGui::TextWrapped("UPDATE PREVIEW: simulated release data for layout testing.");
 if(newer_coop(u)&&!newer_las(u)){  /* V13: a new LA-Online release - just the update window */
  update_now_panel(u,scale);
  if(!u.coop.url.empty()&&ImGui::Button("Open the release on GitHub",ImVec2(-1,0)))SDL_OpenURL(u.coop.url.c_str());
  return;
 }
 ImGui::BeginDisabled(checking||lao_updater::busy());if(ImGui::Button(checking?"Checking GitHub...":"Check for updates",ImVec2(-1,32*scale)))start_update_check();ImGui::EndDisabled();
 ImGui::Spacing();ImGui::TextUnformatted("Co-op releases");ImGui::TextWrapped("%s",u.coop_status.c_str());
 if(u.coop.rank>=0)ImGui::TextWrapped("Latest published: %s",u.coop.tag.c_str());
 ImGui::Spacing();ImGui::Separator();ImGui::TextUnformatted("Original LAS Recompiled");ImGui::TextWrapped("%s",u.las_status.c_str());
 if(u.las.rank>=0)ImGui::TextWrapped("Latest published: %s",tag_label(u.las.tag,true).c_str());
 if(newer_las(u)){
  if(u.verified_coop.empty()){
   ImGui::TextColored(ImVec4(1,.82f,.35f,1),"Multiplayer Compatibility Notice");
   ImGui::TextWrapped("%s is available, but multiplayer compatibility has not been verified. To continue playing online, remain on %s until a matching Co-op release has verified support.",tag_label(u.las.tag,true).c_str(),tag_label(LAO_LAS_BASELINE,true).c_str());
  }else ImGui::TextWrapped("The developer's published compatibility declaration verifies %s with Co-op %s. Install the matching Co-op release together with LAS.",tag_label(u.las.tag,true).c_str(),u.verified_coop.c_str());
 }
 ImGui::Spacing();ImGui::TextWrapped("This Beta 5.4 integration is a development build. Updates only install when you press UPDATE NOW.");
 ImGui::Spacing();
 if(!u.coop.url.empty()&&ImGui::Button("View Co-op release notes",ImVec2(-1,30*scale)))SDL_OpenURL(u.coop.url.c_str());
 if(ImGui::Button("View original LAS releases",ImVec2(-1,30*scale)))SDL_OpenURL("https://github.com/peiikai/LASRecomp/releases");
}
void tropical_guide_draw(bool *open,float scale){
 if(!*open)return;ImGui::SetNextWindowSize(ImVec2(720*scale,560*scale),ImGuiCond_Appearing);if(ImGui::Begin("Help / Guide",open,ImGuiWindowFlags_NoCollapse)){
 ImGui::TextWrapped("Start with your own supported game files. Play Vanilla stays offline. Play Co-op uses your configured server, lobby and player name. Every player needs the same compatible client and relay.");ImGui::Separator();
 ImGui::TextWrapped("Co-op Randomizer: generate a new seed in the full Randomizer settings, or import a friend's .laoseed with Browse Seeds. Download Seed exports the selected world. Host sharing and Sync use the configured relay. Check the Seed ID and full checksum before starting; importing does not generate a replacement world.");ImGui::Separator();
 ImGui::TextWrapped("F1 opens the same Settings during play. T opens chat; Enter sends and Escape cancels. Controls keeps the original keyboard and controller remapping, with safe menu recovery keys.");ImGui::Separator();
 ImGui::TextWrapped("Supported equipment and indexed collectibles can be shared. Full story catch-up, Tarin's starting-house gate and arbitrary randomizer check transactions remain unfinished. A pickup popup alone does not prove progression was applied. Save normally and keep backups.");ImGui::Separator();
 ImGui::TextWrapped("Report / Logs offers preview, direct submission and optional local export. Webhook secrets stay on the server. Co-op Updates checks the real GitHub releases; a new LA-Online version installs only when you press UPDATE NOW.");
 if(ImGui::Button("Close",ImVec2(-1,36*scale)))*open=false;}ImGui::End();
}
