#include <assert.h>
#include <stdio.h>
#include "imgui.h"
#include "option_rows.h"
bool ui_input_text(const char*,char*,size_t,int,const char*,const char*){return false;}
bool ui_input_text_cancelled(){return false;}
bool osk_is_open(){return false;}
static bool toggle=false;static float volume=.5f;static int choice=0;
static void frame(float mx,float my,bool down,bool disabled=false){
 auto &io=ImGui::GetIO();io.AddMousePosEvent(mx,my);io.AddMouseButtonEvent(0,down);ImGui::NewFrame();
 ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(800,600));ImGui::Begin("Test",nullptr,ImGuiWindowFlags_NoDecoration);opt_frame_reset();opt_begin("rows",1);
 opt_toggle("Toggle",&toggle,false,"A real boolean preference");
 if(disabled)opt_disable_next();opt_slider("Volume",&volume,0,1,.05f,1,"%.2f","Drag the live volume slider");
 const char *items[]={"First","Second","Third"};opt_choice("Mode",&choice,items,3,0,"A real dropdown");opt_end();ImGui::End();ImGui::Render();}
int main(){ImGui::CreateContext();auto &io=ImGui::GetIO();io.DisplaySize=ImVec2(800,600);io.DeltaTime=1.f/60;io.IniFilename=nullptr;io.Fonts->AddFontDefault();unsigned char *px;int w,h;io.Fonts->GetTexDataAsRGBA32(&px,&w,&h);
 frame(-100,-100,false);frame(400,25,false);frame(400,25,true);frame(400,25,true);frame(400,25,false);frame(400,25,false);assert(toggle);
 frame(700,70,true);assert(volume>.5f);frame(700,70,false);
 float before=volume;frame(580,70,true,true);frame(580,70,false,true);assert(volume==before);
 frame(400,115,false);frame(400,115,true);frame(400,115,true);frame(400,115,false);frame(400,115,false);assert(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel));
 ImGui::DestroyContext();puts("PASS live option rows: mouse toggle, slider drag, disabled slider and dropdown rendering");}
