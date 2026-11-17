#pragma once
/* Additive opt-in V15 extension. Existing movement/cosmetics frames are untouched. */
#include <stdint.h>
#include "laonline_item_rules.h"
#include <stddef.h>
#include <string.h>
#define LAO_COOP_QUERY 0x4c414f53u
#define LAO_COOP_REPLY 0x4c414f54u
#define LAO_COOP_SETUP 0x0cu
#define LAO_COOP_PICKUP 0x0du
#define LAO_COOP_INVENTORY 0x0eu
#define LAO_COOP_READY 0x90u
#define LAO_COOP_EVENT 0x91u
#define LAO_COOP_SNAPSHOT 0x92u
#pragma pack(push,1)
typedef struct {
 uint32_t items, keys;
 uint64_t shells;
 uint32_t pieces;
 uint16_t containers;
 uint8_t songs, upgrades, sensor, leaves, dungeon[9];
 uint8_t flags[32];
} LaoCoopInventory;
typedef struct {
 uint64_t seq;
 uint16_t item, count;
 int16_t index;
 uint16_t map, room;
} LaoPickup;
#pragma pack(pop)
static inline int lao_coop_inventory_valid(const LaoCoopInventory *s) {
 for(unsigned i=LAO_PROGRESS_FLAG_COUNT;i<256;i++)if((s->flags[i>>3]>>(i&7))&1)return 0;
 return !(s->items & ~0xffffu) && !(s->keys & ~0x17ffdu) && !(s->shells >> 50) &&
 !(s->containers & ~0x1ffu) && !(s->songs & ~7u) && !(s->upgrades & ~0x3fu) &&
 s->sensor<=1 && !(s->leaves & ~31u) &&
 !((s->dungeon[0]|s->dungeon[1]|s->dungeon[2]|s->dungeon[3]|s->dungeon[4]|
 s->dungeon[5]|s->dungeon[6]|s->dungeon[7]|s->dungeon[8]) & ~15u);
}
static inline void lao_coop_merge(LaoCoopInventory *dst,const LaoCoopInventory *src) {
 unsigned char *d=(unsigned char *)dst; const unsigned char *s=(const unsigned char *)src;
 for(size_t i=0;i<sizeof *dst;i++) d[i]|=s[i];
}
static inline int lao_coop_resource(int item) {
 switch(item) {
 case 4:case 11:case 21:case 60:case 61:case 62:case 100:case 101:case 102:
 case 104:case 105:case 106:case 108:case 110:return 1;
 default:return 0;
 }
}
static inline int lao_pickup_valid(const LaoPickup *p) {
 return p->seq && p->count>0 && p->count<=999 && p->item<=126 &&
 p->index>=-1 && p->index<=999;
}
