#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <direct.h>
#include "../runtime/dev_events.c"
const char *rt_data_root(void){const char *s=getenv("TEST_LOG_ROOT");return s?s:".";}
char *SDL_GetPrefPath(const char *a,const char *b){(void)a;(void)b;return strdup("fallback/");}
void SDL_free(void *p){free(p);}
static LONG WINAPI test_fault(EXCEPTION_POINTERS *p){(void)p;dev_events_crash();return EXCEPTION_EXECUTE_HANDLER;}
int main(int argc,char **argv){(void)argv;_mkdir("fallback");dev_events_init();dev_events_context("VANILLA","Tester\"name","seed-test");for(int i=0;i<100;i++)dev_events_emit("ITEM","ITEM_PICKUP_OBSERVED","Hookshot",6,-1,0,1,"SAVE_PENDING","LOCAL",i+1);dev_events_context("STORY CO-OP","Tester","none");dev_events_emit("SAVE","SAVE_COMMITTED","save",0,0,0,1,"SUCCESS","LOCAL",0);dev_events_context("CO-OP RANDOMIZER","Tester","seed");dev_events_emit("ITEM","INVENTORY_VERIFIED","Map",45,2,0,1,"SAVE_PENDING","REMOTE",101);dev_events_flush();if(argc>1&&!strcmp(argv[1],"crash")){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(test_fault);RaiseException(0xe0000001,0,0,NULL);}if(argc>1)_exit(0);return 0;}
