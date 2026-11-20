#!/bin/bash
# tools/coop_tests/online_teleport.sh: teleporting to a teammate (runtime/ui/laonline_teleport.mm), two copies of the
# game on this PC and the stand-in relay (tools/laonline_relay.py, port 47640; LAO_RELAY: LA-Online's own), in one
# lobby. Bob warps far across the island (the warp tab's way: Animal Village), then later into Marin & Tarin's house;
# each time Alice - with a local Player 2 - opens the teleport picker on him and teleports (LAONLINE_TEST_TELEPORT: as
# her Confirm would). Expected: the picker lists Bob; each teleport is the game's level jump to an arrival point of
# his level and Alice is put where he stands; her Player 2 comes along; nobody drops off the relay. A window shot of
# the picker: build/frames/teleport_picker.png (Alice's, one teammate) and teleport_picker2.png (Bob's, two:
# Alice and her Player 2, shown only). Prints PASS / FAIL for each.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
PORT=${PORT:-47640}
mkdir -p build/frames
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log build/frames/teleport_picker*.png
python -u tools/laonline_relay.py $PORT > build/relay_teleport.log 2>&1 &
RELAY=$!
sleep 1
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <seconds> <name> <local players> [env...]
    local id=$1 secs=$2 name=$3 n=$4; shift 4
    rm -rf build/save_$id; mkdir -p build/save_$id/save_0; cp ${SEED:-build/save_seed}/* build/save_$id/save_0/
    tools/coop_tests/run_test.sh $id $secs LARECOMP_SAVE_DIR=build/save_$id LARECOMP_COOP=$n LARECOMP_COOP_LOG=1 \
        LARECOMP_HIDDEN=1 "LARECOMP_INPUT=$TITLE" LAONLINE_MODE=server LAONLINE_LOBBY=default \
        LAONLINE_SERVER=127.0.0.1:$PORT LAONLINE_NAME=$name "$@" > /dev/null 2>&1
}
run tp_alice 220 Alice 2 LARECOMP_INPUT_P2=9999:A:1 LAONLINE_TEST_TELEPORT=5200:Bob,9200:Bob \
    LARECOMP_WINDOW_SHOT=5260 LARECOMP_SHOT_PATH=build/frames/teleport_picker.png &
sleep 2
run tp_bob 210 Bob 1 "LARECOMP_WARP_AT=3200:Field:Field_13M_a,7600:MarinTarinHouse:MarinTarinHouse_01A" \
    "LAONLINE_TEST_TELEPORT=4400:?Alice P2" LARECOMP_WINDOW_SHOT=4460 LARECOMP_SHOT_PATH=build/frames/teleport_picker2.png &  # (its end stops Alice's too)
wait $(jobs -p | grep -v "^$RELAY$")
kill $RELAY 2>/dev/null
fail=0
check() { if (eval "$2"); then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
A=build/run_tp_alice.log B=build/run_tp_bob.log
# (where a teleport put her, and where Player 1 then stood: within 3 of it)
near_after() {  # <n-th teleport> <level>
    python - "$A" "$1" "$2" <<'PY'
import re, sys
lines = open(sys.argv[1], encoding='utf-8', errors='replace').read().splitlines()
n, level = int(sys.argv[2]), sys.argv[3]
placed = [i for i, l in enumerate(lines) if l.startswith('[warp] Link placed at') and l.endswith(' in ' + level)]
if len(placed) < 1: sys.exit(1)
i = placed[min(n, len(placed)) - 1]
x, y, z = map(float, re.search(r'\(([-\d.]+), ([-\d.]+), ([-\d.]+)\)', lines[i]).groups())
for l in lines[i + 1:i + 400]:
    m = re.search(r'^\[coop\] frame \d+ P1 \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)(?: P2 \(([-\d.]+) ([-\d.]+) ([-\d.]+)\))?', l)
    if m:
        p1 = list(map(float, m.groups()[:3]))
        ok = abs(p1[0] - x) < 3 and abs(p1[2] - z) < 3
        p2 = m.group(4) is None or (abs(float(m.group(4)) - p1[0]) < 8 and abs(float(m.group(6)) - p1[2]) < 8)
        sys.exit(0 if ok and p2 else 1)
sys.exit(1)
PY
}
check "Bob warped away (Animal Village, then the house)" 'grep -q "\[warp\] queued Field / Field_13M_a" $B && grep -q "\[warp\] queued MarinTarinHouse / MarinTarinHouse_01A" $B'
check "the picker opened with Bob in it" 'grep -qE "\[teleport\] picker open: [1-9] teammate" $A && [ -s build/frames/teleport_picker.png ]'
check "Bob's picker: Alice and her Player 2" 'grep -qE "\[teleport\] picker open: 2 teammate" $B && [ -s build/frames/teleport_picker2.png ]'
check "teleport 1: across the island, to his square" 'grep -qE "\[teleport\] to '"'"'Bob'"'"' .* in .*: Field / Field_1[2-4][L-N]" $A'
check "teleport 1: Alice put where Bob stood (her Player 2 with her)" 'near_after 1 Field'
check "teleport 2: into the house" 'grep -qE "\[teleport\] to '"'"'Bob'"'"' .*: MarinTarinHouse / " $A'
check "teleport 2: Alice put where Bob stood" 'near_after 1 MarinTarinHouse'
check "nobody dropped off the relay" '! grep -qF "[laonline] server: disconnected" $A $B'
check "no crash" '! grep -qE "FATAL|\[crash\]" $A $B'
grep -hE "\[teleport\]|\[warp\] (queued|Link placed)" $A | head -12
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
