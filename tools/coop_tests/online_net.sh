#!/bin/bash
# tools/coop_tests/online_net.sh: online play (LA-Online) between two copies of the game on this PC - its direct mode,
# over UDP on 127.0.0.1: one hosts (LAONLINE_MODE=host, port 47621), the other joins it. Both boot into the seed's file
# 1; each player walks (the host left, the one who joined right), and each should see the other: "player '<name>'
# connected" and "remote Link '<name>' spawned" in both logs, then the other's puppet following where he walked (his
# position in the log). The host's listening port may raise Windows Firewall's question on the first run: on one PC
# the copies reach each other either way. Prints PASS / FAIL for each check.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
mkdir -p save/save_0 build/frames
rm -f save/save_0/savedata*.bin save/save_0/screenshot*.bin; cp ${SEED:-build/save_seed}/* save/save_0/  # (once, for both)
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <P1 walk> [env...]
    local id=$1 walk=$2; shift 2
    tools/coop_tests/run_test.sh $id ${SECS:-100} LARECOMP_COOP=1 LARECOMP_COOP_LOG=1 LARECOMP_HIDDEN=1 \
        "LARECOMP_INPUT=$TITLE,$walk" LARECOMP_INPUT_P2=9999:A:1 "$@" > /dev/null 2>&1
}
run online_host "2600:STICK_LEFT:150" LAONLINE_MODE=host LAONLINE_PORT=47621 &
sleep 2  # (the host's port open first)
run online_join "2600:STICK_RIGHT:150" LAONLINE_MODE=join LAONLINE_PEER=127.0.0.1:47621 &
wait
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
for id in online_host online_join; do
    check "$id: listening / sending" 'grep -qE "\[laonline\] (host: listening|join: sending)" build/run_$id.log'
    check "$id: the other player connected" 'grep -qF "[laonline] player " build/run_$id.log'
    check "$id: his puppet spawned" 'grep -qF "[laonline] remote Link " build/run_$id.log'
    check "$id: no crash" '! grep -qE "FATAL|\[crash\]" build/run_$id.log'
done
grep -hF "[laonline]" build/run_online_host.log | grep -E "listening|connected|spawned" | head -4
grep -hF "[laonline]" build/run_online_join.log | grep -E "sending|connected|spawned" | head -4
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
