/* Executes the production ownership guard and TCP thread without booting a
 * game or accessing any game memory. Unused engine functions are link-GCed. */
#include "../../runtime/hle/laonline.c"
#include <assert.h>
Settings g_settings;
const GameAddrs *g_ga;
int game_in_play(void) { return 0; }
uint64_t doorjump_player(uint64_t frame) { (void)frame; abort(); }
void *hle_alloc(uint64_t size) { (void)size; abort(); }
static int ready, creates, releases, kills;
static uint64_t scene, actor, actor_control;
int laonline_level_ready(void) { return ready; }
void guest_call(Cpu *cpu, uint64_t address) {
    if (address == GA(SCENE_LOCK)) { ST64(cpu->x[0], scene); ST64(cpu->x[0]+8, 123); }
    else if (address == GA(SP_RELEASE)) ++releases;
    else if (address == GA(WEAK_RELEASE)) {}
    else if (address == GA(ACTOR_KILL)) { ++kills; ST64(actor_control+8, (uint64_t)-1); }
    else if (address == GA(CREATE_ACTOR)) {
        ++creates; ST64(actor, GA(SHADOW_LINK_VTABLE)); ST64(actor_control+8, 0);
        ST64(cpu->x[0], actor); ST64(cpu->x[0]+8, actor_control);
    } else abort();
}
bool col_target(const CosColour *colour, int slot, uint32_t *rgb) {
    (void)colour; (void)slot; (void)rgb; return false;
}
uint64_t plat_now_ns(void) { return (uint64_t)GetTickCount64() * 1000000ull; }

static void holder(uint64_t h, uint64_t info, uint64_t room, const char *name, int loaded) {
    ST64(h+0x50,h); ST64(h+0x1a8,info); ST64(h+0x1b8,room); ST64(h+0x1c0,room+0x80);
    ST32(h+0x240,loaded); strcpy((char *)(uintptr_t)(info+0x74),name);
}
int main(void) {
    assert(VirtualAlloc((void *)(uintptr_t)HEAP_BASE,0x100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(VirtualAlloc((void *)(uintptr_t)MAIN_BASE,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    static GameAddrs ga; g_ga=&ga;
    ga.GAME_MGR=HEAP_BASE; ga.SCENE_LOCK=1; ga.SP_RELEASE=2; ga.WEAK_RELEASE=3;
    ga.ACTOR_KILL=4; ga.CREATE_ACTOR=5; ga.SHADOW_LINK_VTABLE=MAIN_BASE+0x1c16730;
    ST64(HEAP_BASE,HEAP_BASE+0x1000); scene=HEAP_BASE+0x2000;
    uint64_t h=HEAP_BASE+0x10000;
    ST64(scene+0x228,h); g_room_args=HEAP_BASE+0x3000; g_spawn_args=HEAP_BASE+0x4000;
    actor=HEAP_BASE+0x60000; actor_control=HEAP_BASE+0x65000;
    ST32(MAIN_BASE+LAO_SHADOW_UPDATE_OFF,0xd10103ffu);
    ST32(MAIN_BASE+LAO_PLAY_ANIMATION_OFF,0xd10183ffu);
    ST32(MAIN_BASE+LAO_POP_VISUAL_OFF,0xf81e0ff3u);
    ST32(MAIN_BASE+0xd15b08,0x94000004); ST32(MAIN_BASE+0xd106c8,0x94000004);
    holder(h,HEAP_BASE+0x50000,HEAP_BASE+0x70000,"Field_01A.leb",0);
    holder(h+0x260,HEAP_BASE+0x51000,HEAP_BASE+0x71000,"path/Field_01B.leb",1);
    g_rp.valid=1; g_rp.at=100; g_rp.st.id=7; g_cfg.type=0x21e;
    strcpy(g_rp.st.level,"Field"); g_rp_room=lao_room_id("Field","Field_01B");
    Cpu c={0}; uint64_t room,control;
    assert(remote_room_pair(&c,"Field",&room,&control) && room==HEAP_BASE+0x71000);
    assert(releases==1);
    puppet_tick(&c,100,"Field","Field_01A"); assert(!creates);
    ready=1; puppet_tick(&c,101,"Field","Field_01A"); assert(!creates);
    puppet_tick(&c,106,"Field","Field_01A"); assert(!creates);
    puppet_tick(&c,107,"Field","Field_01A"); assert(creates==1 && g_rp.owner_room==room);
    g_rp.popped_id=7; ST64(actor_control+8,(uint64_t)-1);
    puppet_tick(&c,108,"Field","Field_01A"); assert(creates==2 && g_rp.popped_id==7);
    g_rp_room=lao_room_id("Field","Field_01A");
    puppet_tick(&c,109,"Field","Field_01A"); assert(kills==1 && creates==2);
    puppet_tick(&c,115,"Field","Field_01A"); assert(creates==3 && g_rp.owner_room==HEAP_BASE+0x70000);
    ready=0; puppet_tick(&c,116,"Field","Field_01A"); assert(creates==3);
    ready=1; g_rp_room=LAO_UNKNOWN; puppet_tick(&c,117,"Field","Field_01A"); assert(!g_rp.ctrl && kills==2);
    g_rp_room=lao_room_id("Field","Field_01B"); ST32(h+0x260+0x240,0);
    assert(!remote_room_pair(&c,"Field",&room,&control));
    ST32(h+0x260+0x240,1);
    holder(h+0x4c0,HEAP_BASE+0x52000,HEAP_BASE+0x72000,"Field_01B.leb",1);
    assert(!remote_room_pair(&c,"Field",&room,&control));
    puts("PASS A1: transition gate, stable wait, remote holder, immediate unload retry, room migration, quiet-join memory, unloaded/unknown/ambiguous rooms");
    return 0;
}
