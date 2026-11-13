#pragma once
#include <stdint.h>
#include <stddef.h>
/* Versioned, monotonic entrance unlocks only. No chests, reward ownership,
 * cutscene/demo state, encounter phases, control locks or completion flags. */
#define LAO_STORY_QUERY 0x4c414f67u
#define LAO_STORY_REPLY 0x4c414f68u
#define LAO_STORY_SEND 0x1au
#define LAO_STORY_RECEIVE 0xa1u
#define LAO_STORY_COUNT 7
#define LAO_STORY_MASK 0x7fu
static const uint16_t lao_story_flags[LAO_STORY_COUNT]={100,101,102,103,105,106,107};
void laonline_story_transport(void);
void laonline_story_capability(void);
void laonline_story_tick(void);
int laonline_story_packet(uint8_t *,int,int,const uint8_t seed[32]);
void laonline_story_receive(const uint8_t *,size_t);
