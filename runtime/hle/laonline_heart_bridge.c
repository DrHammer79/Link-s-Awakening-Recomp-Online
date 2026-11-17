#include <string.h>
// Runtime observer for native ItemRecoveryHeart behavior.
// Factory chain: ItemRecoveryHeart -> main+8a24e0 -> 8a2800 -> 8a2860 (1.0.0 / 1.0.1),
// behavior vtable LAONLINE_HEART_VT, collection member LAONLINE_HEART_PICKUP_SLOT = LAONLINE_HEART_PICKUP.
// Unlike scripted AddItem this method writes PlayerStatus+84 directly, then sets
// the one-shot collected byte +39a. Observe completion, never infer HP deltas.
#include "../hle.h"
#include "../game_addresses.h"
#include "laonline_diag.h"
#ifdef _WIN32
#include <windows.h>
static GuestFn original;
static uint64_t stub;
static int installed,warned;
extern void laonline_coop_pickup(int,int,int);
static void collected(Cpu *c){
 uint64_t obj=c->x[0];int amount=(int)(int32_t)c->x[2];
 int ours=obj>=HEAP_BASE&&obj+0x480<HEAP_BASE+HEAP_MAX&&
  LD64(obj)==GA(LAONLINE_HEART_VT)&& !LD8(obj+0x39a)&&amount>0&&amount<=400&&amount%4==0;
 original(c);
 if(ours&&LD8(obj+0x39a)){
  Cpu result=*c;laonline_coop_pickup(100,amount/4,-1);*c=result;
  LOG("[laonline] M7 native recovery collected object=%llx quarter_hearts=%d\n",(unsigned long long)obj,amount);
 }
}
void laonline_heart_bridge_install(void){
 if(installed||!g_ga||!GA(LAONLINE_HEART_PICKUP))return;
 uint64_t slot=GA(LAONLINE_HEART_PICKUP_SLOT),fn=GA(LAONLINE_HEART_PICKUP);
 if(LD32(fn)!=0xf81c0ff7u||LD32(fn+0x14)!=0x394e6808u||LD32(fn+0x74)!=0x79010928u||LD32(fn+0xd8)!=0x390e6a68u){if(!warned++){LOG("[laonline] Recovery observer unavailable: native fingerprint mismatch; no native patch applied\n");laonline_diag("recovery.observer_rejected",0xffff,0xffff,0,0,0);}return;}
 if(!stub)stub=hle_register("laonline_native_recovery_collected",collected);
 if(LD64(slot)!=fn&&LD64(slot)!=stub)return;
 original=dispatch_lookup(fn);if(!original)return;
 DWORD prior,ignored;if(!VirtualProtect((void*)(uintptr_t)slot,8,PAGE_READWRITE,&prior))return;
 InterlockedExchange64((volatile LONG64*)(uintptr_t)slot,(LONG64)stub);
 VirtualProtect((void*)(uintptr_t)slot,8,prior,&ignored);installed=1;laonline_diag("recovery.observer_installed",0xffff,0xffff,0,0,1);
 LOG("[laonline] M7 native recovery observer installed: fingerprint verified, AddItem bypass covered\n");
}
RT_ON_RESET(laonline_heart_bridge_reset){installed=warned=0;original=NULL;stub=0;}
#else
void laonline_heart_bridge_install(void){}
#endif
