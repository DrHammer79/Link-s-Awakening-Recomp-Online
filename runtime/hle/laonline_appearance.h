/* Versioned appearance v1: effective colours, default flags, and verified Playerlink mesh schema. */
#pragma once
#include <stdint.h>
#include <string.h>
#define LAO_APPEARANCE_VERSION 1
#define LAO_CAP_QUERY 0x4c414f51u
#define LAO_CAP_REPLY 0x4c414f52u
#define LAO_APPEARANCE_TYPE 0x0b
#define LAO_APPEARANCE_REPLY 0x8d
#define LAO_MESH_COUNT 20
#pragma pack(push, 1)
typedef struct {
    uint8_t version, mask; /* bits0..3: custom tunic/hair/skin/shield; bit4: mesh schema verified */
    uint8_t rgb[4][3];
    uint32_t meshes;
} LaoAppearance;
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(LaoAppearance) == 18, "appearance wire size");
#else
_Static_assert(sizeof(LaoAppearance) == 18, "appearance wire size");
#endif
static inline int lao_appearance_valid(const LaoAppearance *p) {
    return p->version == 1 && !(p->mask & ~31u) && !(p->meshes >> LAO_MESH_COUNT) &&
           ((p->mask & 16) || !p->meshes);
}
static const char *const lao_mesh_names[LAO_MESH_COUNT] = {
    "MagicRod_magicRodJem_low__MagicRod_MI_magicRod",
    "Ocarina_ocarina_low__Ocarina_MI_ocarina",
    "Shovel_handle_low__Shovel_MI_shovel",
    "arm__low__MI_body",
    "belt_low__MI_body",
    "body_low__MI_body",
    "boots_low__MI_boots",
    "face_low__MI_ear",
    "face_low__MI_eye",
    "face_low__MI_mouth",
    "flipperL_low__MI_flippers",
    "hair_low__MI_hair",
    "linkHookShot_handl_low__linkHookShot_MI_hookShot",
    "linkShieldA_bis_low__linkShieldA_MI_shieldA",
    "linkShieldB_bis_low__linkShieldB_MI_shieldB",
    "linkShieldB_bis_low__linkShieldB_MI_shieldBMirror",
    "linkSwordA_blade_low__linkSwordA_MI_sowrdA",
    "linkSwordA_handle_low__linkSwordA_MI_sowrdAball",
    "linkSwordB_blade_low__linkSwordB_MI_sowrdB",
    "linkSwordB_handle_low__linkSwordB_MI_sowrdBball",
};
/* Runtime API. Render thread receives copies, never guest actor pointers. */
struct Cpu;
void laonline_appearance_capture(LaoAppearance *out);
void laonline_appearance_clear(void);
void laonline_appearance_publish(struct Cpu *c, int id, uint64_t actor, const LaoAppearance *profile);
int laonline_appearance_draw(uint64_t palette, uint64_t size, LaoAppearance *out);

void laonline_appearance_forget(int id);
int laonline_appearance_owner(uint64_t palette,uint64_t size);
