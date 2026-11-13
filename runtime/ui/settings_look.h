#pragma once
/* V12.5 Settings look, from the reference screenshots: one icon per option row, toggles / dropdowns / sliders on the
 * right, a dark category list with a green selected row, and the Reset to Default / Cancel / Save Settings bar.
 * Drawing only (pure Dear ImGui) - every setting, its behaviour and the controller navigation stay in menu.mm /
 * option_rows.mm. Header-only so nothing has to be added to the Makefile. */
#include "imgui.h"
#include "lao_icons.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

namespace settings_look {
using namespace lao_icon;
static inline void speaker(ImDrawList *d,ImVec2 c,float r,ImU32 ink){ImVec2 p[4]={v(c.x-r*.85f,c.y-r*.3f),v(c.x-r*.4f,c.y-r*.3f),v(c.x-r*.4f,c.y+r*.3f),v(c.x-r*.85f,c.y+r*.3f)};d->AddConvexPolyFilled(p,4,ink);
 d->AddTriangleFilled(v(c.x-r*.45f,c.y-r*.3f),v(c.x+r*.05f,c.y-r*.8f),v(c.x+r*.05f,c.y+r*.8f),ink);d->AddTriangleFilled(v(c.x-r*.45f,c.y-r*.3f),v(c.x+r*.05f,c.y+r*.8f),v(c.x-r*.45f,c.y+r*.3f),ink);
 d->PathArcTo(v(c.x+r*.1f,c.y),r*.45f,-.9f,.9f,8);d->PathStroke(ink,0,r*.14f);d->PathArcTo(v(c.x+r*.1f,c.y),r*.78f,-.9f,.9f,10);d->PathStroke(ink,0,r*.14f);}
static inline void note(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddCircleFilled(v(c.x-r*.45f,c.y+r*.55f),r*.3f,ink);d->AddCircleFilled(v(c.x+r*.5f,c.y+r*.4f),r*.3f,ink);d->AddLine(v(c.x-r*.2f,c.y+r*.55f),v(c.x-r*.2f,c.y-r*.7f),ink,r*.16f);d->AddLine(v(c.x+r*.75f,c.y+r*.4f),v(c.x+r*.75f,c.y-r*.85f),ink,r*.16f);d->AddLine(v(c.x-r*.2f,c.y-r*.7f),v(c.x+r*.75f,c.y-r*.85f),ink,r*.3f);}
static inline void film(ImDrawList *d,ImVec2 c,float r,ImU32 ink,ImU32 bg){d->AddRectFilled(v(c.x-r*.9f,c.y-r*.7f),v(c.x+r*.9f,c.y+r*.7f),ink,r*.15f);for(int i=0;i<3;i++){float x=c.x-r*.6f+i*r*.6f;d->AddRectFilled(v(x-r*.1f,c.y-r*.6f),v(x+r*.1f,c.y-r*.45f),bg);d->AddRectFilled(v(x-r*.1f,c.y+r*.45f),v(x+r*.1f,c.y+r*.6f),bg);}d->AddTriangleFilled(v(c.x-r*.2f,c.y-r*.3f),v(c.x+r*.35f,c.y),v(c.x-r*.2f,c.y+r*.3f),bg);}
static inline void echo(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddLine(v(c.x-r*.7f,c.y-r*.7f),v(c.x-r*.7f,c.y+r*.7f),ink,r*.16f);for(int i=0;i<3;i++){d->PathArcTo(v(c.x-r*.6f,c.y),r*(.35f+i*.3f),-.9f,.9f,10);d->PathStroke(ink,0,r*.13f);}}
static inline void monitor(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddRect(v(c.x-r*.9f,c.y-r*.7f),v(c.x+r*.9f,c.y+r*.4f),ink,r*.1f,0,r*.16f);d->AddLine(v(c.x,c.y+r*.4f),v(c.x,c.y+r*.7f),ink,r*.16f);d->AddLine(v(c.x-r*.45f,c.y+r*.75f),v(c.x+r*.45f,c.y+r*.75f),ink,r*.16f);}
static inline void bulb(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddCircleFilled(v(c.x,c.y-r*.2f),r*.55f,ink);d->AddRectFilled(v(c.x-r*.28f,c.y+r*.2f),v(c.x+r*.28f,c.y+r*.65f),ink,r*.08f);d->AddLine(v(c.x-r*.22f,c.y+r*.82f),v(c.x+r*.22f,c.y+r*.82f),ink,r*.12f);}
static inline void ring_mark(ImDrawList *d,ImVec2 c,float r,ImU32 ink,bool cross){d->AddCircle(c,r*.8f,ink,24,r*.16f);if(cross){d->AddLine(v(c.x-r*.32f,c.y-r*.32f),v(c.x+r*.32f,c.y+r*.32f),ink,r*.18f);d->AddLine(v(c.x+r*.32f,c.y-r*.32f),v(c.x-r*.32f,c.y+r*.32f),ink,r*.18f);}else{d->AddLine(v(c.x-r*.38f,c.y),v(c.x-r*.08f,c.y+r*.3f),ink,r*.18f);d->AddLine(v(c.x-r*.08f,c.y+r*.3f),v(c.x+r*.4f,c.y-r*.3f),ink,r*.18f);}}
static inline void swap(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddLine(v(c.x-r*.8f,c.y-r*.35f),v(c.x+r*.8f,c.y-r*.35f),ink,r*.15f);chevron(d,v(c.x+r*.6f,c.y-r*.35f),r*.3f,1,ink,r*.15f);d->AddLine(v(c.x-r*.8f,c.y+r*.35f),v(c.x+r*.8f,c.y+r*.35f),ink,r*.15f);chevron(d,v(c.x-r*.6f,c.y+r*.35f),r*.3f,0,ink,r*.15f);}
static inline void dpad(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddRectFilled(v(c.x-r*.28f,c.y-r*.85f),v(c.x+r*.28f,c.y+r*.85f),ink,r*.1f);d->AddRectFilled(v(c.x-r*.85f,c.y-r*.28f),v(c.x+r*.85f,c.y+r*.28f),ink,r*.1f);}
static inline void palette(ImDrawList *d,ImVec2 c,float r,ImU32 ink,ImU32 bg){d->AddCircleFilled(c,r*.85f,ink);const ImU32 dots[4]={IM_COL32(230,70,70,255),IM_COL32(70,160,240,255),IM_COL32(250,200,60,255),IM_COL32(80,200,110,255)};for(int i=0;i<4;i++){float a=-2.4f+i*.85f;d->AddCircleFilled(v(c.x+cosf(a)*r*.5f,c.y+sinf(a)*r*.5f),r*.15f,dots[i]);}d->AddCircleFilled(v(c.x+r*.35f,c.y+r*.4f),r*.2f,bg);}
static inline void swirl(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddCircle(c,r*.8f,ink,24,r*.15f);d->AddCircle(c,r*.45f,ink,20,r*.15f);d->AddCircleFilled(c,r*.15f,ink);}
static inline void floppy(ImDrawList *d,ImVec2 c,float r,ImU32 ink,ImU32 bg){d->AddRectFilled(v(c.x-r*.8f,c.y-r*.8f),v(c.x+r*.8f,c.y+r*.8f),ink,r*.15f);d->AddRectFilled(v(c.x-r*.45f,c.y-r*.8f),v(c.x+r*.35f,c.y-r*.3f),bg);d->AddRectFilled(v(c.x-r*.5f,c.y+r*.1f),v(c.x+r*.5f,c.y+r*.65f),bg,r*.08f);}
static inline void key(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddCircle(v(c.x-r*.4f,c.y),r*.35f,ink,16,r*.18f);d->AddLine(v(c.x-r*.05f,c.y),v(c.x+r*.85f,c.y),ink,r*.18f);d->AddLine(v(c.x+r*.6f,c.y),v(c.x+r*.6f,c.y+r*.35f),ink,r*.16f);d->AddLine(v(c.x+r*.35f,c.y),v(c.x+r*.35f,c.y+r*.3f),ink,r*.16f);}
static inline void bubble(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddRectFilled(v(c.x-r*.85f,c.y-r*.65f),v(c.x+r*.85f,c.y+r*.4f),ink,r*.3f);d->AddTriangleFilled(v(c.x-r*.4f,c.y+r*.35f),v(c.x-r*.05f,c.y+r*.35f),v(c.x-r*.5f,c.y+r*.8f),ink);}
static inline void gear(ImDrawList *d,ImVec2 c,float r,ImU32 ink,ImU32 bg){for(int i=0;i<8;i++){float a=i*.785f;d->AddLine(v(c.x+cosf(a)*r*.45f,c.y+sinf(a)*r*.45f),v(c.x+cosf(a)*r*.9f,c.y+sinf(a)*r*.9f),ink,r*.28f);}d->AddCircleFilled(c,r*.62f,ink);d->AddCircleFilled(c,r*.25f,bg);}
static inline void sparkle(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddQuadFilled(v(c.x-r*.1f,c.y-r*.95f),v(c.x+r*.18f,c.y-r*.05f),v(c.x-r*.1f,c.y+r*.85f),v(c.x-r*.38f,c.y-r*.05f),ink);d->AddQuadFilled(v(c.x-r*.95f,c.y-r*.05f),v(c.x-r*.1f,c.y-r*.3f),v(c.x+r*.75f,c.y-r*.05f),v(c.x-r*.1f,c.y+r*.2f),ink);d->AddCircleFilled(v(c.x+r*.6f,c.y-r*.6f),r*.16f,ink);d->AddCircleFilled(v(c.x+r*.55f,c.y+r*.6f),r*.12f,ink);}
static inline void bolt(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddTriangleFilled(v(c.x+r*.2f,c.y-r*.9f),v(c.x-r*.55f,c.y+r*.15f),v(c.x+r*.05f,c.y+r*.15f),ink);d->AddTriangleFilled(v(c.x-r*.2f,c.y+r*.9f),v(c.x+r*.55f,c.y-r*.15f),v(c.x-r*.05f,c.y-r*.15f),ink);}
static inline void reset_arrow(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->PathArcTo(c,r*.75f,-2.6f,2.2f,20);d->PathStroke(ink,0,r*.2f);ImVec2 a(c.x+cosf(-2.6f)*r*.75f,c.y+sinf(-2.6f)*r*.75f);d->AddTriangleFilled(v(a.x-r*.38f,a.y-r*.05f),v(a.x+r*.3f,a.y-r*.35f),v(a.x+r*.15f,a.y+r*.35f),ink);}
static inline void check(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddLine(v(c.x-r*.8f,c.y),v(c.x-r*.25f,c.y+r*.55f),ink,r*.3f);d->AddLine(v(c.x-r*.25f,c.y+r*.55f),v(c.x+r*.85f,c.y-r*.6f),ink,r*.3f);}
static inline void cross(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddLine(v(c.x-r*.7f,c.y-r*.7f),v(c.x+r*.7f,c.y+r*.7f),ink,r*.3f);d->AddLine(v(c.x+r*.7f,c.y-r*.7f),v(c.x-r*.7f,c.y+r*.7f),ink,r*.3f);}
static inline void bomb(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddCircleFilled(v(c.x-r*.1f,c.y+r*.15f),r*.62f,ink);d->AddLine(v(c.x+r*.3f,c.y-r*.35f),v(c.x+r*.6f,c.y-r*.7f),ink,r*.15f);d->AddCircleFilled(v(c.x+r*.7f,c.y-r*.8f),r*.13f,IM_COL32(255,190,60,255));}
static inline void shield(ImDrawList *d,ImVec2 c,float r,ImU32 ink){ImVec2 p[5]={v(c.x-r*.75f,c.y-r*.75f),v(c.x+r*.75f,c.y-r*.75f),v(c.x+r*.7f,c.y+r*.05f),v(c.x,c.y+r*.9f),v(c.x-r*.7f,c.y+r*.05f)};d->AddConvexPolyFilled(p,5,ink);}
static inline bool has(const char *hay,const char *needle){return strstr(hay,needle)!=nullptr;}
/* The icon for an option row, chosen from its label (the reference pages give every row one). bg: the row colour. */
static inline void row_icon(ImDrawList *d,ImVec2 c,float r,const char *label,ImU32 ink,ImU32 bg){
 char l[96];size_t n=0;for(;label[n]&&n<sizeof l-1;n++)l[n]=(char)tolower((unsigned char)label[n]);l[n]=0;
 if(has(l,"severity"))bolt(d,c,r,ink);
 else if(has(l,"category"))bubble(d,c,r,ink);
 else if(has(l,"connection")||has(l,"network"))globe(d,c,r*.85f,ink);
 else if(has(l,"diagnostic")||has(l,"log"))monitor(d,c,r,ink);
 else if(has(l,"movie")||has(l,"cutscene"))film(d,c,r,ink,bg);
 else if(has(l,"reverb")||has(l,"echo"))echo(d,c,r,ink);
 else if(has(l,"music"))note(d,c,r,ink);
 else if(has(l,"volume")||has(l,"sound")||has(l,"audio"))speaker(d,c,r,ink);
 else if(has(l,"d-pad")||has(l,"dpad"))dpad(d,c,r,ink);
 else if(has(l,"confirm"))ring_mark(d,c,r,ink,false);
 else if(has(l,"cancel")||has(l,"back button"))ring_mark(d,c,r,ink,true);
 else if(has(l,"swap")||has(l,"convention")||has(l,"layout"))swap(d,c,r,ink);
 else if(has(l,"light"))bulb(d,c,r,ink);
 else if(has(l,"rumble")||has(l,"vibrat")||has(l,"intensity"))bolt(d,c,r,ink);
 else if(has(l,"controller")||has(l,"gamepad")||has(l,"button")||has(l,"stick")||has(l,"input")||has(l,"bind")||has(l,"keyboard"))gamepad(d,c,r,ink,bg);
 else if(has(l,"colo")||has(l,"tunic")||has(l,"hair")||has(l,"skin")||has(l,"palette"))palette(d,c,r,ink,bg);
 else if(has(l,"shield"))shield(d,c,r,ink);
 else if(has(l,"heart")||has(l,"health")||has(l,"life"))heart(d,c,r*.8f,ink);
 else if(has(l,"rupee")||has(l,"wallet")||has(l,"money"))rupee(d,c,r,ink);
 else if(has(l,"bomb"))bomb(d,c,r,ink);
 else if(has(l,"sword")||has(l,"weapon")||has(l,"damage")||has(l,"attack"))sword(d,c,r,ink);
 else if(has(l,"death")||has(l,"ohko")||has(l,"one-hit")||has(l,"one hit")||has(l,"difficult")||has(l,"enemy")||has(l,"enemies")||has(l,"boss"))skull(d,c,r,ink);
 else if(has(l,"time")||has(l,"speed")||has(l,"fps")||has(l,"frame")||has(l,"clock")||has(l,"timer"))clock(d,c,r,ink);
 else if(has(l,"seed")||has(l,"random")||has(l,"shuffle")||has(l,"logic")||has(l,"pool"))die(d,c,r,ink);
 else if(has(l,"warp")||has(l,"teleport"))swirl(d,c,r,ink);
 else if(has(l,"save")||has(l,"slot")||has(l,"file"))floppy(d,c,r,ink,bg);
 else if(has(l,"key"))key(d,c,r,ink);
 else if(has(l,"chat")||has(l,"message")||has(l,"text")||has(l,"hint"))bubble(d,c,r,ink);
 else if(has(l,"name")||has(l,"player")||has(l,"username"))person(d,c,r,ink);
 else if(has(l,"co-op")||has(l,"coop")||has(l,"online")||has(l,"lobby")||has(l,"room")||has(l,"teammate")||has(l,"remote"))people(d,c,r,2,ink);
 else if(has(l,"language")||has(l,"region")||has(l,"server"))globe(d,c,r*.85f,ink);
 else if(has(l,"window")||has(l,"display")||has(l,"resolution")||has(l,"screen")||has(l,"fullscreen")||has(l,"vsync")||has(l,"monitor")||has(l,"upscal")||has(l,"render"))monitor(d,c,r,ink);
 else if(has(l,"anti")||has(l,"aliasing")||has(l,"sharp")||has(l,"shader")||has(l,"graphic")||has(l,"bloom")||has(l,"effect")||has(l,"quality")||has(l,"grass"))sparkle(d,c,r,ink);
 else if(has(l,"cheat")||has(l,"infinite")||has(l,"unlock")||has(l,"item"))sparkle(d,c,r,ink);
 else gear(d,c,r*.85f,ink,bg);
}
/* A category in the left list: dark row with a thin divider; the open one green with a bright border (reference). */
static inline void nav_row(ImDrawList *d,ImVec2 a,ImVec2 b,bool open,bool hot,bool lit,bool last,float s){
 if(open){d->AddRectFilledMultiColor(a,b,IM_COL32(18,170,70,255),IM_COL32(18,170,70,255),IM_COL32(6,120,48,255),IM_COL32(6,120,48,255));d->AddRect(a,b,IM_COL32(90,245,130,255),8*s,0,2*s);return;}
 if(hot||lit)d->AddRectFilled(a,b,IM_COL32(40,80,130,200),8*s);
 if(lit)d->AddRect(a,b,IM_COL32(236,194,84,255),8*s,0,2*s);
 if(!last)d->AddLine(ImVec2(a.x+8*s,b.y+1*s),ImVec2(b.x-8*s,b.y+1*s),IM_COL32(70,110,160,110),1*s);
}
/* A category's icon, name and subtitle, sized from the row height (not the menu font) so the list never crowds:
 * icon about half the row, name about a third, subtitle a quarter, the two lines centred together. tex: the
 * nav-<category>.png icon if it loaded (else a drawn one). */
static inline void nav_label(ImDrawList *d,ImFont *f,ImVec2 a,ImVec2 b,const char *name,const char *sub,ImTextureID tex,bool on,float s){
 float h=b.y-a.y,ic=h*.55f,cx=a.x+10*s+ic*.5f,cy=(a.y+b.y)*.5f;
 if(tex)d->AddImage(tex,ImVec2(cx-ic*.5f,cy-ic*.5f),ImVec2(cx+ic*.5f,cy+ic*.5f),ImVec2(0,0),ImVec2(1,1),on?IM_COL32_WHITE:IM_COL32(150,160,170,200));
 else row_icon(d,ImVec2(cx,cy),ic*.42f,name,on?IM_COL32_WHITE:IM_COL32(150,160,170,200),IM_COL32(6,22,46,255));
 float x=cx+ic*.5f+12*s,avail=b.x-x-8*s;bool two=sub&&*sub&&h>=34*s;
 float ns=two?h*.37f:h*.44f,ss=h*.27f;
 if(f->CalcTextSizeA(ns,1e6f,0,name).x>avail)ns*=avail/f->CalcTextSizeA(ns,1e6f,0,name).x;
 if(two&&f->CalcTextSizeA(ss,1e6f,0,sub).x>avail)ss*=avail/f->CalcTextSizeA(ss,1e6f,0,sub).x;
 float block=two?ns+ss+2*s:ns,y=cy-block*.5f;
 d->PushClipRect(a,b,true);
 d->AddText(f,ns,ImVec2(x,y),on?IM_COL32(255,255,255,255):IM_COL32(150,160,170,200),name);
 if(two)d->AddText(f,ss,ImVec2(x,y+ns+2*s),on?IM_COL32(200,222,240,225):IM_COL32(130,140,150,170),sub);
 d->PopClipRect();
}
static inline void panel(ImDrawList *d,ImVec2 a,ImVec2 b,float s){d->AddRectFilled(a,b,IM_COL32(6,22,46,238),12*s);d->AddRect(a,b,IM_COL32(60,120,190,170),12*s,0,1.3f*s);}
/* One of the three big buttons at the bottom of a settings page. kind: 0 Reset to Default, 1 Cancel, 2 Save Settings */
static inline bool action_button(const char *id,int kind,const char *title,const char *sub,ImVec2 size,float s,bool enabled=true){
 ImGui::BeginDisabled(!enabled);ImVec2 p=ImGui::GetCursorScreenPos();bool click=ImGui::InvisibleButton(id,size,ImGuiButtonFlags_EnableNav);bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused(),down=ImGui::IsItemActive();ImGui::EndDisabled();
 ImDrawList *d=ImGui::GetWindowDrawList();ImVec2 e(p.x+size.x,p.y+size.y);
 ImU32 top,bot,line;if(kind==0){top=IM_COL32(16,54,104,255);bot=IM_COL32(10,36,74,255);line=IM_COL32(90,160,240,255);}else if(kind==1){top=IM_COL32(170,32,52,255);bot=IM_COL32(120,18,36,255);line=IM_COL32(255,110,120,255);}else{top=IM_COL32(26,175,72,255);bot=IM_COL32(10,125,50,255);line=IM_COL32(120,255,150,255);}
 if(!enabled){top=IM_COL32(40,50,62,255);bot=IM_COL32(30,38,48,255);line=IM_COL32(80,95,110,255);}
 if(hot&&enabled)for(int i=3;i>0;i--)d->AddRect(ImVec2(p.x-i*s,p.y-i*s),ImVec2(e.x+i*s,e.y+i*s),(line&0xffffff)|((50/i)<<24),9*s+i*s,0,2*s);
 d->AddRectFilled(p,e,bot,8*s);d->AddRectFilledMultiColor(ImVec2(p.x+2*s,p.y+2*s),ImVec2(e.x-2*s,e.y-2*s),top,top,bot,bot);d->AddRect(p,e,hot?IM_COL32(255,255,255,235):line,8*s,0,(hot?2.4f:1.6f)*s);
 float ih=size.y*.5f;ImVec2 ic(p.x+size.y*.6f,p.y+size.y*.5f+(down?1*s:0));ImU32 ink=enabled?IM_COL32_WHITE:IM_COL32(150,160,170,255);
 if(kind==0)reset_arrow(d,ic,ih*.55f,ink);else if(kind==1)cross(d,ic,ih*.45f,ink);else check(d,ic,ih*.5f,ink);
 ImFont *f=ImGui::GetFont();float fs=ImGui::GetFontSize(),x=p.x+size.y*1.15f,avail=e.x-x-8*s;
 // text sized from the button (V12.5: with the menu font it ran out of the button)
 bool two=sub&&*sub&&size.y>=34*s;float ts=fminf(fs,size.y*(two?.36f:.44f)),ss=fminf(fs*.72f,size.y*.26f);(void)fs;
 float tw=f->CalcTextSizeA(ts,1e6f,0,title).x;if(tw>avail)ts*=avail/tw;
 if(two){float sw=f->CalcTextSizeA(ss,1e6f,0,sub).x;if(sw>avail)ss*=avail/sw;}
 float block=two?ts+ss+2*s:ts,ty=p.y+(size.y-block)*.5f;
 d->PushClipRect(p,e,true);d->AddText(f,ts,ImVec2(x,ty),ink,title);
 if(two)d->AddText(f,ss,ImVec2(x,ty+ts+2*s),enabled?IM_COL32(225,236,248,235):IM_COL32(140,150,160,255),sub);
 d->PopClipRect();
 return click&&enabled;
}
/* A page's header: icon, title, one-line description and the info box on the right. Every text is sized to fit its
 * box (V12.5: the info text ran out of its box with the menu font). icon: texture or 0 (then draw_icon is used). */
static inline void page_header(ImDrawList *d,ImFont *f,ImVec2 p,float w,float h,ImTextureID icon,const char *title,const char *desc,const char *info,float s){
 float isz=h*.78f;ImVec2 ic(p.x+isz*.5f+2*s,p.y+h*.5f);
 if(icon)d->AddImage(icon,ImVec2(ic.x-isz*.5f,ic.y-isz*.5f),ImVec2(ic.x+isz*.5f,ic.y+isz*.5f));else row_icon(d,ic,isz*.42f,title,IM_COL32_WHITE,IM_COL32(6,22,46,255));
 float bw=w>620*s?fminf(w*.36f,420*s):0,tx=p.x+isz+16*s,tw_max=w-bw-(tx-p.x)-16*s;
 float ts=h*.44f,ds=h*.26f;float tw=f->CalcTextSizeA(ts,1e6f,0,title).x;if(tw>tw_max)ts*=tw_max/tw;
 float dw=f->CalcTextSizeA(ds,1e6f,0,desc).x;if(dw>tw_max)ds=fmaxf(ds*tw_max/dw,h*.18f);
 float block=ts+ds+4*s,ty=p.y+(h-block)*.5f;
 d->PushClipRect(ImVec2(tx,p.y),ImVec2(tx+tw_max,p.y+h),true);d->AddText(f,ts,ImVec2(tx,ty),IM_COL32_WHITE,title);d->AddText(f,ds,ImVec2(tx,ty+ts+4*s),IM_COL32(205,225,240,255),desc);d->PopClipRect();
 if(bw>0&&info&&*info){ImVec2 a(p.x+w-bw,p.y+4*s),b(p.x+w,p.y+h-4*s);
  d->AddRectFilled(a,b,IM_COL32(8,45,78,235),10*s);d->AddRect(a,b,IM_COL32(110,180,230,200),10*s,0,1.5f*s);
  float bh=b.y-a.y,r=fminf(11*s,bh*.22f);ImVec2 c(a.x+r+10*s,(a.y+b.y)*.5f);d->AddCircle(c,r,IM_COL32_WHITE,20,2*s);
  float is=r*1.4f;ImVec2 isz2=f->CalcTextSizeA(is,1e6f,0,"i");d->AddText(f,is,ImVec2(c.x-isz2.x*.5f,c.y-isz2.y*.5f),IM_COL32_WHITE,"i");
  float x=c.x+r+10*s,wrap=b.x-x-10*s,fs2=bh*.3f;while(fs2>bh*.16f&&f->CalcTextSizeA(fs2,1e6f,wrap,info).y>bh-10*s)fs2-=.5f*s;
  float th=f->CalcTextSizeA(fs2,1e6f,wrap,info).y;d->PushClipRect(a,b,true);d->AddText(f,fs2,ImVec2(x,(a.y+b.y-th)*.5f),IM_COL32(225,235,245,255),info,nullptr,wrap);d->PopClipRect();}
}
/* A wide button (Report a Bug: Preview / Submit / Save locally). kind 0 blue, 1 bright blue, 2 teal */
static inline bool wide_button(const char *label,ImVec2 size,int kind,float s,bool enabled=true){
 ImGui::BeginDisabled(!enabled);ImVec2 p=ImGui::GetCursorScreenPos();bool click=ImGui::InvisibleButton(label,size,ImGuiButtonFlags_EnableNav);bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused();ImGui::EndDisabled();
 ImDrawList *d=ImGui::GetWindowDrawList();ImVec2 e(p.x+size.x,p.y+size.y);
 ImU32 top=kind==2?IM_COL32(16,120,96,255):kind==1?IM_COL32(26,96,200,255):IM_COL32(20,70,150,255),bot=kind==2?IM_COL32(8,80,66,255):kind==1?IM_COL32(14,62,140,255):IM_COL32(12,44,100,255),line=kind==2?IM_COL32(90,230,180,255):IM_COL32(110,180,255,255);
 if(!enabled){top=IM_COL32(40,50,62,255);bot=IM_COL32(30,38,48,255);line=IM_COL32(80,95,110,255);}
 d->AddRectFilled(p,e,bot,7*s);d->AddRectFilledMultiColor(ImVec2(p.x+2*s,p.y+2*s),ImVec2(e.x-2*s,e.y-2*s),top,top,bot,bot);d->AddRect(p,e,hot&&enabled?IM_COL32(255,255,255,235):line,7*s,0,(hot?2.2f:1.4f)*s);
 const char *end=strstr(label,"##");ImFont *f=ImGui::GetFont();float fs=fminf(ImGui::GetFontSize(),size.y*.46f);ImVec2 ts=f->CalcTextSizeA(fs,1e6f,0,label,end);if(ts.x>size.x-12*s){fs*=(size.x-12*s)/ts.x;ts=f->CalcTextSizeA(fs,1e6f,0,label,end);}
 d->AddText(f,fs,ImVec2(p.x+(size.x-ts.x)*.5f,p.y+(size.y-ts.y)*.5f),enabled?IM_COL32_WHITE:IM_COL32(150,160,170,255),label,end);
 return click&&enabled;
}
/* "< Back" pill for the start-screen pages (reference: bottom-left, dark blue, white border). Its own spot, never
 * under a page panel, so the whole button reacts to the mouse. */
static inline bool back_button(const char *id,ImVec2 size,float s,bool enabled=true){
 ImGui::BeginDisabled(!enabled);ImVec2 p=ImGui::GetCursorScreenPos();bool click=ImGui::InvisibleButton(id,size,ImGuiButtonFlags_EnableNav);bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused(),down=ImGui::IsItemActive();ImGui::EndDisabled();
 ImDrawList *d=ImGui::GetWindowDrawList();ImVec2 e(p.x+size.x,p.y+size.y);float r=size.y*.32f;
 if(hot&&enabled)for(int i=3;i>0;i--)d->AddRect(ImVec2(p.x-i*s,p.y-i*s),ImVec2(e.x+i*s,e.y+i*s),IM_COL32(150,210,255,50/i),r+i*s,0,2*s);
 d->AddRectFilled(p,e,IM_COL32(8,40,96,240),r);d->AddRectFilledMultiColor(ImVec2(p.x+3*s,p.y+3*s),ImVec2(e.x-3*s,e.y-3*s),hot?IM_COL32(40,110,210,255):IM_COL32(28,86,180,255),hot?IM_COL32(40,110,210,255):IM_COL32(28,86,180,255),IM_COL32(12,52,120,255),IM_COL32(12,52,120,255));
 d->AddRect(p,e,hot?IM_COL32(255,255,255,255):IM_COL32(200,225,255,230),r,0,(hot?3.f:2.2f)*s);
 ImFont *f=ImGui::GetFont();float fs=size.y*.46f;ImVec2 ts=f->CalcTextSizeA(fs,1e6f,0,"Back");float cw=fs*.32f,total=cw+fs*.45f+ts.x,x=p.x+(size.x-total)*.5f,cy=p.y+size.y*.5f+(down?1*s:0);
 ImU32 ink=enabled?IM_COL32_WHITE:IM_COL32(160,170,180,255);chevron(d,ImVec2(x+cw*.5f,cy),cw*.9f,0,ink,fs*.14f);
 d->AddText(f,fs,ImVec2(x+cw+fs*.45f,cy-ts.y*.5f),ink,"Back");
 return click&&enabled;
}
/* A section title inside a page (e.g. "Controller Settings"): small icon + white heading, as written. */
static inline void section(ImDrawList *d,ImVec2 p,float w,const char *title,float fs,float s){
 ImVec2 c(p.x+fs*.55f,p.y+fs*.6f);row_icon(d,c,fs*.42f,title,IM_COL32(235,242,255,255),IM_COL32(6,22,46,255));
 d->AddText(ImGui::GetFont(),fs*.95f,ImVec2(p.x+fs*1.3f,p.y+fs*.08f),IM_COL32(245,250,255,255),title);
 d->AddLine(ImVec2(p.x,p.y+fs*1.32f),ImVec2(p.x+w,p.y+fs*1.32f),IM_COL32(70,120,180,90),1*s);
}
}
