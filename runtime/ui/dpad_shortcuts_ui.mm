#include "tropical_ui.h"
#include "laonline_item_icons.h"
#include "../hle/dpad_shortcuts.h"
#include <SDL3/SDL.h>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <math.h>
extern "C" {
#include "../settings.h"
int input_pad_family(void);
int laonline_death_active(void),laonline_level_ready(void),game_in_play(void);
bool laonline_ui_paused(void);
}
static std::mutex drag_mx;
static std::atomic<bool> dragging(false),position_save(false);
static std::atomic<int> next_x(-1),next_y(-1);
static ImVec2 hit_pos,hit_size,screen,grab;
static bool hit_visible;
static uint64_t hit_time;
extern "C" bool dpad_shortcuts_dragging(void){return dragging.load();}
bool dpad_shortcuts_event(const SDL_Event *e,float density){
 std::lock_guard<std::mutex> guard(drag_mx);
 if(e->type==SDL_EVENT_WINDOW_FOCUS_LOST){if(dragging.exchange(false)){SDL_CaptureMouse(false);dpad_shortcuts_pointer_capture(0);position_save=true;}return false;}
 if(!g_settings.dpad_shortcuts||!g_settings.dpad_hud||!game_in_play()||laonline_ui_paused()||laonline_death_active()||!laonline_level_ready()){bool was=dragging.exchange(false);hit_visible=false;if(was){SDL_CaptureMouse(false);dpad_shortcuts_pointer_capture(0);position_save=true;}return was;}
 if(e->type==SDL_EVENT_MOUSE_BUTTON_DOWN&&e->button.button==SDL_BUTTON_LEFT&&hit_visible&&SDL_GetTicks()-hit_time<200){
  ImVec2 mouse(e->button.x*density,e->button.y*density);
  if(mouse.x>=hit_pos.x&&mouse.x<=hit_pos.x+hit_size.x&&mouse.y>=hit_pos.y&&mouse.y<=hit_pos.y+hit_size.y){grab=ImVec2(mouse.x-hit_pos.x,mouse.y-hit_pos.y);dragging=true;dpad_shortcuts_pointer_capture(1);SDL_CaptureMouse(true);return true;}
 }
 if(!dragging)return false;
 if(e->type==SDL_EVENT_MOUSE_MOTION){float x=e->motion.x*density-grab.x,y=e->motion.y*density-grab.y;next_x=(int)roundf(100*std::clamp(x,0.f,std::max(0.f,screen.x-hit_size.x))/std::max(1.f,screen.x-hit_size.x));next_y=(int)roundf(100*std::clamp(y,0.f,std::max(0.f,screen.y-hit_size.y))/std::max(1.f,screen.y-hit_size.y));return true;}
 if(e->type==SDL_EVENT_MOUSE_BUTTON_UP&&e->button.button==SDL_BUTTON_LEFT){dragging=false;SDL_CaptureMouse(false);dpad_shortcuts_pointer_capture(0);position_save=true;return true;}
 return e->type==SDL_EVENT_MOUSE_BUTTON_DOWN||e->type==SDL_EVENT_MOUSE_BUTTON_UP||e->type==SDL_EVENT_MOUSE_WHEEL;
}
void dpad_shortcuts_draw(ImVec2 display,float scale){
 int visible=0;uint32_t own=dpad_shortcuts_inventory(&visible);
 int x=next_x.exchange(-1),y=next_y.exchange(-1);if(x>=0)g_settings.dpad_x=x;if(y>=0)g_settings.dpad_y=y;
 if(position_save.exchange(false))settings_save();
 bool active=visible&&g_settings.dpad_shortcuts&&g_settings.dpad_hud&&!laonline_death_active()&&laonline_level_ready();
 static float fade=0;float step=std::min(1.f,ImGui::GetIO().DeltaTime*12.f);fade+=(float(active)-fade)*step;
 if(laonline_death_active()||laonline_ui_paused())fade=0;
 {std::lock_guard<std::mutex> guard(drag_mx);hit_visible=false;}
 if(fade<.01f)return;

 float size=(156+g_settings.dpad_size*52)*scale;float alpha=g_settings.dpad_opacity*.01f*fade;
 ImGui::SetNextWindowPos(ImVec2((display.x-size)*g_settings.dpad_x*.01f,(display.y-size)*g_settings.dpad_y*.01f));ImGui::SetNextWindowSize(ImVec2(size,size));ImGui::SetNextWindowBgAlpha(0);
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));ImGui::Begin("##dpad-shortcuts",NULL,ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing);
 auto *dl=ImGui::GetWindowDrawList();ImVec2 p=ImGui::GetWindowPos();{std::lock_guard<std::mutex> guard(drag_mx);hit_pos=p;hit_size=ImVec2(size,size);screen=display;hit_visible=active;hit_time=SDL_GetTicks();}ImTextureID graphic=0;float aspect=1;int family=input_pad_family();
 const char *art=family==2?"dpad-playstation":family==1?"dpad-xbox":"dpad";
 if(tropical_image(art,&graphic,&aspect)){float w=size,h=size;if(aspect>1)h/=aspect;else w*=aspect;dl->AddImage(graphic,ImVec2(p.x+(size-w)*.5f,p.y+(size-h)*.5f),ImVec2(p.x+(size+w)*.5f,p.y+(size+h)*.5f),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(alpha*220)));}
 int slots[]={g_settings.dpad_up,g_settings.dpad_down,g_settings.dpad_left,g_settings.dpad_right};float xs[]={.5f,.5f,.16f,.84f},ys[]={.16f,.84f,.5f,.5f};
 for(int i=0;i<4;i++){int slot=slots[i],item=dpad_slot_item(slot);if(item<0)continue;ImTextureID icon=0;float ratio=1;if(!laonline_item_icon(item,&icon,&ratio))continue;float h=size*.27f,w=h*ratio;if(w>h*1.25f){w=h*1.25f;h=w/ratio;}bool owned=dpad_slot_owned(slot,own,dpad_shortcuts_songs());ImVec2 a(p.x+size*xs[i]-w*.5f,p.y+size*ys[i]-h*.5f);dl->AddImage(icon,a,ImVec2(a.x+w,a.y+h),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(alpha*(owned?255:65))));}
 ImGui::End();ImGui::PopStyleVar();
}
