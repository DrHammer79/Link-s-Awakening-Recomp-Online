#!/bin/bash
# tools/coop_tests/online_seats.sh: local players' places in a session room (runtime/ui/laonline_lobby.mm). The relay
# lets only a room's members into its lobby, and a room has 4 places, so each local player takes one. Two copies of the
# game on this PC and the stand-in relay (tools/laonline_relay.py, port 47638; LAO_RELAY: LA-Online's own). Alice plays
# with 3 local players and hosts a private session; Bob, with 2, joins it by its code. Expected: Alice's Players 2 and
# 3 and Bob take the room's other places, Bob's Player 2 finds it full and plays on his PC only; all Ready, the session
# starts, the four go online in its lobby and the relay turns nobody away. Then Alice's Player 3 leaves the game: his
# place is given back, and Bob's Player 2 takes it and goes online in the session. Prints PASS / FAIL for each.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
PORT=${PORT:-47638}
mkdir -p build/frames
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
python -u tools/laonline_relay.py $PORT > build/relay_seats.log 2>&1 &
RELAY=$!
sleep 1
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <seconds> <name> <local players> <plan> [env...]
    local id=$1 secs=$2 name=$3 n=$4 plan=$5; shift 5
    rm -rf build/save_$id; mkdir -p build/save_$id/save_0; cp ${SEED:-build/save_seed}/* build/save_$id/save_0/
    tools/coop_tests/run_test.sh $id $secs LARECOMP_SAVE_DIR=build/save_$id LARECOMP_COOP=$n LARECOMP_COOP_LOG=1 \
        LARECOMP_HIDDEN=1 "LARECOMP_INPUT=$TITLE" LARECOMP_INPUT_P2=9999:A:1 LARECOMP_INPUT_P3=9999:A:1 \
        LAONLINE_SERVER=127.0.0.1:$PORT LAONLINE_NAME=$name LAONLINE_TEST_LOBBY=$plan "$@" > /dev/null 2>&1
}
# (Alice's Player 3 leaves the game at render frame 9000 - well after the session began)
run seats_alice 260 Alice 3 host LARECOMP_COOP_LEAVE=9000:3 &
CODE=
for i in $(seq 1 120); do
    CODE=$(grep -oE "session host [A-Z0-9]{6}" build/relay_seats.log | head -1 | cut -d' ' -f3)
    [ -n "$CODE" ] && break
    sleep 1
done
[ -n "$CODE" ] && run seats_bob 210 Bob 2 "join:$CODE"  # (its end stops Alice's too)
wait $(jobs -p | grep -v "^$RELAY$")
kill $RELAY 2>/dev/null
fail=0
check() { if (eval "$2"); then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
R=build/relay_seats.log A=build/run_seats_alice.log B=build/run_seats_bob.log
at() { grep -n -m1 -F "$1" $2 | cut -d: -f1; }
check "Alice hosted ($CODE), Bob joined" '[ -n "$CODE" ] && grep -q "session join $CODE Bob " $R'
check "her Players 2 and 3 took places of their own" 'grep -q "session join $CODE Alice P2 " $R && grep -q "session join $CODE Alice P3 " $R && grep -qF "[lobby] player 2 has a place in session $CODE as '"'"'Alice P2'"'"'" $A && grep -qF "[lobby] player 3 has a place in session $CODE as '"'"'Alice P3'"'"'" $A'
check "Bob's Player 2 found it full (on his PC only)" 'grep -qF "[lobby] player 2 gets no place in session $CODE" $B'
check "the session started with the four" 'grep -q "session start $CODE lobby V10-$CODE (4 players)" $R'
check "the four online in its lobby" 'for n in Alice "Alice P2" "Alice P3" Bob; do grep -q "^join [0-9]* $n lobby V10-$CODE " $R || exit 1; done'
check "the relay turned nobody away" '! grep -q "^refused" $R'
check "Alice's Player 3 gave his place back when he left the game" 'grep -qF "[lobby] player 3 left session $CODE" $A && grep -q "session leave $CODE Alice P3" $R'
check "Bob's Player 2 took it, then went online" '[ -n "$(at "session join $CODE Bob P2 " $R)" ] && [ "$(at "session leave $CODE Alice P3" $R)" -lt "$(at "session join $CODE Bob P2 " $R)" ] && grep -q "^join [0-9]* Bob P2 lobby V10-$CODE " $R'
check "no crash" '! grep -qE "FATAL|\[crash\]" $A $B'
grep -E "^(session|join|leave|refused)" $R | head -20
grep -hF "[lobby] player" $A $B | head -8
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
