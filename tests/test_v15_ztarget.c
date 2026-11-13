/* V15: Z-targeting (runtime/hle/ztarget.c) - self-calibration, lock on a moving enemy (not a pot), facing, circling
 * with the stick, toggle off, and the learned damage animation. Simulated memory, no game. */
#include <windows.h>
#include <assert.h>
#include "../runtime/hle/ztarget.c"
const GameAddrs *g_ga;
static uint64_t frame=100;
uint64_t plat_now_ns(void){return frame*16000000ull;}
int game_in_play(void){return 1;}
bool menu_input_captured(void){return false;}
int laonline_v12_peer(int id,float pos[3],uint16_t *map,int *local){(void)id;(void)pos;(void)map;*local=0;return 0;}
int laonline_v12_pvp_local(void){return 0;}
int laonline_v12_pvp_peer(int id){(void)id;return 0;}
#define H(o) (HEAP_BASE+(o))
uint64_t laonline_local_link(void){return H(0x1000);}
static float link_yaw,faced_yaw;static int faced;
int laonline_local_yaw(float *y){*y=link_yaw;return 1;}
int laonline_local_set_yaw(Cpu *c,float y){(void)c;faced_yaw=y;faced++;return 1;}
static uint16_t anim_id;static uint64_t anim_at;
uint16_t laonline_local_anim(uint64_t *at){*at=anim_at;return anim_id;}
uint64_t doorjump_player(uint64_t f){(void)f;return H(0x3000);}
static float *me_pc,*me_tr;
static void place(float x,float y,float z){me_pc[0]=x;me_pc[1]=y;me_pc[2]=z;me_tr[0]=x;me_tr[1]=y;me_tr[2]=z;}
static float *actor(uint64_t o,uint64_t vt,float x,float z){ST64(H(o),vt);ST64(H(o)+0x80,H(o+0x1000));float *p=(float*)(uintptr_t)(H(o+0x1000)+0x84);p[0]=x;p[1]=0;p[2]=z;return p;}
int main(void){
 assert(VirtualAlloc((void*)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 assert(VirtualAlloc((void*)(uintptr_t)HEAP_BASE,0x100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 static GameAddrs ga={.LIVE_SAVE=MAIN_BASE+0x1cbb220,.PLAYER_STATUS_PTR=MAIN_BASE+0x100000,.SHADOW_LINK_VTABLE=MAIN_BASE+0x777};g_ga=&ga;
 ST64(MAIN_BASE+0x100000,MAIN_BASE+0x200000);ST16(MAIN_BASE+0x200000+0x84,12);  /* PlayerStatus: 3 hearts */
 ST64(H(0x1000)+0x80,H(0x2000));me_tr=(float*)(uintptr_t)(H(0x2000)+0x84);me_pc=(float*)(uintptr_t)(H(0x3000)+0x80);
 remove("laonline-damage-anim.txt");
 int pass=1;
 #define T(name,ok) do{int _o=!!(ok);printf("%-62s %s\n",name,_o?"PASS":"FAIL");pass&=_o;}while(0)
 Cpu c={0};int32_t lx,ly;
 /* walk right (+x), stick right; then walk "up" the screen (-z), stick up: the game turns Link as he moves */
 float x=0,z=0;place(x,0,z);
 for(int i=0;i<320;i++){x+=.06f;place(x,0,z);link_yaw=atan2f(-1,0);  /* this game's convention here: atan2(-dx, dz) */
   lx=32767;ly=0;ztarget_stick(&lx,&ly);frame++;ztarget_tick(&c,frame);}
 for(int i=0;i<320;i++){z-=.06f;place(x,0,z);link_yaw=atan2f(0,-1);lx=0;ly=32767;ztarget_stick(&lx,&ly);frame++;ztarget_tick(&c,frame);}
 T("actor position found in Link's transform (+0x84)",g_pos_off==0x84);
 T("Link's facing convention learned (atan2(-dx, dz))",g_yaw_conv==1);
 T("stick directions learned (right = +x, up = -z)",g_axes_known&&g_ax==1&&g_az==-1);
 /* a Moblin walking about 3 ahead, a pot 1.5 away that never moves */
 float *mob=actor(0x10000,MAIN_BASE+0x900,x,z-3),*pot=actor(0x20000,MAIN_BASE+0x901,x+1.5f,z);(void)pot;
 for(int i=0;i<40;i++){mob[0]=x+.06f*(i%10);frame++;ztarget_actor_seen(H(0x10000));ztarget_actor_seen(H(0x20000));ztarget_tick(&c,frame);}
 ztarget_toggle();frame++;ztarget_actor_seen(H(0x10000));ztarget_actor_seen(H(0x20000));ztarget_tick(&c,frame);
 float tp[3];int pl=0;
 T("ZL: locks on the moving enemy, not the (closer) pot",ztarget_get(tp,&pl)&&fabsf(tp[2]-(z-3))<.01f&&pl==-1);
 T("Link turns to face it",faced>0&&ang_diff(faced_yaw,yaw_for(1,tp[0]-x,tp[2]-z))<.01f);
 /* circling: stick right while the target is straight up the screen -> around it, same distance */
 /* the Moblin up and a bit left: stick right must go around it (right and a little up, on the circle), not straight right */
 mob[0]=x-1;mob[2]=z-3;frame++;ztarget_actor_seen(H(0x10000));ztarget_tick(&c,frame);
 lx=32767;ly=0;ztarget_stick(&lx,&ly);
 printf("   stick right -> (%d, %d)\n",lx,ly);
 T("stick right while locked: goes around the target (tangent)",lx>25000&&ly>5000);
 mob[0]=x;mob[2]=z-3;frame++;ztarget_actor_seen(H(0x10000));ztarget_tick(&c,frame);
 lx=0;ly=32767;ztarget_stick(&lx,&ly);
 T("stick up while locked: straight to the target (unchanged)",lx==0&&ly==32767);
 /* it walks away */
 mob[2]=z-20;for(int i=0;i<3;i++){frame++;ztarget_actor_seen(H(0x10000));ztarget_tick(&c,frame);}
 T("target 20 away: lock lets go",!ztarget_get(tp,NULL));
 mob[2]=z-3;frame++;ztarget_actor_seen(H(0x10000));ztarget_toggle();frame++;ztarget_actor_seen(H(0x10000));ztarget_tick(&c,frame);
 T("ZL again: locked again",ztarget_get(tp,NULL));
 ztarget_toggle();frame++;ztarget_actor_seen(H(0x10000));ztarget_tick(&c,frame);
 T("ZL once more: off",!ztarget_get(tp,NULL));
 frame+=200;ztarget_toggle();frame++;ztarget_tick(&c,frame);
 T("nothing moving nearby: nothing locked",!ztarget_get(tp,NULL));
 /* damage animation: an enemy hits Link twice, he plays animation 0x31 each time */
 for(int hit=0;hit<2;hit++){
   int hp=(int16_t)LD16(MAIN_BASE+0x200000+0x84);ST16(MAIN_BASE+0x200000+0x84,hp-2);frame+=60;ztarget_tick(&c,frame);
   frame+=3;anim_id=0x31;anim_at=frame*16;ztarget_tick(&c,frame);anim_id=0x67;anim_at=frame*16+500;frame+=40;ztarget_tick(&c,frame);}
 T("damage animation learned from two enemy hits (0x31)",ztarget_damage_anim()==0x31);
 FILE *f=fopen("laonline-damage-anim.txt","r");unsigned v=0;if(f){if(fscanf(f,"%u",&v)!=1)v=0;fclose(f);}
 T("...and kept for next time (laonline-damage-anim.txt)",v==0x31);
 /* a PvP hit must not teach it anything */
 ztarget_note_pvp_hit();ST16(MAIN_BASE+0x200000+0x84,2);frame+=2;ztarget_tick(&c,frame);anim_id=0x55;anim_at=frame*16;frame+=3;ztarget_tick(&c,frame);
 T("PvP hits don't count as enemy hits",g_dmg_n==1);
 remove("laonline-damage-anim.txt");
 return pass?0:1;
}
