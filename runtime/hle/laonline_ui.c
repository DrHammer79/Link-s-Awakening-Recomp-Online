/* V14 read-only presentation state. No actor, cosmetic, animation or save writes. */
#include "../game_addresses.h"
#include "../hle.h"
#include "../settings.h"
#include <pthread.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
extern uint64_t renderer_present_count(void);
extern int game_in_play(void),laonline_level_ready(void);
extern int laonline_players(int,char *,int,char *,int,uint8_t *);
static pthread_mutex_t ui_lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t ui_frame;
static int ui_paused=1;
static uint64_t gameplay_since;  /* V13: first frame of the current gameplay stretch (0 while a menu is up) */
#ifdef _WIN32
static int ui_read(uint64_t at,void *out,size_t size) {
    SIZE_T got=0;
    return at && ReadProcessMemory(GetCurrentProcess(),(void *)(uintptr_t)at,out,size,&got) && got==size;
}
static uint64_t ui_ptr(uint64_t at) { uint64_t p=0;ui_read(at,&p,8);return p; }
static int ui_heap(uint64_t at) { return at>=HEAP_BASE && at+0x100<HEAP_BASE+HEAP_MAX && !(at&7); }
#endif
void laonline_ui_tick(uint64_t frame) {
    if(!g_settings.dpad_shortcuts&&laonline_players(-1,NULL,0,NULL,0,NULL)<0) return;
    int paused=1;
#ifdef _WIN32
    const GameVersion *v=game_version();
    if(g_ga && v && !strcmp(v->key,"v1_0_0")) {
        /* Native UI registry traversed by main+e789b0/e79560 on verified 1.0.0:
         * sentinel main+1c97b10, next+8, page+10. Live map checked with state02,
         * page vtable main+1c281b0 and root N_Map_00. Registry reads stay on game thread. */
        uint64_t head=MAIN_BASE+0x1c97b10,node=ui_ptr(head+8);
        paused=0;
        for(unsigned i=0;node && node!=head && i<128;++i) {
            if(!ui_heap(node)) {paused=1;break;}
            uint64_t page=ui_ptr(node+0x10),root=ui_ptr(page+0x50);
            uint8_t state=0;char name[48]={0};
            if(ui_heap(page) && ui_heap(root) && ui_read(page+0xfd,&state,1) &&
               ui_read(root+0xb0,name,sizeof name-1) &&
               (state==1 || state==2 || state==0x10 || state==0x0f) &&
               (!strncmp(name,"N_",2) || !strcmp(name,"RootPane")) && strcmp(name,"N_HUD_00")) paused=1;
            uint64_t next=ui_ptr(node+8);if(next==node) {paused=1;break;}node=next;
        }
    }
#endif
#ifdef _WIN32
    if(!paused&&g_ga&&game_in_play()&&laonline_level_ready()){uint64_t ps=ui_ptr(GA(PLAYER_STATUS_PTR));int16_t hp=0;if(((ps>=MAIN_BASE&&ps+0x100<SDK_BASE)||ui_heap(ps))&&ui_read(ps+0x84,&hp,2)&&hp>0){extern void laonline_death_rearm_alive(void);laonline_death_rearm_alive();}}
#endif
    pthread_mutex_lock(&ui_lock);
    int changed=ui_frame && paused!=ui_paused;
    if(changed) LOG("[laonline] V14 pause/menu presentation %s\n",paused?"hidden":"gameplay");
    if(paused) gameplay_since=0; else if(!gameplay_since) gameplay_since=frame;
    ui_frame=frame;ui_paused=paused;
    pthread_mutex_unlock(&ui_lock);
    /* V13 (Velvet LAO-5KYCR "Can't unpause"): pause / menu changes go into bug reports, so the next report shows them */
    if(changed){extern void laonline_diag(const char *,uint16_t,uint16_t,uint64_t,uint64_t,int);laonline_diag(paused?"ui.menu_open":"ui.gameplay",0,0,0,0,(int)(frame&0x7fffffff));}
}
/* V13: true once gameplay has been back for `frames` presented frames in a row. Remote grants wait for this, so
 * nothing is handed to the game while its pause / save menu is still closing (a menu page in a closing state is not
 * counted as "paused" by the registry walk above). */
bool laonline_ui_settled(unsigned frames) {
    pthread_mutex_lock(&ui_lock);
    uint64_t now=renderer_present_count();
    bool ok=!ui_paused && ui_frame && now>=ui_frame && now-ui_frame<=8 && gameplay_since && now>=gameplay_since && now-gameplay_since>=frames;
    pthread_mutex_unlock(&ui_lock);
    return ok;
}
bool laonline_ui_paused(void) {
    pthread_mutex_lock(&ui_lock);
    uint64_t now=renderer_present_count();
    bool paused=ui_paused || !ui_frame || now<ui_frame || now-ui_frame>8;
    pthread_mutex_unlock(&ui_lock);
    return paused;
}
