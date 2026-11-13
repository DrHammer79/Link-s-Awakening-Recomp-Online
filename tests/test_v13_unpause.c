/* V13: remote grants wait until the pause / save menu has really closed (Velvet LAO-5KYCR "Can't unpause"). */
#include <windows.h>
#include "../runtime/hle/laonline_ui.c"
const GameAddrs *g_ga;Settings g_settings;static uint64_t frame=1000;
uint64_t renderer_present_count(void){return frame;}int game_in_play(void){return 1;}int laonline_level_ready(void){return 1;}
int laonline_players(int a,char *b,int c,char *d,int e,uint8_t *f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 1;}
const GameVersion *game_version(void){return NULL;}  /* registry walk skipped: paused stays as set below */
static int diag_n;void laonline_diag(const char *e,uint16_t a,uint16_t b,uint64_t c,uint64_t d,int r){(void)e;(void)a;(void)b;(void)c;(void)d;(void)r;diag_n++;}
void laonline_death_rearm_alive(void){}
static void step(int paused){pthread_mutex_lock(&ui_lock);int ch=ui_frame&&paused!=ui_paused;if(paused)gameplay_since=0;else if(!gameplay_since)gameplay_since=frame;ui_frame=frame;ui_paused=paused;pthread_mutex_unlock(&ui_lock);if(ch)diag_n++;}
int main(void){
 for(int i=0;i<100;i++){frame++;step(1);}
 frame++;step(0);
 printf("%-60s %s\n","menu just closed: grants wait",!laonline_ui_settled(45)?"PASS":"FAIL");
 for(int i=0;i<20;i++){frame++;step(0);} frame++;step(1);frame++;step(0);  /* menu flickers back (closing animation) */
 for(int i=0;i<30;i++){frame++;step(0);}
 printf("%-60s %s\n","menu flickered during close: timer restarted",!laonline_ui_settled(45)?"PASS":"FAIL");
 for(int i=0;i<20;i++){frame++;step(0);}
 printf("%-60s %s\n","45 calm frames later: grants allowed",laonline_ui_settled(45)?"PASS":"FAIL");
 frame+=20;  /* game thread stopped ticking the UI (e.g. loading) */
 printf("%-60s %s\n","UI state stale: grants wait",!laonline_ui_settled(45)?"PASS":"FAIL");
 return 0;
}
