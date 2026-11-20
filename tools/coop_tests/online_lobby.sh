#!/bin/bash
# tools/coop_tests/online_lobby.sh: online play started from the menu's Online page (runtime/ui/laonline_lobby.mm), two
# copies of the game on this PC and the stand-in relay (tools/laonline_relay.py, port 47631, with its session service).
# Both have only a relay set up (LAONLINE_SERVER, no mode): online play is set up but not started. Alice hosts a private
# session; once the relay has it, Bob joins it with its room code (LAONLINE_TEST_LOBBY: the same requests as the page's
# buttons); both mark Ready and Alice starts it. Expected: nothing joins the relay before the session starts; then both
# go online in the session's own lobby ("V10-<code>") with its password, and each sees the other's Link. Prints
# PASS / FAIL for each.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
PORT=${PORT:-47631}
mkdir -p build/frames
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
python -u tools/laonline_relay.py $PORT > build/relay_lobby.log 2>&1 &
RELAY=$!
sleep 1
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <seconds> <name> <lobby plan> <P1 walk>
    # (each copy its own save folder, from the seed: two games writing one folder at once fail each other's saves -
    # a sharing violation, and a game whose save fails stops - as two PCs never do)
    rm -rf build/save_$1; mkdir -p build/save_$1/save_0; cp ${SEED:-build/save_seed}/* build/save_$1/save_0/
    tools/coop_tests/run_test.sh $1 $2 LARECOMP_SAVE_DIR=build/save_$1 LARECOMP_HIDDEN=1 "LARECOMP_INPUT=$TITLE,$5" LAONLINE_SERVER=127.0.0.1:$PORT \
        LAONLINE_NAME=$3 LAONLINE_TEST_LOBBY=$4 > /dev/null 2>&1
}
run online_lobby_alice 240 Alice host "2700:STICK_LEFT:150" &
CODE=
for i in $(seq 1 120); do
    CODE=$(grep -oE "session host [A-Z0-9]{6}" build/relay_lobby.log | head -1 | cut -d' ' -f3)
    [ -n "$CODE" ] && break
    sleep 1
done
[ -n "$CODE" ] && run online_lobby_bob 120 Bob "join:$CODE" "2700:STICK_RIGHT:150"  # (its end stops Alice's too)
wait $(jobs -p | grep -v "^$RELAY$")
kill $RELAY 2>/dev/null
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
first() { grep -n -m1 -E "$1" build/relay_lobby.log | cut -d: -f1; }
check "Alice hosted a private session ($CODE)" '[ -n "$CODE" ] && grep -q "session host $CODE by Alice (private, co-op)" build/relay_lobby.log'
check "Bob joined it by its code" 'grep -q "session join $CODE Bob" build/relay_lobby.log'
check "both ready, Alice started it" 'grep -q "session ready $CODE Alice 1" build/relay_lobby.log && grep -q "session ready $CODE Bob 1" build/relay_lobby.log && grep -q "session start $CODE lobby V10-$CODE (2 players)" build/relay_lobby.log'
at() { grep -n -m1 -F "$1" $2 | cut -d: -f1; }  # (the first line with it)
check "nothing online before the session started" '[ -n "$(first "^join ")" ] && [ "$(first "^join ")" -gt "$(first "^session start")" ] && [ "$(at "launch mode=" build/run_online_lobby_alice.log)" -gt "$(at "[lobby] in session $CODE: 2 player(s), started" build/run_online_lobby_alice.log)" ]'
check "Alice online in the session's lobby" 'grep -qF "[lobby] session $CODE started: online play as '"'"'Alice'"'"' in lobby V10-$CODE (co-op)" build/run_online_lobby_alice.log && grep -q "join [0-9]* Alice lobby V10-$CODE " build/relay_lobby.log'
check "Bob online in the session's lobby" 'grep -qF "[lobby] session $CODE started: online play as '"'"'Bob'"'"' in lobby V10-$CODE (co-op)" build/run_online_lobby_bob.log && grep -q "join [0-9]* Bob lobby V10-$CODE " build/relay_lobby.log'
check "Alice sees Bob's Link" 'grep -qF "[laonline] remote Link '"'"'Bob'"'"' spawned" build/run_online_lobby_alice.log'
check "Bob sees Alice's Link" 'grep -qF "[laonline] remote Link '"'"'Alice'"'"' spawned" build/run_online_lobby_bob.log'
check "no crash" '! grep -qE "FATAL|\[crash\]" build/run_online_lobby_alice.log build/run_online_lobby_bob.log'
grep -E "^(session|join|leave|refused)" build/relay_lobby.log | head -12
grep -hF "[lobby]" build/run_online_lobby_alice.log build/run_online_lobby_bob.log | head -8
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
