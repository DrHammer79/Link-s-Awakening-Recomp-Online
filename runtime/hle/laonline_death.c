/* Native GameOver request observation, never an HP-drop heuristic.
 * 1.0.0 main+dab000 runs immediately after EventManager::Submit returns.
 * The enclosing native path explicitly constructs Common.bfevfl/GameOver.
 * No cause mapping has been audited: only cause 0 (unknown) is accepted.
 */
#include "laonline_death.h"
#include "../hle.h"
#include "../game_addresses.h"
#include "../settings.h"
#include "laonline_session.h"
#include "laonline_location.h"
#include "laonline_diag.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
static pthread_mutex_t mx=PTHREAD_MUTEX_INITIALIZER;
static int connected,cap,pending,latched;
static uint16_t current_map=0xffff,current_room=0xffff,death_map,death_room;
static uint64_t sequence,send_sequence,seen[4];
void laonline_death_rearm_alive(void){pthread_mutex_lock(&mx);latched=0;pthread_mutex_unlock(&mx);}
int laonline_death_active(void){pthread_mutex_lock(&mx);int dead=latched;pthread_mutex_unlock(&mx);return dead;}
extern void laonline_notify_item(const char *,int);
void laonline_death_transport(int on){pthread_mutex_lock(&mx);connected=on;cap=pending=0;memset(seen,0,sizeof seen);pthread_mutex_unlock(&mx);}
void laonline_death_capability(void){pthread_mutex_lock(&mx);cap=1;pthread_mutex_unlock(&mx);}
void laonline_death_peer_left(int id){if(id<0||id>=4)return;pthread_mutex_lock(&mx);seen[id]=0;pthread_mutex_unlock(&mx);}
void laonline_death_context(uint16_t map,uint16_t room,int alive){
 pthread_mutex_lock(&mx);current_map=map;current_room=room;
 // Called only during valid, unpaused gameplay. Loading/disconnect never rearms a death.
 if(alive)latched=0;
 pthread_mutex_unlock(&mx);
}
void laonline_death_native(Cpu *c){
 if(!g_ga||!game_version())return;
 uint64_t mgr=LD64(GA(GAME_MGR));if(!mgr||LD64(mgr+0x4b8)!=c->x[19])return;
 // Both native request construction and manager submission must have succeeded.
 if(c->sp<GUEST_LO||c->sp>GUEST_HI-0xa0||!LD64(c->sp+0x20)||!LD64(c->sp+0x10))return;
 pthread_mutex_lock(&mx);
 int fresh=!latched;
 if(fresh){latched=1;if(connected&&cap){death_map=current_map;death_room=current_room;send_sequence=++sequence;pending=1;}}
 pthread_mutex_unlock(&mx);
 if(fresh)laonline_diag("death.native_gameover_submitted",current_map,current_room,0,sequence,0);
}
int laonline_death_packet(uint8_t *out,int capacity){
 if(capacity<16)return 0;pthread_mutex_lock(&mx);int n=0;
 if(connected&&cap&&pending){out[0]=14;out[1]=0;out[2]=0x19;memcpy(out+3,&send_sequence,8);memcpy(out+11,&death_map,2);memcpy(out+13,&death_room,2);out[15]=0;pending=0;n=16;}
 pthread_mutex_unlock(&mx);return n;
}
void laonline_death_message(const uint8_t *b,size_t n){
 if(!b||n!=32||b[0]!=0xa0||b[1]>=4||b[14]!=0)return;
 uint64_t seq;uint16_t map,room;memcpy(&seq,b+2,8);memcpy(&map,b+10,2);memcpy(&room,b+12,2);
 if(!seq)return;char name[17];memcpy(name,b+15,17);name[16]=0;
 for(int i=0;name[i];){
  unsigned char first=(unsigned char)name[i++];unsigned cp=first;int extra=0;
  if(first>=0xc2&&first<=0xdf){cp=first&31;extra=1;}
  else if(first>=0xe0&&first<=0xef){cp=first&15;extra=2;}
  else if(first>=0xf0&&first<=0xf4){cp=first&7;extra=3;}
  else if(first>=0x80)return;
  int bytes=extra;
  while(extra--){unsigned char b=(unsigned char)name[i];if(!b||(b&0xc0)!=0x80)return;cp=(cp<<6)|(b&63);i++;}
  if(cp<32||(cp>=127&&cp<=159)||(bytes==1&&cp<128)||(bytes==2&&cp<2048)||(bytes==3&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return;
 }
 if(!name[0])return;
 pthread_mutex_lock(&mx);int accept=connected&&cap&&seq>seen[b[1]];if(accept)seen[b[1]]=seq;pthread_mutex_unlock(&mx);
 if(!accept)return;
 laonline_diag("death.session_received",map,room,b[1],seq,0);
 if(!g_settings.laonline_death_notices)return;
 char where[33],text[160];if(map==0xffff)snprintf(where,sizeof where,"Unknown area");else lao_location_ids(map,room,where);
 static const char *humor[]={"Koholint wins this round!","A short hero break.","Time to regroup, hero!","The adventure needs a retry."};
 extern int laonline_v12_pvp_killer(int victim,char *name,int cap);char killer[17];
 if(laonline_v12_pvp_killer(b[1],killer,sizeof killer))  /* V15.1: PvP knock-outs say who did it (was "Cause unknown") */
  snprintf(text,sizeof text,"%s was knocked out in PvP\nby %s!\nAt: %s",name,killer,where);
 else if(g_settings.laonline_death_style)snprintf(text,sizeof text,"%s was defeated.\n%s\nAt: %s",name,humor[(seq*2654435761u+b[1])%4],where);
 else snprintf(text,sizeof text,"%s was defeated.\nCause unknown\nAt: %s",name,where);
 laonline_notify_item(text,-2);
}
RT_ON_RESET(laonline_death_reset){pthread_mutex_lock(&mx);connected=cap=pending=latched=0;sequence=send_sequence=0;current_map=current_room=0xffff;memset(seen,0,sizeof seen);pthread_mutex_unlock(&mx);}
