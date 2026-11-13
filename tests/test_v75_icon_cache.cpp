#include <assert.h>
#include <string>
#include "../runtime/ui/laonline_item_icons.mm"
static uint64_t gen=1,next=100;static int uploaded;
extern "C" uint64_t vk_imgui_texture_generation(void){return gen;}
extern "C" uint64_t vk_imgui_texture_rgba(const void*,int,int){uploaded++;return ++next;}
extern "C" bool rt_resource(const char *name,const char*,char *out,size_t cap){std::string p="work/hf3/pc/"+std::string(name);FILE *f=fopen(p.c_str(),"rb");if(!f)p="work/hf3/pc/assets/"+std::string(name);else fclose(f);snprintf(out,cap,"%s",p.c_str());return true;}
int main(){auto a=ImGui::CreateContext();ImGui::SetCurrentContext(a);ImTextureID id=0,again=0;float aspect=0;assert(laonline_item_icon(6,&id,&aspect)&&uploaded==1&&aspect>0);assert(laonline_item_icon(6,&again,&aspect)&&again==id&&uploaded==1);auto b=ImGui::CreateContext();ImGui::SetCurrentContext(b);gen=2;assert(laonline_item_icon(6,&again,&aspect)&&again!=id&&uploaded==2);ImGui::SetCurrentContext(a);gen=1;assert(laonline_item_icon(6,&again,&aspect)&&again==id&&uploaded==2);gen=3;assert(laonline_item_icon(6,&again,&aspect)&&again!=id&&uploaded==3);uint8_t cap[3]={0x3f,0xa3,0x4d},hair[3]={0xff,0xdf,0x57};assert(laonline_avatar_icon(cap,hair,&id,&aspect)&&uploaded==4&&aspect==1);assert(laonline_avatar_icon(cap,hair,&again,&aspect)&&again==id&&uploaded==4);gen=4;assert(laonline_avatar_icon(cap,hair,&again,&aspect)&&again!=id&&uploaded==5);ImGui::DestroyContext(b);ImGui::DestroyContext(a);puts("PASS original item/avatar PNG uploads, aspect ratio, live-cache reuse and backend-context recreation");}
