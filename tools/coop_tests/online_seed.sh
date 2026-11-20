#!/bin/bash
# tools/coop_tests/online_seed.sh: a Co-op Randomizer room's seed, from the host's game to a joiner's (runtime/ui/
# laonline_lobby.mm, tools/rando/lao_seed_transfer.py), two copies of the game on this PC and the stand-in relay
# (tools/laonline_relay.py, port 47635). Needs a seed: rando/LaoTest (make it once: the Randomizer page's generation,
#   tools/coop_tests/run_test.sh seedgen 420 LARECOMP_HIDDEN=1 LARECOMP_MENU_AT=600 LARECOMP_MENU_TAB=3 LARECOMP_RANDO_GEN_TEST=LaoTest).
# First the helper alone: the seed exported as patches against this PC's game files and imported into an empty folder
# - the same world (checksum), no game file in the package. Then the game: Alice plays the seed and hosts a private
# session (a Co-op Randomizer one); her game uploads the seed. Bob plays vanilla and joins by its code; his game
# downloads the seed (here the same data folder already has it: the exact world is found and reused) and switches to
# it, and the session begins for both in its ".rando" lobby. Prints PASS / FAIL for each.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
[ -f rando/LaoTest/settings.json ] || { echo "no rando/LaoTest: make the test seed first (see the top of this file)"; exit 1; }
PORT=${PORT:-47635}
PY=third_party/LAS-Randomizer/venv/Scripts/python.exe; [ -x "$PY" ] || PY=third_party/LAS-Randomizer/venv/bin/python
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
# ---- the helper: export, then import into an empty folder
T=build/seed_roundtrip; rm -rf $T; mkdir -p $T/rando
$PY tools/rando/las_headless.py --lao-export rando/LaoTest extracted/romfs $T/world.laoseed > $T/export.log 2>&1
$PY tools/rando/las_headless.py --lao-import $T/world.laoseed extracted/romfs $T/rando $T/result.txt > $T/import.log 2>&1
check "export: the seed as patches" 'grep -q "^Exported exact named world" $T/export.log'
check "the package holds no game file (patches and the seed's own files only)" '$PY -c "import zipfile,json,sys; m=json.loads(zipfile.ZipFile(sys.argv[1]).read(\"manifest.json\")); sys.exit(0 if all(e[\"kind\"]==\"xor\" or not e[\"path\"].startswith(\"romfs/\") for e in m[\"entries\"]) else 1)" $T/world.laoseed'
check "import: the same world, rebuilt from this PC's files" 'grep -q "verified_without_generation" $T/import.log && [ -d "$T/rando/$(cat $T/result.txt)/romfs" ]'
# ---- the game
mkdir -p build/frames
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log rando/.lao-room.ini rando/.lao-transfer-result.txt
rm -f build/test_ini/seed_alice.ini build/test_ini/seed_bob.ini  # (a switch is saved there: Bob starts on vanilla)
python -u tools/laonline_relay.py $PORT > build/relay_seed.log 2>&1 &
RELAY=$!
sleep 1
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <seconds> <name> <plan> [env...]
    local id=$1 secs=$2 name=$3 plan=$4; shift 4
    rm -rf build/save_$id; mkdir -p build/save_$id/save_0; cp ${SEED:-build/save_seed}/* build/save_$id/save_0/
    tools/coop_tests/run_test.sh $id $secs LARECOMP_SAVE_DIR=build/save_$id LARECOMP_HIDDEN=1 "LARECOMP_INPUT=$TITLE" \
        LAONLINE_SERVER=127.0.0.1:$PORT LAONLINE_NAME=$name LAONLINE_TEST_LOBBY=$plan "$@" > /dev/null 2>&1
}
run seed_alice 300 Alice host LARECOMP_RANDO_SEED=LaoTest &
CODE=
for i in $(seq 1 150); do
    CODE=$(grep -oE "session host [A-Z0-9]{6}" build/relay_seed.log | head -1 | cut -d' ' -f3)
    [ -n "$CODE" ] && break
    sleep 1
done
[ -n "$CODE" ] && run seed_bob 170 Bob "join:$CODE"  # (its end stops Alice's too)
wait $(jobs -p | grep -v "^$RELAY$")
kill $RELAY 2>/dev/null
check "Alice hosted a Co-op Randomizer session ($CODE)" '[ -n "$CODE" ] && grep -q "session host $CODE by Alice (private, co-op randomizer)" build/relay_seed.log'
check "Alice's game uploaded the seed" 'grep -qE "seed upload V10-$CODE.rando by Alice: [0-9]+ bytes$" build/relay_seed.log'
check "Bob's game downloaded it" 'grep -qE "seed download V10-$CODE.rando by Bob: [0-9]+ bytes" build/relay_seed.log && grep -qF "[lobby] seed download done" build/run_seed_bob.log'
check "Bob's game switched to it" 'grep -qF "[lobby] the host'"'"'s seed is here (LaoTest): switching to it" build/run_seed_bob.log && grep -qF "[reboot] requested" build/run_seed_bob.log'
check "the session began for both in its .rando lobby" 'grep -q "join [0-9]* Alice lobby V10-$CODE.rando " build/relay_seed.log && grep -q "join [0-9]* Bob lobby V10-$CODE.rando " build/relay_seed.log'
check "no crash" '! grep -qE "FATAL|\[crash\]" build/run_seed_alice.log build/run_seed_bob.log'
grep -E "^(session|seed|join)" build/relay_seed.log | head -12
grep -hF "[lobby]" build/run_seed_alice.log build/run_seed_bob.log | head -10
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log rando/.lao-room.ini rando/.lao-transfer-result.txt
exit $fail
