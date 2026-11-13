#include <windows.h>
#include <assert.h>
#include <stdlib.h>
#include "../runtime/hle/keysanity.c"
const GameAddrs *g_ga;static char seeddir[256];static int has_seed=1;static unsigned char fl[256];
const char *rando_seed_dir(void){return has_seed?seeddir:NULL;}
bool flags_ext_get(int i){return (fl[i>>3]>>(i&7))&1;}void flags_put(int i,bool on){if(on)fl[i>>3]|=1u<<(i&7);else fl[i>>3]&=~(1u<<(i&7));}
int triforce_add_item(int a,int b){(void)a;(void)b;return 0;}int rt_shop_add_item(Cpu*c){(void)c;return 0;}int rt_pet_item(int n){(void)n;return 0;}
void rt_silent_add(Cpu*c){(void)c;}uint64_t lao_dev_item_begin(int a,int b,int c){(void)a;(void)b;(void)c;return 0;}void lao_dev_item_end(uint64_t t,int a,int b,int c,int d){(void)t;(void)a;(void)b;(void)c;(void)d;}void laonline_coop_pickup(int a,int b,int c){(void)a;(void)b;(void)c;}
#define INV (*(uint32_t*)(uintptr_t)(GA(LIVE_SAVE)+0x5568))
static void cfg(int bombs,int powder){FILE*f=fopen("/tmp/claude-0/v12/tests/seed/settings.json","w");fprintf(f,"{\"shuffle-bombs\": %s, \"shuffle-powder\": %s}",bombs?"true":"false",powder?"true":"false");fclose(f);g_bag_bombs=g_bag_powder=-1;INV=0;memset(fl,0,sizeof fl);}
static Cpu add(int item,int count,int index){Cpu c={0};c.x[0]=item;c.x[1]=count;c.x[2]=(uint64_t)(int64_t)index;rt_add_item(&c);return c;}
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220};g_ga=&ga;system("mkdir -p /tmp/claude-0/v12/tests/seed");snprintf(seeddir,sizeof seeddir,"/tmp/claude-0/v12/tests/seed");
 for(int bb=0;bb<2;bb++)for(int pb=0;pb<2;pb++){
  cfg(bb,pb);
  Cpu c=add(4,3,80);                     /* pot/bush bomb refill */
  assert(bb? (c.x[0]==102 && !(INV&4)) : (c.x[0]==4 && (INV&4)));
  c=add(11,3,80);                         /* pot/bush powder refill */
  assert(pb? (c.x[0]==102 && !(INV&256)) : (c.x[0]==11 && (INV&256) && flags_ext_get(295)));
  if(bb){INV&=~4u;add(4,10,-1);assert(!(INV&4));}       /* shop bombs with the bag required: unchanged (game's rule) */
  else{INV&=~4u;add(4,10,-1);assert(INV&4);}             /* shop bombs, bag not required: unlocked */
  c=add(60,3,80);assert(c.x[0]==102);                     /* arrows still need the bow */
  INV|=8;c=add(60,3,80);assert(c.x[0]==60);
  printf("PASS config bombs_bag=%d powder_bag=%d\n",bb,pb);
 }
 has_seed=0;g_bag_bombs=-1;INV=0;Cpu c=add(4,3,80);assert(c.x[0]==102&&!(INV&4));add(4,10,-1);assert(!(INV&4));
 puts("PASS vanilla (no seed): unchanged");return 0;}
