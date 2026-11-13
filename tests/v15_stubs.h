/* V15 test stubs: what laonline_v12.c calls in ztarget.c / laonline.c (the game links the real ones). */
#ifndef V15_REAL_ZTARGET
static float zt_pos[3]; static int zt_on, zt_toggles;
void ztarget_toggle(void){zt_toggles++;}
int ztarget_get(float pos[3], int *player){if(zt_on)memcpy(pos,zt_pos,12);if(player)*player=-1;return zt_on;}
void ztarget_note_pvp_hit(void){}
int ztarget_yaw_toward(float dx,float dz,float *yaw){(void)dx;(void)dz;(void)yaw;return 0;}
uint16_t ztarget_damage_anim(void){return 0;}
#endif
static int v15_faced, v15_anim;
int laonline_local_set_yaw(struct Cpu *c,float yaw){(void)c;(void)yaw;v15_faced++;return 1;}
int laonline_local_play_anim(struct Cpu *c,uint16_t id){(void)c;v15_anim=id;return 1;}
static uint64_t v15_pc;
uint64_t doorjump_player(uint64_t frame){(void)frame;return v15_pc;}
/* V15.1: remote player names for death notices (laonline.c in the game) */
int laonline_remote_tag_at(int id,char *name,int cap,float pos[3],uint8_t rgb[3],int *health,int *max_health){(void)pos;(void)rgb;(void)health;(void)max_health;
 static const char *n[]={"Sanic","Velvet","Kit","Raygar"};if(id<0||id>3)return 0;snprintf(name,(size_t)cap,"%s",n[id]);return 1;}
