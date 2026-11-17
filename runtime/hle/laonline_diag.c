#include "laonline_session.h"
#include "../laonline_build.h"
#ifndef _CRT_RAND_S
#define _CRT_RAND_S
#endif
#include <stdlib.h>
#include "laonline_diag.h"
#include "../dev_events.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
static atomic_uint_fast64_t remote_draw[LAO_PLAYERS],remote_missing_since[LAO_PLAYERS],remote_probe_at[LAO_PLAYERS];
void laonline_remote_draw_recorded(int id,uint64_t frame){if(id>=0&&id<LAO_PLAYERS)atomic_store_explicit(&remote_draw[id],frame,memory_order_relaxed);}
void laonline_remote_draw_reset(int id){if(id>=0&&id<LAO_PLAYERS){atomic_store(&remote_draw[id],0);atomic_store(&remote_missing_since[id],0);atomic_store(&remote_probe_at[id],0);}}
/* V13 (Velvet's video: the name tag blinked on for single frames): the overlay is drawn a few frames after the
 * frame that recorded the puppet, so "drawn within 2 frames" was false most of the time. The tag now stays while the
 * puppet was drawn within the last half second; the 2-frame test is kept for the stall recovery below. */
int laonline_remote_draw_seen(int id,uint64_t frame){if(id<0||id>=LAO_PLAYERS)return 0;uint64_t at=atomic_load_explicit(&remote_draw[id],memory_order_relaxed);return at && frame+4>=at && (frame<at || frame-at<=30);}
int laonline_remote_draw_recent(int id,uint64_t frame){if(id<0||id>=LAO_PLAYERS)return 0;uint64_t at=atomic_load_explicit(&remote_draw[id],memory_order_relaxed);return at && frame>=at && frame-at<=2;}
// Render thread supplies only expected on-screen, nearby actors during live gameplay.
// Game thread may recover a stalled native incarnation, never alter model/render state.
void laonline_remote_draw_probe(int id,uint64_t frame,uint64_t ms,int expected){
 if(id<0||id>=LAO_PLAYERS)return;
 if(!expected||laonline_remote_draw_recent(id,frame)){atomic_store(&remote_missing_since[id],0);atomic_store(&remote_probe_at[id],0);return;}
 uint64_t last=atomic_load(&remote_probe_at[id]);
 if(!last||ms<last||ms-last>250)atomic_store(&remote_missing_since[id],ms);
 atomic_store(&remote_probe_at[id],ms);
}
int laonline_remote_draw_stalled(int id,uint64_t ms){
 if(id<0||id>=LAO_PLAYERS)return 0;uint64_t start=atomic_load(&remote_missing_since[id]),last=atomic_load(&remote_probe_at[id]);
 return start&&ms>=last&&ms-last<=250&&ms>=start&&ms-start>=2500;
}
// Whitelisted event labels and numeric identities only: no chat text, endpoint,
// password, file paths or report contents enter this bounded diagnostic stream.
static pthread_mutex_t mx=PTHREAD_MUTEX_INITIALIZER;
static char identity_name[100]="Player",identity_code[10]="";
static char ring[128][512];static unsigned next,used;
static char seed_diagnostic[1900];
static void seed_escape(const char *in,char *out,size_t cap){size_t n=0;for(size_t i=0;in&&in[i]&&n+3<cap;i++){unsigned char c=in[i];if(c<32)continue;if(c=='"'||c=='\\')out[n++]='\\';out[n++]=c;}out[n]=0;}
void laonline_diag_seed(const char *stage,const char *host_id,const char *client_id,const char *host_hash,const char *client_hash,const char *folder,const char *reason){
 char st[90],hi[200],ci[200],hh[140],ch[140],fo[300],re[300];
 seed_escape(stage,st,sizeof st);seed_escape(host_id,hi,sizeof hi);seed_escape(client_id,ci,sizeof ci);seed_escape(host_hash,hh,sizeof hh);seed_escape(client_hash,ch,sizeof ch);seed_escape(folder,fo,sizeof fo);seed_escape(reason,re,sizeof re);
 pthread_mutex_lock(&mx);
 snprintf(seed_diagnostic,sizeof seed_diagnostic,"{\"build\":\"HF3-SEED-REPAIR\",\"event\":\"seed.validation\",\"log_code\":\"%s\",\"stage\":\"%s\",\"host_id\":\"%s\",\"client_id\":\"%s\",\"host_world_sha256\":\"%s\",\"client_world_sha256\":\"%s\",\"active_folder\":\"%s\",\"generator\":\"see_seed_identity_record\",\"reason\":\"%s\"}",identity_code,st,hi,ci,hh,ch,fo,re);
 FILE *f=fopen("laonline-events.jsonl","ab");if(f){fprintf(f,"%s\n",seed_diagnostic);fclose(f);}fprintf(stderr,"[laonline] %s\n",seed_diagnostic);pthread_mutex_unlock(&mx);
}
static uint64_t window_at;static unsigned window_count;
void laonline_diag_identity(const char *name){
 pthread_mutex_lock(&mx);
 if(!identity_code[0]){
  const char *given=getenv("LAO_LOG_CODE");const char *alphabet="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";int valid=given&&strlen(given)==9&&!strncmp(given,"LAO-",4);
  if(valid)for(int i=4;i<9;i++)if(!strchr(alphabet,given[i]))valid=0;
  if(valid)snprintf(identity_code,sizeof identity_code,"%s",given);
  else {
   unsigned value=0;int ok=0;
#ifdef _WIN32
   ok=rand_s(&value)==0;
#else
   FILE *random=fopen("/dev/urandom","rb");if(random){ok=fread(&value,sizeof value,1,random)==1;fclose(random);}
#endif
   if(ok){memcpy(identity_code,"LAO-",4);for(int i=4;i<9;i++){identity_code[i]=alphabet[value&31];value>>=5;}identity_code[9]=0;}
  }
 }
 size_t used=0;for(unsigned i=0;name&&name[i]&&i<16;i++){
  unsigned char c=(unsigned char)name[i];if(c<32)continue;
  if(c=='"'||c=='\\')identity_name[used++]='\\';identity_name[used++]=(char)c;
 }identity_name[used]=0;
 fprintf(stdout,"[laonline] Session log code=%s player=%s\n",identity_code[0]?identity_code:"unavailable",identity_name);
 pthread_mutex_unlock(&mx);
}
void laonline_diag_log_code(char *out,size_t cap){pthread_mutex_lock(&mx);if(out&&cap)snprintf(out,cap,"%s",identity_code);pthread_mutex_unlock(&mx);}
void laonline_diag(const char *event,uint16_t map,uint16_t room,uint64_t object,uint64_t sequence,int result){
 dev_events_emit("DIAGNOSTIC",event,"Existing multiplayer diagnostic",(int)object,room,map,result,"OBSERVED_NOT_SAVE_PROOF","NETWORK_OR_RUNTIME",sequence);
 for(const char *p=event;*p;p++)if(!((*p>='a'&&*p<='z')||*p=='.'||*p=='_'))return;
 if(strlen(event)>40)return;
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);uint64_t ms=(uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
 pthread_mutex_lock(&mx);
 if(ms-window_at>=1000){window_at=ms;window_count=0;}
 if(window_count++>=20){pthread_mutex_unlock(&mx);return;}
 char mt[24]="null",rt[24]="null",ot[24]="null",st[24]="null";
 if(map!=0xffff)snprintf(mt,sizeof mt,"%u",map);if(room!=0xffff)snprintf(rt,sizeof rt,"%u",room);
 if(object)snprintf(ot,sizeof ot,"%llu",(unsigned long long)object);if(sequence)snprintf(st,sizeof st,"%llu",(unsigned long long)sequence);
 char *line=ring[next];snprintf(line,512,"{\"build\":\"" LAO_MOD_VERSION "\",\"log_code\":\"%s\",\"player_name\":\"%s\",\"baseline\":\"" LAO_LAS_BASELINE "\",\"monotonic_ms\":%llu,\"event\":\"%s\",\"map_id\":%s,\"room_id\":%s,\"object\":%s,\"sequence\":%s,\"result\":%d}",identity_code,identity_name,(unsigned long long)ms,event,mt,rt,ot,st,result);
 next=(next+1)%128;if(used<128)used++;
 FILE *f=fopen("laonline-events.jsonl","ab");
 if(f){fseek(f,0,SEEK_END);long size=ftell(f);if(size>=524288){fclose(f);remove("laonline-events.previous.jsonl");rename("laonline-events.jsonl","laonline-events.previous.jsonl");f=fopen("laonline-events.jsonl","wb");}
  if(f){fprintf(f,"%s\n",line);fclose(f);}}
 pthread_mutex_unlock(&mx);
}
int laonline_diag_snapshot(char *out,size_t cap){
 if(!cap)return 0;pthread_mutex_lock(&mx);size_t n=0;
 size_t extra=strlen(seed_diagnostic);if(extra+2>cap)extra=0;size_t available=cap-(extra?extra+1:0);
 unsigned start=used;size_t total=1;
 while(start){const char *p=ring[(next+128-used+start-1)%128];size_t len=strlen(p);if(total+len+1>available)break;total+=len+1;start--;}
 for(unsigned i=start;i<used;i++){const char *p=ring[(next+128-used+i)%128];size_t len=strlen(p);memcpy(out+n,p,len);n+=len;out[n++]='\n';}
 if(extra){memcpy(out+n,seed_diagnostic,extra);n+=extra;out[n++]='\n';}out[n]=0;pthread_mutex_unlock(&mx);return (int)n;
}
