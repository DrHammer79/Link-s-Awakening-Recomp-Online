#include "laonline_story.h"
#include "laonline_dev_events.h"
#include "../dev_events.h"
#include "../hle.h"
#include <pthread.h>
#include <string.h>
extern bool flags_ext_get(int);
extern void flags_put(int,bool);
static pthread_mutex_t mx=PTHREAD_MUTEX_INITIALIZER;
static unsigned local_mask,remote_mask,last_sent;
static int queried,capable,have_seed,sent,blocked;
static uint8_t world[32];
void laonline_story_transport(void){pthread_mutex_lock(&mx);local_mask=remote_mask=last_sent=0;queried=capable=have_seed=sent=blocked=0;memset(world,0,sizeof world);pthread_mutex_unlock(&mx);}
RT_ON_RESET(laonline_story_reset){laonline_story_transport();}
void laonline_story_capability(void){pthread_mutex_lock(&mx);capable=1;pthread_mutex_unlock(&mx);}
void laonline_story_tick(void){
 unsigned proposed;pthread_mutex_lock(&mx);if(blocked){pthread_mutex_unlock(&mx);return;}proposed=remote_mask;pthread_mutex_unlock(&mx);
 unsigned observed=0;
 for(unsigned i=0;i<LAO_STORY_COUNT;i++){
  int flag=lao_story_flags[i],before=flags_ext_get(flag);
  if(!before && (proposed&(1u<<i))){
   flags_put(flag,true);int after=flags_ext_get(flag);
   dev_events_emit("STORY_FLAG",after?"STORY_UNLOCK_APPLIED":"STORY_UNLOCK_REJECTED","Verified permanent entrance unlock",flag,1,before,after,after?"NATIVE_FLAG_VERIFIED_SAVE_PENDING":"NATIVE_READBACK_FAILED","EXACT_WORLD_SERVER_RECEIPT",0);
  }
  if(flags_ext_get(flag))observed|=1u<<i;
 }
 pthread_mutex_lock(&mx);local_mask|=observed;pthread_mutex_unlock(&mx);
}
int laonline_story_packet(uint8_t *out,int capacity,int ready,const uint8_t seed[32]){
 if(!out||!seed||capacity<38)return 0;int n=0;pthread_mutex_lock(&mx);
 /* Changing worlds requires the existing transport/reset handshake. Never
  * publish a previous world's sampled entrance flags under a new digest. */
 if(ready&&have_seed&&memcmp(world,seed,32)){blocked=1;remote_mask=0;}
 if(blocked){pthread_mutex_unlock(&mx);return 0;}
 if(ready&&!have_seed){memcpy(world,seed,32);have_seed=1;remote_mask=0;sent=0;}
 if(!queried){out[0]=5;out[1]=0;out[2]=7;uint32_t q=LAO_STORY_QUERY;memcpy(out+3,&q,4);queried=1;n=7;}
 else if(ready&&capable&&have_seed&&(!sent||last_sent!=local_mask)){out[0]=36;out[1]=0;out[2]=LAO_STORY_SEND;out[3]=1;memcpy(out+4,world,32);out[36]=(uint8_t)local_mask;out[37]=0;last_sent=local_mask;sent=1;n=38;}
 pthread_mutex_unlock(&mx);return n;
}
void laonline_story_receive(const uint8_t *b,size_t n){
 if(!b||n!=36||b[0]!=LAO_STORY_RECEIVE||b[1]!=1||b[35]||(b[34]&~LAO_STORY_MASK))return;
 pthread_mutex_lock(&mx);if(!blocked&&capable&&have_seed&&!memcmp(world,b+2,32))remote_mask|=b[34];pthread_mutex_unlock(&mx);
}
