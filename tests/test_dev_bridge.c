#include <windows.h>
#include <assert.h>
#include <string.h>
#include "../runtime/hle/laonline_dev_events.c"
const GameAddrs *g_ga;Settings g_settings;static GameVersion ver={.key="v1_0_0"};static int in_play=1,verified,persisted,failed,changed,flushed;static uint64_t seen_transaction;
const GameVersion *game_version(void){return &ver;}int game_in_play(void){return in_play;}const char *rando_seed_dir(void){return NULL;}
bool flags_ext_get(int i){return i<2048&&(LD8(GA(LIVE_SAVE)+0x5dc0+i/8)>>(i%8)&1);}
void dev_events_context(const char *a,const char *b,const char *c){(void)a;(void)b;(void)c;}
void dev_events_flush(void){flushed++;}
void dev_events_emit(const char *cat,const char *event,const char *subject,int id,int index,int64_t before,int64_t after,const char *result,const char *origin,uint64_t transaction){(void)cat;(void)subject;(void)id;(void)index;(void)before;(void)after;(void)result;(void)origin;if(!strcmp(event,"INVENTORY_VERIFIED")){verified++;seen_transaction=transaction;}if(!strcmp(event,"SAVE_PERSISTENCE_VERIFIED")&&transaction){persisted++;assert(transaction==seen_transaction);}if(!strcmp(event,"SAVE_PERSISTENCE_FAILED"))failed++;if(!strcmp(event,"FLAG_CHANGED_OBSERVED"))changed++;}
int main(){assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1000};g_ga=&ga;unsigned char *s=(void*)(uintptr_t)GA(LIVE_SAVE);ST32(GA(LIVE_SAVE),0xe);s[0x5388]='L';lao_dev_tick(0);lao_dev_item(6,1,-1,0);ST32(GA(LIVE_SAVE)+0x5568,1u<<4);lao_dev_tick(6);assert(verified==1&&persisted==0);lao_dev_save("savedata-test.bin",0);assert(failed==1&&persisted==0);FILE *f=fopen("savedata-test.bin","wb");assert(f);fwrite(s,1,0x5ee0,f);fclose(f);lao_dev_save("savedata-test.bin",1);assert(persisted==1&&flushed==2);ST8(GA(LIVE_SAVE)+0x5dc0+100/8,1u<<(100%8));lao_dev_tick(12);assert(changed==1);remove("savedata-test.bin");puts("PASS native ownership readback, pending-before-save, failed-save retained, committed-file readback, transaction correlation and exact flag delta");}
