#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../cpu.h"
void laonline_death_context(uint16_t map,uint16_t room,int alive);
void laonline_death_native(Cpu *c);
void laonline_death_transport(int on);
void laonline_death_capability(void);
void laonline_death_peer_left(int id);
int laonline_death_packet(uint8_t *out,int capacity);
void laonline_death_message(const uint8_t *b,size_t n);
#define LAO_DEATH_QUERY 0x4c414f61u
#define LAO_DEATH_REPLY 0x4c414f62u
