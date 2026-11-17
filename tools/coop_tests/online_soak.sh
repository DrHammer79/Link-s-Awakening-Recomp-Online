#!/bin/bash
# tools/coop_tests/online_soak.sh [seconds]: a long online run - two copies of the game on this PC and the stand-in relay
# (tools/laonline_relay.py, port 47637), Alice with 2 local players, Bob alone, in one lobby for ~8 minutes (the
# argument: longer or shorter). Everyone walks to and fro and swings now and then, PvP on for all. Expected: no crash,
# nobody dropped by the relay or disconnected along the way, both games still drawing frames at the end, the hits
# taken as the game's own all along. Prints PASS / FAIL for each, and the last frame-rate line of each game.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
PORT=${PORT:-47637}
SECS=${1:-480}
mkdir -p build/frames
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
python -u tools/laonline_relay.py $PORT > build/relay_soak.log 2>&1 &
RELAY=$!
sleep 1
FRAMES=$((SECS * 60))
INPUT=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)] + ['2700:RSTICK:6']
for f in range(3000, $FRAMES, 600): s += [f'{f}:STICK_LEFT:90', f'{f+200}:STICK_RIGHT:90', f'{f+400}:B:6']
print(','.join(s))")
INPUT2=$(python -c "
s=['9999:A:1', '2720:RSTICK:6']
for f in range(3100, $FRAMES, 600): s += [f'{f}:STICK_RIGHT:90', f'{f+200}:STICK_LEFT:90', f'{f+400}:B:6']
print(','.join(s))")
run() {  # <id> <seconds> <name> <local players> [env...]
    local id=$1 secs=$2 name=$3 n=$4; shift 4
    rm -rf build/save_$id; mkdir -p build/save_$id/save_0; cp ${SEED:-build/save_seed}/* build/save_$id/save_0/
    tools/coop_tests/run_test.sh $id $secs LARECOMP_SAVE_DIR=build/save_$id LARECOMP_COOP=$n LARECOMP_COOP_LOG=1 \
        LARECOMP_HIDDEN=1 LAONLINE_MODE=server LAONLINE_LOBBY=default LAONLINE_SERVER=127.0.0.1:$PORT \
        LAONLINE_NAME=$name "$@" > /dev/null 2>&1
}
run soak_alice $((SECS + 60)) Alice 2 "LARECOMP_INPUT=$INPUT" "LARECOMP_INPUT_P2=$INPUT2" &
sleep 2
run soak_bob $SECS Bob 1 "LARECOMP_INPUT=$INPUT" &  # (its end stops Alice's too)
wait $(jobs -p | grep -v "^$RELAY$")
kill $RELAY 2>/dev/null
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
check "all three joined once (nobody dropped and back)" '[ "$(grep -c "^join " build/relay_soak.log)" = 3 ]'
check "nobody left before the end" '[ "$(grep -c "^leave " build/relay_soak.log)" -le 3 ] && ! grep -qF "[laonline] server: disconnected" build/run_soak_alice.log build/run_soak_bob.log'
check "both games drew frames to the end" 'for f in alice bob; do t=$(grep -oE "^\[vk\] t=[0-9.]+" build/run_soak_$f.log | tail -1 | cut -d= -f2); [ "${t%.*}" -ge $((SECS - 90)) ] || exit 1; done'
check "PvP hits taken along the way" 'grep -qE "PvP: player [0-9] hit by" build/run_soak_alice.log build/run_soak_bob.log'
check "no crash" '! grep -qE "FATAL|\[crash\]" build/run_soak_alice.log build/run_soak_bob.log'
for f in alice bob; do echo "$f: $(grep -E "^\[vk\] t=" build/run_soak_$f.log | tail -1 | cut -c1-90)"; done
echo "hits: $(grep -c "^pvp hit" build/relay_soak.log)"
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
