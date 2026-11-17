// Exact-seed team tracker receipts. Never writes native quest flags or grants rewards.
// Only flags explicitly attached to C rows in this seed's tracker.tsv are accepted.
#include "laonline_checks.h"
#include "../hle.h"
#include "../rando.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define LIMIT 45056
#define BYTES (LIMIT/8)
static pthread_mutex_t mx=PTHREAD_MUTEX_INITIALIZER;
static unsigned char allowed[BYTES],found[BYTES],team[BYTES],sent[BYTES];
static int have,cap,query,hello;
extern bool flags_ext_get(int);
static int bit(const unsigned char *b,int i){return i>=0&&i<LIMIT&&((b[i>>3]>>(i&7))&1);}
static void put(unsigned char *b,int i){b[i>>3]|=1u<<(i&7);}
RT_ON_RESET(laonline_checks_reset){pthread_mutex_lock(&mx);memset(allowed,0,sizeof allowed);memset(found,0,sizeof found);memset(team,0,sizeof team);memset(sent,0,sizeof sent);have=cap=query=hello=0;pthread_mutex_unlock(&mx);}
void laonline_checks_load(void){
 const char *dir=rando_seed_dir();if(!dir)return;char path[1200],line[4096];snprintf(path,sizeof path,"%s/tracker.tsv",dir);FILE *f=fopen(path,"rb");if(!f)return;
 unsigned char table[BYTES]={0};int count=0;
 while(fgets(line,sizeof line,f)){
  if(line[0]!='C'||line[1]!='\t')continue;char *p=line;int col=0;
  while(*p&&col<5){if(*p++=='\t')col++;}if(col!=5)continue;
  char *end;long flag=strtol(p,&end,10);if(end==p||(*end!='\t'&&*end!='\r'&&*end!='\n'&&*end)||flag<0||flag>=LIMIT)continue;
  if(!bit(table,flag)){put(table,flag);count++;}
 }
 fclose(f);pthread_mutex_lock(&mx);memcpy(allowed,table,sizeof table);have=count>0;pthread_mutex_unlock(&mx);
 LOG("[laonline] M7 shared tracker allowlist: %d exact-seed check flags\n",count);
}
void laonline_checks_transport(int on){(void)on;pthread_mutex_lock(&mx);cap=query=hello=0;memset(sent,0,sizeof sent);memset(team,0,sizeof team);pthread_mutex_unlock(&mx);}
void laonline_checks_capability(void){pthread_mutex_lock(&mx);cap=1;pthread_mutex_unlock(&mx);}
void laonline_checks_tick(void){
 // Guest reads stay on the game thread; rendering/network use copies only.
 unsigned char sample[BYTES]={0};for(int i=0;i<LIMIT;i++)if(bit(allowed,i)&&flags_ext_get(i))put(sample,i);
 pthread_mutex_lock(&mx);for(int i=0;i<BYTES;i++)found[i]|=sample[i];pthread_mutex_unlock(&mx);
}
int laonline_checks_packet(uint8_t *out,int size,int ready){
 if(size<7)return 0;int n=0;pthread_mutex_lock(&mx);
 if(have&&!query){out[0]=5;out[1]=0;out[2]=7;uint32_t v=LAO_CHECK_QUERY;memcpy(out+3,&v,4);query=1;n=7;}
 else if(have&&cap&&ready){
  if(!hello){out[0]=2;out[1]=0;out[2]=LAO_CHECK_SEND;out[3]=0;hello=1;n=4;}
  else for(int i=0;i<LIMIT;i++)if(bit(found,i)&&!bit(sent,i)){
   out[0]=4;out[1]=0;out[2]=LAO_CHECK_SEND;out[3]=1;out[4]=i;out[5]=i>>8;put(sent,i);n=6;break;
  }
 }
 pthread_mutex_unlock(&mx);return n;
}
void laonline_checks_receive(const uint8_t *b,size_t n){
 if(!b||n<2||b[0]!=LAO_CHECK_RECEIVE||n!=2u+2u*b[1])return;
 pthread_mutex_lock(&mx);if(cap&&hello)for(unsigned j=0;j<b[1];j++){unsigned flag=b[2+2*j]|b[3+2*j]<<8;if(bit(allowed,flag))put(team,flag);}
 pthread_mutex_unlock(&mx);
}
int laonline_check_team_collected(int flag){pthread_mutex_lock(&mx);int yes=cap&&hello&&bit(allowed,flag)&&bit(team,flag);pthread_mutex_unlock(&mx);return yes;}
