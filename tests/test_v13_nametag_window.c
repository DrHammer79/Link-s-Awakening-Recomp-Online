#include <stdio.h>
#include <stdint.h>
#include "../runtime/hle/laonline_diag.c"
void dev_events_emit(const char*a,const char*b,const char*c,int d,int e,int64_t f,int64_t g,const char*h,const char*i,uint64_t j){}
int main(){int f=0;laonline_remote_draw_recorded(1,100);
 f|=!laonline_remote_draw_seen(1,100);f|=!laonline_remote_draw_seen(1,106);f|=!laonline_remote_draw_seen(1,130);f|=laonline_remote_draw_seen(1,131);
 f|=!laonline_remote_draw_seen(1,98);f|=laonline_remote_draw_seen(2,100);f|=laonline_remote_draw_recent(1,106);
 printf("draw_seen window: %s\n",f?"FAIL":"PASS");return f;}
