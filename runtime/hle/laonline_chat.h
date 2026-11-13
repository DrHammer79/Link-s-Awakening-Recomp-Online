#pragma once
#include <stdint.h>
#include <stddef.h>
#define LAO_CHAT_QUERY 0x4c414f57u
#define LAO_CHAT_REPLY 0x4c414f58u
#define LAO_CHAT_SEND 0x10
#define LAO_TYPING_SEND 0x11
#define LAO_CHAT_RECEIVE 0x94
#define LAO_TYPING_RECEIVE 0x95
#define LAO_CHAT_BYTES 601
#ifdef __cplusplus
extern "C" {
#endif
int lao_chat_valid(const char *,size_t);
void laonline_chat_capability(int);
int laonline_chat_available(void);
int laonline_chat_self(void);
void laonline_chat_typing(int);
int laonline_chat_submit(const char *);
int laonline_chat_packet(uint8_t *,int);
void laonline_chat_receive(const uint8_t *,size_t);
void laonline_chat_peer_left(int);
void laonline_chat_identity(int);
void laonline_chat_actor(int,const float *,int);
int laonline_chat_bubble(int,char *,float *,int *,uint64_t *);
void laonline_chat_peer_name(int id,const char *name);
int laonline_chat_feed_sender(int i);
int laonline_chat_feed(int i,char name[17],char text[LAO_CHAT_BYTES],uint64_t *age);
size_t lao_chat_truncate(char *text,unsigned max_chars);
#ifdef __cplusplus
}
#endif
