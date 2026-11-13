#include "../../runtime/hle/laonline_native.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(lao_bl_target(0x1000,0x94000004) == 0x1010);
    assert(lao_bl_target(0x1000,0x97ffffff) == 0xffc);
    assert(!lao_bl_target(0x1000,0xd503201f));
    for (int i=-30;i<=30;++i) {
        float a = (float)i*.1f, q[4]; lao_yaw_quaternion(a,q);
        assert(fabsf(lao_quaternion_yaw(q)-a) < .00001f);
        assert(fabsf(q[1]*q[1]+q[3]*q[3]-1.f) < .00001f);
    }
    assert(!lao_animation_changed(3,0x40,3,0x40));
    assert(lao_animation_changed(3,0x40,3,0x60));
    assert(lao_animation_changed(3,0x40,4,0x40));
    puts("PASS: BL relocation, quaternion facing, repeated native action events");
}
