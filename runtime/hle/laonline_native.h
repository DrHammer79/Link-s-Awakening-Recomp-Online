#pragma once
/* Verified against the owner's 1.0.0 code; runtime fingerprints are mandatory
 * on either supported version. No inference from a game filename or version. */
#include <stdint.h>
#include <math.h>
#define LAO_SHADOW_UPDATE_OFF 0xd0a970u
#define LAO_PLAY_ANIMATION_OFF 0xdeca80u
#define LAO_POP_VISUAL_OFF 0xd117c0u
#define LAO_LOCAL_ANIMATION_OFF 0x1568u
#define LAO_REMOTE_ANIMATION_OFF 0x16e0u
#define LAO_ANIMATION_VALID 0x40u
#define LAO_ANIMATION_EVENT 0x20u

static inline uintptr_t lao_bl_target(uintptr_t at, uint32_t op) {
    if ((op & 0xfc000000u) != 0x94000000u) return 0;
    int64_t displacement = (int64_t)(op & 0x03ffffffu);
    if (displacement & 0x02000000u) displacement -= 0x04000000ll;
    return (uintptr_t)((int64_t)at + displacement * 4);
}
static inline float lao_quaternion_yaw(const float *q) {
    return atan2f(2.f * (q[3]*q[1] + q[0]*q[2]),
                  1.f - 2.f * (q[1]*q[1] + q[2]*q[2]));
}
static inline void lao_yaw_quaternion(float yaw, float *q) {
    q[0] = q[2] = 0.f; q[1] = sinf(yaw * .5f); q[3] = cosf(yaw * .5f);
}
static inline int lao_animation_changed(uint16_t old_id, uint8_t old_flags,
                                       uint16_t id, uint8_t flags) {
    return old_id != id || ((old_flags ^ flags) & LAO_ANIMATION_EVENT);
}
