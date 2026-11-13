/* Executes the production ownership guard and TCP thread without booting a
 * game or accessing any game memory. Unused engine functions are link-GCed. */
#include "../../runtime/hle/laonline.c"
#include <assert.h>
Settings g_settings;
const GameAddrs *g_ga;
int game_in_play(void) { return 0; }
uint64_t doorjump_player(uint64_t frame) { (void)frame; abort(); }
void *hle_alloc(uint64_t size) { (void)size; abort(); }
void guest_call(Cpu *cpu, uint64_t address) { (void)cpu; (void)address; abort(); }
bool col_target(const CosColour *colour, int slot, uint32_t *rgb) {
    (void)colour; (void)slot; (void)rgb; return false;
}
uint64_t plat_now_ns(void) { return (uint64_t)GetTickCount64() * 1000000ull; }
int main(int argc, char **argv) {
    Cpu cpu = {0}, before;
    int64_t control[3] = {0};
    g_rp.phase = 2; /* V5+: actor initializes before the guard holds it */ g_rp.obj = 0x12340000; g_rp.ctrl = (uint64_t)(uintptr_t)control;
    cpu.x[0] = g_rp.obj; before = cpu;
    assert(laonline_native_shadow_update(&cpu) == 1);
    assert(!memcmp(&cpu,&before,sizeof cpu));
    cpu.x[0] = 0x98760000; /* ordinary Shadow Link or local actor */
    assert(laonline_native_shadow_update(&cpu) == 0);
    cpu.x[0] = g_rp.obj; control[1] = -1;
    assert(laonline_native_shadow_update(&cpu) == 0);
    memset(&g_rp,0,sizeof g_rp);
    puts("PASS: production AI guard isolates only the owned live puppet"); fflush(stdout);
    if (argc == 1) return 0;
    snprintf(g_cfg.server,sizeof g_cfg.server,"127.0.0.1:%s",argv[1]);
    snprintf(g_cfg.name,sizeof g_cfg.name,"RuntimeTest");
    pthread_t thread; assert(!pthread_create(&thread,NULL,srv_thread,NULL));
    pthread_detach(thread);
    uint64_t start=now_ms(); int saw_peer=0, published=0;
    while(now_ms()-start<23000) {
        pthread_mutex_lock(&g_srv_lock);
        if (!published && g_srv_session.connected) {
            g_srv_out=(WireState){.map=37,.room=42,.x=3,.y=4,.z=5,.rot_y=1.25f,.anim_id=0x67,.flags=0x40};
            g_srv_out_ms=now_ms(); g_srv_out_ready=1; published=1;
        }
        int ready=lao_peer_ready(&g_srv_session,0,37,42,now_ms());
        if (ready && !saw_peer) {
            assert(g_srv_session.peers[0].state.anim_id==123);
            assert(g_srv_session.peers[0].state.flags==0x60);
            saw_peer=1; puts("PEER_READY"); fflush(stdout);
        }
        int left=saw_peer && !g_srv_session.peers[0].joined;
        pthread_mutex_unlock(&g_srv_lock);
        if (left) { puts("PEER_REMOVED"); return 0; }
        sleep_ms(10);
    }
    fputs("TCP integration timed out\n",stderr); return 1;
}

