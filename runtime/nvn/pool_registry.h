#pragma once
#include <stdint.h>
#include <string.h>
#include <pthread.h>
// CPU metadata only. GPU resource creation/destruction and fence ownership stay unchanged.
typedef struct {uint64_t owner,base,size,generation;int flags;} LaoPoolRecord;
typedef struct {pthread_mutex_t lock;unsigned used;uint64_t next;LaoPoolRecord records[4096];} LaoPoolRegistry;
#define LAO_POOL_REGISTRY_INIT {PTHREAD_MUTEX_INITIALIZER,0,0,{{0}}}
static inline uint64_t lao_pool_add(LaoPoolRegistry *r,uint64_t owner,uint64_t base,uint64_t size,int flags){
 if(!owner||!size||base>UINT64_MAX-size)return 0;
 pthread_mutex_lock(&r->lock);unsigned i;
 for(i=0;i<r->used;i++)if(r->records[i].owner==owner)break;
 if(i==r->used){if(r->used==4096){pthread_mutex_unlock(&r->lock);return 0;}r->used++;}
 if(++r->next==0)++r->next;
 r->records[i]=(LaoPoolRecord){owner,base,size,r->next,flags};uint64_t generation=r->next;
 pthread_mutex_unlock(&r->lock);return generation;
}
static inline void lao_pool_remove(LaoPoolRegistry *r,uint64_t owner){
 pthread_mutex_lock(&r->lock);for(unsigned i=0;i<r->used;i++)if(r->records[i].owner==owner){r->records[i]=r->records[--r->used];break;}pthread_mutex_unlock(&r->lock);
}
static inline uint64_t lao_pool_find(LaoPoolRegistry *r,uint64_t addr,uint64_t size,int *flags){
 uint64_t generation=0;*flags=-1;if(!size)return 0;
 pthread_mutex_lock(&r->lock);for(unsigned i=0;i<r->used;i++){const LaoPoolRecord *p=&r->records[i];if(addr>=p->base && size<=p->size && addr-p->base<=p->size-size && p->generation>generation){generation=p->generation;*flags=p->flags;}}
 pthread_mutex_unlock(&r->lock);return generation;
}
static inline void lao_pool_reset(LaoPoolRegistry *r){pthread_mutex_lock(&r->lock);r->used=0;pthread_mutex_unlock(&r->lock);}
