#!/bin/bash
# tools/coop_tests/online_pvp_ko.sh: a PvP knock-out in the middle of a session (runtime/hle/laonline_v12.c), two
# copies of the game on this PC and the stand-in relay (tools/laonline_relay.py, port 47639; LAO_RELAY: LA-Online's
# own). As online_pvp.sh: Alice with 2 local players, Bob alone, all with PvP on; Bob is put at half a heart
# (LARECOMP_TEST_HP) just before Alice's Player 2's spin attack reaches him. Expected: the hit knocks Bob out, his game
# plays its own death and Game Over (the others hear of it), and both games go on drawing frames to the end - no game
# stops. Prints PASS / FAIL for each.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
PORT=${PORT:-47639}
SECS=${SECS:-170}
mkdir -p build/frames
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
python -u tools/laonline_relay.py $PORT > build/relay_pvp_ko.log 2>&1 &
RELAY=$!
sleep 1
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <seconds> <name> <local players> [env...]
    local id=$1 secs=$2 name=$3 n=$4; shift 4
    rm -rf build/save_$id; mkdir -p build/save_$id/save_0; cp ${SEED:-build/save_seed}/* build/save_$id/save_0/
    tools/coop_tests/run_test.sh $id $secs LARECOMP_SAVE_DIR=build/save_$id LARECOMP_COOP=$n LARECOMP_COOP_LOG=1 \
        LARECOMP_HIDDEN=1 LAONLINE_MODE=server LAONLINE_LOBBY=default LAONLINE_SERVER=127.0.0.1:$PORT LAONLINE_NAME=$name \
        "$@" > /dev/null 2>&1
}
# (online_pvp.sh's moves: Bob's spin knocks Alice and her Player 2 back, her Player 2 walks back and makes one at him;
# Bob's hearts go down after his own spin, before hers)
run pvpko_alice $((SECS + 10)) Alice 2 "LARECOMP_INPUT=$TITLE,2700:RSTICK:6,3300:ZL:6" "LARECOMP_INPUT_P2=9999:A:1,2720:RSTICK:6,3300:ZL:6,3400:STICK_RIGHT:40,3500:B:60" &
sleep 2
run pvpko_bob $SECS Bob 1 "LARECOMP_INPUT=$TITLE,2700:RSTICK:6,2900:B:60" LARECOMP_TEST_HP=3100:2 &  # (its end stops Alice's too)
wait $(jobs -p | grep -v "^$RELAY$")
kill $RELAY 2>/dev/null
fail=0
check() { if (eval "$2"); then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
A=build/run_pvpko_alice.log B=build/run_pvpko_bob.log
last_t() { grep -oE "^\[vk\] t=[0-9.]+" $1 | tail -1 | cut -d= -f2; }
check "Bob at half a heart before the hit" 'grep -qF "[laonline] test: Player 1'"'"'s hearts set to 2/4" $B'
check "the hit knocked Bob out" 'grep -qE "V13.5 PvP: player 1 knocked out by player #[0-9]+" $B'
check "his game played its Game Over" 'grep -qF "V13.5 PvP death: the game started its Game Over" $B'
check "Alice heard of it" 'grep -qF "Bob was knocked out in PvP" $A || grep -qF "Bob was defeated" $A'
check "Bob's game drew frames to the end" 't=$(last_t $B); [ "${t%.*}" -ge $((SECS - 25)) ] && ! grep -qF "[hang] =====" $B'
check "Alice's game drew frames to the end" 't=$(last_t $A); [ "${t%.*}" -ge $((SECS - 25)) ] && ! grep -qF "[hang] =====" $A'
check "no crash" '! grep -qE "FATAL|\[crash\]" $A $B'
for f in $A $B; do echo "$f: $(grep -E "^\[vk\] t=" $f | tail -1 | cut -c1-60)"; done
grep -hE "knocked out|Game Over|hearts set|\[hang\] =====" $A $B | head -8
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
