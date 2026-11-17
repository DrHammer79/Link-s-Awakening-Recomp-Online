#!/bin/bash
# tools/coop_tests/online_global.sh: Global and the leaderboards from the menu's Online page (runtime/ui/laonline_lobby.mm),
# two copies of the game on this PC and the stand-in relay (tools/laonline_relay.py, port 47634). Both have only a relay
# set up; Alice plays with 2 local players. Each joins Global (LAONLINE_TEST_LOBBY: what the Global tab's Join Global
# does); Bob also submits a completed run and reads the leaderboard (the Leaderboards tab's requests). Expected: Alice,
# her Player 2 and Bob in Global's lobby (V10-GLOBAL, its own HELLO), each sees the others' Links; Bob's run on the
# relay and back in the board he reads. Prints PASS / FAIL for each.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
PORT=${PORT:-47634}
mkdir -p build/frames
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
python -u tools/laonline_relay.py $PORT > build/relay_global.log 2>&1 &
RELAY=$!
sleep 1
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <name> <local players> <plan> [env...]
    local id=$1 name=$2 n=$3 plan=$4; shift 4
    # (each copy its own save folder, from the seed: two games writing one folder at once fail each other's saves)
    rm -rf build/save_$id; mkdir -p build/save_$id/save_0; cp ${SEED:-build/save_seed}/* build/save_$id/save_0/
    tools/coop_tests/run_test.sh $id ${SECS:-100} LARECOMP_SAVE_DIR=build/save_$id LARECOMP_COOP=$n LARECOMP_COOP_LOG=1 \
        LARECOMP_HIDDEN=1 "LARECOMP_INPUT=$TITLE" LAONLINE_SERVER=127.0.0.1:$PORT LAONLINE_NAME=$name \
        LAONLINE_TEST_LOBBY=$plan "$@" > /dev/null 2>&1
}
run global_alice Alice 2 global LARECOMP_INPUT_P2=9999:A:1 &
sleep 2
run global_bob Bob 1 global,board &
wait $(jobs -p | grep -v "^$RELAY$")
kill $RELAY 2>/dev/null
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
check "Alice in Global" 'grep -q "join [0-9]* Alice lobby V10-GLOBAL " build/relay_global.log && grep -qF "[lobby] Global: online play as '"'"'Alice'"'"'" build/run_global_alice.log'
check "her Player 2 in Global too" 'grep -q "join [0-9]* Alice P2 lobby V10-GLOBAL " build/relay_global.log'
check "Bob in Global" 'grep -q "join [0-9]* Bob lobby V10-GLOBAL " build/relay_global.log'
check "Alice sees Bob's Link" 'grep -qF "[laonline] remote Link '"'"'Bob'"'"' spawned" build/run_global_alice.log'
check "Bob sees Alice's and her Player 2's" 'grep -qF "[laonline] remote Link '"'"'Alice'"'"' spawned" build/run_global_bob.log && grep -qF "[laonline] remote Link '"'"'Alice P2'"'"' spawned" build/run_global_bob.log'
check "Bob's run on the leaderboard" 'grep -q "^board run Bob mode 1 3725s" build/relay_global.log && grep -qF "[lobby] leaderboard: 1 run(s), first: Bob" build/run_global_bob.log'
check "no crash" '! grep -qE "FATAL|\[crash\]" build/run_global_alice.log build/run_global_bob.log'
grep -E "^(join|board|leave)" build/relay_global.log | head -8
grep -hF "[lobby]" build/run_global_alice.log build/run_global_bob.log | head -6
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
