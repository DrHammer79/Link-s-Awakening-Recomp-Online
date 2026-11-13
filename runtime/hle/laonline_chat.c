// Negotiated additive chat on the existing framed TCP relay. No guest pointers
// cross threads; message content is deliberately excluded from diagnostic logs.
#include "laonline_chat.h"
#include "laonline_session.h"
#include "laonline_diag.h"
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
static pthread_mutex_t mx=PTHREAD_MUTEX_INITIALIZER;
static int capable,self=-1,typing,pending_typing;
static uint64_t sequence,typing_sent;
typedef struct {char text[LAO_CHAT_BYTES];uint64_t seq,at,typing_at,actor_at;float pos[3];int visible;} Bubble;
static uint64_t clock_ms(void);
static Bubble bubbles[LAO_PLAYERS];
static char names[LAO_PLAYERS][17];
static struct {char name[17],text[LAO_CHAT_BYTES];uint64_t at;int sender;} feed[6];
static unsigned feed_count;
void laonline_chat_peer_name(int id,const char *name){if(id<0||id>=LAO_PLAYERS||!name)return;pthread_mutex_lock(&mx);snprintf(names[id],17,"%.16s",name);pthread_mutex_unlock(&mx);}
size_t lao_chat_truncate(char *s,unsigned max_chars){unsigned count=0;size_t i=0;while(s[i]){if(((unsigned char)s[i]&192)!=128 && count++>=max_chars){s[i]=0;break;}i++;}return i;}
int laonline_chat_feed_sender(int i){pthread_mutex_lock(&mx);int id=i>=0&&(unsigned)i<feed_count?feed[i].sender:-1;pthread_mutex_unlock(&mx);return id;}
int laonline_chat_feed(int i,char name[17],char text[LAO_CHAT_BYTES],uint64_t *age){
 pthread_mutex_lock(&mx);uint64_t now=clock_ms();int ok=capable&&i>=0&&(unsigned)i<feed_count&&now>=feed[i].at&&now-feed[i].at<15000;
 if(ok){memcpy(name,feed[i].name,17);memcpy(text,feed[i].text,LAO_CHAT_BYTES);*age=now-feed[i].at;}pthread_mutex_unlock(&mx);return ok;
}
static char outgoing[8][LAO_CHAT_BYTES];static unsigned count;
static uint64_t clock_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
int lao_chat_valid(const char *s,size_t n){
 if(!n||n>600)return 0;unsigned chars=0;
 for(size_t i=0;i<n;){
  unsigned c=(unsigned char)s[i++],cp;int k;
  if(c<0x80){if(c<32||c==127)return 0;cp=c;k=0;}
  else if(c>=0xc2&&c<=0xdf){cp=c&31;k=1;}
  else if(c>=0xe0&&c<=0xef){cp=c&15;k=2;}
  else if(c>=0xf0&&c<=0xf4){cp=c&7;k=3;}else return 0;
  if(i+k>n)return 0;int bytes=k+1;
  while(k--){c=(unsigned char)s[i++];if((c&192)!=128)return 0;cp=(cp<<6)|(c&63);}
  if((bytes==2&&cp<128)||(bytes==3&&cp<2048)||(bytes==4&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||(cp>=128&&cp<=159)||cp==0x2028||cp==0x2029)return 0;
  if(++chars>150)return 0;
 }return (int)chars;
}
void laonline_chat_capability(int on){pthread_mutex_lock(&mx);capable=on;if(!on){self=-1;typing=count=pending_typing=0;memset(bubbles,0,sizeof bubbles);memset(feed,0,sizeof feed);memset(names,0,sizeof names);feed_count=0;}pthread_mutex_unlock(&mx);}
int laonline_chat_available(void){pthread_mutex_lock(&mx);int on=capable&&self>=0;pthread_mutex_unlock(&mx);return on;}
int laonline_chat_self(void){pthread_mutex_lock(&mx);int id=self;pthread_mutex_unlock(&mx);return id;}
void laonline_chat_identity(int id){pthread_mutex_lock(&mx);self=id>=0&&id<LAO_PLAYERS?id:-1;pthread_mutex_unlock(&mx);}
void laonline_chat_typing(int on){pthread_mutex_lock(&mx);if(typing!=!!on){typing=!!on;pending_typing=1;}pthread_mutex_unlock(&mx);}
int laonline_chat_submit(const char *s){size_t n=strlen(s);if(!lao_chat_valid(s,n))return 0;pthread_mutex_lock(&mx);int ok=capable&&self>=0&&count<8;if(ok){memcpy(outgoing[count++],s,n+1);typing=0;pending_typing=1;}pthread_mutex_unlock(&mx);return ok;}
int laonline_chat_packet(uint8_t *b,int cap){
 if(cap<614)return 0;pthread_mutex_lock(&mx);int n=0;uint64_t now=clock_ms();
 if(capable&&count){size_t len=strlen(outgoing[0]);n=(int)len+13;b[2]=LAO_CHAT_SEND;uint64_t seq=++sequence;memcpy(b+3,&seq,8);b[11]=(uint8_t)len;b[12]=(uint8_t)(len>>8);memcpy(b+13,outgoing[0],len);memmove(outgoing,outgoing+1,(--count)*LAO_CHAT_BYTES);}
 else if(capable&&(pending_typing||(typing&&now-typing_sent>=1500))){n=4;b[2]=LAO_TYPING_SEND;b[3]=(uint8_t)typing;pending_typing=0;typing_sent=now;}
 if(n){b[0]=(uint8_t)(n-2);b[1]=(uint8_t)((n-2)>>8);}pthread_mutex_unlock(&mx);return n;
}
void laonline_chat_receive(const uint8_t *b,size_t n){
 if(n<3||b[1]>=LAO_PLAYERS)return;pthread_mutex_lock(&mx);Bubble *p=&bubbles[b[1]];
 if(b[0]==LAO_TYPING_RECEIVE&&n==3&&b[2]<=1)p->typing_at=b[2]?clock_ms():0;
 if(b[0]==LAO_CHAT_RECEIVE&&n>=12){uint64_t seq;memcpy(&seq,b+2,8);size_t len=b[10]|b[11]<<8;if(seq>p->seq&&n==12+len&&lao_chat_valid((const char *)b+12,len)){memcpy(p->text,b+12,len);p->text[len]=0;p->seq=seq;p->at=clock_ms();p->typing_at=0;
 if(feed_count==6){memmove(feed,feed+1,5*sizeof *feed);feed_count--;}
 snprintf(feed[feed_count].name,17,"%s",names[b[1]][0]?names[b[1]]:"Player");
 memcpy(feed[feed_count].text,p->text,LAO_CHAT_BYTES);feed[feed_count].sender=b[1];feed[feed_count++].at=p->at;
 laonline_diag("chat.delivered",0xffff,0xffff,b[1],seq,1);fprintf(stderr,"[laonline] chat delivered peer=%u sequence=%llu bytes=%zu\n",b[1],(unsigned long long)seq,len);}}
 pthread_mutex_unlock(&mx);
}
void laonline_chat_peer_left(int id){if(id<0||id>=LAO_PLAYERS)return;pthread_mutex_lock(&mx);memset(&bubbles[id],0,sizeof bubbles[id]);memset(names[id],0,17);pthread_mutex_unlock(&mx);}
void laonline_chat_actor(int id,const float *pos,int visible){if(id<0||id>=LAO_PLAYERS)return;pthread_mutex_lock(&mx);Bubble *p=&bubbles[id];p->visible=visible;p->actor_at=clock_ms();if(pos)memcpy(p->pos,pos,12);pthread_mutex_unlock(&mx);}
int laonline_chat_bubble(int id,char *text,float *pos,int *dots,uint64_t *age){
 if(id<0||id>=LAO_PLAYERS)return 0;pthread_mutex_lock(&mx);Bubble *p=&bubbles[id];uint64_t now=clock_ms();
 *dots=id!=self&&p->typing_at&&now-p->typing_at<4000;*age=p->at?now-p->at:~0ull;
 int ok=capable&&p->visible&&now-p->actor_at<500&&(*dots||*age<30000);
 if(ok){memcpy(text,p->text,LAO_CHAT_BYTES);memcpy(pos,p->pos,12);}pthread_mutex_unlock(&mx);return ok;
}
