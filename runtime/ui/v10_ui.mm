#include "tropical_ui.h"
#include "lao_icons.h"
#include "../laonline_build.h"  // V13.1: version in What's New
#include <math.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
static ImFont *heading_font,*body_font,*serif_font,*global_title_font,*global_text_font;
void tropical_fonts_init(void){
 auto &io=ImGui::GetIO();
#ifdef _WIN32
 heading_font=io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeuib.ttf",40);
 body_font=io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",30);
 serif_font=io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/georgiab.ttf",44);
 global_title_font=io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/georgiab.ttf",80);global_text_font=io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/georgia.ttf",32);
#endif
 if(!heading_font)heading_font=io.FontDefault?io.FontDefault:io.Fonts->Fonts[0];if(!body_font)body_font=heading_font;if(!serif_font)serif_font=heading_font;if(!global_title_font)global_title_font=serif_font;if(!global_text_font)global_text_font=body_font;
}
ImFont *tropical_body_font(void){return body_font;}
ImFont *tropical_heading_font(void){return heading_font;}

// Native ImGui widgets: mouse, keyboard and gamepad share the same focus item.
static void tile_icon(ImDrawList *d,ImVec2 c,float r,int icon,ImU32 ink){
 if(icon==0){d->AddLine(ImVec2(c.x-r*.7f,c.y+r*.7f),ImVec2(c.x+r*.7f,c.y-r*.7f),ink,r*.35f);d->AddLine(ImVec2(c.x-r*.6f,c.y),ImVec2(c.x,c.y+r*.6f),ink,r*.22f);}
 else if(icon==1){for(int i=-1;i<=1;i+=2){d->AddCircleFilled(ImVec2(c.x+i*r*.42f,c.y-r*.4f),r*.3f,ink);d->AddRectFilled(ImVec2(c.x+i*r*.42f-r*.32f,c.y),ImVec2(c.x+i*r*.42f+r*.32f,c.y+r*.6f),ink,r*.2f);}}
 else if(icon==2){d->AddRect(ImVec2(c.x-r*.7f,c.y-r*.7f),ImVec2(c.x+r*.7f,c.y+r*.7f),ink,r*.2f,0,r*.15f);for(int i=-1;i<=1;i++)d->AddCircleFilled(ImVec2(c.x+i*r*.4f,c.y+i*r*.4f),r*.12f,ink);}
 else if(icon==3){d->AddCircle(c,r,ink,32,r*.12f);d->AddEllipse(c,ImVec2(r*.45f,r),ink,0,24,r*.1f);d->AddLine(ImVec2(c.x-r,c.y),ImVec2(c.x+r,c.y),ink,r*.1f);}
 else if(icon==4){d->AddRectFilled(ImVec2(c.x-r*.6f,c.y-r*.8f),ImVec2(c.x+r*.6f,c.y+r*.2f),ink,r*.2f);d->AddLine(ImVec2(c.x,c.y),ImVec2(c.x,c.y+r*.8f),ink,r*.2f);d->AddLine(ImVec2(c.x-r*.5f,c.y+r*.8f),ImVec2(c.x+r*.5f,c.y+r*.8f),ink,r*.2f);}
 else {d->AddRect(ImVec2(c.x-r*.7f,c.y-r),ImVec2(c.x+r*.7f,c.y+r),ink,r*.1f,0,r*.14f);for(int i=-1;i<=1;i++)d->AddLine(ImVec2(c.x-r*.4f,c.y+i*r*.4f),ImVec2(c.x+r*.4f,c.y+i*r*.4f),ink,r*.09f);}
}
bool tropical_tile(const char *id,const char *art,const char *title,const char *description,ImVec2 size,ImU32 accent,int icon){
 ImGui::PushID(id);ImVec2 p=ImGui::GetCursorScreenPos();bool clicked=ImGui::InvisibleButton("##tile",size,ImGuiButtonFlags_EnableNav);
 bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused(),down=ImGui::IsItemActive();auto *d=ImGui::GetWindowDrawList();float scale=size.y/220.f,round=13*scale,pad=9*scale;
 ImVec2 end(p.x+size.x,p.y+size.y);if(hot){for(int i=3;i;i--)d->AddRect(ImVec2(p.x-i*scale,p.y-i*scale),ImVec2(end.x+i*scale,end.y+i*scale),(accent&0xffffff)|((45/i)<<24),round+i*scale,0,3*scale);}
 d->AddRectFilled(p,end,IM_COL32(0,24,48,245),round);d->AddRect(p,end,accent,round,0,(hot?3:1.5f)*scale);
 ImVec2 a(p.x+pad,p.y+pad),b(end.x-pad,p.y+size.y*.65f);ImTextureID tex=0;float aspect=1;
 if(tropical_image(art,&tex,&aspect)){float target=(b.x-a.x)/(b.y-a.y);ImVec2 uv0(0,0),uv1(1,1);if(target>aspect){float h=aspect/target;uv0.y=(1-h)*.5f;uv1.y=1-uv0.y;}else{float w=target/aspect;uv0.x=(1-w)*.5f;uv1.x=1-uv0.x;}d->AddImageRounded(tex,a,b,uv0,uv1,IM_COL32_WHITE,round*.7f);
  if(!strcmp(art,"coop-velvet")){  // V15.1: a small dedication to the artist (developer's request)
   const char *t="Art by Velvet_Charm";float ts=12.5f*scale;ImVec2 sz=body_font->CalcTextSizeA(ts,1e6f,0,t);
   ImVec2 q(b.x-sz.x-14*scale,b.y-sz.y-9*scale);d->AddRectFilled(ImVec2(q.x-7*scale,q.y-3*scale),ImVec2(b.x-6*scale,b.y-5*scale),IM_COL32(0,16,34,175),8*scale);
   d->AddText(body_font,ts,ImVec2(q.x+1,q.y+1),IM_COL32(0,0,0,160),t);d->AddText(body_font,ts,q,IM_COL32(255,214,92,255),t);}}
 d->AddRectFilledMultiColor(ImVec2(p.x+2,b.y),ImVec2(end.x-2,end.y-12*scale),(accent&0xffffff)|0xa0000000,IM_COL32(0,24,48,240),IM_COL32(0,24,48,240),(accent&0xffffff)|0x60000000);
 ImVec2 lower(p.x+pad,b.y+7*scale);float badge=48*scale;char asset[32];snprintf(asset,sizeof asset,"tile-icon-%d",!strcmp(id,"coop")?2:!strcmp(id,"randomizer")||!strcmp(id,"solo-rando")?3:!strcmp(id,"global")?4:!strcmp(id,"leaders")?5:!strcmp(id,"credits")?6:!strcmp(id,"browse")?1:0);ImTextureID badge_tex=0;float badge_aspect=1;
 if(tropical_image(asset,&badge_tex,&badge_aspect))d->AddImageRounded(badge_tex,lower,ImVec2(lower.x+badge,lower.y+badge),ImVec2(0,0),ImVec2(1,1),IM_COL32_WHITE,8*scale);else{d->AddRectFilled(lower,ImVec2(lower.x+badge,lower.y+badge),accent,8*scale);tile_icon(d,ImVec2(lower.x+badge*.5f,lower.y+badge*.5f),badge*.29f,icon,IM_COL32_WHITE);}
 float x=lower.x+badge+9*scale,available=end.x-x-24*scale,fs=23*scale;float width=heading_font->CalcTextSizeA(fs,1e6f,0,title).x;if(width>available)fs*=available/width;
 d->AddText(heading_font,fs,ImVec2(x,lower.y+(down?2:0)),IM_COL32_WHITE,title);
 float desc_size=14*scale,desc_width=body_font->CalcTextSizeA(desc_size,1e6f,0,description).x;if(desc_width>available)desc_size*=available/desc_width;
 d->AddText(body_font,desc_size,ImVec2(x,lower.y+31*scale),IM_COL32(238,245,250,255),description);
 d->AddLine(ImVec2(end.x-17*scale,lower.y+10*scale),ImVec2(end.x-11*scale,lower.y+16*scale),IM_COL32_WHITE,2*scale);d->AddLine(ImVec2(end.x-11*scale,lower.y+16*scale),ImVec2(end.x-17*scale,lower.y+22*scale),IM_COL32_WHITE,2*scale);
 ImGui::PopID();return clicked;
}
bool tropical_utility(const char *title,const char *subtitle,ImVec2 size,int icon){
 ImGui::PushID(title);ImVec2 p=ImGui::GetCursorScreenPos();bool click=ImGui::InvisibleButton("action",size,ImGuiButtonFlags_EnableNav);bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused();auto *d=ImGui::GetWindowDrawList();float u=size.y/74.f;
 d->AddRectFilled(p,ImVec2(p.x+size.x,p.y+size.y),hot?IM_COL32(0,105,155,245):IM_COL32(0,40,67,230),12*u);d->AddRect(p,ImVec2(p.x+size.x,p.y+size.y),IM_COL32(175,220,239,230),12*u,0,1.5f*u);
 ImVec2 c(p.x+40*u,p.y+size.y*.5f);if(icon==0){d->AddCircle(c,15*u,IM_COL32_WHITE,24,8*u);for(int i=0;i<8;i++){float a=i*3.14159265f/4;d->AddLine(ImVec2(c.x+15*u*cosf(a),c.y+15*u*sinf(a)),ImVec2(c.x+22*u*cosf(a),c.y+22*u*sinf(a)),IM_COL32_WHITE,7*u);}}
 else if(icon==1){d->AddRect(ImVec2(c.x-19*u,c.y-18*u),ImVec2(c.x+19*u,c.y+18*u),IM_COL32_WHITE,2*u,0,3*u);d->AddLine(ImVec2(c.x,c.y-17*u),ImVec2(c.x,c.y+17*u),IM_COL32_WHITE,3*u);for(int i=0;i<3;i++)d->AddLine(ImVec2(c.x-14*u,c.y-10*u+i*8*u),ImVec2(c.x-5*u,c.y-10*u+i*8*u),IM_COL32_WHITE,2*u);}
 else {d->AddRectFilled(ImVec2(c.x-15*u,c.y-20*u),ImVec2(c.x+10*u,c.y+20*u),IM_COL32_WHITE,2*u);d->AddCircleFilled(ImVec2(c.x+4*u,c.y),2*u,IM_COL32(0,35,65,255));}
 d->AddText(heading_font,18*u,ImVec2(p.x+85*u,p.y+12*u),IM_COL32_WHITE,title);d->AddText(body_font,14*u,ImVec2(p.x+85*u,p.y+37*u),IM_COL32(208,230,245,255),subtitle);
 ImGui::PopID();return click;
}

// V12.5 Credits - laid out like the reference: CREDITS banner with triforces, two founder cards (gold / green) and
// three cards below (Beta Testers, Special Thanks, Tools & Technologies). Drawn at the size of the page, so it fits
// any window; when the window is very small the page scrolls instead of squashing the text.
static void global_palm(ImDrawList *d,ImVec2 c,float r,ImU32 ink);
static void credit_swirl(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->PathArcTo(c,r,0.3f,5.6f,20);d->PathStroke(ink,0,r*.22f);d->PathArcTo(ImVec2(c.x+r*.2f,c.y-r*.05f),r*.5f,3.4f,8.6f,14);d->PathStroke(ink,0,r*.18f);}
static void credit_corners(ImDrawList *d,ImVec2 a,ImVec2 b,float r,ImU32 ink,float th){
 const ImVec2 pts[4]={a,ImVec2(b.x,a.y),b,ImVec2(a.x,b.y)};const float sx[4]={1,-1,-1,1},sy[4]={1,1,-1,-1};
 for(int i=0;i<4;i++){ImVec2 p=pts[i];d->AddLine(p,ImVec2(p.x+sx[i]*r,p.y),ink,th);d->AddLine(p,ImVec2(p.x,p.y+sy[i]*r),ink,th);d->AddCircle(ImVec2(p.x+sx[i]*r*.45f,p.y+sy[i]*r*.45f),r*.18f,ink,10,th*.8f);}
}
static void credit_card(ImDrawList *d,ImVec2 a,ImVec2 b,ImU32 accent,ImU32 top,ImU32 bottom,float u,bool glow){
 if(glow)for(int i=3;i>0;i--)d->AddRect(ImVec2(a.x-i*2*u,a.y-i*2*u),ImVec2(b.x+i*2*u,b.y+i*2*u),(accent&0xffffff)|((40/i)<<24),12*u+i*2*u,0,2.5f*u);
 d->AddRectFilled(a,b,bottom,12*u);d->AddRectFilledMultiColor(ImVec2(a.x+2*u,a.y+2*u),ImVec2(b.x-2*u,b.y-2*u),top,top,bottom,bottom);
 d->AddRect(a,b,accent,12*u,0,2.2f*u);
}
static void credit_portrait(ImDrawList *d,ImVec2 c,float r,const uint8_t rgb[3],ImU32 ring){
 static const uint8_t hair[3]={255,223,87};ImTextureID tex=0;float aspect=1;d->AddCircleFilled(c,r+3,ring);d->AddCircleFilled(c,r,IM_COL32(110,200,240,255));
 if(laonline_avatar_icon(rgb,hair,&tex,&aspect)||tropical_image("credit-portrait",&tex,&aspect))  // ThyHeroOfTime's Link avatars (laonline-avatars/)
 d->AddImageRounded(tex,ImVec2(c.x-r,c.y-r),ImVec2(c.x+r,c.y+r),ImVec2(0,0),ImVec2(1,1),IM_COL32_WHITE,r);
 else{d->AddCircleFilled(c,r,IM_COL32(rgb[0],rgb[1],rgb[2],255));d->AddCircleFilled(ImVec2(c.x,c.y-r*.2f),r*.55f,IM_COL32(255,215,170,255));d->PathArcTo(ImVec2(c.x,c.y-r*.35f),r*.62f,3.3f,6.1f,12);d->PathFillConvex(IM_COL32(255,223,87,255));}
}
void tropical_credits(float scale){
 using namespace lao_icon;
 ImVec2 avail=ImGui::GetContentRegionAvail(),o=ImGui::GetCursorScreenPos();
 // design size 1360 x 650; never smaller than ~0.6 of the UI scale (then the page scrolls)
 float u=fmaxf(fminf(avail.x/1360.f,avail.y/650.f),.6f*scale),W=fmaxf(avail.x,1360*u*.98f),H=fmaxf(avail.y,650*u);
 auto *d=ImGui::GetWindowDrawList();const ImU32 gold=IM_COL32(255,212,70,255),white=IM_COL32(245,250,255,255),soft=IM_COL32(215,228,245,255);
 auto text=[&](ImFont *f,float fs,float x,float y,ImU32 c,const char *t,float wrap=0){d->AddText(f,fs,ImVec2(x,y),c,t,nullptr,wrap);};
 auto height=[&](ImFont *f,float fs,const char *t,float wrap){return f->CalcTextSizeA(fs,1e6f,wrap,t).y;};
 auto centred=[&](ImFont *f,float fs,float cx,float y,ImU32 c,const char *t){float w=f->CalcTextSizeA(fs,1e6f,0,t).x;d->AddText(f,fs,ImVec2(cx-w*.5f,y),c,t);};
 float cx=o.x+W*.5f;
 // ---- banner ----
 {float tw=serif_font->CalcTextSizeA(50*u,1e6f,0,"CREDITS").x;centred(serif_font,50*u,cx+2*u,o.y+3*u,IM_COL32(90,55,0,200),"CREDITS");centred(serif_font,50*u,cx,o.y,gold,"CREDITS");
  for(int side=-1;side<=1;side+=2){triforce(d,ImVec2(cx+side*(tw*.5f+44*u),o.y+30*u),17*u,gold);credit_swirl(d,ImVec2(cx+side*(tw*.5f+125*u),o.y+30*u),10*u,gold);
   d->AddLine(ImVec2(cx+side*(tw*.5f+145*u),o.y+30*u),ImVec2(cx+side*(tw*.5f+260*u),o.y+30*u),IM_COL32(255,212,70,120),1.5f*u);}
  centred(body_font,18*u,cx,o.y+60*u,white,"A special thanks to everyone who has made this project possible.");
  d->AddLine(ImVec2(cx-300*u,o.y+86*u),ImVec2(cx+300*u,o.y+86*u),IM_COL32(255,212,70,110),1.2f*u);heart(d,ImVec2(cx,o.y+86*u),5*u,gold);}
 float gap=14*u,top=o.y+98*u,top_h=(H-98*u-gap)*.40f,low=top+top_h+gap,low_h=o.y+H-low-24*u,half=(W-gap)*.5f;
 // ---- founders ----
 struct Founder{const char *heading,*name,*role,*project,*message;ImU32 accent,top,bottom;uint8_t rgb[3];int icon;};
 const Founder founders[2]={
  {"Original Creator & Co-Founder","peiikai","Original Creator & Co-Founder","LAS Recompiled","Special thanks to peiikai for creating LAS Recompiled and for giving full permission and support to allow the development of the multiplayer mod and additional features. This project would not be possible without his incredible work and dedication to the community.",gold,IM_COL32(48,40,14,245),IM_COL32(16,20,24,245),{0x3f,0xa3,0x4d},0},
  {"Multiplayer Mod Co-Founder & Lead Developer","ThyHeroOfTime","Multiplayer Mod Co-Founder & Lead Developer",nullptr,"Thank you to everyone who continues to support this project. This multiplayer mod is a passion project built for the community, and I'm honored to work alongside such an amazing group of people.",IM_COL32(60,235,130,255),IM_COL32(10,52,36,245),IM_COL32(8,26,26,245),{0x3f,0xa3,0x4d},1}};
 for(int i=0;i<2;i++){const Founder &f=founders[i];ImVec2 a(o.x+i*(half+gap),top),b(a.x+half,top+top_h);credit_card(d,a,b,f.accent,f.top,f.bottom,u,true);
  ImVec2 ic(a.x+30*u,a.y+29*u);if(f.icon==0)triforce(d,ic,15*u,gold);else gamepad(d,ic,16*u,white,f.bottom);
  float hs=24*u,hw=serif_font->CalcTextSizeA(hs,1e6f,0,f.heading).x,room=half-110*u;if(hw>room)hs*=room/hw;text(serif_font,hs,a.x+60*u,a.y+15*u,f.accent,f.heading);
  credit_swirl(d,ImVec2(b.x-28*u,a.y+29*u),9*u,(f.accent&0xffffff)|0xc0000000);
  ImVec2 fa(a.x+14*u,a.y+56*u),fb(b.x-14*u,b.y-12*u);d->AddRect(fa,fb,(f.accent&0xffffff)|0x90000000,8*u,0,1.4f*u);credit_corners(d,ImVec2(fa.x+5*u,fa.y+5*u),ImVec2(fb.x-5*u,fb.y-5*u),14*u,(f.accent&0xffffff)|0xa0000000,1.4f*u);
  global_palm(d,ImVec2(fa.x+48*u,fb.y-42*u),40*u,(f.accent&0xffffff)|0x38000000);global_palm(d,ImVec2(fb.x-50*u,fb.y-48*u),46*u,(f.accent&0xffffff)|0x38000000);
  float r=fminf(46*u,(fb.y-fa.y)*.3f);ImVec2 pc(fa.x+30*u+r,fa.y+16*u+r);credit_portrait(d,pc,r,f.rgb,IM_COL32(240,248,255,255));
  float x=pc.x+r+20*u,wrap=fb.x-x-18*u,y=fa.y+14*u;text(heading_font,28*u,x,y,white,f.name);y+=34*u;
  float rs=19*u,rw=heading_font->CalcTextSizeA(rs,1e6f,0,f.role).x;if(rw>wrap)rs*=wrap/rw;text(heading_font,rs,x,y,f.accent,f.role);y+=24*u;
  if(f.project){text(body_font,17*u,x,y,soft,f.project);y+=24*u;}else y+=12*u;
  float ms=16.5f*u;while(ms>11*u&&y+height(body_font,ms,f.message,wrap)>fb.y-8*u)ms-=.5f*u;text(body_font,ms,x,y,white,f.message,wrap);}
 // ---- lower cards ----
 float third=(W-2*gap)/3;
 auto card_head=[&](ImVec2 a,float w,int icon,ImU32 icon_ink,const char *title,const char *desc){ImVec2 ic(a.x+30*u,a.y+30*u);if(icon==0)people(d,ic,15*u,2,icon_ink);else if(icon==1)heart(d,ic,15*u,icon_ink);else wrench(d,ic,15*u,icon_ink);
  text(heading_font,22*u,a.x+62*u,a.y+12*u,white,title);float ds=15*u;text(body_font,ds,a.x+62*u,a.y+40*u,soft,desc,w-80*u);return a.y+44*u+height(body_font,ds,desc,w-80*u)+10*u;};
 // Beta Testers
 {ImVec2 a(o.x,low),b(a.x+third,low+low_h);credit_card(d,a,b,IM_COL32(70,130,210,255),IM_COL32(12,34,68,245),IM_COL32(8,22,46,245),u,false);
  float y=card_head(a,third,0,white,"Beta Testers","A huge thank you to our dedicated beta testers for their time, feedback, and support throughout development.");
  // Locus is spelled as the player asked for in the V12 request (the reference image shows "Lucas")
  struct Tester{const char *name,*tip;uint8_t rgb[3];};const Tester testers[7]={{"Heroes of Twilight",nullptr,{0x3f,0xa3,0x4d}},{"Locus",nullptr,{0xd6,0x3a,0x3a}},{"Sanic",nullptr,{0x2f,0x6f,0xd6}},{"LunarFrankie",nullptr,{0x8a,0x4f,0xd8}},
   {"Velvet","Velvet_Charm - had the idea for the Co-op mod. Thank you for helping inspire this shared adventure!",{0xe8,0x6a,0xa8}},{"LunarGamerz",nullptr,{0x22,0x9f,0xd6}},{"Kit",nullptr,{0x3f,0xa3,0x4d}}};
  float cw=(third-40*u)*.5f,ch=fminf(44*u,(b.y-y-14*u)/4-6*u);if(ch<26*u)ch=26*u;
  for(int i=0;i<7;i++){ImVec2 p(a.x+14*u+(i%2)*(cw+12*u),y+(i/2)*(ch+6*u)),e(p.x+cw,p.y+ch);d->AddRectFilled(p,e,IM_COL32(10,28,56,250),8*u);d->AddRect(p,e,IM_COL32(60,100,160,220),8*u,0,1.2f*u);
   float r=ch*.4f;credit_portrait(d,ImVec2(p.x+10*u+r,p.y+ch*.5f),r,testers[i].rgb,IM_COL32(230,240,255,255));text(body_font,17*u,p.x+20*u+r*2+8*u,p.y+ch*.5f-11*u,white,testers[i].name);
   if(testers[i].tip&&ImGui::IsMouseHoveringRect(p,e))ImGui::SetTooltip("%s",testers[i].tip);}}
 // Special Thanks
 {ImVec2 a(o.x+third+gap,low),b(a.x+third,low+low_h);credit_card(d,a,b,IM_COL32(150,90,240,255),IM_COL32(52,24,96,245),IM_COL32(22,14,52,245),u,false);
  float y=card_head(a,third,1,IM_COL32(255,150,215,255),"Special Thanks","Additional thanks to everyone who has helped, supported, or believed in this project.");
  const char *items[][2]={{"The Legend of Zelda Community","For the continuous support and encouragement."},{"Playtesters & Community Members","For reporting bugs, sharing feedback, and helping shape the experience."},{"Nintendo","For creating The Legend of Zelda: Link's Awakening."},{"Modding Community","For the tools, resources, and shared knowledge."},{"All Contributors","To everyone who has helped in any way, big or small."}};
  float quote_h=50*u,wrap=third-66*u,space=b.y-quote_h-18*u-y,fs=1;auto need=[&](float k){float n=0;for(auto &it:items)n+=19*u*k+height(body_font,14*u*k,it[1],wrap)+4*u*k;return n;};while(fs>.6f&&need(fs)>space)fs-=.03f;
  for(auto &it:items){d->AddCircleFilled(ImVec2(a.x+30*u,y+10*u*fs),3.5f*u,white);text(heading_font,16*u*fs,a.x+42*u,y,white,it[0]);y+=19*u*fs;text(body_font,14*u*fs,a.x+42*u,y,soft,it[1],wrap);y+=height(body_font,14*u*fs,it[1],wrap)+4*u*fs;}
  ImVec2 qa(a.x+14*u,b.y-quote_h-12*u),qb(b.x-14*u,b.y-12*u);d->AddRectFilled(qa,qb,IM_COL32(16,10,40,240),8*u);d->AddRect(qa,qb,gold,8*u,0,1.4f*u);credit_corners(d,ImVec2(qa.x+4*u,qa.y+4*u),ImVec2(qb.x-4*u,qb.y-4*u),9*u,gold,1.2f*u);
  centred(body_font,14*u,(qa.x+qb.x)*.5f,qa.y+7*u,white,"To the community - thank you for being part of this journey.");centred(body_font,14*u,(qa.x+qb.x)*.5f,qa.y+26*u,white,"Your support keeps the dream alive.");heart(d,ImVec2((qa.x+qb.x)*.5f,qb.y),5*u,gold);}
 // Tools & Technologies
 {ImVec2 a(o.x+2*(third+gap),low),b(a.x+third,low+low_h);credit_card(d,a,b,IM_COL32(235,80,80,255),IM_COL32(92,22,30,245),IM_COL32(52,12,22,245),u,false);
  // faint egg-on-the-mountain silhouette, like the reference
  {ImVec2 m(b.x-70*u,b.y-60*u);ImU32 ghost=IM_COL32(255,140,140,20);d->AddTriangleFilled(ImVec2(m.x-70*u,b.y-6*u),ImVec2(m.x+60*u,b.y-6*u),ImVec2(m.x-5*u,m.y-20*u),ghost);d->AddEllipseFilled(ImVec2(m.x,m.y-60*u),ImVec2(30*u,40*u),ghost);}
  float y=card_head(a,third,2,white,"Tools & Technologies","This project was made possible with the help of amazing tools and open-source projects.");
  const char *items[][2]={{"Nintendo Switch Homebrew Community","For the tools and resources that make this possible."},{"Reverse Engineering Community","For documentation and research."},{"Open Source Libraries","Dear ImGui, SDL3, Vulkan, FFmpeg and more - for their incredible tools and frameworks."},{"Testing & Debugging Tools","For helping improve stability and performance."},{"Special Thanks","To all developers, creators, and communities who continue to support the preservation and expansion of classic games."}};
  float wrap=third-66*u,space=b.y-12*u-y,fs=1;auto need=[&](float k){float n=0;for(auto &it:items)n+=19*u*k+height(body_font,14*u*k,it[1],wrap)+5*u*k;return n;};while(fs>.6f&&need(fs)>space)fs-=.03f;
  for(auto &it:items){d->AddCircleFilled(ImVec2(a.x+30*u,y+10*u*fs),3.5f*u,IM_COL32(255,90,90,255));text(heading_font,16*u*fs,a.x+42*u,y,white,it[0]);y+=19*u*fs;text(body_font,14*u*fs,a.x+42*u,y,soft,it[1],wrap);y+=height(body_font,14*u*fs,it[1],wrap)+5*u*fs;}}
 centred(body_font,12.5f*u,cx,o.y+H-18*u,IM_COL32(160,178,200,255),"The Legend of Zelda is a trademark of Nintendo. LA-Online Recomp is a fan project, not affiliated with or endorsed by Nintendo.");
 ImGui::Dummy(ImVec2(W,H));
}

static void global_palm(ImDrawList *d,ImVec2 c,float r,ImU32 ink){
 d->AddTriangleFilled(ImVec2(c.x-r,c.y+r*.65f),ImVec2(c.x+r,c.y+r*.65f),ImVec2(c.x,c.y+r*.25f),ink);
 for(int tree=0;tree<2;tree++){float x=c.x+(tree? .25f:-.45f)*r,y=c.y-(tree?.65f:.1f)*r;ImVec2 top(x,y);d->AddBezierCubic(ImVec2(x-r*.12f,c.y+r*.5f),ImVec2(x+r*.12f,c.y),ImVec2(x+r*.22f,y),top,ink,r*.11f);
 for(int side=-1;side<=1;side+=2)for(int leaf=0;leaf<3;leaf++){float dx=side*r*(.45f+leaf*.05f),dy=r*(-.22f+leaf*.18f);d->AddBezierCubic(top,ImVec2(x+dx*.5f,y-r*.3f),ImVec2(x+dx,y+dy-r*.2f),ImVec2(x+dx,y+dy+r*.2f),ink,r*.07f);}}
}
static bool g_global_rando_open;  // V14.1: the Randomizer door's seed panel is open (chevron up)
void tropical_global_rando_open(bool open){g_global_rando_open=open;}
int tropical_global(float scale,const char *count_text,bool enabled,const char *seed_text){
 (void)scale;auto *d=ImGui::GetWindowDrawList();ImVec2 p=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();float w=size.x,h=size.y,u=fminf(w/1500.f,h/720.f);float cx=p.x+w*.5f;
 const ImU32 cyan=IM_COL32(132,221,255,255),white=IM_COL32(245,252,255,255),gold=IM_COL32(255,210,91,255);
 auto centered=[&](ImFont *font,const char *text,float fs,float y,ImU32 color){float tw=font->CalcTextSizeA(fs,1e6f,0,text).x;if(tw>w-32*u){fs*=(w-32*u)/tw;tw=font->CalcTextSizeA(fs,1e6f,0,text).x;}d->AddText(font,fs,ImVec2(cx-tw*.5f,y),color,text);};
 auto line=[&](float x1,float x2,float y,ImU32 color){d->AddLine(ImVec2(cx+x1*w,p.y+y*h),ImVec2(cx+x2*w,p.y+y*h),color,1.4f*u);};
 global_palm(d,ImVec2(cx,p.y+h*.075f),27*u,cyan);line(-.20f,-.037f,.075f,IM_COL32(103,201,244,150));line(.037f,.20f,.075f,IM_COL32(103,201,244,150));
 centered(global_title_font,"Global Online",74*u,p.y+h*.115f,IM_COL32(35,104,140,140));centered(global_title_font,"Global Online",74*u,p.y+h*.112f,white);
 centered(global_text_font,"Roam Koholint with other players in a shared online world.",25*u,p.y+h*.222f,cyan);
 line(-.23f,-.035f,.293f,IM_COL32(135,214,241,145));line(.035f,.23f,.293f,IM_COL32(135,214,241,145));for(int i=-1;i<=1;i++){ImVec2 c(cx+i*23*u,p.y+h*.293f);float r=(i?5.f:7.f)*u;d->AddQuadFilled(ImVec2(c.x-r,c.y),ImVec2(c.x,c.y-r),ImVec2(c.x+r,c.y),ImVec2(c.x,c.y+r),gold);}
 // V12: two doors into Global - Vanilla (your own save) or Randomizer (your own uploaded seed). Nothing is shared in
 // either: players see each other and chat; PvP is opt-in in-game (R3 / F6).
 int result=0;
 auto door=[&](ImVec2 a,ImVec2 b,const char *id,const char *label,const char *sub,int icon)->bool{
  ImGui::SetCursorScreenPos(a);ImGui::BeginDisabled(!enabled);bool click=ImGui::InvisibleButton(id,ImVec2(b.x-a.x,b.y-a.y),ImGuiButtonFlags_EnableNav);ImGui::EndDisabled();bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused();
  if(!enabled&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Finish the game setup before entering Global Online.");
  for(int i=4;i>0;i--)d->AddRect(ImVec2(a.x-i*2*u,a.y-i*2*u),ImVec2(b.x+i*2*u,b.y+i*2*u),IM_COL32(255,196,68,(hot?100:65)/i),24*u+i*u,0,3*u);
  d->AddRectFilled(a,b,IM_COL32(233,168,34,255),24*u);d->AddRectFilledMultiColor(ImVec2(a.x+7*u,a.y+7*u),ImVec2(b.x-7*u,b.y-7*u),IM_COL32(255,237,139,255),IM_COL32(255,225,112,255),IM_COL32(235,169,40,255),IM_COL32(255,207,65,255));
  d->AddRect(a,b,IM_COL32(255,251,214,255),24*u,0,3*u);d->AddRect(ImVec2(a.x+6*u,a.y+6*u),ImVec2(b.x-6*u,b.y-6*u),IM_COL32(255,224,115,255),19*u,0,2*u);
  ImU32 ink=IM_COL32(72,42,10,255);float cy=(a.y+b.y)*.5f;tile_icon(d,ImVec2(a.x+45*u,cy),20*u,icon,ink);
  float fs=30*u,bx=a.x+80*u,available=b.x-bx-40*u;float tw=serif_font->CalcTextSizeA(fs,1e6f,0,label).x;if(tw>available){fs*=available/tw;}
  d->AddText(serif_font,fs,ImVec2(bx,cy-fs*.85f),ink,label);
  float ss=17*u,sw=global_text_font->CalcTextSizeA(ss,1e6f,0,sub).x;if(sw>available)ss*=available/sw;
  d->AddText(global_text_font,ss,ImVec2(bx,cy+fs*.25f),IM_COL32(95,58,14,255),sub);
  if(icon==2){float dy=g_global_rando_open?-1.f:1.f;  /* V14.1: a drop-down - chevron down, up while its panel is open */
   d->AddLine(ImVec2(b.x-38*u,cy-5*u*dy),ImVec2(b.x-29*u,cy+5*u*dy),ink,4*u);d->AddLine(ImVec2(b.x-29*u,cy+5*u*dy),ImVec2(b.x-20*u,cy-5*u*dy),ink,4*u);}
  else{d->AddLine(ImVec2(b.x-34*u,cy-10*u),ImVec2(b.x-25*u,cy),ink,4*u);d->AddLine(ImVec2(b.x-25*u,cy),ImVec2(b.x-34*u,cy+10*u),ink,4*u);}
  return click;
 };
 float gap2=w*.012f;
 if(door(ImVec2(cx-w*.33f,p.y+h*.335f),ImVec2(cx-gap2,p.y+h*.48f),"Join Global Vanilla","Join Global Vanilla","Your own save - see and chat with everyone",3))result=1;
 if(door(ImVec2(cx+gap2,p.y+h*.335f),ImVec2(cx+w*.33f,p.y+h*.48f),"Join Global Randomizer","Join Global Randomizer",seed_text&&*seed_text?seed_text:"Upload your randomizer seed",2))result=2;
 int count=-1;sscanf(count_text,"%d",&count);float count_y=p.y+h*.518f;float count_width=global_text_font->CalcTextSizeA(23*u,1e6f,0,count_text).x;d->AddCircleFilled(ImVec2(cx-count_width*.5f-22*u,count_y+13*u),6*u,count>=0?IM_COL32(0,238,80,255):gold);centered(global_text_font,count_text,23*u,count_y,cyan);
 float cw=w*.205f,gap=w*.016f,left=cx-(3*cw+2*gap)*.5f,top=p.y+h*.585f,ch=h*.154f;
 const char *headings[]={"Players Online","Public World","Free Roam"},*values[]={nullptr,"Global","Koholint"},*sub[]={"in Global","Open to all players","Explore together"};char number[16];if(count>=0)snprintf(number,sizeof number,"%d",count);else snprintf(number,sizeof number,"--");values[0]=number;
 for(int i=0;i<3;i++){float x=left+i*(cw+gap);ImVec2 ca(x,top),cb(x+cw,top+ch);d->AddRectFilled(ca,cb,IM_COL32(5,49,81,175),18*u);d->AddRect(ca,cb,IM_COL32(101,191,234,160),18*u,0,1.5f*u);ImVec2 icon(x+51*u,top+ch*.5f);
 if(i==2)global_palm(d,icon,28*u,gold);else tile_icon(d,icon,24*u,i==0?1:3,i==0?IM_COL32(104,255,214,255):IM_COL32(61,214,255,255));
 float tx=x+96*u;d->AddText(global_text_font,20*u,ImVec2(tx,top+14*u),cyan,headings[i]);d->AddText(serif_font,30*u,ImVec2(tx,top+39*u),white,values[i]);d->AddText(global_text_font,18*u,ImVec2(tx,top+75*u),cyan,sub[i]);}
 return result;
}

// V12: homepage "What's New?" - short, player-friendly notes for this release (opened from the homepage button and
// once automatically after an update).
void tropical_whats_new(float scale){
 const ImVec4 gold(1,.83f,.27f,1),green(.35f,1,.6f,1),blue(.55f,.85f,1,1),grey(.75f,.8f,.85f,1);
 ImGui::PushFont(serif_font);ImGui::SetWindowFontScale(.9f);ImGui::TextColored(gold,"What's New?  Beta 5.4  CO-OP V" LAO_MOD_VERSION);ImGui::SetWindowFontScale(1);ImGui::PopFont();
 ImGui::PushStyleColor(ImGuiCol_Text,grey);ImGui::TextWrapped("V" LAO_MOD_VERSION " is a test build - please report anything that doesn't work as described.");ImGui::PopStyleColor();ImGui::Separator();
 auto head=[&](const char *t,ImVec4 c){ImGui::Spacing();ImGui::PushFont(heading_font);ImGui::SetWindowFontScale(.6f);ImGui::TextColored(c,"%s",t);ImGui::SetWindowFontScale(1);ImGui::PopFont();};
 auto item=[&](const char *t){ImGui::Bullet();ImGui::SameLine();ImGui::PushTextWrapPos(0);ImGui::TextUnformatted(t);ImGui::PopTextWrapPos();};
 (void)scale;
 head("New in CO-OP V15.2",gold);
 item("Co-op Randomizer: fixed players waiting forever on \"Waiting for the host's seed\". The host's game now finds the room's seed and uploads it within 15 seconds, and the server keeps it even when it restarts.");
 item("SEND LOGS TO DEVELOPER uses your player name when you send from the start screen.");
 head("New in CO-OP V15.1",gold);
 item("Co-op Randomizer: chests, Pieces of Heart, seashell spots and items lying on the floor are now shared too - one a teammate already took is opened / gone for you when you enter that room (pots and grass already were).");
 item("Fixed: getting stuck in a room with a fight or puzzle (like Eagle's Tower's \"match the shapes\" room or Bottle Grotto) when a teammate finished it in their game at the same time. Now it waits until you leave the room, and the room is done when you come back.");
 item("Fixed: the team could get the Noble Sword (or Mirror Shield / Bracelet Lv2) without finding the second one.");
 item("PvP: the Boomerang can only hit again once it's back, and its reach is smaller. Hookshot, Bombs and the other items have a short wait too.");
 item("Co-op PvP: when a teammate is knocked out in PvP, the notice says who did it (it said \"Cause unknown\").");
 item("PvP: no more attacking while knocked out, no more being frozen after a Boomerang hit, the hearts on the HUD stay right, and a PvP KO plays Link's real death.");
 item("Settings > Report / Logs > SEND LOGS TO DEVELOPER: every log (world, rejoin, PvP, events, game logs...) goes in one zip straight to the developer's Discord. If the server can't be reached, you get the zip to drag into Discord. They're also kept in the folder \"LOGS FOR DEVELOPER\" next to the game.");
 item("The Play Co-Op card has a new picture - art by Velvet_Charm. Thank you, Velvet!");
 head("New in CO-OP V15",gold);
 item("PvP in Co-op and Co-op Randomizer too (R3 / F6 turns it on) - not only Global.");
 item("Getting hit in PvP knocks Link back from the attacker and turns him toward them.");
 item("Z-targeting on ZL (also L3 / F8): lock on to enemies, bosses or PvP players. Link faces the target, and left / right on the stick circles around it. ZL again lets go.");
 item("Global: a kill feed in the bottom-left corner for everyone, and your own KO card with a laughing Link (bottom right) - a different sarcastic old-tongue line every time (\"Lo! Velvet hath met their match...\") - and your Kills / Losses at the bottom centre.");
 item("Everyone online needs the newest version (older versions are told to UPDATE NOW).");
 head("New in CO-OP V14.1",gold);
 item("Everyone online must be on the same LA-Online version now. If you're on an older one, the game tells you to update - press UPDATE NOW. Offline play still works.");
 item("When a new version comes out, a small notice shows at the top of the screen for a few seconds, even while you play.");
 item("Join Global Randomizer: a drop-down with your seeds (it shows which one has your saves), Upload Seed and Use My Current Seed.");
 item("Global: when someone finishes the game, everyone in Global sees \"<name> woke the Wind Fish and saved Koholint!\" in the bottom right.");
 item("Global stays your own world: your own seed, your own save, nothing from anyone's story is shared.");
 head("New in CO-OP V14 - the biggest fix update yet",gold);
 item("Share the world (Co-op Randomizer): items your team already picked up, pots they opened and item bushes they cut are gone for you too when you enter that room - nothing gets collected twice. This fixes the duplicated Small Keys. (Settings > Co-Op > Share the world.)");
 item("Chat bubbles no longer sit on top of a teammate's name tag - they move up above it.");
 item("Connection drops fixed on the server side: if the server's save folder went missing, every save kicked a player. The server now keeps going (host: update server.py).");
 item("Global: when someone finishes the game, everyone in Global sees \"<name> woke the Wind Fish and saved Koholint!\" in the bottom right. Global stays your own world: no story or items are shared.");
 item("Join Global Randomizer: a list of every seed on your PC opens - pick your seed, load your seed file, or make a new one. Your choice is remembered. Global is your own world: nothing is synced.");
 item("Making a new randomizer seed no longer fails with \"Canonical seed ID mismatch\" after you had joined someone else's co-op game.");
 item("Big seeds work in co-op, and the seed reaches players automatically when they join.");
 item("PvP: items hit too - Bow, Bombs, Boomerang, Hookshot, Magic Rod and Magic Powder. A hit makes the player stagger, flashes the screen red and rumbles the controller.");
 item("PvP: you SEE other players' attacks now - bombs with a fuse and a blast, flying arrows, a spinning boomerang, the hookshot chain, Magic Rod fireballs and Magic Powder clouds.");
 item("New laonline-world-log.txt in the game folder: who picked up what, which pots were opened and which item bushes were cut - you and your team.");
 item("D-pad + Ocarina: the Ocarina on the D-pad plays a song you know (no more \"you don't know any songs\"), and you can put a specific song on the D-pad (Settings > Controls > D-pad item shortcuts).");
 item("Randomizer: songs found as items are selected automatically, so the Ocarina works as soon as you have it and a song.");
 item("Randomizer tracker: the Face Shrine instrument, Facade, its pots and the Rapids island chest can finally show as reachable (fixed for seeds you already have too).");
 item("PvP can kill now: the hit that takes your last heart plays Link's death and the Game Over screen (a bottled fairy still saves you). It counts as a kill for the other player.");
 item("PvP: EVERY weapon hurts - Sword (Noble Sword x2), Spin attack, Bombs, Bow, Bomb arrows (Bow + Bombs together), Boomerang, Hookshot, Magic Rod, Magic Powder, Pegasus Boots dash and a Roc's Feather stomp. Everyone sees every attack on their screen.");
 item("PvP hit markers: land a hit and an X hit marker + the damage pop up over that player; everyone sees the hit burst.");
 item("PvP lock-on (like Z-targeting): L3 (click the left stick) or F8 near a PvP player - your attacks aim at them. Press again to let go.");
 item("Kill feed + kill counter (bottom right): a funny line and the score, like \"Sanic 1 - Velvet 0\". Everyone in Global sees it; the server keeps the totals.");
 item("Tracker: instruments tick off when you get them, and checks the game can't detect (Marin, Tarin, Kiki, seashell rewards...) can be marked with Y \"Mark as done\".");
 item("Report a Bug: attach a screenshot or a video.");
 item("If you came here with UPDATE NOW: it worked! Your name, lobby and saves are the same.");
 head("New in CO-OP V13",green);
 item("Join an adventure that's already running: if a co-op room has a free slot, Join stays open even after the host has started playing.");
 item("Co-op Randomizer: if you don't have the host's seed, the game downloads it from the room for you and checks it. If you already have the exact seed, your copy is used.");
 item("Leaving frees your slot for someone else. If your game closes, you can rejoin and get your own slot back within 2 minutes.");
 item("Shared story progress and items are sent to you when you arrive, like before.");
 item("Fixed: Power Bracelet Lv2, Mirror Shield and Noble Sword - if your team finds both copies, everyone gets the upgrade (before, everyone could stay at Lv1).");
 item("New in Settings > Co-Op: choose what you receive from your team - story progress, items, upgrades, small keys, supplies and boss souls.");
 item("Fixed: Small Keys a teammate picks up now reach you too, so keys from shared chests and drops are no longer lost.");
 item("Boss souls and mini-boss souls are shared in Co-op Randomizer.");
 item("Everyone in a room needs V13 (the server must be updated too).");
 head("New in CO-OP V12.5",green);
 item("Randomizer: with \"Needs Bomb Bag\" / \"Needs Magic Powder Bag\" off, the first Bombs or Magic Powder you find or buy now unlock them (they used to turn into blue rupees).");
 item("Report A Bug works again - also from the start screen. If a report can't be sent it is saved, and \"Send saved reports\" tries again later.");
 item("Settings with a controller: Up / Down move through the categories, A or Right goes into a page, B comes back to the list. L1 / R1 still switch categories.");
 item("Settings look cleaner: each category has a short description, and switching categories reacts immediately.");
 item("Lobby: Back, Ready Up and Start no longer flicker. You see when a player connects or leaves, and the host's Start Adventure lights up once everyone is ready.");
 item("New \"Play Randomizer\" card on the home screen for solo randomizer games.");
 item("Final boss: other players stay visible in the Wind Fish's Egg but can't touch or block Dethl.");
 item("New Credits page and new Leaderboards: sort by time, score, rupees or deaths, filter by mode and player count, 50 runs per page.");
 item("Settings got a new look: an icon on every option, switches, dropdowns and sliders, and big Reset to Default / Cancel / Save Settings buttons. Report a Bug has the new two-column layout.");
 item("Graphics crash (\"device lost\"): the game now explains it in plain words, saves a crash note, and Report / Logs offers to send it next time.");
 item("Update Now: when a new LA-Online version is out, a window shows its patch notes. Press UPDATE NOW, watch the bar go to 100%, and the game reopens updated - same name, lobby and saves.");
 item("Name tags: your teammates' name and hearts now sit right above their head and stay on screen instead of blinking, floating two heads too high.");
 item("Pause fix: items from your team wait until the pause / save menu has fully closed, a few at a time, instead of all at once.");
 item("Full wallet: rupees from your team no longer cause repeated retries in the log when you already have 9999.");
 item("New Play Co-Op screen, and a bigger Back button that is no longer covered on Global, Leaderboards, Credits and Browse Lobbies.");
 item("Co-Op settings now wait for Save Settings like every other tab (Cancel undoes them), and the Co-Op page describes what V12 really shares.");
 item("Fixed: closing or cancelling Settings on the start screen crashed the game after you had played Global Online.");
 item("Report A Bug now goes through the co-op server you already connect to, and tells you why if a report can't be sent.");
 head("From LAS Recompiled Beta 5.4 (original developer)",blue);
 item("Smoother visuals: grass and high-resolution shader fixes, BowWow and screen-transition graphics, missing letters and Chinese text.");
 item("Controllers: the game follows the controller you are actually using.");
 item("Randomizer: picked-up items show right away, Armos stairs, peach tunic, dungeon ledge / shutter / pot fixes and a better tracker.");
 item("Your wallet can now hold up to 9,999 rupees.");
 head("Fixed in CO-OP V12",blue);
 item("Bomb Arrows work again with the Bow and/or Bombs on the D-pad - press both together.");
 item("Rupees your teammates pick up now stay in your wallet (they used to vanish a moment later).");
 item("Final boss: other players stay visible in the Wind Fish's Egg, but they can no longer touch or block Dethl - their colliders and sword hitboxes are switched off there.");
 item("Catfish's Maw: if diving in doesn't work, hold R3 (or F7) for 2 seconds in Martha's Bay.");
 head("Added in CO-OP V12",blue);
 item("Shared story: opened dungeons, locked doors, boss doors, cracked walls, hidden stairs, mini-boss and field boss wins, the Walrus and more now sync to everyone in your room - even players who join later.");
 item("Shared items: Bombs, Magic Powder, Bow, bottles, bag upgrades, Golden Leaves and the Seashell Sensor now sync too.");
 item("Shared supplies: rupees, bombs, arrows and Magic Powder anyone picks up count for the whole team. Recovery Hearts heal everyone in the room.");
 item("Global Online: Join Global Vanilla or Join Global Randomizer (just upload your seed). Nothing is shared in Global - you keep your own save, see each other and chat.");
 item("PvP in Global: press R3 or F6. PvP players glow red with a red name tag. PvP can't knock anyone out.");
 head("Improved",blue);
 item("Credits: Locus added to the testers.");
 item("Installer: no extra launch scripts - start the game the normal way.");
}
