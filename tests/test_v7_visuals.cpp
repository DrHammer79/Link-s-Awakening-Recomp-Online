#include <cassert>
#include <cstdio>
#include "../runtime/ui/tropical_ui.mm"
#include "../runtime/ui/dpad_shortcuts_ui.mm"
static uint64_t epoch=1;static unsigned uploads;static bool mask_checked;
extern "C" Settings g_settings={};
extern "C" bool rt_resource(const char *rel,const char *,char *out,size_t cap){snprintf(out,cap,"work/hf3/pc/assets/%s",rel);return true;}
extern "C" uint64_t vk_imgui_texture_generation(){return epoch;}
extern "C" uint64_t vk_imgui_texture_rgba(const void *data,int w,int h){auto p=(const unsigned char*)data;assert(w>0&&h>0);if(w==200&&h==200){assert(p[3]==0);for(int i=0;i<w*h;i++)if(p[4*i+3]){assert(p[4*i]==235&&p[4*i+1]==245&&p[4*i+2]==255);mask_checked=true;break;}}return ++uploads;}
extern "C" uint32_t dpad_shortcuts_inventory(int *visible){*visible=1;return 0xffff;}
extern "C" int input_pad_family(){return 2;}
bool laonline_item_icon(int item,ImTextureID *id,float *aspect){*id=1000+item;*aspect=1;return true;}
static ImGuiContext *context(){auto c=ImGui::CreateContext();ImGui::SetCurrentContext(c);auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1920,1080);io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();unsigned char *data;int w,h;io.Fonts->GetTexDataAsRGBA32(&data,&w,&h);return c;}
int main(){assert(release_objects(R"([{"draft":false,"tag_name":"BETA-V8","body":"brace } text","author":{"name":"test"}},{"draft":true,"tag_name":"V99"}])").size()==2);assert(!is_draft("{\"draft\":false}"));assert(is_draft("{\"draft\":true}"));assert(is_draft("{\"draft\":  "));auto a=context();ImTextureID first,again;float aspect;tropical_image("logo",&first,&aspect);tropical_image("logo",&again,&aspect);assert(first==again&&uploads==1);
 auto b=context();epoch=2;tropical_image("logo",&again,&aspect);assert(uploads==2&&again!=first);
 ImGui::SetCurrentContext(a);epoch=1;tropical_image("logo",&again,&aspect);assert(uploads==2&&again==first);
 epoch=3;tropical_image("logo",&again,&aspect);assert(uploads==3&&again!=first);
 g_settings.dpad_shortcuts=g_settings.dpad_hud=true;g_settings.dpad_opacity=80;g_settings.dpad_x=80;g_settings.dpad_y=7;
 for(int size=0;size<3;size++){g_settings.dpad_size=size;ImGui::NewFrame();dpad_shortcuts_draw(ImVec2(1920,1080),1);ImGui::Render();auto *data=ImGui::GetDrawData();assert(data->TotalVtxCount==4);assert(data->TotalIdxCount==6);float left=1e9,right=-1e9;for(int n=0;n<data->CmdListsCount;n++)for(auto &v:data->CmdLists[n]->VtxBuffer){if(v.pos.x<left)left=v.pos.x;if(v.pos.x>right)right=v.pos.x;}assert(right-left==float(156+size*52));}
 assert(mask_checked);g_settings.dpad_up=9;g_settings.dpad_down=2;g_settings.dpad_left=4;g_settings.dpad_right=3;ImGui::NewFrame();dpad_shortcuts_draw(ImVec2(1920,1080),1);ImGui::Render();assert(ImGui::GetDrawData()->TotalVtxCount==20);
 ImGui::DestroyContext(b);ImGui::DestroyContext(a);puts("PASS context-specific artwork, backend-generation reload, unchanged live-context cache, transparent controller mask/tint, no square backdrop/border, 156/208/260 sizes and four item icons");}

