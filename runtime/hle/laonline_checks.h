#pragma once
#include <stdint.h>
#include <stddef.h>
#define LAO_CHECK_QUERY 0x4c414f59u
#define LAO_CHECK_REPLY 0x4c414f5au
#define LAO_CHECK_SEND 0x12u
#define LAO_CHECK_RECEIVE 0x96u
void laonline_checks_load(void);
void laonline_checks_transport(int on);
void laonline_checks_capability(void);
void laonline_checks_tick(void);
int laonline_checks_packet(uint8_t *,int,int);
void laonline_checks_receive(const uint8_t *,size_t);
int laonline_check_team_collected(int flag);
