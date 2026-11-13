#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/settings.h"
#include "../runtime/input_bind.h"
int16_t g_bind_pad[BIND_PAD_COUNT],g_bind_key[BIND_KEY_COUNT];
void bind_settings_save(FILE *f){(void)f;}
bool bind_settings_load(const char *a,const char*b){(void)a;(void)b;return false;}
int main(void){
 Settings initial=g_settings;g_bind_pad[0]=4;g_bind_key[0]=7;
 settings_edit_begin();g_settings.volume=.3f;g_settings.laonline_death_notices=false;g_bind_pad[0]=9;g_bind_key[0]=10;
 settings_save();FILE *f=fopen("ui-transaction.ini","rb");assert(!f);
 strcpy(g_settings.laonline_rando_seed,"KeepGeneratedWorld");settings_edit_cancel();
 assert(g_settings.volume==initial.volume&&g_settings.laonline_death_notices==initial.laonline_death_notices);assert(g_bind_pad[0]==4&&g_bind_key[0]==7);assert(!strcmp(g_settings.laonline_rando_seed,"KeepGeneratedWorld"));
 settings_edit_begin();g_settings.volume=.6f;g_settings.dpad_shortcuts=true;g_settings.dpad_up=9;g_settings.dpad_down=2;g_settings.dpad_left=4;g_settings.dpad_right=3;settings_edit_commit();f=fopen("ui-transaction.ini","rb");assert(f);char data[16000];size_t n=fread(data,1,sizeof(data)-1,f);data[n]=0;fclose(f);assert(strstr(data,"volume = 0.60"));assert(strstr(data,"dpad_up = 9"));g_settings.dpad_up=0;settings_load();assert(g_settings.dpad_up==9&&g_settings.dpad_down==2&&g_settings.dpad_left==4&&g_settings.dpad_right==3&&g_settings.dpad_shortcuts);
 settings_edit_begin();g_settings.volume=.2f;settings_edit_cancel();assert(g_settings.volume==.6f);
 Settings d;settings_defaults(&d);assert(d.volume==1.f&&d.laonline_death_notices);remove("ui-transaction.ini");puts("PASS settings preview, deferred persistence, cancel, bindings, seed identity, commit and defaults");return 0;
}

