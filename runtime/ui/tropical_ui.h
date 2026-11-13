#pragma once
#include "imgui.h"
bool tropical_image(const char *name,ImTextureID *id,float *aspect);
void tropical_header(ImVec2 pos,ImVec2 size,float scale);
bool tropical_card(const char *id,const char *art,const char *title,const char *credit,const char *desc,ImVec2 size,ImU32 accent);
void tropical_update_draw(float scale);
bool tropical_update_prompt(void);
bool tropical_update_required(void);       // V14.1: a newer LA-Online release is out (online play needs it)
void tropical_update_required_note(bool on);  // V14.1: show "update to keep playing online" in the update window  // V13: true once per launch when a newer LA-Online release can be installed
void tropical_guide_draw(bool *open,float scale);
void tropical_style(void);
bool tropical_tile(const char *id,const char *art,const char *title,const char *description,ImVec2 size,ImU32 accent,int icon);
void tropical_credits(float scale);
bool tropical_utility(const char *title,const char *subtitle,ImVec2 size,int icon);
void tropical_fonts_init(void);
ImFont *tropical_body_font(void);
ImFont *tropical_heading_font(void);

void tropical_update_notice();

int tropical_global(float scale,const char *count_text,bool enabled,const char *seed_text);
void tropical_global_rando_open(bool open);  // V14.1: chevron of the Randomizer door (its seed panel open)  // V12: 0 none, 1 Global Vanilla, 2 Global Randomizer
void tropical_whats_new(float scale);  // V12 homepage changelog

bool tropical_home_updates(ImVec2 display_size,float scale);
