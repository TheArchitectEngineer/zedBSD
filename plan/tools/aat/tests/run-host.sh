#!/bin/sh
# ws173-p003: the host self-test of plan/tools/aat/aat.  --local runs every command on this host, with
# fake-inject.py for the target's injector (the same FIFO and done-file protocol, README.md) and fake-shot.py for
# keiland-shot, so the command line, the protocol's lines, the logs' marks and waits, the windows from ZWL lines and
# the transfers are checked without a target.  The SSH transport is the same code with ssh in front (checked against
# QEMU by T1, README.md).  Last line: "aat-host: PASS" or "aat-host: FAIL".
#   sh plan/tools/aat/tests/run-host.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../../.."
here=plan/tools/aat/tests
tmp=$(mktemp -d)
trap 'python3 plan/tools/aat/aat --local stop >/dev/null 2>&1; rm -rf "$tmp"' EXIT
export AAT_RUN_DIR="$tmp/run" AAT_STATE="$tmp/state" AAT_LOG="$tmp/session.log"
export AAT_INJECT="python3 $PWD/$here/fake-inject.py" AAT_SHOT="python3 $PWD/$here/fake-shot.py {path}"
aat() { timeout 60 python3 plan/tools/aat/aat --local "$@"; }
status=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; status=1; }
record="$tmp/run/inject.done.record"
expect_record() {
	# The lines the injector took since the last check, against what was expected.
	got=$(cat "$record" 2>/dev/null)
	: > "$record" 2>/dev/null
	if [ "$got" = "$2" ]; then ok "$1"; else bad "$1: got [$got] want [$2]"; fi
}

# The injector, with the screen's size from a shot.
aat start | grep -q 'screen 64x48' && ok start || bad start
: > "$record"

# A click: the point, the press and the release.
aat click 10 20 && expect_record click "abs 10 20
wait 30
button 272 1
wait 60
button 272 0"

# A double right click.
aat click 1 2 --button right --count 2 && expect_record double "abs 1 2
wait 30
button 273 1
wait 60
button 273 0
wait 120
button 273 1
wait 60
button 273 0"

# A drag in two steps.
aat drag 0 0 10 20 --steps 2 --ms 5 && expect_record drag "abs 0 0
wait 50
button 272 1
wait 80
abs 5 10
wait 5
abs 10 20
wait 5
wait 80
button 272 0"

# The wheel, a relative move, a chord, typing.
aat wheel 3 4 2 && expect_record wheel "abs 3 4
wait 30
wheel 2 0"
aat rel -5 7 && expect_record rel "rel -5 7"
aat key ctrl+alt+t && expect_record chord "key 29 1
key 56 1
key 20 1
key 20 0
key 56 0
key 29 0
wait 40"
aat type 'Hi!' && expect_record type "key 42 1
key 35 1
key 35 0
key 42 0
key 23 1
key 23 0
key 42 1
key 2 1
key 2 0
key 42 0"

# Refusals here: off the screen, an unknown key, a character a US layout lacks.
aat click 64 10 2>/dev/null && bad "off-screen accepted" || ok "off-screen refused"
aat key ctrl+nosuchkey 2>/dev/null && bad "unknown key accepted" || ok "unknown key refused"
aat type 'é' 2>/dev/null && bad "non-ASCII accepted" || ok "non-ASCII refused"
: > "$record"

# The injector's own refusal comes back as a failure (the fake, like the real one, refuses a wait over 10 s).
aat drag 0 0 1 1 --steps 1 --ms 20000 2>"$tmp/refused" && bad "injector refusal" || { grep -q 'refused a command' "$tmp/refused" && ok "injector refusal reported" || bad "injector refusal message"; }
: > "$record"

# The log: a mark, a line written after it is waited for and found; one before it is not.
printf 'ZWL MAP client=3 surface=9 x=100 y=50\nold line\n' > "$AAT_LOG"
aat mark before >/dev/null
(sleep 1; printf 'ZWL WINDOW centred surface=9 x=120 y=60 width=640 height=480\nNOTES APPEARANCE appearance=1\n' >> "$AAT_LOG") &
aat wait-log 'APPEARANCE appearance=1' --since before --timeout 10 | grep -q 'NOTES APPEARANCE' && ok wait-log || bad wait-log
aat lines 'old line' --since before >/dev/null && bad "lines before the mark" || ok "lines after the mark only"
aat lines 'old line' >/dev/null && ok "lines from the start" || bad "lines from the start"
aat wait-log 'never' --timeout 1 2>/dev/null && bad "wait-log timeout" || ok "wait-log timeout"
aat where 9 | grep -qx '120 60 640 480' && ok where || bad where
aat windows | grep -q '^9 3 True 120 60 640 480$' && ok windows || bad windows
printf 'ZWL UNMAP client=3 surface=9\n' >> "$AAT_LOG"
aat windows | grep -q '^9 3 False ' && ok unmap || bad unmap

# A shot, a command, files both ways.
aat shot "$tmp/screen.png" | grep -q '64x48' && ok shot || bad shot
[ "$(aat run 'echo hi; exit 3'; echo " $?")" = "hi
 3" ] && ok run || bad run
echo payload > "$tmp/a.txt"
aat put "$tmp/a.txt" "$tmp/b.txt" >/dev/null && aat get "$tmp/b.txt" "$tmp/c.txt" >/dev/null && cmp -s "$tmp/a.txt" "$tmp/c.txt" && ok files || bad files

# Stopped: the next command says the injector is not running.
aat stop >/dev/null
aat click 1 1 2>/dev/null && bad "stopped injector" || ok "stopped injector refused"
[ $status -eq 0 ] && echo "aat-host: PASS" || echo "aat-host: FAIL"
exit $status
