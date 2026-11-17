#!/bin/bash
# tools/coop_tests/online_relay.sh: online play (LA-Online) through a relay, two copies of the game on this PC and the
# stand-in relay (tools/laonline_relay.py, port 47630; LA-Online's own isn't public). Alice and Bob boot into the seed's
# file 1 in Story Co-op; at frame 2500 Alice gets a Boomerang, 5 bombs and the Seashell Sensor (LARECOMP_GIVE_ITEM: the
# game's AddItem, as a pickup would). Expected: both join the relay and see each other's Link; Alice's AddItem is reported to the team as
# her pickup (the bombs) and as shared progress (the Boomerang's item bit, key 0x7105); Bob gets both through the
# game's own AddItem ("team grant" in his log: no direct save writes, LAS Recomp's review point 2), and nothing is
# reported back from Bob as his own. Alice plays with 2 local players: her Player 2 goes on the relay as a player of his
# own ("Alice P2", runtime/hle/laonline_local.c) - Bob sees him too, Alice doesn't see him as a remote one. Prints PASS /
# FAIL for each. (Rupees take the bombs' path; the seed's wallet is
# full, so they can't show here.)
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
PORT=${PORT:-47630}
mkdir -p save/save_0 build/frames
rm -f save/save_0/savedata*.bin save/save_0/screenshot*.bin; cp ${SEED:-build/save_seed}/* save/save_0/  # (once, for both)
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
python -u tools/laonline_relay.py $PORT > build/relay.log 2>&1 &
RELAY=$!
sleep 1
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <name> <local players> <P1 walk> [env...]
    local id=$1 name=$2 n=$3 walk=$4; shift 4
    tools/coop_tests/run_test.sh $id ${SECS:-110} LARECOMP_COOP=$n LARECOMP_COOP_LOG=1 LARECOMP_HIDDEN=1 \
        "LARECOMP_INPUT=$TITLE,$walk" LARECOMP_INPUT_P2=9999:A:1 LAONLINE_MODE=server \
        LAONLINE_SERVER=127.0.0.1:$PORT LAONLINE_NAME=$name "$@" > /dev/null 2>&1
}
run online_alice Alice 2 "2700:STICK_LEFT:150" LARECOMP_GIVE_ITEM=2500:7:1:-1,2500:4:5:-1,2500:116:1:-1 &
sleep 2
run online_bob Bob 1 "2700:STICK_RIGHT:150" &
wait $(jobs -p | grep -v "^$RELAY$")
kill $RELAY 2>/dev/null
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
check "relay: Alice and Bob joined" 'grep -q "join .* Alice" build/relay.log && grep -q "join .* Bob" build/relay.log'
check "Alice sees Bob's Link" 'grep -qF "[laonline] remote Link '"'"'Bob'"'"' spawned" build/run_online_alice.log'
check "Bob sees Alice's Link" 'grep -qF "[laonline] remote Link '"'"'Alice'"'"' spawned" build/run_online_bob.log'
check "Alice's bombs reported as her pickup" 'grep -q "pickup 4 x5 .* from 0" build/relay.log'
check "the Boomerang shared as progress (key 0x7105)" 'grep -q "item 0x7105 = 1 from 0" build/relay.log'
check "Bob got the bombs through AddItem" 'grep -qF "[laonline] team grant: item 4 x5" build/run_online_bob.log'
check "Bob has the Boomerang (a reward: AddItem)"     'grep -oE "progression.audit .*local_items=[0-9a-f]+" build/run_online_bob.log | tail -1 | grep -qE "local_items=[0-9a-f]*[2367abef][0-9a-f]$"'
check "the Sensor shared as progress (key 0x7130)" 'grep -q "item 0x7130 = 1 from 0" build/relay.log'
check "Bob got the Sensor through AddItem" 'grep -qF "[laonline] team grant: item 116 x1" build/run_online_bob.log'
check "nothing reported back from Bob" '! grep -qE "(pickup 4|pickup 7|item 0x7105).* from 1" build/relay.log'
check "Alice's Player 2 on the relay as a player of his own" 'grep -q "join .* Alice P2" build/relay.log'
check "Bob sees Alice's Player 2" 'grep -qF "[laonline] remote Link '"'"'Alice P2'"'"' spawned" build/run_online_bob.log'
check "Alice doesn't see her own Player 2 as remote" '! grep -qF "remote Link '"'"'Alice P2'"'"'" build/run_online_alice.log'
check "no crash" '! grep -qE "FATAL|\[crash\]" build/run_online_alice.log build/run_online_bob.log'
grep -E "^(join|pickup|item|reward|leave|refused|ignored)" build/relay.log | head -12
grep -hF "[laonline] team grant" build/run_online_bob.log | head -6
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
