#!/bin/bash
# tools/coop_tests/online_pvp.sh: PvP for every player of a PC (runtime/hle/laonline_v12.c), two copies of the game on
# this PC and the stand-in relay (tools/laonline_relay.py, port 47633). Alice plays with 2 local players, Bob alone; all
# start on the same spot (the seed's file 1). Each turns PvP on with his own R3 (Alice's Player 2 with his: his own
# switch, on his own relay connection). Bob makes a spin attack (B held 1 s and let go: they start 2.2 apart, past the
# sword's 1.9, inside the spin's 2.7); it knocks Alice and her Player 2 back (about 1.6), so her Player 2 walks back
# to Bob and makes one. Expected: the relay has each one's switch; Bob's spin reaches Alice and her Player 2, Alice's
# Player 2's reaches Bob - sent on his own connection, so the relay names him the attacker; each hit is taken as the game's own (the difficulty, then co-op's split: Player 2's
# off his own hearts) - "PvP: player N hit by ..." with the hearts before and after. Alice and her Player 2 each press
# ZL (their own Z-targets, runtime/hle/ztarget.c): each locks on to a player - Bob - and not on the other's Link.
# Prints PASS / FAIL for each.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would override the modes: move it away"; exit 1; }
PORT=${PORT:-47633}
mkdir -p build/frames
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
python -u tools/laonline_relay.py $PORT > build/relay_pvp.log 2>&1 &
RELAY=$!
sleep 1
TITLE=$(python -c "
s=[f'{f}:L:10,{f}:R:10' for f in range(400, 1400, 90)] + [f'{f}:A:8' for f in range(1400, 1800, 100)]
print(','.join(s))")
run() {  # <id> <name> <local players> [env...]
    local id=$1 name=$2 n=$3; shift 3
    # (each copy its own save folder, from the seed: two games writing one folder at once fail each other's saves)
    rm -rf build/save_$id; mkdir -p build/save_$id/save_0; cp ${SEED:-build/save_seed}/* build/save_$id/save_0/
    tools/coop_tests/run_test.sh $id ${SECS:-110} LARECOMP_SAVE_DIR=build/save_$id LARECOMP_COOP=$n LARECOMP_COOP_LOG=1 \
        LARECOMP_HIDDEN=1 LAONLINE_MODE=server LAONLINE_LOBBY=default LAONLINE_SERVER=127.0.0.1:$PORT LAONLINE_NAME=$name \
        "$@" > /dev/null 2>&1
}
run pvp_alice Alice 2 "LARECOMP_INPUT=$TITLE,2700:RSTICK:6,3300:ZL:6" "LARECOMP_INPUT_P2=9999:A:1,2720:RSTICK:6,3300:ZL:6,3400:STICK_RIGHT:40,3500:B:60" &
sleep 2
run pvp_bob Bob 1 "LARECOMP_INPUT=$TITLE,2700:RSTICK:6,2900:B:60" &
wait $(jobs -p | grep -v "^$RELAY$")
kill $RELAY 2>/dev/null
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
id_of() { grep -m1 -E "^join [0-9]+ $1 lobby" build/relay_pvp.log | cut -d' ' -f2; }
A=$(id_of Alice); A2=$(id_of "Alice P2"); B=$(id_of Bob)
check "all three on the relay (Alice $A, Alice P2 $A2, Bob $B)" '[ -n "$A" ] && [ -n "$A2" ] && [ -n "$B" ]'
check "each one's own PvP switch" 'grep -q "^pvp on $A$" build/relay_pvp.log && grep -q "^pvp on $A2$" build/relay_pvp.log && grep -q "^pvp on $B$" build/relay_pvp.log'
check "Bob's spin reached Alice" 'grep -q "^pvp hit $B -> $A " build/relay_pvp.log'
check "Bob's spin reached Alice's Player 2" 'grep -q "^pvp hit $B -> $A2 " build/relay_pvp.log'
check "Alice's Player 2's spin reached Bob, as himself" 'grep -q "^pvp hit $A2 -> $B " build/relay_pvp.log'
check "Alice took Bob's hit as the game's own" 'grep -qE "PvP: player 1 hit by player #$B for [0-9]+/4 \(the game.s hearts [0-9]+ -> [0-9]+\)" build/run_pvp_alice.log'
check "Alice's Player 2 took his off his own hearts" 'grep -qE "PvP: player 2 hit by player #$B" build/run_pvp_alice.log && grep -qE "\[coop\] player 2 hit for [0-9]+ \([0-9]+ left\)" build/run_pvp_alice.log'
check "Bob took Alice's Player 2's hit" 'grep -qE "PvP: player 1 hit by player #$A2 for" build/run_pvp_bob.log'
check "Alice's own Z-target on a player" 'grep -qF "[ztarget] player 1 locked on a player" build/run_pvp_alice.log'
check "her Player 2's own Z-target on a player" 'grep -qF "[ztarget] player 2 locked on a player" build/run_pvp_alice.log'
check "no crash" '! grep -qE "FATAL|\[crash\]" build/run_pvp_alice.log build/run_pvp_bob.log'
grep -E "^(join|pvp)" build/relay_pvp.log | head -14
grep -hE "PvP: player|\[coop\] player [0-9] hit" build/run_pvp_alice.log build/run_pvp_bob.log | head -8
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
exit $fail
