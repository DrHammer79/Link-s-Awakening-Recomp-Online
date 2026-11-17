/* V12a: read-only, bounded Windows diagnostics. No guest calls or guest writes. */
#include "../rt.h"
#include "../game_addresses.h"
#ifdef _WIN32
#include <windows.h>
static int look_read(uint64_t at, void *out, size_t n) {
    SIZE_T got = 0;
    return at && ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)at, out, n, &got) && got == n;
}
static uint64_t look_ptr(uint64_t at) {
    uint64_t p = 0; look_read(at, &p, sizeof p); return p;
}
static int look_text(uint64_t at, char *out, size_t cap) {
    size_t i;
    for (i = 0; i + 1 < cap; ++i) {
        unsigned char ch;
        if (!look_read(at + i, &ch, 1) || ch < 32 || ch > 126) break;
        out[i] = (char)ch;
    }
    out[i] = 0; return (int)i;
}
static void look_names(const char *who, uint64_t base) {
    /* Field offsets here are diagnostic candidates, never assumed ownership.
     * Search only 512 bytes and one pointer hop; report provenance on every hit. */
    unsigned hits = 0;
    for (unsigned off = 0; off < 512 && hits < 8; off += 8) {
        uint64_t p = look_ptr(base + off);
        if (!((p >= HEAP_BASE && p < HEAP_BASE + HEAP_MAX) ||
              (p >= MAIN_BASE && p < SDK_BASE))) continue;
        char text[65];
        if (look_text(p, text, sizeof text) >= 4 &&
            (strstr(text, "Link") || strstr(text, "link") || strstr(text, ".bfres"))) {
            LOG("[laonline] V12 look: %s +%#x -> %llx text='%s'\n", who, off, (unsigned long long)p, text);
            ++hits;
        }
    }
    LOG("[laonline] V12 look: %s bounded name scan hits=%u (zero does not prove absent)\n", who, hits);
}
static void look_model(const char *who, uint64_t actor, unsigned off) {
    uint64_t model = look_ptr(actor + off), ctrl = look_ptr(actor + off + 8);
    LOG("[laonline] V12 look: %s actor=%llx model(+%#x)=%llx control=%llx vtable=%llx\n",
        who, (unsigned long long)actor, off, (unsigned long long)model,
        (unsigned long long)ctrl, (unsigned long long)look_ptr(model));
    if (!model) return;
    look_names(who, model);
    /* Log candidate object fields, without calling them model/resource objects. */
    for (unsigned i = 0; i < 8; ++i)
        LOG("[laonline] V12 look: %s model field +%#x=%llx\n", who, i * 8, (unsigned long long)look_ptr(model + i * 8));
    for (unsigned i = 1; i < 8; ++i) {
        uint64_t p = look_ptr(model + i * 8);
        if (p >= HEAP_BASE && p < HEAP_BASE + HEAP_MAX) look_names(who, p);
    }
}
#endif
void laonline_look_diag(Cpu *c, uint64_t obj) {
    (void)c;
#ifdef _WIN32
    static unsigned snapshots;
    const GameVersion *version = game_version();
    if (!version || strcmp(version->key, "v1_0_0") || !g_ga || !obj || snapshots >= 8) return;
    uint64_t status = look_ptr(GA(PLAYER_STATUS_PTR)); char level[65];
    if (!look_text(status, level, sizeof level) || !strncmp(level, "Panel", 5)) return;
    if (look_ptr(obj) != GA(SHADOW_LINK_VTABLE)) return;
    uint64_t mgr = look_ptr(GA(GAME_MGR)), local = look_ptr(mgr + 0x4b8);
    if (!local || local == obj) return;
    LOG("[laonline] V12 look: snapshot %u puppet=%llx level=%s read-only\n", ++snapshots, (unsigned long long)obj, level);
    /* Constant addresses verified from 1.0.0 main_273.c / main_275.c only. */
    static const unsigned names[] = {0x16d91da, 0x16d9199, 0x1701040, 0x16dae6f,
        0x16e867f, 0x16ba421, 0x1701388, 0x16ba458, 0x16d0d02, 0x1701178,
        0x17016d4, 0x16dae74, 0x16cd265, 0x1701054, 0x17016e8, 0x16c2c29,
        0x17016fc, 0x16e6fc6};
    if (snapshots == 1) for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i) {
        char text[81]; look_text(MAIN_BASE + names[i], text, sizeof text);
        LOG("[laonline] V12 look: rodata main+%x text='%s'%s\n", names[i], text,
            names[i] == 0x16d91da ? " (sized string length=20 in d0be50)" : "");
    }
    look_model("puppet", obj, 0x1640);
    look_model("local", local, 0x2a8); /* stored by d2c2e8 -> actor+2a8, control+2b0 */
#else
    (void)obj;
#endif
}
/* V12b: use Link's already loaded resource during this puppet's native setup.
 * The 1.0.0 shadow factory invokes actor vfunc+20 through rt_dispatch at d07738.
 * A temporary routing slot reaches this adapter; it restores that slot BEFORE
 * calling the native initializer. Only the instance from our scoped factory call
 * borrows a resource pointer. Native component factories own all resulting refs.
 * No model instance, skeleton, animation controller, or gameplay data is shared. */
#ifdef _WIN32
static __thread int look_creating;
static uint64_t look_slot, look_original, look_stub;
static GuestFn look_init;
static int look_slot_write(uint64_t value) {
    DWORD prior, ignored;
    if (!VirtualProtect((void *)(uintptr_t)look_slot, 8, PAGE_READWRITE, &prior)) return 0;
    InterlockedExchange64((volatile LONG64 *)(uintptr_t)look_slot, (LONG64)value);
    if (!VirtualProtect((void *)(uintptr_t)look_slot, 8, prior, &ignored))
        LOG("[laonline] V12 look: routing-slot protection restoration failed\n");
    return 1;
}
void laonline_look_end(void) {
    look_creating = 0;
    if (look_slot && look_original && look_ptr(look_slot) == look_stub) {
        if (!look_slot_write(look_original))
            LOG("[laonline] V12 look: cannot restore routing slot; adapter remains pass-through\n");
    }
}
static void look_initialize(Cpu *c) {
    uint64_t obj=c->x[0], archive=0, saved=0, archive_control=0;
    int ours=look_creating && c->x[30]==GA(LAONLINE_SL_SETUP_RET) &&
             look_ptr(obj)==GA(SHADOW_LINK_VTABLE);
    if (ours) {
        laonline_look_end(); /* shared dispatch is restored before native setup */
        uint64_t mgr=look_ptr(GA(GAME_MGR)), local=look_ptr(mgr+0x4b8);
        uint64_t model=look_ptr(local+0x2a8);
        /* Match local Link's d2c26c -> 057d50 resolution exactly. +478 is a
         * resource handle, not an archive pointer. Hold the native returned
         * strong reference through initialization, then release it below. */
        Cpu entry=*c;
        c->sp-=0x20;
        uint64_t result=c->sp;
        ST64(result,0); ST64(result+8,0);
        c->x[0]=result; c->x[1]=local+0x478;
        guest_call(c,GA(LAONLINE_RESOURCE_GET));
        archive=look_ptr(result); archive_control=look_ptr(result+8);
        *c=entry;
        uint64_t metadata=look_ptr(archive+0x80);
        /* 1047360(index 0) produces these exact three fields; check them against
         * the model Link actually rendered, rather than assume entry zero is him. */
        if (!local || local==obj || look_ptr(model)!=GA(LAONLINE_MODEL_COMPONENT_VT) ||
            !archive || !metadata || !look_ptr(model+0x148) ||
            look_ptr(archive+8)!=look_ptr(model+0x168) ||
            look_ptr(archive+0x10)!=look_ptr(model+0x170) ||
            look_ptr(metadata+0x28)!=look_ptr(model+0x178)) {
            LOG("[laonline] V12 look: resolved archive=%llx control=%llx metadata=%llx resource=%llx expected=%llx entry=%llx expected-entry=%llx; identity mismatch, keeping shadow\n",
                (unsigned long long)archive,(unsigned long long)archive_control,(unsigned long long)metadata,
                (unsigned long long)look_ptr(archive+8),(unsigned long long)look_ptr(model+0x168),
                (unsigned long long)look_ptr(metadata+0x28),(unsigned long long)look_ptr(model+0x178));
            archive=0;
        } else {
            saved=look_ptr(obj+0x130);
            ST64(obj+0x130,archive);
            LOG("[laonline] V12 look: redirecting puppet %llx to verified local Link resource %llx\n",
                (unsigned long long)obj,(unsigned long long)archive);
        }
    }
    look_init(c); /* original recompiled function, no dispatch recursion */
    if (ours && archive) {
        ST64(obj+0x130,saved);
        uint64_t model=look_ptr(obj+0x1640), instance=look_ptr(model+0x148);
        LOG("[laonline] V12 look: puppet model=%llx independent instance=%llx resource=%llx; actor resource restored\n",
            (unsigned long long)model,(unsigned long long)instance,(unsigned long long)look_ptr(model+0x168));
    }
    if (archive_control) {
        Cpu result=*c;
        c->x[0]=archive_control; guest_call(c,GA(SP_RELEASE));
        *c=result;
    }
}
void laonline_look_begin(int enabled,const char *level) {
    if (!enabled || !level || !strncmp(level,"Panel",5) || !g_ga || !GA(LAONLINE_SL_SETUP)) return;
    look_slot=GA(SHADOW_LINK_VTABLE)+0x20;
    look_original=GA(LAONLINE_SL_SETUP);
    if (look_ptr(look_slot)!=look_original) {
        LOG("[laonline] V12 look: initializer fingerprint mismatch; keeping shadow appearance\n"); return;
    }
    look_init=dispatch_lookup(look_original);
    if (!look_init) { LOG("[laonline] V12 look: native initializer unavailable; keeping shadow appearance\n"); return; }
    if (!look_stub) look_stub=hle_register("laonline_v12_puppet_init",look_initialize);
    if (!look_slot_write(look_stub)) {
        LOG("[laonline] V12 look: initializer routing unavailable; keeping shadow appearance\n"); return;
    }
    look_creating=1;
}
#else
void laonline_look_begin(int enabled,const char *level) { (void)enabled; (void)level; }
void laonline_look_end(void) {}
#endif

/* V13: verified 1.0.0 Playerlink instances. Exact palette ownership was independently
 * matched to both live Vulkan frame buffers on 2026-10-25; no screen-position matching. */
#include "laonline_appearance.h"
#include "../settings.h"
#include "../cosmetic_colour.h"
#include <pthread.h>
#include <stdatomic.h>
static atomic_uint appearance_active;
static pthread_mutex_t appearance_lock = PTHREAD_MUTEX_INITIALIZER;
#include "laonline_session.h"
static struct { uint64_t palette[2], size; LaoAppearance profile; } appearance_draws[LAO_PLAYERS];
void laonline_appearance_clear(void) {
    pthread_mutex_lock(&appearance_lock);
    memset(appearance_draws, 0, sizeof appearance_draws);
    atomic_store_explicit(&appearance_active,0,memory_order_release);
    pthread_mutex_unlock(&appearance_lock);
}
#ifdef _WIN32
static int appearance_model(uint64_t actor, unsigned offset, uint64_t *instance, uint64_t *g3d) {
    if (!g_ga || !GA(LAONLINE_MODEL_COMPONENT_VT) || !actor) return 0;
    uint64_t component = look_ptr(actor + offset);
    if (look_ptr(component) != GA(LAONLINE_MODEL_COMPONENT_VT)) return 0;
    *instance = look_ptr(component + 0x148);
    if (look_ptr(*instance) != GA(LAONLINE_MODEL_INSTANCE_VT)) return 0;
    *g3d = look_ptr(look_ptr(*instance + 0x60) + 0x10);
    return *g3d != 0;
}
static int appearance_meshes(uint64_t instance, uint64_t model, uint8_t bits[20]) {
    uint16_t count = 0;
    if (!look_read(model + 0x30, &count, 2) || count != LAO_MESH_COUNT) return 0;
    uint64_t shapes = look_ptr(model + 0x40);
    for (unsigned i = 0; i < LAO_MESH_COUNT; ++i) {
        uint64_t resource = look_ptr(shapes + i * 0x70);
        char name[96];
        look_text(look_ptr(resource + 8) + 2, name, sizeof name);
        if (strcmp(name, lao_mesh_names[i])) return 0;
    }
    if (!look_read(look_ptr(instance + 0x378), bits, LAO_MESH_COUNT)) return 0;
    for (unsigned i = 0; i < LAO_MESH_COUNT; ++i) if (bits[i] > 1) return 0;
    return 1;
}
#endif
void laonline_appearance_capture(LaoAppearance *out) {
    memset(out, 0, sizeof *out); out->version = 1;
    const CosColour *colours[] = {&g_settings.tunic, &g_settings.hair, &g_settings.skin, &g_settings.shield};
    const int slots[] = {COL_SLOT_TUNIC, COL_SLOT_HAIR, COL_SLOT_SKIN, COL_SLOT_SHIELD};
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t rgb;
        if (!col_target(colours[i], slots[i], &rgb)) continue;
        out->mask |= 1u << i;
        out->rgb[i][0] = rgb >> 16; out->rgb[i][1] = rgb >> 8; out->rgb[i][2] = rgb;
    }
#ifdef _WIN32
    if (!g_ga) return;
    uint64_t mgr = look_ptr(GA(GAME_MGR)), local = look_ptr(mgr + 0x4b8), instance, model;
    uint8_t bits[20];
    if (appearance_model(local, 0x2a8, &instance, &model) && appearance_meshes(instance, model, bits)) {
        out->mask |= 16;
        for (unsigned i = 0; i < 20; ++i) out->meshes |= (uint32_t)bits[i] << i;
    }
#endif
}

void laonline_appearance_forget(int id) {
    if(id<0||id>=LAO_PLAYERS)return;
    pthread_mutex_lock(&appearance_lock);
    memset(&appearance_draws[id],0,sizeof appearance_draws[id]);
    int active=0;for(int i=0;i<LAO_PLAYERS;i++)if(appearance_draws[i].size)active=1;
    atomic_store_explicit(&appearance_active,active,memory_order_release);
    pthread_mutex_unlock(&appearance_lock);
}
int laonline_appearance_owner(uint64_t palette,uint64_t size) {
    int id=-1;pthread_mutex_lock(&appearance_lock);
    for(int i=0;i<LAO_PLAYERS;i++)if(size && size==appearance_draws[i].size && (palette==appearance_draws[i].palette[0]||palette==appearance_draws[i].palette[1])){id=i;break;}
    pthread_mutex_unlock(&appearance_lock);return id;
}
void laonline_appearance_publish(Cpu *c, int id, uint64_t actor, const LaoAppearance *profile) {
    if (id < 0 || id >= LAO_PLAYERS) return;
    if(!lao_appearance_valid(profile)){laonline_appearance_forget(id);return;}
#ifdef _WIN32
    uint64_t instance, model;
    if (!g_ga || look_ptr(actor) != GA(SHADOW_LINK_VTABLE) || !appearance_model(actor, 0x1640, &instance, &model)) { laonline_appearance_forget(id); return; }
    uint8_t bits[20];
    if ((profile->mask & 16) && appearance_meshes(instance, model, bits)) {
        Cpu saved = *c;
        uint64_t component = look_ptr(actor + 0x1640);
        /* Native shape-index setter (LAONLINE_MESH_SET_VISIBLE), independently verified from
         * the name setter beside it. Only this owning remote instance is changed. */
        for (unsigned i = 0; i < 20; ++i) if (bits[i] != ((profile->meshes >> i) & 1)) {
            c->x[0] = component; c->x[1] = i; c->x[2] = (profile->meshes >> i) & 1;
            guest_call(c, GA(LAONLINE_MESH_SET_VISIBLE));
        }
        *c = saved;
    }
    uint64_t skeleton = look_ptr(model + 0x38), pool = look_ptr(skeleton + 0x30);
    uint64_t base = look_ptr(pool + 8), offset = look_ptr(skeleton + 0x38), size = look_ptr(skeleton + 0x48);
    /* The schema has 47 skinning matrices, 48 bytes each. Two 256-aligned buffers.
     * Reject changed layouts instead of assigning unrelated geometry a profile. */
    if (size != 2256 || base < HEAP_BASE || base >= HEAP_BASE + HEAP_MAX ||
        offset > HEAP_MAX || base + offset + 4608 > HEAP_BASE + HEAP_MAX) { laonline_appearance_forget(id); return; }
    pthread_mutex_lock(&appearance_lock);
    memset(&appearance_draws[id],0,sizeof appearance_draws[id]);
    static uint64_t logged_actor[LAO_PLAYERS];
    static LaoAppearance logged_profile[LAO_PLAYERS];
    if (logged_actor[id] != actor || memcmp(&logged_profile[id],profile,sizeof *profile)) {
        LOG("[laonline] V13 player #%d appearance bound actor=%llx palettes=%llx/%llx mask=%02x meshes=%05x\n",
            id,(unsigned long long)actor,(unsigned long long)(base+offset),
            (unsigned long long)(base+offset+2304),profile->mask,profile->meshes);
        logged_actor[id]=actor;logged_profile[id]=*profile;
    }
    appearance_draws[id].palette[0] = base + offset;
    appearance_draws[id].palette[1] = base + offset + 2304;
    appearance_draws[id].size = size;
    appearance_draws[id].profile = *profile;
    atomic_store_explicit(&appearance_active,1,memory_order_release);
    pthread_mutex_unlock(&appearance_lock);
#else
    (void)c; (void)actor;
#endif
}
int laonline_appearance_draw(uint64_t palette, uint64_t size, LaoAppearance *out) {
    if (!atomic_load_explicit(&appearance_active,memory_order_acquire)) return 0;
    int found = 0;
    pthread_mutex_lock(&appearance_lock);
    for (unsigned i = 0; i < LAO_PLAYERS; ++i)
        if (size && size == appearance_draws[i].size &&
            (palette == appearance_draws[i].palette[0] || palette == appearance_draws[i].palette[1])) {
            *out = appearance_draws[i].profile; found = 1;
            static unsigned logged;
            if (logged++ < 8) LOG("[laonline] V13 render matched player #%u palette=%llx mask=%02x\n",
                i,(unsigned long long)palette,out->mask);
            break;
        }
    pthread_mutex_unlock(&appearance_lock);
    return found;
}
