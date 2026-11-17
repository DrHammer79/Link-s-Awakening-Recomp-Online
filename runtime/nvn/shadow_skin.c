// LA-Online: the remote player's Link is a Shadow Link actor (runtime/hle/laonline.c). Here his dark textures are
// replaced with Link's own: when one of Shadow Link's textures is uploaded (recognised by its bytes, as texpatch.c
// recognises the tunic), its guest memory is overwritten with the matching Playerlink.bfres texture before the
// backend copies it - same name, size, format and mip count, so the block-linear layout is the same.
// Only while laonline.ini exists. Every Shadow Link then looks like Link (Dampe's chamber too).
// LAONLINE_NO_SKIN=1: off. Logs "[skin]" lines: which textures were paired.
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "nvn.h"

typedef struct {
    char name[64];
    uint32_t w, h, levels, format, bhl, size;
    uint8_t *data;
} Tex;

typedef struct { Tex sh; uint8_t *link; } Pair;

static Pair g_pairs[64];
static int g_np;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static uint8_t *read_file(const char *rel, size_t *len) {
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", g_romfs_dir ? g_romfs_dir : "extracted/romfs", rel);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) free(b), b = NULL;
    fclose(f);
    *len = b ? (size_t)n : 0;
    return b;
}

// All textures of the BNTX inside a model file (data pointers into `f`)
static int list_textures(const uint8_t *f, size_t len, Tex *out, int max) {
    size_t base = (size_t)-1;
    for (size_t i = 0; i + 8 < len; i += 4)
        if (!memcmp(f + i, "BNTX\0\0\0\0", 8)) { base = i; break; }
    if (base == (size_t)-1 || base + 0x30 > len || memcmp(f + base + 0x20, "NX  ", 4)) return 0;
    const uint8_t *b = f + base;
    size_t bl = len - base;
    uint32_t count = rd32(b + 0x24);
    uint64_t infos = rd64(b + 0x28);
    int n = 0;
    for (uint32_t i = 0; i < count && n < max && infos + 8 * (i + 1) <= bl; i++) {
        uint64_t brti = rd64(b + infos + 8 * i);
        if (brti + 0x90 > bl || memcmp(b + brti, "BRTI", 4)) continue;
        const uint8_t *ti = b + brti + 16;
        uint64_t name_at = rd64(ti + 80), dptrs = rd64(ti + 96);
        if (name_at + 2 > bl || dptrs + 8 > bl) continue;
        uint16_t nl = rd16(b + name_at);
        if (nl >= sizeof out->name || name_at + 2 + nl > bl) continue;
        Tex *t = &out[n];
        memcpy(t->name, b + name_at + 2, nl), t->name[nl] = 0;
        t->levels = rd16(ti + 6), t->format = rd32(ti + 12), t->w = rd32(ti + 20), t->h = rd32(ti + 24);
        t->bhl = rd32(ti + 36), t->size = rd32(ti + 64);
        uint64_t d = rd64(b + dptrs);
        if (!t->size || d + t->size > bl) continue;
        t->data = (uint8_t *)(b + d);
        n++;
    }
    return n;
}

static int has_ci(const char *s, const char *sub) {
    for (; *s; s++) {
        size_t i = 0;
        while (sub[i] && tolower((unsigned char)s[i]) == sub[i]) i++;
        if (!sub[i]) return 1;
    }
    return 0;
}

static void load(void) {
    FILE *ini = fopen("laonline.ini", "r");
    LOG("[skin] start (laonline.ini %s)\n", ini ? "found" : "not found in the working folder");
    if (!ini || getenv("LAONLINE_NO_SKIN")) { if (ini) fclose(ini); return; }
    fclose(ini);
    size_t ll;
    uint8_t *lf = read_file("region_common/actor/Playerlink.bfres", &ll);  // (kept for the process' life)
    static Tex lt[256], st[256];
    int nl = lf ? list_textures(lf, ll, lt, 256) : 0;
    LOG("[skin] Playerlink.bfres: %d textures\n", nl);
    for (int i = 0; i < nl; i++) LOG("[skin]   link %s %ux%u fmt %#x mips %u\n", lt[i].name, lt[i].w, lt[i].h, lt[i].format, lt[i].levels);
    char dir[1200];
    snprintf(dir, sizeof dir, "%s/region_common/actor", g_romfs_dir ? g_romfs_dir : "extracted/romfs");
    DIR *d = opendir(dir);
    for (struct dirent *e; d && (e = readdir(d));) {
        size_t l = strlen(e->d_name);
        if (l < 7 || strcmp(e->d_name + l - 6, ".bfres") || !has_ci(e->d_name, "shadowlink")) continue;
        char rel[300];
        snprintf(rel, sizeof rel, "region_common/actor/%s", e->d_name);
        size_t sl;
        uint8_t *sf = read_file(rel, &sl);
        int ns = sf ? list_textures(sf, sl, st, 256) : 0;
        LOG("[skin] %s: %d textures\n", e->d_name, ns);
        for (int i = 0; i < ns; i++) {
            const Tex *s = &st[i];
            int m = -1;
            for (int k = 0; k < nl && m < 0; k++)
                if (!strcmp(lt[k].name, s->name)) m = k;
            const Tex *L = m >= 0 ? &lt[m] : NULL;
            int ok = L && L->w == s->w && L->h == s->h && L->format == s->format && L->levels == s->levels &&
                     L->size == s->size && L->bhl == s->bhl && memcmp(L->data, s->data, s->size);
            LOG("[skin]   shadow %s %ux%u fmt %#x mips %u -> %s\n", s->name, s->w, s->h, s->format, s->levels,
                ok ? "Link's" : !L ? "no Link texture of that name" : !memcmp(L->data, s->data, s->size) ? "already Link's" : "differs in shape");
            if (ok && g_np < 64) {
                Pair *p = &g_pairs[g_np++];
                p->sh = *s;
                p->sh.data = malloc(s->size), memcpy(p->sh.data, s->data, s->size);
                p->link = L->data;
            }
        }
        free(sf);
    }
    if (d) closedir(d);
    LOG("[skin] %d Shadow Link textures will be drawn as Link's\n", g_np);
}

// Called by texpatch_identify for every newly uploaded texture (before the backend reads its memory)
void shadow_skin_swap(const NvnTexture *t, uint64_t total) {
    pthread_once(&g_once, load);
    for (int i = 0; i < g_np; i++) {
        const Tex *s = &g_pairs[i].sh;
        if (s->w != t->width || s->h != t->height || s->levels != t->levels || s->size > total) continue;
        if (memcmp((const void *)(uintptr_t)t->addr, s->data, s->size)) continue;
        memcpy((void *)(uintptr_t)t->addr, g_pairs[i].link, s->size);
        LOG("[skin] Shadow Link's %s uploaded: drawn as Link's\n", s->name);
        return;
    }
}
