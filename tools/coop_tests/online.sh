#!/bin/bash
# tools/coop_tests/online.sh: online play (LA-Online, runtime/hle/laonline*.c) on one PC, three runs from the seed's spot.
# - off: no laonline.ini, no LAONLINE_MODE: online play isn't set up, so the game runs as without it (LAS Recomp's
#   review, point 4) - no "[laonline]" line, no journal (logs/, dev-events-log.txt) written, Z-targeting off.
# - echo: LAONLINE_MODE=echo, Player 1 alone: a puppet (a Shadow Link actor dressed as Link) replays his path 1.5 s
#   late; it connects and spawns.
# - echo_coop: the same with 2 local players in: the puppet spawns beside both Links (frames saved:
#   build/frames/online_echo_coop_frame_02700.png shows three Links).
# Prints PASS / FAIL for each.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -f laonline.ini ] && { echo "a laonline.ini in the checkout would set online play up for every run: move it away"; exit 1; }
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log
P1="2400:STICK_LEFT:120,2600:STICK_RIGHT:120,2800:STICK_UP:120,2950:ZL:8"
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

SECS=${SECS:-80} tools/coop_tests/ic.sh online_off 1 - "$P1" "" "" LARECOMP_HIDDEN=1
check "off: no online play in the log" '! grep -qF "[laonline]" build/run_online_off.log'
check "off: no journal written" '[ ! -e logs ] && [ ! -e dev-events-log.txt ]'
check "off: the game ran into play" 'grep -qE "^\[coop\] frame 29[0-9]1 P1 \([1-9]" build/run_online_off.log'

SECS=${SECS:-80} tools/coop_tests/ic.sh online_echo 1 - "$P1" "" "" LARECOMP_HIDDEN=1 LAONLINE_MODE=echo
check "echo: the puppet connects" 'grep -qF "[laonline] player '"'"'echo'"'"' connected" build/run_online_echo.log'
check "echo: the puppet spawns" 'grep -qF "[laonline] remote Link '"'"'echo'"'"' spawned" build/run_online_echo.log'

SECS=${SECS:-80} tools/coop_tests/ic.sh online_echo_coop 2 - "$P1" "2400:STICK_RIGHT:60" "2700" LARECOMP_HIDDEN=1 \
    LAONLINE_MODE=echo
check "echo with 2 players: the puppet spawns" \
    'grep -qF "[laonline] remote Link '"'"'echo'"'"' spawned" build/run_online_echo_coop.log'
check "echo with 2 players: both Links in play" \
    'grep -qE "^\[coop\] frame 27[0-9]1 P1 \([1-9][^)]*\) P2 \([1-9]" build/run_online_echo_coop.log'
check "no crash" '! grep -qE "FATAL|\[crash\]" build/run_online_off.log build/run_online_echo.log build/run_online_echo_coop.log'
rm -rf logs dev-events-log.txt laonline-*.txt laonline-*.jsonl laonline-*.log  # (the echo runs' journal)
exit $fail
