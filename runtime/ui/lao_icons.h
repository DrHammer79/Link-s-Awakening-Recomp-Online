#pragma once
/* V12.5: small vector icons drawn with ImGui's draw list (no extra image files needed). Used by the Credits,
 * Leaderboards and Settings pages so they match the reference designs. Header-only: nothing to add to the Makefile. */
#include "imgui.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <ctype.h>

extern bool laonline_avatar_icon(const uint8_t *,const uint8_t *,ImTextureID *,float *);
namespace lao_icon {
static inline ImVec2 v(float x,float y){return ImVec2(x,y);}
static inline ImU32 alpha(ImU32 c,int a){return (c&0x00ffffffu)|((ImU32)a<<24);}
static inline void triforce(ImDrawList *d,ImVec2 c,float r,ImU32 ink){
 float h=r*.866f;ImVec2 top(c.x,c.y-r),l(c.x-r,c.y+h*.85f),rr(c.x+r,c.y+h*.85f);
 ImVec2 ml((top.x+l.x)*.5f,(top.y+l.y)*.5f),mr((top.x+rr.x)*.5f,(top.y+rr.y)*.5f),mb((l.x+rr.x)*.5f,l.y);
 d->AddTriangleFilled(top,ml,mr,ink);d->AddTriangleFilled(ml,l,mb,ink);d->AddTriangleFilled(mr,mb,rr,ink);
}
static inline void person(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddCircleFilled(v(c.x,c.y-r*.45f),r*.38f,ink);d->PathArcTo(v(c.x,c.y+r*.75f),r*.72f,3.14159f,6.28318f,16);d->PathFillConvex(ink);}
static inline void people(ImDrawList *d,ImVec2 c,float r,int n,ImU32 ink){
 if(n<=1){person(d,c,r,ink);return;}float step=r*.85f,x0=c.x-step*(n-1)*.5f;for(int i=0;i<n;i++)person(d,v(x0+i*step,c.y),r*.8f,ink);
}
static inline void clock(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddCircle(c,r*.78f,ink,24,r*.18f);d->AddLine(c,v(c.x,c.y-r*.5f),ink,r*.15f);d->AddLine(c,v(c.x+r*.35f,c.y+r*.1f),ink,r*.15f);d->AddRectFilled(v(c.x-r*.2f,c.y-r*1.08f),v(c.x+r*.2f,c.y-r*.86f),ink);}
static inline void trophy(ImDrawList *d,ImVec2 c,float r,ImU32 ink){
 d->PathArcTo(v(c.x,c.y-r*.55f),r*.62f,0,3.14159f,16);d->PathFillConvex(ink);d->AddRectFilled(v(c.x-r*.62f,c.y-r*.75f),v(c.x+r*.62f,c.y-r*.5f),ink);
 d->AddCircle(v(c.x-r*.62f,c.y-r*.4f),r*.25f,ink,12,r*.12f);d->AddCircle(v(c.x+r*.62f,c.y-r*.4f),r*.25f,ink,12,r*.12f);
 d->AddRectFilled(v(c.x-r*.1f,c.y),v(c.x+r*.1f,c.y+r*.45f),ink);d->AddRectFilled(v(c.x-r*.42f,c.y+r*.45f),v(c.x+r*.42f,c.y+r*.68f),ink,r*.06f);
}
static inline void rupee(ImDrawList *d,ImVec2 c,float r,ImU32 ink){
 ImVec2 p[6]={v(c.x,c.y-r),v(c.x+r*.55f,c.y-r*.55f),v(c.x+r*.55f,c.y+r*.55f),v(c.x,c.y+r),v(c.x-r*.55f,c.y+r*.55f),v(c.x-r*.55f,c.y-r*.55f)};
 d->AddConvexPolyFilled(p,6,ink);d->AddRectFilled(v(c.x-r*.2f,c.y-r*.45f),v(c.x+r*.2f,c.y+r*.45f),IM_COL32(190,255,190,150));
}
static inline void skull(ImDrawList *d,ImVec2 c,float r,ImU32 ink){
 d->AddCircleFilled(v(c.x,c.y-r*.12f),r*.72f,ink);d->AddRectFilled(v(c.x-r*.42f,c.y+r*.2f),v(c.x+r*.42f,c.y+r*.78f),ink,r*.12f);
 ImU32 hole=IM_COL32(8,22,40,255);d->AddCircleFilled(v(c.x-r*.28f,c.y-r*.05f),r*.19f,hole);d->AddCircleFilled(v(c.x+r*.28f,c.y-r*.05f),r*.19f,hole);
 d->AddTriangleFilled(v(c.x,c.y+r*.15f),v(c.x-r*.1f,c.y+r*.35f),v(c.x+r*.1f,c.y+r*.35f),hole);
 for(int i=-1;i<=1;i++)d->AddLine(v(c.x+i*r*.18f,c.y+r*.5f),v(c.x+i*r*.18f,c.y+r*.78f),hole,r*.07f);
}
static inline void sword(ImDrawList *d,ImVec2 c,float r,ImU32 ink){
 d->AddLine(v(c.x-r*.15f,c.y+r*.15f),v(c.x+r*.8f,c.y-r*.8f),ink,r*.28f);d->AddTriangleFilled(v(c.x+r*.66f,c.y-r*.95f),v(c.x+r*.95f,c.y-r*.95f),v(c.x+r*.95f,c.y-r*.66f),ink);
 d->AddLine(v(c.x-r*.55f,c.y-r*.05f),v(c.x+r*.05f,c.y+r*.55f),ink,r*.2f);d->AddLine(v(c.x-r*.25f,c.y+r*.25f),v(c.x-r*.75f,c.y+r*.75f),ink,r*.2f);
}
static inline void die(ImDrawList *d,ImVec2 c,float r,ImU32 ink){
 d->AddRectFilled(v(c.x-r*.8f,c.y-r*.8f),v(c.x+r*.8f,c.y+r*.8f),ink,r*.25f);ImU32 dot=IM_COL32(10,30,55,255);
 for(int i=-1;i<=1;i+=2)for(int j=-1;j<=1;j+=2)d->AddCircleFilled(v(c.x+i*r*.4f,c.y+j*r*.4f),r*.13f,dot);d->AddCircleFilled(c,r*.13f,dot);
}
static inline void grid(ImDrawList *d,ImVec2 c,float r,ImU32 ink){for(int i=0;i<2;i++)for(int j=0;j<2;j++){float x=c.x-r*.8f+i*r*.9f,y=c.y-r*.8f+j*r*.9f;d->AddRect(v(x,y),v(x+r*.7f,y+r*.7f),ink,r*.12f,0,r*.14f);}}
static inline void globe(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddCircle(c,r,ink,24,r*.13f);d->AddEllipse(c,v(r*.45f,r),ink,0,20,r*.11f);d->AddLine(v(c.x-r,c.y),v(c.x+r,c.y),ink,r*.11f);d->AddLine(v(c.x-r*.85f,c.y-r*.45f),v(c.x+r*.85f,c.y-r*.45f),ink,r*.08f);d->AddLine(v(c.x-r*.85f,c.y+r*.45f),v(c.x+r*.85f,c.y+r*.45f),ink,r*.08f);}
static inline void chevron(ImDrawList *d,ImVec2 c,float r,int dir,ImU32 ink,float th){ // dir: 0 left 1 right 2 down 3 up
 ImVec2 a,b,e;if(dir==0){a=v(c.x+r*.4f,c.y-r);b=v(c.x-r*.4f,c.y);e=v(c.x+r*.4f,c.y+r);}else if(dir==1){a=v(c.x-r*.4f,c.y-r);b=v(c.x+r*.4f,c.y);e=v(c.x-r*.4f,c.y+r);}
 else if(dir==2){a=v(c.x-r,c.y-r*.4f);b=v(c.x,c.y+r*.4f);e=v(c.x+r,c.y-r*.4f);}else{a=v(c.x-r,c.y+r*.4f);b=v(c.x,c.y-r*.4f);e=v(c.x+r,c.y+r*.4f);}
 d->AddLine(a,b,ink,th);d->AddLine(b,e,ink,th);
}
static inline void arrow_left(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddLine(v(c.x-r*.7f,c.y),v(c.x+r*.7f,c.y),ink,r*.22f);chevron(d,v(c.x-r*.35f,c.y),r*.6f,0,ink,r*.22f);}
static inline void heart(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddCircleFilled(v(c.x-r*.45f,c.y-r*.25f),r*.5f,ink);d->AddCircleFilled(v(c.x+r*.45f,c.y-r*.25f),r*.5f,ink);d->AddTriangleFilled(v(c.x-r*.93f,c.y-r*.1f),v(c.x+r*.93f,c.y-r*.1f),v(c.x,c.y+r*.9f),ink);}
static inline void wrench(ImDrawList *d,ImVec2 c,float r,ImU32 ink){d->AddLine(v(c.x-r*.7f,c.y+r*.7f),v(c.x+r*.3f,c.y-r*.3f),ink,r*.32f);d->AddCircle(v(c.x+r*.45f,c.y-r*.45f),r*.4f,ink,16,r*.22f);}
static inline void gamepad(ImDrawList *d,ImVec2 c,float r,ImU32 ink,ImU32 bg){
 d->AddRectFilled(v(c.x-r,c.y-r*.5f),v(c.x+r,c.y+r*.45f),ink,r*.45f);d->AddCircleFilled(v(c.x-r*.65f,c.y+r*.4f),r*.38f,ink);d->AddCircleFilled(v(c.x+r*.65f,c.y+r*.4f),r*.38f,ink);
 d->AddRectFilled(v(c.x-r*.66f,c.y-r*.08f),v(c.x-r*.26f,c.y+r*.06f),bg);d->AddRectFilled(v(c.x-r*.53f,c.y-r*.22f),v(c.x-r*.39f,c.y+r*.2f),bg);
 d->AddCircleFilled(v(c.x+r*.38f,c.y-r*.08f),r*.1f,bg);d->AddCircleFilled(v(c.x+r*.6f,c.y+r*.1f),r*.1f,bg);
}
/* "B" face-button glyph used by the "B Back" hints */
static inline void face_button(ImDrawList *d,ImFont *font,ImVec2 c,float r,const char *letter){
 d->AddCircleFilled(c,r,IM_COL32(245,245,245,255));ImVec2 ts=font->CalcTextSizeA(r*1.3f,1e6f,0,letter);d->AddText(font,r*1.3f,v(c.x-ts.x*.5f,c.y-ts.y*.5f),IM_COL32(15,25,40,255),letter);
}
/* stable tunic colour per player name, so the same player always gets the same avatar colour */
static inline void name_colour(const std::string &name,uint8_t rgb[3]){
 // the Link avatar caps (laonline-avatars/cap-NN): green, blue, red, purple, pink, teal, orange, yellow
 static const uint8_t palette[][3]={{0x3f,0xa3,0x4d},{0x2f,0x6f,0xd6},{0xd6,0x3a,0x3a},{0x8a,0x4f,0xd8},{0xe8,0x6a,0xa8},{0x22,0xa7,0xa0},{0xf0,0x8a,0x24},{0xe8,0xb8,0x30}};
 unsigned h=2166136261u;for(unsigned char ch:name)h=(h^ch)*16777619u;const uint8_t *p=palette[h%8];rgb[0]=p[0];rgb[1]=p[1];rgb[2]=p[2];
}
/* round Link avatar (the same renderer the lobby uses), tinted per player name; falls back to an initial */
static inline void avatar(ImDrawList *d,ImFont *font,ImVec2 c,float r,const std::string &name,ImU32 ring){
 uint8_t rgb[3];name_colour(name,rgb);static const uint8_t hair[3]={255,223,87};ImTextureID tex=0;float aspect=1;
 d->AddCircleFilled(c,r+2,ring);d->AddCircleFilled(c,r,IM_COL32(110,200,240,255));
 if(laonline_avatar_icon(rgb,hair,&tex,&aspect))d->AddImageRounded(tex,v(c.x-r,c.y-r),v(c.x+r,c.y+r),v(0,0),v(1,1),IM_COL32_WHITE,r);
 else{d->AddCircleFilled(c,r,IM_COL32(rgb[0],rgb[1],rgb[2],255));char s[2]={name.empty()?'?':(char)toupper((unsigned char)name[0]),0};ImVec2 ts=font->CalcTextSizeA(r*1.1f,1e6f,0,s);d->AddText(font,r*1.1f,v(c.x-ts.x*.5f,c.y-ts.y*.5f),IM_COL32_WHITE,s);}
}
static inline std::string thousands(unsigned long long n){char raw[32];snprintf(raw,sizeof raw,"%llu",n);std::string s=raw,out;int k=0;for(int i=(int)s.size()-1;i>=0;i--){out.insert(out.begin(),s[i]);if(++k%3==0&&i)out.insert(out.begin(),',');}return out;}
}
