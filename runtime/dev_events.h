#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void dev_events_init(void);
const char *dev_events_path(void);
const char *dev_events_status(void);
uint64_t dev_events_written(void);
void dev_events_context(const char *mode,const char *player,const char *seed);
void dev_events_emit(const char *category,const char *event,const char *subject,int id,int index,int64_t before,int64_t after,const char *result,const char *origin,uint64_t transaction);
void dev_events_flush(void);
void dev_events_crash(void);
void dev_events_shutdown(void);
#ifdef __cplusplus
}
#endif
