#!/bin/sh
# ws173-p003: the host self-test of plan/tools/aat/aat.  --local runs every command on this host, with
# fake-aat-input.py for P1's aat-input (the same command line and answers) and fake-shot.py for keiland-shot, so the
# command line, the commands sent, the logs' marks and waits, the windows from ZWL lines and the transfers are checked
# without a target.  The SSH transport is the same code with ssh in front (checked against QEMU by T1, README.md).
# Last line: "aat-host: PASS" or "aat-host: FAIL".
#   sh plan/tools/aat/tests/run-host.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../../.."
here=plan/tools/aat/tests
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export AAT_RUN_DIR="$tmp/run" AAT_STATE="$tmp/state" AAT_LOG="$tmp/session.log" FAKE_AAT_DIR="$tmp"
export AAT_INPUT="python3 $PWD/$here/fake-aat-input.py" AAT_SHOT="python3 $PWD/$here/fake-shot.py {path}"
aat() { timeout 60 python3 plan/tools/aat/aat --local "$@"; }
status=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; status=1; }
record="$tmp/record"
expect_record() {
	# The commands aat-input took since the last check, against what was expected.
	got=$(cat "$record" 2>/dev/null)
	: > "$record"
	if [ "$got" = "$2" ]; then ok "$1"; else bad "$1: got [$got] want [$2]"; fi
}

# The input, with the screen's size from a shot.
aat start | grep -q 'screen 64x48' && ok start || bad start
: > "$record"

aat click 10 20 && expect_record click "click left 10 20"
aat click 1 2 --button right --count 2 && expect_record double "double-click right 1 2"
aat click 1 2 --count 3 && expect_record triple "click left 1 2
click left 1 2
click left 1 2"
aat drag 0 0 10 20 --steps 2 && expect_record drag "drag 0 0 10 20 2"
aat wheel 3 4 -2 1 && expect_record wheel "move-to 3 4
wheel -2
hwheel 1"
aat move 5 6 && expect_record move "move-to 5 6"
aat rel -5 7 && expect_record rel "move -5 7"
aat down --button middle && aat up --button middle && expect_record buttons "down middle
up middle"
aat key ctrl+alt+t Enter && expect_record chord "key ctrl+alt+t
key enter"
aat type 'Hi there!
ok' && expect_record type "type Hi there!
key enter
type ok"

# Refusals here: off the screen, a button not known, a character a US layout lacks.
aat click 64 10 2>/dev/null && bad "off-screen accepted" || ok "off-screen refused"
aat down --button fourth 2>/dev/null && bad "unknown button accepted" || ok "unknown button refused"
aat type 'é' 2>/dev/null && bad "non-ASCII accepted" || ok "non-ASCII refused"
# aat-input's own refusal (a key it does not know) comes back as a failure with its answer.
aat key ctrl+nosuchkey 2>"$tmp/refused" && bad "aat-input refusal" || { grep -q 'error bad key' "$tmp/refused" && ok "aat-input refusal reported" || bad "aat-input refusal message"; }
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

# Stopped: the next command says there is no server.
aat stop >/dev/null
aat click 1 1 2>"$tmp/stopped" && bad "stopped input" || { grep -q 'no-server' "$tmp/stopped" && ok "stopped input refused" || bad "stopped input message"; }
[ $status -eq 0 ] && echo "aat-host: PASS" || echo "aat-host: FAIL"
exit $status
