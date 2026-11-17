#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void laonline_diag(const char *event,uint16_t map,uint16_t room,uint64_t object,uint64_t sequence,int result);
void laonline_diag_identity(const char *name);
void laonline_diag_log_code(char *out,size_t cap);
int laonline_diag_snapshot(char *out,size_t cap);
void laonline_diag_seed(const char *stage,const char *host_id,const char *client_id,const char *host_hash,const char *client_hash,const char *folder,const char *reason);
void laonline_remote_draw_recorded(int id,uint64_t frame);
void laonline_remote_draw_reset(int id);
void laonline_remote_draw_probe(int id,uint64_t frame,uint64_t ms,int expected);
int laonline_remote_draw_stalled(int id,uint64_t ms);
int laonline_remote_draw_recent(int id,uint64_t frame);
int laonline_remote_draw_seen(int id,uint64_t frame);  /* V13: drawn within ~0.5 s (name tags, bubbles) */
#ifdef __cplusplus
}
#endif
