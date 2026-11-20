#!/bin/bash
# tools/coop_tests/online_flows.sh: moving between sessions, and the relay going away, with local players along - two
# copies of the game on this PC and the stand-in relay (tools/laonline_relay.py, port 47636). Both have only a relay set
# up; Alice plays with 2 local players. Alice hosts a public session, Bob finds it in the list and joins, both Ready,
# Alice starts it; 15 s later each goes to Global (the Global tab's Join Global: leaving the session). Once both are in
# Global the relay is stopped, and started again 5 s later (empty: no rooms, no lobbies). Expected: Alice's Player 2
# follows her - in the session's lobby, then straight out of it and into Global - and after the relay's restart
# everyone is back on it by themselves (Player 1's connection 3 s after it went, the local players' too).
# Prints PASS / FAIL for each.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
PORT=${PORT:-47636}
mkdir -p build/frames
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
python -u tools/laonline_relay.py $PORT > build/relay_flows1.log 2>&1 &
RELAY=$!
sleep 1
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <seconds> <name> <local players> <plan> [env...]
    local id=$1 secs=$2 name=$3 n=$4 plan=$5; shift 5
    rm -rf build/save_$id; mkdir -p build/save_$id/save_0; cp ${SEED:-build/save_seed}/* build/save_$id/save_0/
    tools/coop_tests/run_test.sh $id $secs LARECOMP_SAVE_DIR=build/save_$id LARECOMP_COOP=$n LARECOMP_COOP_LOG=1 \
        LARECOMP_HIDDEN=1 "LARECOMP_INPUT=$TITLE" LAONLINE_SERVER=127.0.0.1:$PORT LAONLINE_NAME=$name \
        LAONLINE_TEST_LOBBY=$plan "$@" > /dev/null 2>&1
}
run flows_alice 240 Alice 2 host-public,then-global LARECOMP_INPUT_P2=9999:A:1 &
sleep 2
run flows_bob 200 Bob 1 browse,then-global &  # (its end stops Alice's too)
for i in $(seq 1 170); do
    grep -q "join [0-9]* Bob lobby V10-GLOBAL" build/relay_flows1.log && grep -q "join [0-9]* Alice lobby V10-GLOBAL" build/relay_flows1.log && break
    sleep 1
done
sleep 5
kill $RELAY 2>/dev/null
sleep 5
python -u tools/laonline_relay.py $PORT > build/relay_flows2.log 2>&1 &
RELAY=$!
wait $(jobs -p | grep -v "^$RELAY$")
kill $RELAY 2>/dev/null
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
L1=build/relay_flows1.log
line() { grep -n -m1 -E "$1" $L1 | cut -d: -f1; }
CODE=$(grep -oE "session host [A-Z0-9]{6}" $L1 | head -1 | cut -d' ' -f3)
check "the session: hosted, found in the list, started ($CODE)" '[ -n "$CODE" ] && grep -q "session join $CODE Bob" $L1 && grep -q "session start $CODE" $L1'
check "Alice's Player 2 in the session's lobby" 'grep -q "join [0-9]* Alice P2 lobby V10-$CODE " $L1'
check "both left the session for Global" 'grep -q "session leave $CODE Alice" $L1 && grep -q "session leave $CODE Bob" $L1'
check "left for good (the session not begun again while the leave was answered)" '[ "$(grep -c "\[lobby\] session $CODE started" build/run_flows_alice.log)" = 1 ] && [ "$(grep -c "\[lobby\] session $CODE started" build/run_flows_bob.log)" = 1 ]'
check "her Player 2 out of the session's lobby, then into Global" '[ -n "$(line "leave [0-9]+ Alice P2")" ] && [ -n "$(line "join [0-9]+ Alice P2 lobby V10-GLOBAL")" ] && [ "$(line "leave [0-9]+ Alice P2")" -lt "$(line "join [0-9]+ Alice P2 lobby V10-GLOBAL")" ]'
check "after the relay's restart: Alice back by herself" 'grep -q "join [0-9]* Alice lobby V10-GLOBAL" build/relay_flows2.log'
check "after the relay's restart: her Player 2 back" 'grep -q "join [0-9]* Alice P2 lobby V10-GLOBAL" build/relay_flows2.log'
check "after the relay's restart: Bob back" 'grep -q "join [0-9]* Bob lobby V10-GLOBAL" build/relay_flows2.log'
check "no crash" '! grep -qE "FATAL|\[crash\]" build/run_flows_alice.log build/run_flows_bob.log'
grep -E "^(session|join|leave)" $L1 | head -14
echo "--- after the restart"; grep -E "^(join|leave)" build/relay_flows2.log | head -5
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
