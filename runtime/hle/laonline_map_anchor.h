#pragma once
#include "laonline_session.h"
#include "laonline_world_map.h"
#define LAO_MAP_QUERY 0x4c414f55u
#define LAO_MAP_REPLY 0x4c414f56u
#define LAO_MAP_SEND 0x0fu
#define LAO_MAP_RECEIVE 0x93u
#pragma pack(push,1)
typedef struct { uint16_t map,room;float world[3];uint8_t flags; } LaoMapAnchor;
#pragma pack(pop)
static inline int lao_anchor_valid(const LaoMapAnchor *a) {
 if(a->flags&~7u)return 0;
 if(!(a->flags&1))return a->flags==0;
 return a->map!=LAO_UNKNOWN && isfinite(a->world[0]) && isfinite(a->world[1]) && isfinite(a->world[2]) &&
 a->world[0]>=0 && a->world[0]<240 && a->world[2]>=0 && a->world[2]<192 && fabsf(a->world[1])<1000;
}
static inline LaoMapAnchor lao_anchor_sample(uint16_t map,uint16_t room,const float p[3],
 float last_field[3],int *have_field,int vanilla) {
 LaoMapAnchor a={0};a.map=map;a.room=room;
 if(map==lao_name_id("Field")) {
  memcpy(a.world,p,12);a.flags=1;
  if(lao_anchor_valid(&a)){memcpy(last_field,p,12);*have_field=1;}else a.flags=0;
 } else if(map!=LAO_UNKNOWN) {
  if(*have_field){memcpy(a.world,last_field,12);a.flags=3;}
  else if(vanilla && lao_vanilla_anchor(map,a.world))a.flags=7;
 }
 return a;
}
