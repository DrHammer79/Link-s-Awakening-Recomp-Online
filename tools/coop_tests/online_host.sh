#!/bin/bash
# tools/coop_tests/online_host.sh: a relay hosted by the game (the Online page's Relay tab: runtime/laonline_host.cpp)
# and the network's search, two copies of the game on this PC, no other relay. Alice's settings host one on port
# 47641 and choose it (larecomp.ini's laonline_host / laonline_host_port, as "Host whenever the game starts" leaves
# them); she hosts a private session on it. Bob has no relay set up but larecomp.ini's laonline_relays / laonline_relay
# (one added on the Relay tab): its address. He searches the network first (LAONLINE_TEST_FIND), then joins Alice's
# session by its code. Expected: Alice's game hosts the relay; Bob's search finds it; the session is hosted, joined and
# started there, both go online in its lobby and see each other's Link. Prints PASS / FAIL for each.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
PORT=${PORT:-47641}
mkdir -p build/frames build/test_ini
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
printf 'laonline_host = 1\nlaonline_host_port = %s\nlaonline_relay = 127.0.0.1:%s\n' $PORT $PORT > build/test_ini/online_host_alice.ini
printf 'laonline_relays = Alice'"'"'s PC|127.0.0.1:%s\nlaonline_relay = 127.0.0.1:%s\n' $PORT $PORT > build/test_ini/online_host_bob.ini
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <seconds> <name> <lobby plan> <P1 walk> [ENV=value...]
    rm -rf build/save_$1; mkdir -p build/save_$1/save_0; cp ${SEED:-build/save_seed}/* build/save_$1/save_0/
    tools/coop_tests/run_test.sh $1 $2 LARECOMP_SAVE_DIR=build/save_$1 LARECOMP_HIDDEN=1 LAONLINE_HOST_BIND=127.0.0.1 "LARECOMP_INPUT=$TITLE,$5" \
        LAONLINE_NAME=$3 LAONLINE_TEST_LOBBY=$4 "${@:6}" > /dev/null 2>&1
}
A=build/run_online_host_alice.log
B=build/run_online_host_bob.log
run online_host_alice 240 Alice host "2700:STICK_LEFT:150" &
CODE=
for i in $(seq 1 120); do
    CODE=$(grep -oE "\[relay\] session host [A-Z0-9]{6}" $A 2>/dev/null | head -1 | cut -d' ' -f4)
    [ -n "$CODE" ] && break
    sleep 1
done
[ -n "$CODE" ] && run online_host_bob 120 Bob "join:$CODE" "2700:STICK_RIGHT:150" LAONLINE_TEST_FIND=1
wait
fail=0
check() { if (eval "$2"); then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
check "Alice's game hosts the relay on port $PORT" 'grep -qF "[relay] relay listening on 127.0.0.1:$PORT" $A'
check "Bob's search found it" 'grep -qF "[laonline] network relay 127.0.0.1:$PORT '"'"'Alice'"'"'" $B'
check "Alice hosted a private session there ($CODE)" '[ -n "$CODE" ] && grep -qF "[relay] session host $CODE by Alice (private, co-op)" $A'
check "Bob joined it by its code" 'grep -qF "[relay] session join $CODE Bob" $A'
check "both ready, Alice started it" 'grep -qF "[relay] session start $CODE lobby V10-$CODE (2 players)" $A'
check "both online in its lobby" 'grep -qE "\[relay\] join [0-9]+ Alice lobby V10-$CODE " $A && grep -qE "\[relay\] join [0-9]+ Bob lobby V10-$CODE " $A'
check "Alice sees Bob's Link" 'grep -qF "[laonline] remote Link '"'"'Bob'"'"' spawned" $A'
check "Bob sees Alice's Link" 'grep -qF "[laonline] remote Link '"'"'Alice'"'"' spawned" $B'
check "no crash" '! grep -qE "FATAL|\[crash\]" $A $B'
grep -hE "\[relay\] (relay|session|join|leave|refused)" $A | head -12
grep -hF "network relay" $B | head -4
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
