// Diagnostic append journal only. Never grants items, changes saves or enables networking.
#include "dev_events.h"
#include "laonline_build.h"
#include "paths.h"
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#define sync_fd _commit
#else
#include <unistd.h>
#include <fcntl.h>
#define sync_fd fsync
#endif
#define EVENTS 2048
#define WIDTH 1800
static pthread_mutex_t mx=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static char queue[EVENTS][WIDTH],path[1600],mode[40]="LAUNCHER",player[100]="Player",seed[180]="none",session[80];
static unsigned head,tail,count,lost;static uint64_t number;static int started,stopping,busy;
static FILE *file;static atomic_int crash_recorded;
static atomic_ullong written;
static atomic_int write_failed;
static void escape(const char *s,char *out,size_t cap){size_t n=0;for(;s&&*s&&n+7<cap;s++){unsigned char c=*s;if(c=='"'||c=='\\'){out[n++]='\\';out[n++]=c;}else if(c>=32)out[n++]=c;else{out[n++]=' ';}}out[n]=0;}
static void sync_file(void){if(file&&(fflush(file)||sync_fd(fileno(file))))atomic_store(&write_failed,1);}
static void rotate(void){if(!file||ftell(file)<8*1024*1024)return;sync_file();fclose(file);file=NULL;char a[1700],b[1700];snprintf(a,sizeof a,"%s.4",path);remove(a);for(int i=3;i>=1;i--){snprintf(a,sizeof a,"%s.%d",path,i);snprintf(b,sizeof b,"%s.%d",path,i+1);rename(a,b);}snprintf(b,sizeof b,"%s.1",path);rename(path,b);file=fopen(path,"ab");if(file)fseek(file,0,SEEK_END);}
static void *write_loop(void *unused){(void)unused;for(;;){char line[WIDTH];unsigned dropped=0;pthread_mutex_lock(&mx);while(!count&&!stopping)pthread_cond_wait(&wake,&mx);if(!count&&stopping){pthread_mutex_unlock(&mx);break;}memcpy(line,queue[tail],WIDTH);tail=(tail+1)%EVENTS;count--;busy=1;dropped=lost;lost=0;pthread_mutex_unlock(&mx);
 if(file){rotate();if(file){if(dropped)fprintf(file,"{\"EVENT\":\"LOG_QUEUE_OVERFLOW\",\"lost_records\":%u}\n",dropped);if(fputs(line,file)<0||fputc('\n',file)==EOF)atomic_store(&write_failed,1);else atomic_fetch_add(&written,1);}}
 // Bound disk visibility even while a long burst keeps the queue occupied.
 static unsigned batch; if(++batch>=32){sync_file();batch=0;}
 pthread_mutex_lock(&mx);if(!count){sync_file();busy=0;pthread_cond_broadcast(&wake);}pthread_mutex_unlock(&mx);
 }sync_file();return NULL;}
void dev_events_init(void){if(started)return;char dir[1500];snprintf(dir,sizeof dir,"%s/logs",rt_data_root());
#ifdef _WIN32
 _mkdir(dir);
#else
 mkdir(dir,0700);
#endif
// Keep the exact requested filename beside the player's data/saves, not hidden
// in a wrapper's timestamped log folder. Existing logs are never deleted.
snprintf(path,sizeof path,"%s/dev-events-log.txt",rt_data_root());file=fopen(path,"ab");
 if(!file){char *pref=SDL_GetPrefPath("ThyHeroOfTime","LA-Online");if(pref){snprintf(path,sizeof path,"%sdev-events-log.txt",pref);SDL_free(pref);file=fopen(path,"ab");}}
 if(!file){fprintf(stderr,"[dev-events] Cannot create development log; diagnostics unavailable.\n");return;}
 fseek(file,0,SEEK_END);struct timespec now;clock_gettime(CLOCK_REALTIME,&now);snprintf(session,sizeof session,"%lld-%09ld",(long long)now.tv_sec,now.tv_nsec);const char *code=getenv("LAO_LOG_CODE");if(code&&strlen(code)==9&&!strncmp(code,"LAO-",4)&&strspn(code+4,"ABCDEFGHJKLMNPQRSTUVWXYZ23456789")==5)snprintf(session,sizeof session,"%s",code);
 started=1;if(pthread_create(&worker,NULL,write_loop,NULL)){started=0;fclose(file);file=NULL;return;}atexit(dev_events_shutdown);fprintf(stdout,"[dev-events] Log: %s\n",path);dev_events_emit("SESSION","SESSION_STARTED",LAO_RELEASE_LABEL,0,0,0,0,"OPEN","LOCAL",0);dev_events_flush();}
const char *dev_events_path(void){return path;}
const char *dev_events_status(void){return !started?"Unavailable: could not start event logging":atomic_load(&write_failed)?"Write failed: check disk space and folder permissions":"Recording automatically";}
uint64_t dev_events_written(void){return atomic_load(&written);}
void dev_events_context(const char *m,const char *p,const char *s){pthread_mutex_lock(&mx);escape(m,mode,sizeof mode);escape(p,player,sizeof player);escape(s,seed,sizeof seed);pthread_mutex_unlock(&mx);}
void dev_events_emit(const char *category,const char *event,const char *subject,int id,int index,int64_t before,int64_t after,const char *result,const char *origin,uint64_t transaction){if(!started)return;char ca[80],ev[100],su[400],re[120],or[100],stamp[40];escape(category,ca,sizeof ca);escape(event,ev,sizeof ev);escape(subject,su,sizeof su);escape(result,re,sizeof re);escape(origin,or,sizeof or);time_t now=time(NULL);struct tm tm;
#ifdef _WIN32
 localtime_s(&tm,&now);
#else
 localtime_r(&now,&tm);
#endif
strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S %z",&tm);
 pthread_mutex_lock(&mx);if(stopping){pthread_mutex_unlock(&mx);return;}if(count==EVENTS){lost++;pthread_mutex_unlock(&mx);return;}snprintf(queue[head],WIDTH,"{\"TIME\":\"%s\",\"VERSION\":\"" LAO_MOD_VERSION "\",\"BASELINE\":\"" LAO_LAS_BASELINE "\",\"MODE\":\"%s\",\"PLAYER\":\"%s\",\"SESSION\":\"%s\",\"SEED\":\"%s\",\"RECORD\":%llu,\"CATEGORY\":\"%s\",\"EVENT\":\"%s\",\"SUBJECT\":\"%s\",\"ID\":%d,\"INDEX\":%d,\"BEFORE\":%lld,\"AFTER\":%lld,\"RESULT\":\"%s\",\"ORIGIN\":\"%s\",\"TRANSACTION\":%llu}",stamp,mode,player,session,seed,(unsigned long long)++number,ca,ev,su,id,index,(long long)before,(long long)after,re,or,(unsigned long long)transaction);head=(head+1)%EVENTS;count++;pthread_cond_signal(&wake);pthread_mutex_unlock(&mx);}
void dev_events_flush(void){if(!started)return;pthread_mutex_lock(&mx);while(count||busy)pthread_cond_wait(&wake,&mx);sync_file();pthread_mutex_unlock(&mx);}
// A fault handler must not wait for a mutex held by the faulting thread.
void dev_events_crash(void){if(!started||atomic_exchange(&crash_recorded,1))return;
#ifdef _WIN32
 HANDLE h=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(h==INVALID_HANDLE_VALUE)return;const char line[]="{\"EVENT\":\"PROCESS_CRASH\",\"RESULT\":\"queued records may be incomplete\"}\r\n";DWORD n;WriteFile(h,line,sizeof line-1,&n,NULL);FlushFileBuffers(h);CloseHandle(h);
#else
 int fd=open(path,O_WRONLY|O_CREAT|O_APPEND,0600);if(fd<0)return;const char line[]="{\"EVENT\":\"PROCESS_CRASH\",\"RESULT\":\"queued records may be incomplete\"}\n";write(fd,line,sizeof line-1);fsync(fd);close(fd);
#endif
}
void dev_events_shutdown(void){if(!started)return;dev_events_emit("SESSION","SESSION_CLOSED","Application exit",0,0,0,0,"NORMAL_EXIT","LOCAL",0);pthread_mutex_lock(&mx);stopping=1;pthread_cond_signal(&wake);pthread_mutex_unlock(&mx);pthread_join(worker,NULL);fclose(file);file=NULL;started=0;}

