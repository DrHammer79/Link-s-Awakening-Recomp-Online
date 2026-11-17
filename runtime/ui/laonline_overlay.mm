#include "laonline_map_art.h"
#include "../hle/laonline_inventory_view.h"
#include "../hle/laonline_item_rules.h"
// LA-Online overlay (drawn by menu.mm): "X joined" / "X left" toasts for 5 s at the top centre, and a small player
// list in the top-left corner - each other player's name in their tunic colour, "nearby" when in our area.
// The data comes from runtime/hle/laonline.c (laonline_toast / laonline_players).
#include <float.h>
#include <stdint.h>
#include <stdio.h>

#include "imgui.h"
#include "laonline_item_icons.h"
#include "laonline_map_state.h"
#include "../hle/laonline_map_coords.h"
#include "laonline_ui_policy.h"
#include "../settings.h"
#include "menu.h"
#include "laonline_chat_ui.h"
#include "../hle/laonline_chat.h"
#include "../hle/laonline_diag.h"
#include "../hle/laonline_session.h"
#include "../hle/laonline_dungeon_map.h"
#include "../hle/laonline_world_map.h"
extern "C" int laonline_v12_pvp_peer(int);  // V12 (runtime/hle/laonline_v12.c)
extern "C" int laonline_v12_pvp_local(void);
extern "C" int laonline_v12_global_is_randomizer(void);

extern "C" {
int laonline_toast_with_item(int i,char *out,int cap,uint64_t *age_ms,int *item);
int laonline_players(int i, char *name, int cap, char *location, int location_cap, uint8_t rgb[3]);
int laonline_remote_tag_at(int id,char *name, int cap, float pos[3], uint8_t rgb[3], int *health, int *max_health);
bool widescreen_world_to_ndc(float x, float y, float z, float *nx, float *ny);
int game_in_play(void);
bool laonline_map_transform(float plane[3],float matrix[12]);
bool laonline_map_shown(void);
bool laonline_ui_paused(void);
int laonline_death_active(void),laonline_level_ready(void);
bool widescreen_frame_boxed(uint64_t frame);
uint64_t renderer_exec_frame(void);
extern bool g_room_wide;
}

bool laonline_avatar_icon(const uint8_t[3],const uint8_t[3],ImTextureID *,float *);

static void heart_path(ImDrawList *dl, ImVec2 c, float r) {
    dl->PathClear();
    dl->PathLineTo(ImVec2(c.x, c.y + r * .95f));
    dl->PathBezierCubicCurveTo(ImVec2(c.x - r * 1.45f, c.y), ImVec2(c.x - r * 1.05f, c.y - r), ImVec2(c.x, c.y - r * .28f));
    dl->PathBezierCubicCurveTo(ImVec2(c.x + r * 1.05f, c.y - r), ImVec2(c.x + r * 1.45f, c.y), ImVec2(c.x, c.y + r * .95f));
}

static void draw_heart(ImDrawList *dl, ImVec2 c, float r, float fill, float alpha) {
    ImU32 empty = IM_COL32(65, 42, 40, (int)(220 * alpha));
    ImU32 red = IM_COL32(255, 53, 62, (int)(255 * alpha));
    heart_path(dl, c, r); dl->PathFillConvex(empty);
    if (fill > 0) {
        dl->PushClipRect(ImVec2(c.x - r * 1.3f, c.y - r), ImVec2(c.x - r * 1.3f + r * 2.6f * fill, c.y + r), true);
        heart_path(dl, c, r); dl->PathFillConvex(red);
        dl->PopClipRect();
    }
    heart_path(dl, c, r); dl->PathStroke(IM_COL32(40, 20, 20, (int)(255 * alpha)), ImDrawFlags_Closed, 1.5f);
}

// The verified vanilla map parchment is a 16x16 room grid in a 1280x720
// interface. The map-page reader supplies its actual zoom/scroll, rather than
// inventing a transform. A location hash resolves to one Field cell or no pin.
static bool draw_map_players(ImDrawList *dl, ImVec2 display, ImFont *font) {
    if(!laonline_map_shown()) return false;
    if(!g_settings.laonline_indicators || !g_settings.laonline_markers)return true;
    float plane[3],matrix[12];
    if(!laonline_map_transform(plane,matrix)) return true;
    int n = laonline_map_player(-1, NULL);
    if (n <= 0) return true;
    float bw = fminf(display.x, display.y * (16.0f / 9.0f));
    float bh = bw * (9.0f / 16.0f);
    float ox = (display.x - bw) * .5f, oy = (display.y - bh) * .5f, u = bw / 1280.f;
    float r = fmaxf(5.0f, 6.5f * u);
    float fs = fmaxf(13.f, 16.f * u);
    ImVec2 minp(ox + 64 * u, oy + 116 * u);
    ImVec2 maxp(ox + 1216 * u, oy + 636 * u);
    ImVec4 placed[LAO_PLAYERS];int placed_count=0;
    dl->PushClipRect(minp, maxp, true);
    for (int i = 0; i < n; ++i) {
        LaoMapOverlayPlayer p = {};
        if (laonline_map_player(i, &p) < 0) continue;
        if(p.local || !p.position_valid) continue;
        float lx,ly;
        if(!lao_map_native_point(p.world,plane,matrix,&lx,&ly)) continue;
        float sx=ox+(640.f+lx)*u,sy=oy+(360.f-ly)*u;
        if (sx < minp.x + r || sy < minp.y + r || sx > maxp.x - r || sy > maxp.y - r) continue;
        ImU32 colour = IM_COL32(p.rgb[0], p.rgb[1], p.rgb[2], 255);
        dl->AddCircleFilled(ImVec2(sx, sy), r + 2 * u, IM_COL32(17, 20, 22, 240));
        dl->AddCircleFilled(ImVec2(sx, sy), r, colour);
        if(p.interior)dl->AddCircle(ImVec2(sx,sy),r+4*u,colour,0,1.5f*u);
        if(g_settings.laonline_username) {
            char label[96];
            snprintf(label,sizeof label,p.interior?"%s\n%s (inside)":"%s",p.name,p.location);
            ImVec2 sz=font->CalcTextSizeA(fs,FLT_MAX,0,label);
            float tx=fmaxf(minp.x+4*u,fminf(sx-sz.x*.5f,maxp.x-sz.x-4*u));
            float ty=sy-r-sz.y-8*u;
            for(int attempt=0;attempt<LAO_PLAYERS;attempt++) {
                bool overlap=false;
                for(int j=0;j<placed_count;j++)if(tx<placed[j].z && tx+sz.x>placed[j].x && ty<placed[j].w && ty+sz.y>placed[j].y)overlap=true;
                if(!overlap)break;
                ty-=sz.y+6*u;
            }
            if(ty<minp.y+4*u)ty=sy+r+8*u;
            ty=fminf(ty,maxp.y-sz.y-4*u);
            if(placed_count<LAO_PLAYERS)placed[placed_count++]=ImVec4(tx-4*u,ty-3*u,tx+sz.x+4*u,ty+sz.y+3*u);
            dl->AddLine(ImVec2(sx,sy),ImVec2(fmaxf(tx,fminf(sx,tx+sz.x)),ty+sz.y),colour,1.3f*u);
            dl->AddRectFilled(ImVec2(tx-4*u,ty-3*u),ImVec2(tx+sz.x+4*u,ty+sz.y+3*u),IM_COL32(18,20,22,230),4*u);
            dl->AddText(font,fs,ImVec2(tx,ty),IM_COL32(255,255,255,255),label);
        }
    }
    dl->PopClipRect();
    return true;
}



extern "C" bool chaos_dgnmap_shown(void);
extern "C" bool chaos_dgnmap_view(float v[8]);
extern "C" const char *chaos_current_level(void);
static bool draw_dungeon_players(ImDrawList *dl,ImVec2 display,ImFont *font,float scale) {
    if(!chaos_dgnmap_shown())return false;
    if(!g_settings.laonline_markers||!g_settings.laonline_indicators)return true;
    float v[8];if(!chaos_dgnmap_view(v))return true;
    const char *level=chaos_current_level();if(!level)return true;
    float bw=fminf(display.x,display.y*16.f/9.f),u=bw/1280.f;
    float ox=(display.x-bw)*.5f,oy=(display.y-bw*9.f/16.f)*.5f;
    unsigned status_row=0;
    int n=laonline_map_player(-1,NULL);
    for(int i=0;i<n;i++){
        LaoMapOverlayPlayer p={};if(laonline_map_player(i,&p)<0||p.local)continue;
        const char *status=p.fresh?"Elsewhere":"Changing rooms";
        bool drawn=false;
        if(p.fresh && p.map==lao_name_id(level))for(const auto &cell:lao_dungeon_cells){
            if(strcmp(level,cell.level)||p.room!=lao_room_id(cell.level,cell.room))continue;
            status="Another floor";
            if(fabsf(cell.sx-v[6])>.5f || fabsf(cell.sy-v[7])>.5f)break;
            float lx=-326.f+(cell.col+.5f)*81.5f,ly=259.f-(cell.row-.5f)*64.75f;
            float x=v[0]+v[2]*(v[3]+v[5]*(v[6]+lx));
            float y=v[1]+v[2]*(v[4]+v[5]*(v[7]+ly));
            ImVec2 pos(ox+(640+x)*u,oy+(360-y)*u);
            if(pos.x<ox+64*u||pos.x>ox+1216*u||pos.y<oy+116*u||pos.y>oy+636*u){status="Outside map view";break;}
            dl->AddCircleFilled(pos,8*u,IM_COL32(25,20,19,255));
            dl->AddCircleFilled(pos,5.5f*u,IM_COL32(p.rgb[0],p.rgb[1],p.rgb[2],255));
            if(g_settings.laonline_username){
                char text[48];snprintf(text,sizeof text,"P%u - %s",p.id+1,p.name);
                float fs=16*u;ImVec2 sz=font->CalcTextSizeA(fs,FLT_MAX,0,text);
                ImVec2 a(fmaxf(ox+64*u,fminf(pos.x-sz.x*.5f,ox+1216*u-sz.x)),pos.y-26*u);
                dl->AddLine(pos,ImVec2(a.x+sz.x*.5f,a.y+sz.y),IM_COL32(255,255,255,220),u);
                dl->AddRectFilled(ImVec2(a.x-4*u,a.y-3*u),ImVec2(a.x+sz.x+4*u,a.y+sz.y+3*u),IM_COL32(25,20,19,230),4*u);
                dl->AddText(font,fs,a,IM_COL32(255,255,255,255),text);
            }
            drawn=true;break;
        }
        if(!drawn){char text[96];snprintf(text,sizeof text,"%s: %s (%s)",p.name,p.location,status);
            dl->AddText(font,17*u,ImVec2(ox+70*u,oy+(132+23*status_row++)*u),IM_COL32(255,255,255,255),text);}
    }
    return true;
}

static bool chat_nearby(int id,const char *message){
 if(!g_settings.laonline_bubbles || g_settings.laonline_always_feed)return false;
 char text[LAO_CHAT_BYTES];float p[3],nx,ny;int dots;uint64_t age;
 if(!laonline_chat_bubble(id,text,p,&dots,&age)||dots||strcmp(text,message))return false;
 if(age>=(uint64_t)g_settings.laonline_chat_seconds*1000)return false;
 if(id!=laonline_chat_self()&&!laonline_remote_draw_seen(id,renderer_exec_frame()))return false;
 return widescreen_world_to_ndc(p[0],p[1],p[2],&nx,&ny)&&nx>=-1&&nx<=1&&ny>=-1&&ny<=1;
}

static ImVec2 g_plate_a[LAO_PLAYERS],g_plate_b[LAO_PLAYERS];static int g_plate_n;  /* plates drawn this frame */
/* Moves the bubble box [a,b] (+ its tip, `tip` px below b) up until it no longer touches any name plate. */
static void bubble_clear_plates(ImVec2 &a,ImVec2 &b,float tip,float pad){
    for(int pass=0;pass<LAO_PLAYERS;pass++){bool moved=false;
        for(int i=0;i<g_plate_n;i++){
            if(b.x<g_plate_a[i].x||a.x>g_plate_b[i].x||b.y+tip<g_plate_a[i].y-pad||a.y>g_plate_b[i].y)continue;
            float up=(b.y+tip)-(g_plate_a[i].y-pad);a.y-=up;b.y-=up;moved=true;
        }
        if(!moved)break;
    }
}
static void draw_chat(ImDrawList *dl,ImVec2 display,ImFont *font,float scale) {
    static char input[LAO_CHAT_BYTES]={};static bool was_open;static uint64_t generation;
    bool open=laonline_chat_input_open();
    if(open && (!laonline_chat_available() || !g_settings.laonline_chat)){laonline_chat_input_close();open=false;}
    bool fresh=open && generation!=laonline_chat_input_generation();
    if(fresh)generation=laonline_chat_input_generation();
    if(open){
        if(fresh || !was_open)input[0]=0;
        float width=fminf(display.x-32*scale,620*scale);
        ImGui::SetNextWindowPos(ImVec2((display.x-width)*.5f,display.y-90*scale));
        ImGui::SetNextWindowSize(ImVec2(width,65*scale));
        ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.17f,.13f,.09f,.96f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12*scale,10*scale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(10*scale,6*scale));
        ImGui::PushFont(font);
        ImGui::Begin("##lao_chat_input",NULL,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNavInputs|ImGuiWindowFlags_NoNavFocus);
        ImGui::SetWindowFontScale(22.f/font->FontSize);
        if(fresh || !was_open || ImGui::IsWindowAppearing()){input[0]=0;ImGui::SetKeyboardFocusHere();}
        ImGui::SetNextItemWidth(-1);
        bool send=ImGui::InputTextWithHint("##message","Type Here....",input,sizeof input,ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CallbackCharFilter|ImGuiInputTextFlags_CallbackEdit,
            [](ImGuiInputTextCallbackData *d)->int {
                if(d->EventFlag==ImGuiInputTextFlags_CallbackCharFilter)return d->EventChar<32 || (d->EventChar>=127 && d->EventChar<=159);
                if(d->EventFlag==ImGuiInputTextFlags_CallbackEdit){
                    unsigned count=0;int keep=d->BufTextLen;
                    for(int i=0;i<d->BufTextLen;i++)if(((unsigned char)d->Buf[i]&192)!=128 && count++>=150){keep=i;break;}
                    if(keep<d->BufTextLen)d->DeleteChars(keep,d->BufTextLen-keep);
                }
                return 0;
            });
        laonline_chat_typing(g_settings.laonline_typing);
        if(send && laonline_chat_submit(input)){input[0]=0;laonline_chat_input_close();open=false;}
        ImGui::End();ImGui::PopFont();ImGui::PopStyleVar(2);ImGui::PopStyleColor();
    }else if(was_open){input[0]=0;laonline_chat_typing(0);}
    was_open=open;
    if(g_settings.laonline_chat && g_settings.laonline_feed){
        char lines[6][LAO_CHAT_BYTES+24];ImVec2 sizes[6];uint64_t ages[6];int n=0;
        float fs=20*scale,wrap=fminf(360*scale,display.x*.45f),total=0;
        for(int i=0;i<6;i++){char name[17],message[LAO_CHAT_BYTES];uint64_t age;
            if(!laonline_chat_feed(i,name,message,&age))continue;
            uint64_t lifetime=(uint64_t)(g_settings.laonline_feed_seconds>=3?g_settings.laonline_feed_seconds:10)*1000;
            if(age>=lifetime || !g_settings.laonline_feed || chat_nearby(laonline_chat_feed_sender(i),message))continue;
            snprintf(lines[n],sizeof lines[n],"%s: %s",name,message);ages[n]=age;
            sizes[n]=font->CalcTextSizeA(fs,FLT_MAX,wrap,lines[n]);total+=sizes[n].y+16*scale;n++;
        }
        int first=0;float available=fminf(display.y*.32f,display.y-200*scale);
        while(first<n && total>available){total-=sizes[first].y+16*scale;first++;}
        float y=display.y-30*scale-total-(open?84*scale:0);
        for(int i=first;i<n;i++){
            uint64_t lifetime=(uint64_t)(g_settings.laonline_feed_seconds>=3?g_settings.laonline_feed_seconds:10)*1000;
            float alpha=ages[i]>lifetime-1500?(lifetime-ages[i])/1500.f:1.f;
            ImVec2 a(16*scale,y),b(a.x+wrap+20*scale,y+sizes[i].y+12*scale);
            dl->AddRectFilled(a,b,IM_COL32(24,29,26,(int)(190*alpha)),6*scale);
            dl->AddText(font,fs,ImVec2(a.x+10*scale,a.y+6*scale),IM_COL32(255,246,222,(int)(255*alpha)),lines[i],NULL,wrap);
            y=b.y+4*scale;
        }
    }
    if(!g_settings.laonline_chat || !g_settings.laonline_bubbles)return;
    for(int id=0;id<LAO_PLAYERS;id++){
        char text[LAO_CHAT_BYTES];float pos[3],nx,ny;int dots;uint64_t age;
        if(!laonline_chat_bubble(id,text,pos,&dots,&age))continue;
        if(id!=laonline_chat_self()&&!laonline_remote_draw_seen(id,renderer_exec_frame()))continue;
        if(dots){if(!g_settings.laonline_typing)continue;int phase=(int)(ImGui::GetTime()*3)%3;snprintf(text,sizeof text,"%s",phase==0?".  ":phase==1?".. ":"...");}
        else if(g_settings.laonline_always_feed || age>=(uint64_t)g_settings.laonline_chat_seconds*1000)continue;
        if(!widescreen_world_to_ndc(pos[0],pos[1],pos[2],&nx,&ny))continue;
        float vw=display.x,vh=display.y;
        if(widescreen_frame_boxed(renderer_exec_frame())&&!g_room_wide){vw=fminf(display.x,display.y*16.f/9.f);vh=vw*9.f/16.f;}
        if(nx < -1 || nx > 1 || ny < -1 || ny > 1)continue;
        float clearance=18*scale;
        if(id!=laonline_chat_self() && g_settings.laonline_indicators && (g_settings.laonline_username||g_settings.laonline_hearts)){
            char name[17];float world[3];uint8_t rgb[3];int hp=0,max_hp=0;
            if(laonline_remote_tag_at(id,name,sizeof name,world,rgb,&hp,&max_hp)){
                int hearts=g_settings.laonline_hearts?(max_hp+3)/4:0;if(hearts>30)hearts=30;
                int rows=hearts?(hearts+9)/10:0;
                clearance=font->CalcTextSizeA(18*scale,FLT_MAX,0,name).y+(11+rows*13+20+5+8)*scale;
            }
        }
        float x=(display.x-vw)*.5f+(nx*.5f+.5f)*vw,y=(display.y-vh)*.5f+(.5f-ny*.5f)*vh-clearance;
        float fs=22*scale,wrap=fminf(320*scale,display.x*.4f);
        ImVec2 sz=font->CalcTextSizeA(fs,FLT_MAX,wrap,text);
        float alpha=dots?1.f:fminf(1.f,((float)g_settings.laonline_chat_seconds*1000-age)/600.f);
        ImVec2 a(fmaxf(4.f,fminf(x-sz.x*.5f-12*scale,display.x-sz.x-28*scale)),fmaxf(4.f,y-sz.y-31*scale));
        ImVec2 b(a.x+sz.x+24*scale,a.y+sz.y+20*scale);
        bubble_clear_plates(a,b,11*scale,4*scale);
        if(a.y<4.f){float d=4.f-a.y;a.y+=d;b.y+=d;}  /* (top of the screen: stay visible) */
        dl->AddRectFilled(a,b,IM_COL32(246,229,185,(int)(245*alpha)),9*scale);
        dl->AddRect(a,b,IM_COL32(76,49,28,(int)(255*alpha)),9*scale,0,2*scale);
        float tip=fmaxf(a.x+12*scale,fminf(x,b.x-12*scale));
        dl->AddTriangleFilled(ImVec2(tip-8*scale,b.y),ImVec2(tip+8*scale,b.y),ImVec2(tip,b.y+11*scale),IM_COL32(246,229,185,(int)(245*alpha)));
        dl->AddText(font,fs,ImVec2(a.x+12*scale,a.y+10*scale),IM_COL32(47,29,18,(int)(255*alpha)),text,NULL,wrap);
    }
}

extern "C" bool laonline_overlay_wanted(void) {
    if (laonline_players(-1, NULL, 0, NULL, 0, NULL) < 0) {laonline_chat_input_close();return false;}  // (not online)
    return true;
}

static void draw_world_plate(ImDrawList *dl,ImVec2 display,ImFont *font,float scale,int id) {
    // World-space plate for the active remote Link only. No local-player position is provided by the runtime.
    char name[17]; float world[3], nx, ny; uint8_t rgb[3]; int hp = 0, max_hp = 0;
    if (!laonline_remote_tag_at(id,name, sizeof name, world, rgb, &hp, &max_hp) ||
        !widescreen_world_to_ndc(world[0], world[1], world[2], &nx, &ny)) return;
    bool drew=laonline_remote_draw_seen(id,renderer_exec_frame());
    static int last_draw[LAO_PLAYERS]={};
    if(last_draw[id]!=(int)drew){last_draw[id]=(int)drew;laonline_diag(drew?"puppet.draw_seen":"puppet.draw_missing",0xffff,0xffff,0,renderer_exec_frame(),drew);}
    if(!drew)return;
    float vw=display.x,vh=display.y;
    if(widescreen_frame_boxed(renderer_exec_frame()) && !g_room_wide) {
        vw=fminf(display.x,display.y*(16.f/9.f));vh=vw*(9.f/16.f);
    }
    ImVec2 anchor((display.x-vw)*.5f+(nx*.5f+.5f)*vw,
                  (display.y-vh)*.5f+(.5f-ny*.5f)*vh);
    // Compact above-head name and the peer's OWN health in 1/4-heart units.
    // Render the actual maximum, not the old hard-coded three-heart HUD.
    float tag_fs = 18.f * scale, heart_r = 5.f * scale, gap = 13.f * scale;
    ImVec2 ts = font->CalcTextSizeA(tag_fs, FLT_MAX, 0, name);
    int count = g_settings.laonline_hearts && max_hp > 0 ? (max_hp + 3) / 4 : 0;
    if (count > 30) count = 30;  // bounded draw list for malformed health
    int per_row = count > 10 ? 10 : count;
    int rows = count ? (count + per_row - 1) / per_row : 0;
    float hearts_width = per_row ? per_row * gap + 8 * scale : 0;
    float width = fmaxf(ts.x + 16 * scale, hearts_width);
    float height = ts.y + 11 * scale + rows * 13 * scale;
    float top = anchor.y - height - 20 * scale;
    ImVec2 a(anchor.x - width * .5f - 4 * scale, top - 5 * scale);
    ImVec2 b(anchor.x + width * .5f + 4 * scale, top + height + 4 * scale);
    if (g_plate_n < LAO_PLAYERS) { g_plate_a[g_plate_n] = a; g_plate_b[g_plate_n] = b; g_plate_n++; }
    bool pvp = laonline_v12_pvp_peer(id) != 0;  // V12: PvP players get a red plate and outline
    dl->AddRectFilled(a, b, pvp ? IM_COL32(90, 10, 12, 225) : IM_COL32(25, 23, 24, 210), 6 * scale);
    dl->AddRect(a, b, pvp ? IM_COL32(255, 40, 40, 255) : IM_COL32(235, 235, 235, 245), 6 * scale, 0, (pvp ? 3.2f : 1.6f) * scale);
    if (pvp) dl->AddText(font, 13.f * scale, ImVec2(b.x - 30 * scale, a.y - 15 * scale), IM_COL32(255, 70, 70, 255), "PvP");
    if(g_settings.laonline_username) dl->AddText(font, tag_fs, ImVec2(anchor.x - ts.x * .5f, top),
                IM_COL32(255, 255, 255, 255), name);
    for (int row = 0; row < rows; ++row) {
        int shown = count - row * per_row; if (shown > per_row) shown = per_row;
        float start = anchor.x - (shown - 1) * gap * .5f;
        for (int c = 0; c < shown; ++c) {
            int i = row * per_row + c;
            float fill = (hp - i * 4) * .25f;
            if (fill < 0) fill = 0; if (fill > 1) fill = 1;
            draw_heart(dl, ImVec2(start + c * gap, top + ts.y + 9 * scale + row * 13 * scale),
                       heart_r, fill, 1.f);
        }
    }
}

// Calibrated north-up radar. Region colours come from verified Field.lvb
// zone metadata; room radar uses native transforms, never exterior anchors.
static float minimap_size(ImVec2 display,float scale){return fminf((g_settings.laonline_minimap_size?260.f:200.f)*scale,display.y*.28f);}
extern "C" int laonline_coop_dungeon_map(uint16_t map);
static int dungeon_cell(const LaoMapOverlayPlayer &p){
 for(unsigned i=0;i<sizeof lao_dungeon_cells/sizeof *lao_dungeon_cells;i++)if(p.map==lao_name_id(lao_dungeon_cells[i].level) && p.room==lao_room_id(lao_dungeon_cells[i].level,lao_dungeon_cells[i].room))return int(i);
 return -1;
}
static bool dungeon_minimap_available(const LaoMapOverlayPlayer &p){return g_settings.laonline_dungeon_minimap && laonline_coop_dungeon_map(p.map) && dungeon_cell(p)>=0;}
static void draw_dungeon_minimap(ImDrawList *dl,ImVec2 display,ImFont *font,float scale,const LaoMapOverlayPlayer &me,int n){
 if(!dungeon_minimap_available(me))return;int cell=dungeon_cell(me);const auto &floor=lao_dungeon_cells[cell];
 LaoMapArt art={};if(!laonline_dungeon_art(floor.level,floor.sx,floor.sy,&art))return;
 float mx,mz;if(!laonline_dungeon_position(floor.level,floor.sx,floor.sy,floor.room,me.raw[0],me.raw[2],&mx,&mz))return;
 float size=minimap_size(display,scale),k=size/36.f,margin=24*scale;ImVec2 a(display.x-margin-size,display.y*.84f-size),b(a.x+size,a.y+size),center(a.x+size*.5f,a.y+size*.5f);
 dl->AddRectFilled(a,b,IM_COL32(22,30,25,240),14*scale);dl->PushClipRect(ImVec2(a.x+4*scale,a.y+4*scale),ImVec2(b.x-4*scale,b.y-4*scale),true);
 dl->AddImage((ImTextureID)art.texture,ImVec2(center.x+(art.left-mx)*k,center.y+(art.top-mz)*k),ImVec2(center.x+(art.right-mx)*k,center.y+(art.bottom-mz)*k));
 if(g_settings.laonline_markers && g_settings.laonline_indicators)for(int i=0;i<n;i++){
  LaoMapOverlayPlayer p={};if(laonline_map_player(i,&p)<0 || p.local || !p.fresh || p.map!=me.map)continue;
  int other=dungeon_cell(p);if(other<0)continue;const auto &f=lao_dungeon_cells[other];if(fabsf(f.sx-floor.sx)>.01f || fabsf(f.sy-floor.sy)>.01f)continue;
  float px,pz;if(!laonline_dungeon_position(f.level,f.sx,f.sy,f.room,p.raw[0],p.raw[2],&px,&pz))continue;
  float x=center.x+(px-mx)*k,y=center.y+(pz-mz)*k;if(x<a.x+9*scale || x>b.x-9*scale || y<a.y+9*scale || y>b.y-9*scale)continue;
  dl->AddCircleFilled(ImVec2(x,y),6*scale,IM_COL32(255,247,211,255));dl->AddCircleFilled(ImVec2(x,y),4*scale,IM_COL32(p.rgb[0],p.rgb[1],p.rgb[2],255));
 }
 float dx=sinf(me.yaw),dy=cosf(me.yaw),r=9*scale;
 ImVec2 tip(center.x+dx*r,center.y+dy*r),l(center.x-dx*r*.65f-dy*r*.65f,center.y-dy*r*.65f+dx*r*.65f),rr(center.x-dx*r*.65f+dy*r*.65f,center.y-dy*r*.65f-dx*r*.65f);
 dl->AddTriangleFilled(tip,l,rr,IM_COL32(65,202,255,255));dl->AddTriangle(tip,l,rr,IM_COL32(255,255,238,255),2*scale);
 dl->PopClipRect();dl->AddRect(a,b,IM_COL32(225,214,165,240),14*scale,0,2*scale);dl->AddText(font,14*scale,ImVec2(a.x+8*scale,a.y+8*scale),IM_COL32(255,247,215,255),"N  Dungeon");
}
static void draw_minimap(ImDrawList *dl,ImVec2 display,ImFont *font,float scale){
 if(!g_settings.laonline_minimap)return;
 int n=laonline_map_player(-1,NULL);LaoMapOverlayPlayer me={};bool found=false;
 for(int i=0;i<n;i++){LaoMapOverlayPlayer p={};if(laonline_map_player(i,&p)>=0&&p.local&&p.fresh){me=p;found=true;break;}}
 if(!found)return;
 bool field=me.map==lao_name_id("Field");
 // Unsupported interiors must never reuse the exterior entrance as a room map.
 if(!field){draw_dungeon_minimap(dl,display,font,scale,me,n);return;}
 if(field&&(me.raw[0]<0||me.raw[0]>=240||me.raw[2]<0||me.raw[2]>=192))return;
 float size=minimap_size(display,scale),margin=24*scale,range=field?48.f:30.f,k=size/range;
 ImVec2 a(display.x-margin-size,display.y*.84f-size),b(a.x+size,a.y+size),center(a.x+size*.5f,a.y+size*.5f);
 dl->AddRectFilled(a,b,IM_COL32(22,38,33,228),14*scale);
 dl->PushClipRect(ImVec2(a.x+4*scale,a.y+4*scale),ImVec2(b.x-4*scale,b.y-4*scale),true);
 LaoMapArt art={};bool artwork=(field||(me.interior&&me.position_valid))&&laonline_map_art(&art);
 const float plane[3]={-436.5f,212.5f,881.f},identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};float mx=0,my=0;
 if(artwork&&!lao_map_native_point(me.world,plane,identity,&mx,&my))artwork=false;
 float mk=k*240.f/881.f;
 if(artwork){
  dl->AddImage((ImTextureID)art.texture,ImVec2(center.x+(art.left-mx)*mk,center.y+(my-art.top)*mk),ImVec2(center.x+(art.right-mx)*mk,center.y+(my-art.bottom)*mk));
 }
 if(field&&!artwork){
  // Three world units per tile; these are region samples, not invented terrain.
  for(int row=-9;row<=9;row++)for(int col=-9;col<=9;col++){
   float x=floorf(me.raw[0]/3.f)*3.f+col*3.f,z=floorf(me.raw[2]/3.f)*3.f+row*3.f;
   const char *region=lao_field_area(x+1.5f,z+1.5f);unsigned hash=region?lao_name_id(region):0;
   ImU32 colour=region?IM_COL32(49+hash%20,76+hash%33,53+hash%16,230):IM_COL32(22,42,54,230);
   ImVec2 p(center.x+(x-me.raw[0])*k,center.y+(z-me.raw[2])*k);dl->AddRectFilled(p,ImVec2(p.x+3*k,p.y+3*k),colour);
  }
 }
 float grid=field?15.f:5.f;
 if(!artwork)for(int axis=0;axis<2;axis++)for(int j=-12;j<=12;j++){
  float origin=axis?me.raw[2]:me.raw[0];float v=size*.5f+(floorf(origin/grid)*grid+j*grid-origin)*k;
  if(v<0||v>size)continue;
  dl->AddLine(axis?ImVec2(a.x,a.y+v):ImVec2(a.x+v,a.y),axis?ImVec2(b.x,a.y+v):ImVec2(a.x+v,b.y),IM_COL32(169,175,125,65),scale);
 }
 if(g_settings.laonline_markers&&g_settings.laonline_indicators)for(int i=0;i<n;i++){
  LaoMapOverlayPlayer p={};if(laonline_map_player(i,&p)<0||p.local||!p.fresh)continue;
  if(artwork?!p.position_valid:(p.map!=me.map||(!field&&p.room!=me.room)))continue; // Other rooms/floors omitted, never edge-clamped.
  float x=center.x+(p.raw[0]-me.raw[0])*k,y=center.y+(p.raw[2]-me.raw[2])*k;
  if(artwork){float px,py;if(!lao_map_native_point(p.world,plane,identity,&px,&py))continue;x=center.x+(px-mx)*mk;y=center.y+(my-py)*mk;}
  if(x<a.x+9*scale||x>b.x-9*scale||y<a.y+9*scale||y>b.y-9*scale)continue;
  dl->AddCircleFilled(ImVec2(x,y),6*scale,IM_COL32(255,247,211,255));
  dl->AddCircleFilled(ImVec2(x,y),4*scale,IM_COL32(p.rgb[0],p.rgb[1],p.rgb[2],255));
  if(artwork&&p.interior)dl->AddCircle(ImVec2(x,y),8*scale,IM_COL32(p.rgb[0],p.rgb[1],p.rgb[2],255),0,scale);
 }
 float dx=sinf(me.yaw),dy=cosf(me.yaw),r=9*scale;
 ImVec2 tip(center.x+dx*r,center.y+dy*r),l(center.x-dx*r*.65f-dy*r*.65f,center.y-dy*r*.65f+dx*r*.65f),rr(center.x-dx*r*.65f+dy*r*.65f,center.y-dy*r*.65f-dx*r*.65f);
 dl->AddTriangleFilled(tip,l,rr,IM_COL32(65,202,255,255));dl->AddTriangle(tip,l,rr,IM_COL32(255,255,238,255),2*scale);
 dl->PopClipRect();dl->AddRect(a,b,IM_COL32(225,214,165,240),14*scale,0,2*scale);
 dl->AddText(font,14*scale,ImVec2(a.x+8*scale,a.y+8*scale),IM_COL32(255,247,215,255),artwork?(me.interior?"N  Koholint (entrance)":"N  Koholint"):(field?"N  Loading map":"N  Room radar"));
}

// Run independently of username/minimap toggles. Off-screen, distant, paused and
// other-room players never trigger native recovery. A resumed renderer starts a new window.
extern "C" uint64_t plat_now_ns(void);
static void probe_remote_draws(){
 LaoMapOverlayPlayer me={};bool have=false;int n=laonline_map_player(-1,NULL);
 for(int i=0;i<n;i++){LaoMapOverlayPlayer p={};if(laonline_map_player(i,&p)>=0&&p.local&&p.fresh){me=p;have=true;break;}}
 uint64_t frame=renderer_exec_frame(),ms=plat_now_ns()/1000000ull;
 for(int id=0;id<LAO_PLAYERS;id++){
  char name[17];float w[3],nx,ny;uint8_t rgb[3];int hp,maxhp;bool expected=false;
  if(have&&laonline_remote_tag_at(id,name,sizeof name,w,rgb,&hp,&maxhp)&&widescreen_world_to_ndc(w[0],w[1],w[2],&nx,&ny)){
   float dx=w[0]-me.raw[0],dz=w[2]-me.raw[2];
   expected=dx*dx+dz*dz<16.f&&fabsf(w[1]-me.raw[1])<4.f&&fabsf(nx)<.85f&&fabsf(ny)<.85f;
  }
  laonline_remote_draw_probe(id,frame,ms,expected);
 }
}

static void draw_session_header(ImDrawList *dl,ImVec2 display,ImFont *font,float scale){
    if(g_settings.laonline_play_mode==3){int peers=laonline_players(-1,NULL,0,NULL,0,NULL);if(peers<0)return;char label[140];snprintf(label,sizeof label,"Global %s | %d / 100 players | Your own save | %s",laonline_v12_global_is_randomizer()?"Randomizer":"Vanilla",peers+1,laonline_v12_pvp_local()?"PvP ON (R3/F6)":"R3/F6: PvP");ImVec2 size=font->CalcTextSizeA(18*scale,FLT_MAX,0,label);float x=(display.x-size.x)*.5f;dl->AddRectFilled(ImVec2(x-14*scale,8*scale),ImVec2(x+size.x+14*scale,42*scale),IM_COL32(5,25,43,225),12*scale);dl->AddText(font,18*scale,ImVec2(x,15*scale),IM_COL32_WHITE,label);return;}
    // One compact session bar, including the local participant. No native model assets are copied.
    int n=laonline_map_player(-1,NULL);if(n>4)n=4;
    if(n>0){
        float width=fminf(display.x-40*scale,290*scale*n),left=(display.x-width)*.5f;
        float top=12*scale,h=64*scale,cell=width/n;
        dl->AddRectFilled(ImVec2(left,top),ImVec2(left+width,top+h),IM_COL32(22,30,29,225),22*scale);
        dl->AddRect(ImVec2(left,top),ImVec2(left+width,top+h),IM_COL32(178,163,120,230),22*scale,0,1.5f*scale);
        for(int i=0;i<n;i++){
            LaoMapOverlayPlayer p={};laonline_map_player(i,&p);
            float x=left+i*cell;
            if(i)dl->AddLine(ImVec2(x,top+12*scale),ImVec2(x,top+h-12*scale),IM_COL32(180,176,152,145));
            ImVec2 center(x+28*scale,top+32*scale);
            dl->AddCircleFilled(center,19*scale,IM_COL32(p.rgb[0],p.rgb[1],p.rgb[2],255));
            ImTextureID avatar=0;float avatar_aspect=1;
            if(p.avatar_known && laonline_avatar_icon(p.cap_rgb,p.hair_rgb,&avatar,&avatar_aspect)){
                dl->AddImage(avatar,ImVec2(center.x-23*scale,center.y-23*scale),ImVec2(center.x+23*scale,center.y+23*scale));
            }else{
            char initial[2]={p.name[0]?p.name[0]:'?',0};
            ImVec2 is=font->CalcTextSizeA(23*scale,FLT_MAX,0,initial);
            dl->AddText(font,23*scale,ImVec2(center.x-is.x*.5f,center.y-is.y*.5f),IM_COL32(255,255,255,255),initial);
            }
            ImVec4 clip(x+53*scale,top,x+cell-9*scale,top+h);
            dl->AddText(font,20*scale,ImVec2(x+54*scale,top+10*scale),IM_COL32(255,255,255,255),p.name,NULL,0,&clip);
            dl->AddText(font,16*scale,ImVec2(x+54*scale,top+37*scale),IM_COL32(236,231,210,255),p.location[0]?p.location:"Loading",NULL,0,&clip);
        }
        char count[16];snprintf(count,sizeof count,"%d / 4",n);
        ImVec2 sz=font->CalcTextSizeA(14*scale,FLT_MAX,0,count);
        dl->AddText(font,14*scale,ImVec2((display.x-sz.x)*.5f,top+h+3*scale),IM_COL32(255,255,255,230),count);
    }

}

static void draw_inventory_tracker(ImDrawList *dl,ImVec2 display,ImFont *font,float scale){
 if(!g_settings.laonline_item_tracker)return;LaoInventoryView v={};if(!laonline_inventory_view(&v))return;
 float size=(g_settings.laonline_tracker_size==0?26.f:g_settings.laonline_tracker_size==2?42.f:34.f)*scale;
 float w=size*8+16*scale,h=size*4+48*scale;
 float fit=fminf(1.f,fminf((display.x-16)/w,(display.y-16)/h));size*=fit;w*=fit;h*=fit;
 float x=8+(display.x-w-16)*g_settings.laonline_tracker_x/100.f,y=8+(display.y-h-16)*g_settings.laonline_tracker_y/100.f;
 int alpha=(int)(255*g_settings.laonline_tracker_opacity/100.f);
 dl->AddRectFilled(ImVec2(x,y),ImVec2(x+w,y+h),IM_COL32(18,28,27,alpha),10*scale);
 dl->AddText(font,14*scale*fit,ImVec2(x+8*scale,y+5*scale),IM_COL32(240,235,215,alpha),"Your inventory");
 for(int i=0;i<LAO_INVENTORY_ICONS;i++){
  float ix=x+8*scale+(i%8)*size,iy=y+24*scale+(i/8)*size;ImTextureID tex=0;float aspect=1;
  int a=v.owned[i]?alpha:alpha/4;
  if(laonline_item_icon(lao_inventory_icons[i],&tex,&aspect)){float bound=size-3*scale,w=bound,h=bound;if(aspect>1)h/=aspect;else w*=aspect;ImVec2 lo(ix+(bound-w)*.5f,iy+(bound-h)*.5f);dl->AddImage(tex,lo,ImVec2(lo.x+w,lo.y+h),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,a));}
  else {char id[12];snprintf(id,sizeof id,"%.4s",lao_pickup_name(lao_inventory_icons[i]));dl->AddText(font,12*scale*fit,ImVec2(ix,iy),IM_COL32(255,255,255,a),id);}
 }
 char counts[96];snprintf(counts,sizeof counts,"Shells %u/%u | Pieces %u/32 | Containers %u/9",v.shells,v.shell_total,v.pieces,v.containers);
 dl->AddText(font,11*scale*fit,ImVec2(x+8*scale,y+h-18*scale),IM_COL32(240,235,215,alpha),counts);
}

extern "C" float laonline_v12_hit_flash(void);  /* laonline_v12.c (V13.5 PvP) */
extern "C" float laonline_v12_killfeed(int i, char *line1, int cap1, char *line2, int cap2);
extern "C" int laonline_v12_my_kos(int *kills, int *falls);
/* V13.5 PvP kill feed, bottom right under the minimap: "<quote>" and the head-to-head score, newest at the bottom;
 * with PvP on, your own knock-outs / falls this session below it. */
extern "C" int laonline_v12_totals(int *kills, int *losses);
/* V15 (user's picture, "like a Call of Duty / Fortnite kill feed"):
 *  - bottom left, everyone: the KILL FEED - "Sanic  defeated  Velvet  with Bombs", newest on top, fading after 10 s;
 *  - bottom right, only the two players in a KO: a personal card with the laughing portrait (the owner's picture,
 *    assets/laonline-ui/laugh-portrait.png), a funny line written for them, their head-to-head score and a timer bar.
 *    It sits BELOW the minimap and never covers or moves it;
 *  - bottom centre: Kills / Losses (the server's totals for your name). */
static void triforce(ImDrawList *dl,ImVec2 c,float r,ImU32 col,ImU32 edge){
    ImVec2 top(c.x,c.y-r),bl(c.x-r*1.05f,c.y+r*.8f),br(c.x+r*1.05f,c.y+r*.8f);
    ImVec2 ml((top.x+bl.x)*.5f,(top.y+bl.y)*.5f),mr((top.x+br.x)*.5f,(top.y+br.y)*.5f),mb((bl.x+br.x)*.5f,bl.y);
    dl->AddTriangleFilled(top,ml,mr,col);dl->AddTriangleFilled(ml,bl,mb,col);dl->AddTriangleFilled(mr,mb,br,col);
    dl->AddTriangle(top,bl,br,edge,1.2f);
}
static void gold_frame(ImDrawList *dl,ImVec2 a,ImVec2 b,float r,float s,int alpha){
    ImU32 gold=IM_COL32(222,178,84,alpha),pale=IM_COL32(255,232,160,alpha),dark=IM_COL32(90,62,20,alpha);
    dl->AddRect(ImVec2(a.x-1*s,a.y-1*s),ImVec2(b.x+1*s,b.y+1*s),dark,r+1*s,0,3*s);
    dl->AddRect(a,b,gold,r,0,3*s);
    dl->AddRect(ImVec2(a.x+5*s,a.y+5*s),ImVec2(b.x-5*s,b.y-5*s),pale,r-4*s,0,1.2f*s);
    ImVec2 cs[4]={a,ImVec2(b.x,a.y),b,ImVec2(a.x,b.y)};float sx[4]={1,-1,-1,1},sy[4]={1,1,-1,-1};
    for(int k=0;k<4;k++){ImVec2 c(cs[k].x+sx[k]*13*s,cs[k].y+sy[k]*13*s);
        dl->PathArcTo(c,7*s,0,6.2f,14);dl->PathStroke(gold,0,1.6f*s);dl->AddCircleFilled(c,2.2f*s,pale);}
}
extern "C" float laonline_v12_feed_row(int i, char *k, int kc, char *v, int vc, char *weapon, int wc, int *ending);
extern "C" float laonline_v12_feed_panel(float *left_s);
extern "C" float laonline_v12_card(char *line, int lc, char *score, int sc, float *left_s);
static void timer_bar(ImDrawList *dl,ImFont *font,float fs,ImVec2 p,float w,float left_s,float total,int al,float s){
    char t[32];snprintf(t,sizeof t,"Fading in %ds",(int)ceilf(left_s));
    dl->AddText(font,fs,p,IM_COL32(225,232,240,al),t);float tw=font->CalcTextSizeA(fs,FLT_MAX,0,t).x+10*s;
    ImVec2 a(p.x+tw,p.y+fs*.38f),b(p.x+w,p.y+fs*.38f+5*s);
    dl->AddRectFilled(a,b,IM_COL32(255,255,255,(int)(al*.25f)),3*s);
    dl->AddRectFilled(a,ImVec2(a.x+(b.x-a.x)*fmaxf(0,fminf(1,left_s/total)),b.y),IM_COL32(255,214,92,al),3*s);
}
static void draw_kill_feed(ImDrawList *dl,ImVec2 display,ImFont *font,float scale){
    float s=scale;
    /* bottom centre: Kills / Losses */
    int kills=0,losses=0;float kbar_left=display.x;
    if(laonline_v12_totals(&kills,&losses)){
        char t[64];snprintf(t,sizeof t,"Kills %d  -  Losses %d",kills,losses);
        float fs=font->FontSize*.5f*s;ImVec2 ts=font->CalcTextSizeA(fs,FLT_MAX,0,t);
        float w=ts.x+110*s,h=ts.y+22*s,x=(display.x-w)*.5f,y=display.y-h-20*s;kbar_left=x;
        dl->AddRectFilledMultiColor(ImVec2(x,y),ImVec2(x+w,y+h),IM_COL32(32,58,52,235),IM_COL32(32,58,52,235),IM_COL32(14,30,30,235),IM_COL32(14,30,30,235));
        gold_frame(dl,ImVec2(x,y),ImVec2(x+w,y+h),h*.45f,s*.8f,255);
        triforce(dl,ImVec2(x+32*s,y+h*.52f),12*s,IM_COL32(255,214,92,255),IM_COL32(120,80,20,255));
        triforce(dl,ImVec2(x+w-32*s,y+h*.52f),12*s,IM_COL32(255,214,92,255),IM_COL32(120,80,20,255));
        ImVec2 tp(x+(w-ts.x)*.5f,y+(h-ts.y)*.5f);
        dl->AddText(font,fs,ImVec2(tp.x+2*s,tp.y+2*s),IM_COL32(0,0,0,170),t);dl->AddText(font,fs,tp,IM_COL32(255,246,222,255),t);
    }
    /* bottom left: the kill feed */
    float left=0,pa=laonline_v12_feed_panel(&left);
    if(pa>0){
        int al=(int)(255*pa);float fs=font->FontSize*.36f*s,pad=12*s;
        char k[5][32],v[5][32],wp[5][24];int end[5],n=0;float ra[5];
        for(int i=0;i<5;i++){ra[n]=laonline_v12_feed_row(i,k[n],32,v[n],32,wp[n],24,&end[n]);if(ra[n]<=0)break;n++;}
        /* as wide as its longest row (up to 34% of the screen; longer names make the text a bit smaller) */
        auto row_w=[&](int i,float f){  /* exactly as the row is drawn below: parts + gaps */
            const char *parts[4]={k[i],end[i]?"saved Koholint!":"defeated",end[i]?"":v[i],""};char with[40]="";
            if(!end[i]&&wp[i][0]){snprintf(with,sizeof with,"with %s",wp[i]);parts[3]=with;}
            float x=0;for(int q=0;q<4;q++)if(parts[q][0])x+=font->CalcTextSizeA(f,FLT_MAX,0,parts[q]).x+f*.35f;return x;};
        float cap=fminf(display.x*.36f,kbar_left-20*s-10*s)-pad*2-14*s,longest=0,rfs[5];
        for(int i=0;i<n;i++){float rw=row_w(i,fs);rfs[i]=rw>cap?fs*cap/rw*.97f:fs;longest=fmaxf(longest,fminf(rw,cap));}  /* (only a too-long row gets smaller) */
        float hs=fs*.92f,row=fs*1.55f,w=fmaxf(300*s,longest+pad*2+14*s);
        float h=pad+hs*1.4f+n*row+fs*1.5f+pad,x=20*s,y=display.y-h-30*s;
        dl->AddRectFilled(ImVec2(x,y),ImVec2(x+w,y+h),IM_COL32(18,26,34,(int)(190*pa)),12*s);
        dl->PushClipRect(ImVec2(x,y),ImVec2(x+w,y+h),true);  /* (nothing can spill out of the panel) */
        dl->AddRect(ImVec2(x,y),ImVec2(x+w,y+h),IM_COL32(255,255,255,(int)(40*pa)),12*s,0,1*s);
        float cy=y+pad;triforce(dl,ImVec2(x+pad+7*s,cy+hs*.55f),7*s,IM_COL32(255,214,92,al),IM_COL32(120,80,20,al));
        dl->AddText(font,hs,ImVec2(x+pad+20*s,cy),IM_COL32(255,214,92,al),"KILL FEED");
        {const char *t="PVP EVENTS";float tw=font->CalcTextSizeA(hs*.8f,FLT_MAX,0,t).x;dl->AddText(font,hs*.8f,ImVec2(x+w-pad-tw,cy+hs*.12f),IM_COL32(150,200,230,(int)(200*pa)),t);}
        cy+=hs*1.4f;
        for(int i=0;i<n;i++){
            int ra_al=(int)(al*fminf(1.f,ra[i]*1.2f));float tx=x+pad+14*s;
            dl->AddLine(ImVec2(x+pad,cy-2*s),ImVec2(x+w-pad,cy-2*s),IM_COL32(255,255,255,(int)(25*pa)),1*s);
            dl->AddCircleFilled(ImVec2(x+pad+4*s,cy+fs*.55f),3.5f*s,end[i]?IM_COL32(255,214,92,ra_al):IM_COL32(235,90,70,ra_al));
            float f=rfs[i],oy=(fs-f)*.5f;
            auto put=[&](const char *t,ImU32 c){dl->AddText(font,f,ImVec2(tx,cy+oy),c,t);tx+=font->CalcTextSizeA(f,FLT_MAX,0,t).x+f*.35f;};
            if(end[i]){put(k[i],IM_COL32(255,255,255,ra_al));put("saved Koholint!",IM_COL32(255,214,92,ra_al));}
            else{put(k[i],IM_COL32(255,255,255,ra_al));put("defeated",IM_COL32(255,140,80,ra_al));put(v[i],IM_COL32(255,255,255,ra_al));
                 if(wp[i][0]){char t[40];snprintf(t,sizeof t,"with %s",wp[i]);put(t,IM_COL32(170,185,200,ra_al));}}
            cy+=row;
        }
        timer_bar(dl,font,fs*.9f,ImVec2(x+pad,cy+fs*.1f),w-pad*2,left,10.f,(int)(230*pa),s);
        dl->PopClipRect();
    }
    /* bottom right: your own KO card, under the minimap (never on top of it) */
    char line[320]="",score[96]="";float cl=0,ca=laonline_v12_card(line,sizeof line,score,sizeof score,&cl);
    if(ca>0){
        int al=(int)(255*ca);
        float right=display.x-24*s,top=g_settings.laonline_minimap?display.y*.84f+8*s:display.y-190*s,bottom=display.y-16*s;
        float bar_half=0;{int kk,ll;if(laonline_v12_totals(&kk,&ll)){char t[64];snprintf(t,sizeof t,"Kills %d  -  Losses %d",kk,ll);bar_half=(font->CalcTextSizeA(font->FontSize*.5f*s,FLT_MAX,0,t).x+110*s)*.5f;}}
        float left_edge=fmaxf(display.x*.5f+bar_half+16*s,right-display.x*.42f);  /* (beside the Kills / Losses bar, never under it) */
        float h=bottom-top,w=right-left_edge,x=left_edge,y=top;
        float por=h-16*s,pad=10*s,tx=x+pad+por+12*s,tw=x+w-pad-tx;
        float fs=font->FontSize*.4f*s,fs2=fs*.78f;ImVec2 ts;
        for(int k=0;k<10;k++){ts=font->CalcTextSizeA(fs,FLT_MAX,tw,line);if(ts.y+fs2*2.5f<=h-pad*2)break;fs*=.92f;fs2=fs*.78f;}  /* (fits the space) */
        dl->AddRectFilledMultiColor(ImVec2(x,y),ImVec2(x+w,y+h),IM_COL32(30,42,54,(int)(225*ca)),IM_COL32(30,42,54,(int)(225*ca)),IM_COL32(14,22,32,(int)(225*ca)),IM_COL32(14,22,32,(int)(225*ca)));
        dl->AddRect(ImVec2(x,y),ImVec2(x+w,y+h),IM_COL32(222,178,84,(int)(220*ca)),12*s,0,1.8f*s);
        dl->PushClipRect(ImVec2(x,y),ImVec2(x+w,y+h),true);
        {ImVec2 c(x+pad+por*.5f,y+h*.5f);dl->AddCircleFilled(c,por*.4f,IM_COL32(255,220,120,al));}
        float ty=y+pad;
        dl->AddText(font,fs,ImVec2(tx+1.5f*s,ty+1.5f*s),IM_COL32(0,0,0,(int)(170*ca)),line,NULL,tw);
        dl->AddText(font,fs,ImVec2(tx,ty),IM_COL32(255,250,238,al),line,NULL,tw);
        float fy=y+h-pad-fs2*2.3f;
        dl->AddText(font,fs2,ImVec2(tx,fy),IM_COL32(255,211,90,al),score);
        timer_bar(dl,font,fs2,ImVec2(tx,fy+fs2*1.2f),tw,cl,10.f,al,s);
        dl->PopClipRect();
    }
}
extern "C" int laonline_v12_effects(int *item,float pos[][3],float dir[][2],float *age_s,uint16_t *map,int cap);
/* V13.5 PvP: other players' item attacks, drawn at their place in the world (like the name tags; not game models).
 * Bomb: fuse then blast. Arrow / Magic Rod fire: flies forward. Boomerang: spins out and back. Hookshot: chain out and
 * back. Magic Powder: a sparkle cloud. */
extern "C" int laonline_v12_hitmarks(float pos[][3], float *age_s, int *quarters, int *mine, int *on_me, int cap);
extern "C" int laonline_v12_lockon(float pos[3]);
/* V14 PvP: hit markers (an impact burst on whoever was hit - everyone sees it; your own hits also get the X hit
 * marker and the damage popping up) and the lock-on reticle (L3 / F8). */
static void draw_pvp_marks(ImDrawList *dl,ImVec2 display,ImFont *font,float scale){
    float vw=display.x,vh=display.y;
    if(widescreen_frame_boxed(renderer_exec_frame())&&!g_room_wide){vw=fminf(display.x,display.y*16.f/9.f);vh=vw*9.f/16.f;}
    auto screen=[&](float x,float y,float z,ImVec2 &out)->bool{float nx,ny;if(!widescreen_world_to_ndc(x,y,z,&nx,&ny)||nx<-1.2f||nx>1.2f||ny<-1.2f||ny>1.2f)return false;
        out=ImVec2((display.x-vw)*.5f+(nx*.5f+.5f)*vw,(display.y-vh)*.5f+(.5f-ny*.5f)*vh);return true;};
    ImVec2 c;
    float pos[8][3],age[8];int q[8],mine[8],me[8];int n=laonline_v12_hitmarks(pos,age,q,mine,me,8);
    for(int i=0;i<n;i++){
        if(!screen(pos[i][0],pos[i][1]+.9f,pos[i][2],c))continue;float t=age[i];
        if(t<.3f){float k=t/.3f;int a=(int)(255*(1-k));  /* impact burst */
            for(int s=0;s<8;s++){float ang=s*.7854f+.39f,r0=(6+18*k)*scale,r1=(14+30*k)*scale;
                dl->AddLine(ImVec2(c.x+cosf(ang)*r0,c.y+sinf(ang)*r0),ImVec2(c.x+cosf(ang)*r1,c.y+sinf(ang)*r1),IM_COL32(255,240,150,a),3*scale);}
            dl->AddCircleFilled(c,(10*(1-k)+4)*scale,IM_COL32(255,255,255,a));}
        if(mine[i]){
            if(t<.45f){float k=t/.45f,g=(7+5*k)*scale,l=(10)*scale;int a=(int)(255*(1-k*k));  /* the X hit marker */
                for(int s=0;s<4;s++){float dx=(s&1)?1.f:-1.f,dy=(s&2)?1.f:-1.f;ImVec2 p0(c.x+dx*g,c.y+dy*g),p1(c.x+dx*(g+l),c.y+dy*(g+l));
                    dl->AddLine(p0,p1,IM_COL32(0,0,0,a),5*scale);dl->AddLine(p0,p1,IM_COL32(255,255,255,a),2.5f*scale);}}
            char txt[24];
            if(q[i]%4==0)snprintf(txt,sizeof txt,"-%d heart%s",q[i]/4,q[i]>4?"s":"");
            else if(q[i]==2)snprintf(txt,sizeof txt,"-1/2 heart");
            else snprintf(txt,sizeof txt,"-%d/4 heart",q[i]);
            float fs=font->FontSize*.9f*scale,a=t<.6f?1.f:(.9f-t)/.3f;ImVec2 sz=font->CalcTextSizeA(fs,FLT_MAX,0,txt);
            ImVec2 tp(c.x-sz.x*.5f,c.y-34*scale-t*40*scale);
            dl->AddText(font,fs,ImVec2(tp.x+1.5f*scale,tp.y+1.5f*scale),IM_COL32(0,0,0,(int)(220*a)),txt);
            dl->AddText(font,fs,tp,IM_COL32(255,90,70,(int)(255*a)),txt);
        }
    }
}
static void draw_pvp_effects(ImDrawList *dl,ImVec2 display,float scale){
    int item[16];float pos[16][3],dir[16][2],age[16];uint16_t map[16];
    int n=laonline_v12_effects(item,pos,dir,age,map,16);if(!n)return;
    float vw=display.x,vh=display.y;
    if(widescreen_frame_boxed(renderer_exec_frame())&&!g_room_wide){vw=fminf(display.x,display.y*16.f/9.f);vh=vw*9.f/16.f;}
    auto screen=[&](float x,float y,float z,ImVec2 &out)->bool{float nx,ny;if(!widescreen_world_to_ndc(x,y,z,&nx,&ny)||nx<-1.2f||nx>1.2f||ny<-1.2f||ny>1.2f)return false;
        out=ImVec2((display.x-vw)*.5f+(nx*.5f+.5f)*vw,(display.y-vh)*.5f+(.5f-ny*.5f)*vh);return true;};
    for(int i=0;i<n;i++){
        float t=age[i],*p=pos[i],dx=dir[i][0],dz=dir[i][1],h=p[1]+.9f;ImVec2 a,b;
        switch(item[i]){
        case 2:{ /* bomb: placed in front, fuse 1.5 s, blast 0.35 s */
            float bx=p[0]+dx*.9f,bz=p[2]+dz*.9f;if(!screen(bx,p[1]+.3f,bz,a))break;
            if(t<1.5f){bool blink=t>1.0f&&((int)(t*12)&1);dl->AddCircleFilled(a,9*scale,blink?IM_COL32(255,80,60,255):IM_COL32(40,60,140,255));
                dl->AddCircle(a,9*scale,IM_COL32(10,10,30,255),0,2*scale);dl->AddLine(ImVec2(a.x,a.y-9*scale),ImVec2(a.x+5*scale,a.y-15*scale),IM_COL32(255,220,120,255),2*scale);}
            else if(t<1.85f){float k=(t-1.5f)/.35f;dl->AddCircleFilled(a,(14+60*k)*scale,IM_COL32(255,170,40,(int)(220*(1-k))));dl->AddCircleFilled(a,(8+34*k)*scale,IM_COL32(255,250,200,(int)(240*(1-k))));}
            break;}
        case 3: case 6:{ /* arrow / magic rod fire */
            float life=item[i]==3?.5f:.55f,range=item[i]==3?9.f:9.f;if(t>life+.15f)break;float d=fminf(t/life,1.f)*range;
            if(!screen(p[0]+dx*d,h,p[2]+dz*d,a)||!screen(p[0]+dx*(d-.8f),h,p[2]+dz*(d-.8f),b))break;
            if(item[i]==3){dl->AddLine(b,a,IM_COL32(120,80,40,255),3*scale);dl->AddTriangleFilled(a,ImVec2(a.x+(b.y-a.y)*.25f,a.y-(b.x-a.x)*.25f),ImVec2(a.x-(b.y-a.y)*.25f,a.y+(b.x-a.x)*.25f),IM_COL32(220,220,230,255));}
            else{float k=t>life?(t-life)/.15f:0;dl->AddCircleFilled(a,(9+20*k)*scale,IM_COL32(255,120,20,(int)(230*(1-k))));dl->AddCircleFilled(a,(5+8*k)*scale,IM_COL32(255,240,150,(int)(250*(1-k))));}
            break;}
        case 5:{ /* boomerang: out 7 units, back */
            if(t>.9f)break;float d=(t<.45f?t/.45f:1.f-(t-.45f)/.45f)*7.f;if(!screen(p[0]+dx*d,h,p[2]+dz*d,a))break;
            float r=10*scale,ang=t*30.f;for(int k=0;k<2;k++){float c=cosf(ang+k*1.5708f)*r,s=sinf(ang+k*1.5708f)*r;dl->AddLine(ImVec2(a.x-c,a.y-s),ImVec2(a.x+c,a.y+s),IM_COL32(230,40,40,255),4*scale);}
            break;}
        case 4:{ /* hookshot: chain out 7 units and back */
            if(t>.6f)break;float d=(t<.3f?t/.3f:1.f-(t-.3f)/.3f)*7.f;if(!screen(p[0],h,p[2],b)||!screen(p[0]+dx*d,h,p[2]+dz*d,a))break;
            dl->AddLine(b,a,IM_COL32(180,180,190,255),2*scale);for(int k=1;k<8;k++){ImVec2 c(b.x+(a.x-b.x)*k/8.f,b.y+(a.y-b.y)*k/8.f);dl->AddCircle(c,2.5f*scale,IM_COL32(200,200,210,255));}
            dl->AddTriangleFilled(ImVec2(a.x,a.y-7*scale),ImVec2(a.x+6*scale,a.y+5*scale),ImVec2(a.x-6*scale,a.y+5*scale),IM_COL32(230,230,240,255));
            break;}
        case 100:{ /* V14 sword slash: an arc in front */
            if(t>.22f||!screen(p[0],h,p[2],b))break;float k=t/.22f,base=atan2f(dz,dx);
            ImVec2 prev;bool have=false;
            for(int s=0;s<=10;s++){float ang=base-1.2f+2.4f*(s/10.f)*fminf(1.f,k*1.6f),rr=1.5f;ImVec2 q;
                if(!screen(p[0]+cosf(ang)*rr,h,p[2]+sinf(ang)*rr,q)){have=false;continue;}
                if(have)dl->AddLine(prev,q,IM_COL32(230,240,255,(int)(255*(1-k))),5*scale*(1-k*.5f));prev=q;have=true;}
            break;}
        case 101:{ /* V14 spin attack: a full ring */
            if(t>.45f)break;float k=t/.45f;ImVec2 prev;bool have=false;
            for(int s=0;s<=24;s++){float ang=s*0.2618f+k*6.f,rr=2.6f*fminf(1.f,k*2.f);ImVec2 q;
                if(!screen(p[0]+cosf(ang)*rr,h,p[2]+sinf(ang)*rr,q)){have=false;continue;}
                if(have)dl->AddLine(prev,q,IM_COL32(150,220,255,(int)(255*(1-k))),6*scale);prev=q;have=true;}
            break;}
        case 102:{ /* V14 bomb arrow: an arrow with a bomb, then a blast where it ends */
            float life=.5f,range=9.f;if(t>life+.4f)break;float d=fminf(t/life,1.f)*range;
            if(!screen(p[0]+dx*d,h,p[2]+dz*d,a))break;
            if(t<=life){if(screen(p[0]+dx*(d-.8f),h,p[2]+dz*(d-.8f),b))dl->AddLine(b,a,IM_COL32(120,80,40,255),3*scale);
                dl->AddCircleFilled(a,6*scale,IM_COL32(40,60,140,255));dl->AddCircle(a,6*scale,IM_COL32(10,10,30,255),0,1.5f*scale);}
            else{float k=(t-life)/.4f;dl->AddCircleFilled(a,(14+60*k)*scale,IM_COL32(255,170,40,(int)(220*(1-k))));dl->AddCircleFilled(a,(8+34*k)*scale,IM_COL32(255,250,200,(int)(240*(1-k))));}
            break;}
        case 103:{ /* V14 Pegasus Boots dash: dust puffs behind */
            if(t>.5f)break;float k=t/.5f;
            for(int s=0;s<5;s++){float back=.4f+s*.45f;if(!screen(p[0]-dx*back,p[1]+.15f,p[2]-dz*back,a))continue;
                dl->AddCircleFilled(a,(5+6*k+s)*scale,IM_COL32(210,190,150,(int)(170*(1-k)*(1-s/6.f))));}
            break;}
        case 104:{ /* V14 Roc's Feather: a jump, then a landing ring */
            if(t>.8f)break;
            if(t<.48f){float up=sinf(t/.48f*3.1416f)*1.2f;if(screen(p[0],p[1]+.1f,p[2],a))dl->AddCircleFilled(a,(10-5*up)*scale,IM_COL32(0,0,0,90));
                if(screen(p[0],h+up,p[2],b))dl->AddCircle(b,7*scale,IM_COL32(255,255,255,200),0,2*scale);}
            else{float k=(t-.48f)/.32f;if(screen(p[0],p[1]+.1f,p[2],a)){dl->AddCircle(a,(10+30*k)*scale,IM_COL32(255,255,255,(int)(230*(1-k))),0,3*scale);}}
            break;}
        case 8:{ /* magic powder cloud */
            if(t>.7f||!screen(p[0]+dx*1.3f,h-.2f,p[2]+dz*1.3f,a))break;float k=t/.7f;
            for(int s=0;s<10;s++){float ang=s*0.628f+t*4,rr=(8+28*k)*scale*(0.5f+0.5f*((s*37)%10)/10.f);dl->AddCircleFilled(ImVec2(a.x+cosf(ang)*rr,a.y+sinf(ang)*rr*.6f),3*scale,IM_COL32(255,230,120+s*10,(int)(220*(1-k))));}
            break;}
        }
    }
}
extern "C" void laonline_overlay_draw(ImVec2 display, ImFont *font, float scale) {
    { float hit=laonline_v12_hit_flash();  /* V13.5 PvP: red flash when hit */
      if(hit>0){ImDrawList *fg=ImGui::GetForegroundDrawList();int a=(int)(110*fminf(1.f,hit));ImU32 red=IM_COL32(220,20,30,a),clear=IM_COL32(220,20,30,0);
        float w=display.x,h=display.y,e=fminf(w,h)*.18f;
        fg->AddRectFilledMultiColor(ImVec2(0,0),ImVec2(w,e),red,red,clear,clear);fg->AddRectFilledMultiColor(ImVec2(0,h-e),ImVec2(w,h),clear,clear,red,red);
        fg->AddRectFilledMultiColor(ImVec2(0,0),ImVec2(e,h),red,clear,clear,red);fg->AddRectFilledMultiColor(ImVec2(w-e,0),ImVec2(w,h),clear,red,red,clear);}}
    if(menu_is_open()) {laonline_chat_input_close();return;}
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    bool paused=laonline_ui_paused();
    bool death=laonline_death_active()!=0,changing=!game_in_play()||!laonline_level_ready();
    if(death||changing||!paused)draw_session_header(dl,display,font,scale);
    if(death||changing){laonline_chat_input_close();return;}
    bool map_open=draw_dungeon_players(dl,display,font,scale);
    if(!map_open)map_open=draw_map_players(dl,display,font);
    if(map_open || paused) {laonline_chat_input_close();return;}
    if(!game_in_play()){laonline_chat_input_close();return;}
    probe_remote_draws();
    draw_inventory_tracker(dl,display,font,scale);
    float fs = 30.0f * scale;
    // Notifications use the lower-right stacked-card placement from the supplied PSD reference.
    float y = display.y - 82.0f * scale;
    LaoMapOverlayPlayer minimap_local={};bool minimap_visible=false;
    for(int m=0;m<laonline_map_player(-1,NULL);m++)if(laonline_map_player(m,&minimap_local)>=0 && minimap_local.local && minimap_local.fresh){minimap_visible=g_settings.laonline_minimap && (minimap_local.map==lao_name_id("Field") || dungeon_minimap_available(minimap_local));break;}
    if(minimap_visible)y=display.y*.84f-minimap_size(display,scale)-24*scale;
    for (int i = 0; i < 2; i++) {
        char t[160];
        uint64_t age;int item=-1;
        if (!laonline_toast_with_item(i,t,sizeof t,&age,&item)) continue;
        float a = age > 4000 ? (5000 - age) / 1000.0f : 1.0f;
        ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0, t);
        ImTextureID texture=0;float aspect=1;bool icon=laonline_item_icon(item,&texture,&aspect);
        float icon_space=(icon||item==-2)?82*scale:0;
        ImVec2 p(display.x - sz.x - 34 * scale, y - sz.y);
        dl->AddRectFilled(ImVec2(p.x - 16 * scale-icon_space, p.y - 8 * scale), ImVec2(p.x + sz.x + 16 * scale, p.y + sz.y + 8 * scale),
                          IM_COL32(38, 31, 21, (int)(225 * a)), 8 * scale);
        dl->AddRect(ImVec2(p.x - 16 * scale-icon_space, p.y - 8 * scale), ImVec2(p.x + sz.x + 16 * scale, p.y + sz.y + 8 * scale),
                    IM_COL32(145, 126, 76, (int)(255 * a)), 8 * scale, 0, 2 * scale);
        if(item==-2){
            ImVec2 center(p.x-icon_space*.5f-8*scale,p.y+sz.y*.5f);float r=19*scale;
            dl->AddCircleFilled(center,r,IM_COL32(235,225,205,(int)(255*a)));
            dl->AddCircleFilled(ImVec2(center.x-r*.35f,center.y-r*.1f),r*.18f,IM_COL32(35,30,28,(int)(255*a)));
            dl->AddCircleFilled(ImVec2(center.x+r*.35f,center.y-r*.1f),r*.18f,IM_COL32(35,30,28,(int)(255*a)));
            dl->AddRectFilled(ImVec2(center.x-r*.5f,center.y+r*.5f),ImVec2(center.x+r*.5f,center.y+r),IM_COL32(235,225,205,(int)(255*a)),3*scale);
        }
        if(icon){
            float box=64*scale,w=aspect>=1?box:box*aspect,h=aspect>=1?box/aspect:box;
            ImVec2 center(p.x-icon_space*.5f-8*scale,p.y+sz.y*.5f);
            dl->AddImage(texture,ImVec2(center.x-w*.5f,center.y-h*.5f),ImVec2(center.x+w*.5f,center.y+h*.5f),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,(int)(255*a)));
        }
        const char *row=t;float row_y=p.y;int row_index=0;
        while(*row){const char *end=strchr(row,'\n');
            dl->AddText(font,fs,ImVec2(p.x,row_y),row_index==1?IM_COL32(255,211,65,(int)(255*a)):IM_COL32(255,245,220,(int)(255*a)),row,end);
            if(!end)break;row=end+1;row_y+=fs;row_index++;
        }
        y -= sz.y + 28 * scale;
    }

    draw_minimap(dl,display,font,scale);
    draw_kill_feed(dl,display,font,scale);  /* V13.5 PvP knock-outs */
    /* V13.5: name plates first, then chat bubbles on top, moved up clear of every plate (players' bubbles sat right
     * on the teammate's name tag when they stood close together). */
    if(game_in_play()&&!paused&&!map_open)draw_pvp_effects(dl,display,scale);  /* V13.5 PvP attack visuals */
    if(game_in_play()&&!paused&&!map_open)draw_pvp_marks(dl,display,font,scale);  /* V14 hit markers + lock-on */
    g_plate_n=0;
    if(lao_world_plate_visible(true,(g_settings.laonline_username || g_settings.laonline_hearts) && g_settings.laonline_indicators,menu_is_open(),paused,map_open,game_in_play()!=0))
        for(int id=0;id<LAO_PLAYERS;id++)draw_world_plate(dl,display,font,scale,id);
    draw_chat(dl,display,font,scale);
}

/* V15 Z-targeting reticle (runtime/hle/ztarget.c), drawn in every mode (offline too): three arrows circling the
 * target and pointing in, like Z-targeting in other Zelda games. */
extern "C" int ztarget_get(float pos[3], int *player);
extern "C" void ztarget_overlay_draw(ImVec2 display,float scale){
    float lp[3];if(!ztarget_get(lp,NULL))return;
    float vw=display.x,vh=display.y;
    if(widescreen_frame_boxed(renderer_exec_frame())&&!g_room_wide){vw=fminf(display.x,display.y*16.f/9.f);vh=vw*9.f/16.f;}
    float nx,ny;if(!widescreen_world_to_ndc(lp[0],lp[1]+.9f,lp[2],&nx,&ny)||nx<-1.2f||nx>1.2f||ny<-1.2f||ny>1.2f)return;
    ImVec2 c((display.x-vw)*.5f+(nx*.5f+.5f)*vw,(display.y-vh)*.5f+(.5f-ny*.5f)*vh);
    ImDrawList *dl=ImGui::GetForegroundDrawList();
    float t=(float)ImGui::GetTime(),r=(26+4*sinf(t*6))*scale,sz=9*scale;
    for(int k=0;k<3;k++){float a=t*2.2f+k*2.0944f,ca=cosf(a),sa=sinf(a);ImVec2 tip(c.x+ca*r,c.y+sa*r),base(c.x+ca*(r+sz*1.8f),c.y+sa*(r+sz*1.8f));
        ImVec2 l(base.x-sa*sz,base.y+ca*sz),rr(base.x+sa*sz,base.y-ca*sz);
        dl->AddTriangleFilled(tip,l,rr,IM_COL32(255,200,40,235));dl->AddTriangle(tip,l,rr,IM_COL32(120,40,0,255),1.5f*scale);}
    dl->AddCircle(c,r*.45f,IM_COL32(255,80,40,150),24,2*scale);
}
