// Two-pane settings pages (see option_rows.h): rows on the left, the selected / hovered row's description on the right.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // (SetKeyOwner, NavCursorVisible)

extern "C" {
#include "../cosmetic_colour.h"
}
#include "option_rows.h"
#include "osk.h"
#include "settings_look.h"

static float g_scale = 1;
static std::string g_desc_hover, g_desc_focus, g_desc_shown;  // this frame's hovered / selected row, and the last shown
static std::string g_desc_first;  // the page's first row (shown until a row is selected or hovered)

static const ImU32 kGold = IM_COL32(236, 194, 84, 255);
// V12.5: what the row draws on its right (set by the opt_* call just before row())
enum { ROW_TEXT, ROW_TOGGLE, ROW_DROPDOWN, ROW_STEPPER, ROW_SLIDER, ROW_CHEVRON, ROW_RADIO };
static int g_next_kind = ROW_TEXT;
static float g_slider_x0, g_slider_x1, g_slider_y;
static bool g_next_disabled;
void opt_disable_next(void) { g_next_disabled = true; }
static bool g_next_mark, g_next_warn;
void opt_mark_next(void) { g_next_mark = true; }
void opt_warn_next(void) { g_next_warn = true; }
static int g_indent;
static bool g_focus_req, g_take_first;  // (opt_focus_first: requested / this page's first row takes it)
void opt_focus_first(bool on) { g_focus_req = on; }
static int g_focus_row = -1;  // (opt_focus_row: this page's row with that index takes the cursor)
void opt_focus_row(int i) { g_focus_row = i; }
bool opt_focus_first_left(void) {
    bool left = g_focus_req || g_take_first;
    g_focus_req = g_take_first = false;
    g_focus_row = -1;
    return left;
}
// this frame's rows in order (focus checks): labels, the first enabled one, the focused one
static std::vector<std::string> g_rows;
static std::vector<int> g_row_pane;  // (each row's pane: 0 the left one, 1 opt_split's right one)
static int g_row_first = -1, g_row_focus = -1, g_pane;
int opt_row_count(void) { return (int)g_rows.size(); }
const char *opt_row_label(int i) { return i >= 0 && i < (int)g_rows.size() ? g_rows[i].c_str() : ""; }
int opt_first_row(void) { return g_row_first; }
int opt_focused_row(void) { return g_row_focus; }
void opt_frame_reset(void) { g_rows.clear(), g_row_pane.clear(), g_row_first = g_row_focus = -1; }
// Up / Down between option rows go by the rows' order, not ImGui's spatial pick: that skipped indented sub-rows
// (Rumble -> Controller light past Intensity), and past the last row went up to the tab bar
bool opt_nav_step(int dir) {
    int from = g_row_focus, n = (int)g_row_pane.size();  // (the frame before's rows)
    if (from < 0 || from >= n) return false;  // (the cursor isn't on an option row)
    int to = from + dir;
    while (to >= 0 && to < n && g_row_pane[to] != g_row_pane[from]) to += dir;  // (the same pane)
    if (to < 0 || to >= n) {
        if (dir < 0) return false;  // Up from the top row: out of the page (ImGui: the tab bar / controls above)
        to = from;                  // Down from the last row: stays
    }
    g_focus_row = to;
    return true;
}
static std::unordered_map<ImGuiID, int> g_ids;  // this page's rows so far, by id (two rows with one label)
static bool g_held;  // the last row's left / right has been held a while (steppers go faster)
void opt_indent(int step) { g_indent = g_indent + step < 0 ? 0 : g_indent + step; }

void opt_begin(const char *id, float scale) {
    g_scale = scale;
    g_take_first = g_focus_req;
    g_focus_req = false;
    if (g_take_first) ImGui::NavMoveRequestCancel();  // (this frame's Down goes to the first row instead)
    g_indent = 0;
    g_desc_hover.clear(), g_desc_focus.clear();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushID(id);
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 4 * scale));  // (rows close)
    g_desc_first.clear();
    g_ids.clear();
    g_pane = 0;
}

void opt_section(const char *title) {
    // V12.5: a small icon and the title as written (reference), on its own line
    float fs = ImGui::GetFontSize();
    if (ImGui::GetCursorPosY() > ImGui::GetStyle().WindowPadding.y + 1) ImGui::Dummy(ImVec2(0, 6 * g_scale));
    ImVec2 p = ImGui::GetCursorScreenPos();
    settings_look::section(ImGui::GetWindowDrawList(), p, ImGui::GetContentRegionAvail().x, title, fs, g_scale);
    ImGui::Dummy(ImVec2(1, fs * 1.45f));
}

// One row: draws it, handles A / click and left / right while selected. Returns 2 (A / click), -1 / +1 (left / right) or 0.
// arrows: ◀ value ▶ (a choice of several, or a stepper): left / right step it, and the arrows click too
// chevron: a ">" on the right (opt_link)
static bool last_row_disabled;
static int row(const char *label, const std::string &value, bool changed, const char *desc, bool upper = true,
               bool arrows = false, bool chevron = false) {
    // a label a row above already has (Save Editor: Bombs, Magic Powder under Equipment and Ammo) gets an id of its
    // own: two rows with one id share the controller's cursor - ImGui kept the later one's place, so Up / Down from
    // the first went on from the second (a row far below, or the edge of the view)
    int dup = g_ids[ImGui::GetID(label)]++;
    ImGui::PushID(label);
    if (dup) ImGui::PushID(dup);
    if (g_indent) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + g_indent * 28 * g_scale);  // (an option of the row above)
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFontSize() * (desc && *desc ? 2.75f : 2.1f);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));  // (the row's own border shows the selection)
    bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    bool off = g_next_disabled;last_row_disabled=off;  // (greyed out: still selectable, so its description shows, but it can't change)
    if (g_take_first && !off) {  // (opt_focus_first: the first row that isn't greyed out)
        g_take_first = false;
        ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
        ImGui::SetNavCursorVisible(true);
        // the page from its top when the row is in its first screen (its section title shows), else the row at the top
        if (ImGui::GetCursorPosY() < ImGui::GetWindowHeight()) ImGui::SetScrollY(0);
        else ImGui::SetScrollHereY(0);
    }
    if (g_focus_row >= 0 && g_focus_row == (int)g_rows.size()) {  // (opt_focus_row / opt_nav_step)
        g_focus_row = -1;
        ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
        ImGui::SetNavCursorVisible(true);
        ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
    }
    if (!off && g_row_first < 0) g_row_first = (int)g_rows.size();
    if (ImGui::IsItemFocused()) g_row_focus = (int)g_rows.size();
    g_rows.push_back(dup ? std::string(label) + " #" + std::to_string(dup + 1) : std::string(label));
    g_row_pane.push_back(g_pane);
    bool mark = g_next_mark, warn = g_next_warn;
    g_next_disabled = g_next_mark = g_next_warn = false;
    bool hov = ImGui::IsItemHovered(), foc = ImGui::IsItemFocused() && ImGui::GetCurrentContext()->NavCursorVisible;
    if(hov&&desc&&*desc&&ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))ImGui::SetTooltip("%s",desc);
    int step = clicked && !off ? 2 : 0;
    g_held = false;
    if (foc && !off) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft) ||
            ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            step = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight) ||
            ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            step = 1;
        for (ImGuiKey k : {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickLeft,
                           ImGuiKey_GamepadLStickRight, ImGuiKey_LeftArrow, ImGuiKey_RightArrow})
            if (ImGui::GetKeyData(k)->DownDuration > 1.0f) g_held = true;
        // left / right belong to the row (navigation would otherwise try to move sideways)
        for (ImGuiKey k : {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_LeftArrow, ImGuiKey_RightArrow})
            ImGui::SetKeyOwner(k, ImGui::GetItemID());
    }
    if (foc) g_desc_focus = desc ? desc : "";
    if (hov) g_desc_hover = desc ? desc : "";
    if (g_desc_first.empty() && desc) g_desc_first = desc;

    // V12.5 look (reference screenshots): a dark row with the setting's icon, its name and description on the left and
    // its control on the right - a switch (On / Off), a dropdown box, < value > steppers or a slider.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float r = 7 * g_scale, pad = 14 * g_scale, fs = ImGui::GetFontSize();
    int kind = g_next_kind;
    g_next_kind = ROW_TEXT;
    bool lit = hov || foc;
    ImU32 row_bg = lit ? IM_COL32(22, 58, 104, 245) : IM_COL32(10, 32, 64, 235);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), row_bg, r);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), lit ? (off ? IM_COL32(236, 194, 84, 120) : kGold)
                                         : mark ? IM_COL32(236, 194, 84, 130) : IM_COL32(70, 115, 170, off ? 60 : 130),
                r, 0, lit || mark ? 2.0f : 1.0f);
    ImU32 ink = off ? IM_COL32(120, 130, 140, 255) : IM_COL32(240, 246, 255, 255);
    float icon_w = fs * 2.3f;
    settings_look::row_icon(dl, ImVec2(p.x + icon_w * 0.5f + 2 * g_scale, p.y + h * 0.5f), fs * 0.62f, label, ink, row_bg);
    dl->AddLine(ImVec2(p.x + icon_w + 4 * g_scale, p.y + 6 * g_scale), ImVec2(p.x + icon_w + 4 * g_scale, p.y + h - 6 * g_scale),
                IM_COL32(70, 115, 170, 90), 1.0f);
    float tx = p.x + icon_w + 14 * g_scale, right = p.x + w - pad;
    // the control's width on the right (the name and description stop before it)
    float cw = kind == ROW_TOGGLE ? fs * 4.2f : kind == ROW_DROPDOWN ? fminf(fmaxf(w * 0.32f, ImGui::CalcTextSize(value.c_str()).x + fs * 2.6f), w * 0.48f) : kind == ROW_STEPPER ? fs * 8.5f
             : kind == ROW_SLIDER ? fminf(w * 0.36f, fs * 15) : kind == ROW_CHEVRON || kind == ROW_RADIO ? fs * 1.6f
             : value.empty() ? 0 : fminf(ImGui::CalcTextSize(value.c_str()).x + fs, w * 0.4f);
    float text_end = right - cw - fs;
    std::string t = label;
    bool has_desc = desc && *desc;
    float ly = has_desc ? p.y + h * 0.5f - fs * 1.02f : p.y + (h - fs) * 0.5f;
    dl->PushClipRect(ImVec2(tx, p.y), ImVec2(text_end, p.y + h), true);
    dl->AddText(ImVec2(tx, ly), off ? IM_COL32(120, 130, 140, 255) : IM_COL32(250, 252, 255, 255), t.c_str());
    if (has_desc)
        dl->AddText(ImGui::GetFont(), fs * 0.74f, ImVec2(tx, p.y + h * 0.5f + fs * 0.08f),
                    off ? IM_COL32(110, 120, 130, 255) : IM_COL32(175, 200, 228, 255), desc);
    dl->PopClipRect();
    float cy = p.y + h * 0.5f;
    ImU32 val_col = off ? IM_COL32(120, 130, 140, 255) : warn ? IM_COL32(240, 110, 90, 255)
                  : changed ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 230, 242, 255);
    ImU32 arrow_col = off ? IM_COL32(110, 120, 130, 255) : lit ? kGold : IM_COL32(235, 242, 255, 255);
    auto arrow = [&](float x, int dir) {  // a filled triangle, as in the reference (the fonts have no arrow glyphs)
        float a = fs * 0.32f;
        if (dir < 0) dl->AddTriangleFilled(ImVec2(x, cy), ImVec2(x + a, cy - a * 1.1f), ImVec2(x + a, cy + a * 1.1f), arrow_col);
        else dl->AddTriangleFilled(ImVec2(x + a, cy), ImVec2(x, cy - a * 1.1f), ImVec2(x, cy + a * 1.1f), arrow_col);
    };
    auto arrow_hit = [&](float x0, float x1) {  // a click on (or near) an arrow steps it
        float mx = ImGui::GetIO().MousePos.x, slack = fs * 0.4f;
        if (!clicked || off) return 0;
        if (mx >= x0 - slack && mx <= x0 + fs * 0.32f + slack) return -1;
        if (mx >= x1 - slack && mx <= x1 + fs * 0.32f + slack) return 1;
        return 0;
    };
    if (kind == ROW_TOGGLE) {
        bool on = value == "On";
        float ph = fs * 1.15f, pw = ph * 1.9f, px = right - fs * 1.9f - pw, py = cy - ph * 0.5f;
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), off ? IM_COL32(55, 65, 75, 255) : on ? IM_COL32(30, 200, 90, 255)
                                                                                                    : IM_COL32(28, 60, 100, 255), ph * 0.5f);
        if (!on) dl->AddRect(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(90, 140, 200, 200), ph * 0.5f, 0, 1.2f * g_scale);
        dl->AddCircleFilled(ImVec2(px + (on ? pw - ph * 0.5f : ph * 0.5f), cy), ph * 0.41f, IM_COL32(240, 250, 255, 255));
        dl->AddText(ImVec2(right - fs * 1.55f, cy - fs * 0.5f), off ? IM_COL32(120, 130, 140, 255) : on ? IM_COL32(255, 255, 255, 255)
                                                                                                  : IM_COL32(190, 205, 222, 255), value.c_str());
    } else if (kind == ROW_DROPDOWN) {
        float bh = fminf(h * 0.66f, fs * 1.8f), bx = right - cw;
        ImVec2 a(bx, cy - bh * 0.5f), b(right, cy + bh * 0.5f);
        dl->AddRectFilled(a, b, IM_COL32(6, 20, 42, 255), 6 * g_scale);
        dl->AddRect(a, b, lit ? kGold : IM_COL32(110, 160, 220, 200), 6 * g_scale, 0, 1.3f * g_scale);
        dl->PushClipRect(ImVec2(a.x + fs * 0.5f, a.y), ImVec2(b.x - fs * 1.6f, b.y), true);
        dl->AddText(ImVec2(a.x + fs * 0.6f, cy - fs * 0.5f), val_col, value.c_str());
        dl->PopClipRect();
        lao_icon::chevron(dl, ImVec2(b.x - fs * 0.8f, cy), fs * 0.28f, 2, arrow_col, 2.2f * g_scale);
    } else if (kind == ROW_STEPPER) {
        float x0 = right - cw, x1 = right - fs * 0.32f;
        arrow(x0, -1), arrow(x1, 1);
        float vw = ImGui::CalcTextSize(value.c_str()).x, mid = (x0 + x1 + fs * 0.32f) * 0.5f;
        dl->PushClipRect(ImVec2(x0 + fs * 0.6f, p.y), ImVec2(x1 - fs * 0.3f, p.y + h), true);
        dl->AddText(ImVec2(mid - vw * 0.5f, cy - fs * 0.5f), val_col, value.c_str());
        dl->PopClipRect();
        if (int s2 = arrow_hit(x0, x1)) step = s2;
    } else if (kind == ROW_SLIDER) {  // < bar > value - the bar itself is drawn (and dragged) by opt_slider
        float vw = fs * 3.0f, x1 = right - vw - fs * 0.6f, x0 = right - cw;
        arrow(x0, -1), arrow(x1, 1);
        g_slider_x0 = x0 + fs * 0.9f, g_slider_x1 = x1 - fs * 0.55f, g_slider_y = cy;
        float tw = ImGui::CalcTextSize(value.c_str()).x;
        dl->AddText(ImVec2(right - tw, cy - fs * 0.5f), val_col, value.c_str());
        if (int s2 = arrow_hit(x0, x1)) step = s2;
    } else if (kind == ROW_CHEVRON) {
        lao_icon::chevron(dl, ImVec2(right - fs * 0.4f, cy), fs * 0.3f, 1, arrow_col, 2.6f * g_scale);
        if (step == 1) step = 2;  // (Right opens it too)
    } else if (kind == ROW_TEXT && !value.empty()) {
        float vw = ImGui::CalcTextSize(value.c_str()).x;
        dl->PushClipRect(ImVec2(right - cw, p.y), ImVec2(right + 1, p.y + h), true);
        dl->AddText(ImVec2(right - vw, cy - fs * 0.5f), val_col, value.c_str());
        dl->PopClipRect();
    }
    (void)arrows, (void)chevron, (void)upper;
    if (dup) ImGui::PopID();
    ImGui::PopID();
    return step;
}

bool opt_toggle(const char *label, bool *v, bool def, const char *desc) {
    g_next_kind = ROW_TOGGLE;
    if (!row(label, *v ? "On" : "Off", *v != def, desc)) return false;  // (any step flips it)
    *v = !*v;
    return true;
}

bool opt_choice(const char *label, int *v, const char *const *names, int n, int def, const char *desc, bool wrap) {
    int cur = *v >= 0 && *v < n ? *v : 0;
    g_next_kind = ROW_DROPDOWN;
    int step = row(label, names[cur], cur != def, desc, true, true);
    bool selected=false;
    ImGui::PushID((int)ImGui::GetItemID());
    if(step==2)ImGui::OpenPopup("##choices");
    if(ImGui::BeginPopup("##choices")){
      for(int i=0;i<n;i++){if(ImGui::Selectable(names[i],i==cur)){*v=i;selected=i!=cur;}if(i==cur)ImGui::SetItemDefaultFocus();}
      ImGui::EndPopup();
    }
    ImGui::PopID();
    if(selected)return true;
    if (!step||step==2) return false;
    int nv = cur + step;
    nv = wrap ? (nv + n) % n : nv < 0 ? 0 : nv >= n ? n - 1 : nv;
    if (nv == cur) return false;
    *v = nv;return true;
}

bool opt_slider(const char *label, float *v, float lo, float hi, float step, float def, const char *fmt, const char *desc) {
    char buf[32];
    snprintf(buf, sizeof buf, fmt, *v);
    g_next_kind = ROW_SLIDER;
    int s = row(label, buf, fabsf(*v - def) > step * 0.01f, desc, true, true);
    float x=g_slider_x0,end=g_slider_x1,y=g_slider_y,fs=ImGui::GetFontSize();
    float t=(*v-lo)/(hi-lo);t=t<0?0:t>1?1:t;auto *dl=ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x,y-fs*.16f),ImVec2(end,y+fs*.16f),IM_COL32(20,55,95,255),fs*.16f);dl->AddRect(ImVec2(x,y-fs*.16f),ImVec2(end,y+fs*.16f),IM_COL32(70,120,180,160),fs*.16f);
    dl->AddRectFilled(ImVec2(x,y-fs*.16f),ImVec2(x+(end-x)*t,y+fs*.16f),last_row_disabled?IM_COL32(80,95,110,255):IM_COL32(30,220,130,255),fs*.16f);
    dl->AddCircleFilled(ImVec2(x+(end-x)*t,y),fs*.42f,IM_COL32(240,250,255,255));dl->AddCircle(ImVec2(x+(end-x)*t,y),fs*.42f,IM_COL32(20,120,80,200),20,1.2f*g_scale);
    static ImGuiID dragged=0;ImGuiID id=ImGui::GetItemID();
    float press_x=ImGui::GetIO().MouseClickedPos[0].x;bool on_bar=press_x>=x-fs*.15f&&press_x<=end+fs*.15f;  // (V12.5: a press on an arrow steps, it doesn't drag)
    if(!last_row_disabled&&ImGui::IsItemActive()&&ImGui::IsMouseDown(0)&&on_bar){dragged=id;float f=(ImGui::GetIO().MousePos.x-x)/(end-x);f=f<0?0:f>1?1:f;float next=lo+roundf((hi-lo)*f/step)*step;if(next!=*v){*v=next;return true;}}
    if(dragged==id&&ImGui::IsMouseReleased(0)){dragged=0;return false;}
    if (!s) return false;
    float eps = step * 0.01f, n = roundf(*v / step) * step + (s == 2 ? step : s * step);
    if (s == 2 && n > hi + eps) n = lo;  // (A / click past the top wraps to the bottom; left / right stop at the ends)
    *v = n < lo ? lo : n > hi ? hi : n;
    return true;
}

bool opt_int(const char *label, int *v, int lo, int hi, int step, int def, const char *fmt, const char *desc,
             const char *lo_name) {
    char buf[48];
    if (lo_name && *v == lo) snprintf(buf, sizeof buf, "%s", lo_name);
    else snprintf(buf, sizeof buf, fmt, *v);
    g_next_kind = ROW_STEPPER;
    int s = row(label, buf, *v != def, desc, true, true);
    if (!s) return false;
    int d = s == 2 ? step : s * step * (g_held && hi - lo > 40 * step ? 10 : 1);
    int n = *v + d;
    if (s == 2 && n > hi) n = lo;  // (A / click past the top wraps; left / right stop at the ends)
    n = n < lo ? lo : n > hi ? hi : n;
    if (n == *v) return false;
    *v = n;
    return true;
}

bool opt_button(const char *label, const char *desc) { g_next_kind = ROW_CHEVRON; return row(label, "", false, desc) == 2; }

bool opt_radio(const char *label, bool selected, const char *desc) {
    bool off = g_next_disabled;last_row_disabled=off;
    g_next_kind = ROW_RADIO;
    bool chosen = row(label, "", false, desc) == 2;
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    float r = ImGui::GetFontSize() * 0.32f, cx = b.x - 16 * g_scale - r, cy = (a.y + b.y) * 0.5f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 col = off ? IM_COL32(150, 150, 140, 255) : selected ? kGold : IM_COL32(205, 205, 195, 255);
    dl->AddCircle(ImVec2(cx, cy), r, col, 0, 2.0f);
    if (selected) dl->AddCircleFilled(ImVec2(cx, cy), r * 0.55f, col);
    return chosen && !selected;
}

bool opt_action(const char *label, const char *value, const char *desc, bool upper, bool changed) {
    return row(label, value ? value : "", changed, desc, upper) == 2;
}

bool opt_colour(const char *label, CosColour *c, int slot, const char *desc) {
    int prev = c->pick;
    const char *names[N_COL];
    for (int i = 0; i < N_COL; i++) names[i] = g_col_names[i];
    char seed[48];
    int sp = col_pick(c, slot);  // (Default: the seed's own colour, shown as such)
    if (c->pick == COL_DEFAULT && sp != COL_DEFAULT) snprintf(seed, sizeof seed, "Seed: %s", g_col_names[sp]), names[0] = seed;
    bool changed = opt_choice(label, &c->pick, names, N_COL, COL_DEFAULT, desc);
    if (changed && c->pick == COL_CUSTOM && prev != COL_CUSTOM)  // (Custom starts from the colour it was on)
        c->rgb = prev > COL_DEFAULT && prev < COL_RANDOM ? g_col_rgb[prev] : (c->rgb ? c->rgb : 0xF2F0EA);
    if (c->pick != COL_CUSTOM) return changed;
    ImGui::PushID(label);
    char hex[16];
    snprintf(hex, sizeof hex, "#%06X", (unsigned)(c->rgb & 0xFFFFFF));
    opt_indent(1);
    bool open = opt_action("Custom color", hex, "Any color: pick its hue, saturation and brightness.", true, true);
    opt_indent(-1);
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    {   // a swatch of it, left of the value
        float fs = ImGui::GetFontSize(), sw = fs * 1.1f, vw = ImGui::CalcTextSize(hex).x;
        float x1 = mx.x - 16 * g_scale - vw - fs * 0.5f, y0 = (mn.y + mx.y - sw) * 0.5f;
        ImU32 col = IM_COL32(c->rgb >> 16 & 255, c->rgb >> 8 & 255, c->rgb & 255, 255);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x1 - sw, y0), ImVec2(x1, y0 + sw), col, 4 * g_scale);
        ImGui::GetWindowDrawList()->AddRect(ImVec2(x1 - sw, y0), ImVec2(x1, y0 + sw), IM_COL32(255, 255, 255, 90),
                                            4 * g_scale);
    }
    static int test_open = -1;  // LARECOMP_OPEN_PICKER=1 (test): the first Custom row's picker opens by itself
    if (test_open < 0) test_open = getenv("LARECOMP_OPEN_PICKER") != NULL;
    if (test_open) test_open = 0, open = true;
    if (open) ImGui::OpenPopup("##picker");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g_scale, 14 * g_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9 * g_scale);
    ImGui::PushStyleColor(ImGuiCol_Border, kGold);
    ImGui::SetNextWindowPos(ImVec2(mx.x + 12 * g_scale, mn.y), ImGuiCond_Appearing);  // (beside the row, over the description)
    if (ImGui::BeginPopup("##picker")) {
        float f[3] = {(c->rgb >> 16 & 255) / 255.0f, (c->rgb >> 8 & 255) / 255.0f, (c->rgb & 255) / 255.0f};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11);
        if (ImGui::ColorPicker3("##colour", f, ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview |
                                                   ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoAlpha)) {
            c->rgb = (uint32_t)lroundf(f[0] * 255) << 16 | (uint32_t)lroundf(f[1] * 255) << 8 | (uint32_t)lroundf(f[2] * 255);
            changed = true;
        }
        if (ImGui::Button("Done") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

bool opt_link(const char *label, const char *desc) { g_next_kind = ROW_CHEVRON; return row(label, "", false, desc, true, false, true) == 2; }

static ImGuiID g_text_editing;  // the text row turned into a field (one at a time)
bool opt_text_editing(const char *label) { return g_text_editing && g_text_editing == ImGui::GetID(label); }

bool opt_text(const char *label, char *buf, size_t cap, const char *hint, const char *desc) {
    ImGuiID &editing = g_text_editing;
    static int focus;        // frames left to give it the keyboard focus
    ImGuiID id = ImGui::GetID(label);
    if (editing != id) {
        if (row(label, buf[0] ? buf : (hint ? hint : ""), false, desc) == 2) editing = id, focus = 2;
        return false;
    }
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-1);
    if (focus > 0) ImGui::SetKeyboardFocusHere(), focus--;
    bool done = ui_input_text("##text", buf, cap, ImGuiInputTextFlags_EnterReturnsTrue, hint, label);
    if (ImGui::IsItemFocused()) g_desc_focus = desc ? desc : "";
    if (done || ui_input_text_cancelled() || (focus == 0 && !ImGui::IsItemActive() && !osk_is_open())) editing = 0;
    ImGui::PopID();
    return done;
}

// the left pane's end: its child window, and the description the right pane shows
static void left_end(void) {
    ImGui::PopStyleVar();
    if (const char *t = getenv("LARECOMP_OPT_SCROLL")) ImGui::SetScrollY(ImGui::GetScrollMaxY() * (float)atof(t));  // (test)
    ImGui::EndGroup();
    // the description: the hovered row (mouse) wins, else the selected one (controller), else the last shown
    if (!g_desc_hover.empty()) g_desc_shown = g_desc_hover;
    else if (!g_desc_focus.empty()) g_desc_shown = g_desc_focus;
    else if (g_desc_shown.empty()) g_desc_shown = g_desc_first;
}

// the right pane's child window, after a gold line between the two
static void right_begin(const char *id, ImGuiWindowFlags flags) {
    ImGui::SameLine(0, 0);
    ImVec2 p = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    float gap = 24 * g_scale;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + gap * 0.5f, p.y), ImVec2(p.x + gap * 0.5f, p.y + avail.y),
                                        IM_COL32(236, 194, 84, 110), 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + gap, p.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g_scale, 6 * g_scale));
    ImGui::BeginChild(id, ImVec2(0, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding, flags);
    ImGui::PopStyleVar();
}

void opt_desc_text(void) {
    ImGui::SetWindowFontScale(1.3f);  // (the description reads at a glance)
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(g_desc_shown.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SetWindowFontScale(1.0f);
}

void opt_end(void) {
    left_end();

    ImGui::PopID();
}

void opt_split(void) {
    left_end();
    ImGui::Separator();ImGui::BeginGroup();
    g_pane = 1;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 4 * g_scale));  // (as the left)
    g_indent = 0;
}

void opt_split_end(void) {
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::PopID();
}
