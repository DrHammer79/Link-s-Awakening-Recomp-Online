#include "../../runtime/hle/laonline_session.h"
#include <assert.h>
#include <stdio.h>

static void welcome(LaoSession *s) {
    /* local #0, peer #1, name Son, RGB, empty existing progress snapshot */
    const uint8_t b[] = {0x81,0,1,1,3,'S','o','n',10,20,30,0,0,0,0,0,0};
    assert(lao_session_message(s, b, sizeof b, 100));
}
static int state(LaoSession *s, int id, uint16_t map, uint16_t room, uint64_t now) {
    uint8_t b[2 + sizeof(LaoWireState)] = {0x84, (uint8_t)id};
    LaoWireState w = {0};
    w.map = map; w.room = room; w.x = 7; w.y = 2; w.z = -3;
    w.rot_y = 1.25f; w.anim_id = 31; w.anim_frame = 12.5f; w.flags = 3;
    memcpy(b + 2, &w, sizeof w);
    return lao_session_message(s, b, sizeof b, now);
}
int main(void) {
    assert(sizeof(LaoWireState) == 27);
    LaoSession s = {0}; lao_session_reset(&s);
    assert(!state(&s,1,12,13,100));  /* no WELCOME or participant */
    welcome(&s);
    assert(s.local_id == 0 && s.peers[1].joined);
    assert(!lao_peer_ready(&s,1,12,13,100)); /* lobby alone isn't ready */
    assert(state(&s,1,12,13,110));
    assert(!lao_peer_ready(&s,1,12,13,110)); /* late-join cached STATE */
    assert(state(&s,1,12,13,160));
    assert(lao_peer_ready(&s,1,12,13,160));
    assert(lao_peer_ready(&s,1,12,14,160)); /* V6/V10: loaded rooms may differ */
    assert(!lao_peer_ready(&s,1,LAO_UNKNOWN,13,160));
    assert(!lao_peer_ready(&s,1,12,13,159));
    assert(!lao_peer_ready(&s,1,12,13,1661));
    assert(s.peers[1].state.anim_id == 31 && s.peers[1].state.anim_frame == 12.5f &&
           s.peers[1].state.flags == 3 && s.peers[1].state.rot_y == 1.25f);
    uint8_t vitals[] = {0x8b,1,9,12};
    assert(lao_session_message(&s,vitals,sizeof vitals,165));
    assert(s.peers[1].health == 9 && s.peers[1].max_health == 12);
    uint8_t bad_vitals[] = {0x8b,1,13,12};
    assert(!lao_session_message(&s,bad_vitals,sizeof bad_vitals,165));
    uint8_t dup[] = {0x82,1,3,'S','o','n',20,30,40};
    assert(lao_session_message(&s,dup,sizeof dup,170));
    assert(s.peers[1].samples == 2); /* duplicate JOIN does not respawn */
    LaoSession before = s;
    uint8_t malformed[] = {0x82,1,16,'x'};
    assert(!lao_session_message(&s,malformed,sizeof malformed,170));
    assert(!memcmp(&before,&s,sizeof s));
    assert(!state(&s,0,12,13,170)); /* own packets are rejected */
    assert(!state(&s,2,12,13,170)); /* unknown ID is rejected */
    uint8_t nan[2 + sizeof(LaoWireState)] = {0x84,1};
    LaoWireState bad = {0}; bad.x = NAN; memcpy(nan+2,&bad,sizeof bad);
    assert(!lao_session_message(&s,nan,sizeof nan,170));
    assert(state(&s,1,12,14,200));
    assert(lao_peer_ready(&s,1,12,14,200)); /* V9: room change keeps sample readiness */
    assert(state(&s,1,12,14,250));
    assert(lao_peer_ready(&s,1,12,14,250));
    uint8_t leave[] = {0x83,1};
    assert(lao_session_message(&s,leave,sizeof leave,260));
    assert(!lao_peer_ready(&s,1,12,14,260));
    assert(!state(&s,1,12,14,270)); /* late packet after LEAVE */
    assert(lao_session_message(&s,dup,sizeof dup,280));
    assert(state(&s,1,12,14,300));
    assert(!lao_peer_ready(&s,1,12,14,300)); /* reused ID starts fresh */
    assert(state(&s,1,12,14,350));
    assert(lao_peer_ready(&s,1,12,14,350));
    assert(state(&s,1,LAO_UNKNOWN,LAO_UNKNOWN,400));
    assert(!lao_peer_ready(&s,1,12,14,400)); /* menu/save unload */
    lao_session_reset(&s);
    assert(!s.connected && !lao_peer_ready(&s,1,12,14,450));
    welcome(&s);
    assert(s.peers[1].samples == 0); /* reconnect does not replay old readiness */
    assert(lao_room_id("Field","Field_09B") == lao_name_id("09B"));
    assert(lao_room_id("Field","level/Field/Field_09B.leb") == lao_name_id("09B"));
    assert(lao_room_id("Field","09B") == lao_name_id("09B"));
    assert(lao_room_id("Field","") == LAO_UNKNOWN);
    assert(lao_room_id("Field",".leb") == LAO_UNKNOWN);
    puts("PASS: protocol, ownership/readiness, stale/invalid frames, reconnect, room IDs");
}

