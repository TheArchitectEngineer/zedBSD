#!/bin/sh
# ws166-p003: the on-screen keyboard's predictions on the Venus guest of config-amd64-osk-predict.mk (built from the commit
# under test: zdesktop, keiland-ime and its dictionary come with it), zdesktop --glass at 1280x800, which starts the input
# method itself (Japanese).  ime-probe is the text input.  The pointer is driven through QMP; the flick panel's keys are
# 72 px at 968,488 and every 78 px (the kana face: か at 1082,524, わ at 1082,758 with ん flicked up, Del at 1238,524).
#  1. The panel opened by the bottom-right corner's swipe; か then ん: zdesktop logs "ZWL OSK reading=か" and
#     "reading=かん", the input method "KEI-IME PREDICT", and "ZWL OSK predictions ... reading=かん count=N" with N > 0;
#     the candidates' tab came up by itself (predict-kan.png: the words in the grid under the tabs).
#  2. Del: the reading is か again ("ZWL OSK reading=か" once more, a new answer for it).
#  3. The first word tapped (its cell from the log's "ZWL OSK crect slot=0"): "ZWL OSK candidate commit sent=1 slot=0",
#     ime-probe gets a deletion of the reading's 3 bytes and the word ("PROBE DELETE before=3", its text the word),
#     the input method learns it ("KEI-IME LEARN reading=..."), the reading ends ("reason=chosen"); chosen.png.
#  4. か again: the word chosen comes first now ("predictions ... reading=か ... first=WORD").
#  5. A space key: the reading ends ("reason=other"); the tab says there is no reading (space.png).
#  6. No ERROR in zdesktop's log.
# PASS: the last line "osk-predict: status 0", and the pictures (to Q1).
#
#   plan/ws095/tests/ime-guest.sh start IMAGE      (the guest must be up; GUEST_RUNTIME=build/ws095-run)
#   plan/ws166/tests/osk-predict-guest.sh [OUTDIR]  (default build/ws166-shots/osk)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws095-run}"
out=${1:-build/ws166-shots/osk}
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width 1280 --height 800 "$GUEST_RUNTIME/qmp.sock" "$@"; }
shot() {
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	echo "shot $1"
}
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[i]me-probe|[k]eiland-ime" | awk "{print \$1}"); do kill $p; done; sleep 1'
key_tap() { pointer move "$1" "$2" sleep 150 down sleep 60 up sleep 500; }
key_flick() {
	pointer move "$1" "$2" sleep 150 down sleep 50 move $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 )) sleep 50 move $(( $1 + $3 )) $(( $2 + $4 )) sleep 80 up sleep 500
}
swipe() {
	pointer move "$1" "$2" sleep 200 down sleep 80 move $(( ($1 + $3) / 2 )) $(( ($2 + $4) / 2 )) sleep 60 move "$3" "$4" sleep 120 up sleep 700
}

# The keyboard's and the input method's lines, read on the host (UTF-8 through the guest's shell is not reliable).
lines() { guest "grep -aE 'ZWL OSK|KEI-IME|ERROR' /tmp/zdesktop.log" > "$out/log-now.txt"; }
expect_text() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
		lines
		found=$(grep -cF -- "$1" "$out/log-now.txt")
		[ "$found" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "$found" -gt 0 ] 2>/dev/null; then echo "text: $1 ok"; else echo "text: $1 MISSING"; status=1; fi
}
expect_re() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
		lines
		found=$(grep -cE -- "$1" "$out/log-now.txt")
		[ "$found" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "$found" -gt 0 ] 2>/dev/null; then echo "log: $1 ok"; else echo "log: $1 MISSING"; status=1; fi
}

# zdesktop (it starts the input method), and ime-probe on top.
guest "$stop_all" >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0 /tmp/ime-probe.log /root/.config/kei/ime/ja-user.dict
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q 'KEI-IME READY' /tmp/zdesktop.log && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 1
/bin/ime-probe --log=/tmp/ime-probe.log --seconds=600 > /dev/null 2>&1 </dev/null & sleep 4; echo started" >/dev/null
expect_text 'KEI-IME READY'

# 1. か, ん.
swipe 1272 792 1130 650
expect_text 'ZWL OSK open kind=flick'
key_tap 1082 524
expect_text 'ZWL OSK reading=か serial='
key_flick 1082 758 0 -30
expect_text 'ZWL OSK reading=かん serial='
expect_re 'KEI-IME PREDICT serial=[0-9]+ reading=かん bytes=[1-9]'
expect_re 'ZWL OSK predictions serial=[0-9]+ reading=かん count=[1-9]'
shot predict-kan.png

# 2. Del: か again.
key_tap 1238 524
expect_re 'ZWL OSK predictions serial=[0-9]+ reading=か count=[1-9]'

# 3. The first word.
lines
place=$(grep -a 'ZWL OSK crect slot=0 ' "$out/log-now.txt" | tail -1 | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
if [ -n "$place" ]; then
	set -- $place
	key_tap $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 ))
else
	echo "crect: MISSING"
	status=1
fi
expect_re 'ZWL OSK candidate commit sent=1 slot=0 word=[^ ]+ reading=か'
word=$(grep -a 'ZWL OSK candidate commit sent=1 slot=0' "$out/log-now.txt" | tail -1 | sed -n 's/.* word=\([^ ]*\) reading=.*/\1/p')
echo "word: $word"
expect_text "KEI-IME LEARN reading="
expect_text 'ZWL OSK reading end reason=chosen'
guest 'cat /tmp/ime-probe.log' > "$out/ime-probe.log"
grep -qF 'PROBE DELETE before=3 after=0' "$out/ime-probe.log" && echo "probe: delete 3 ok" || { echo "probe: delete MISSING"; status=1; }
[ -n "$word" ] && grep -qF "PROBE TEXT text=$word" "$out/ime-probe.log" && echo "probe: $word ok" || { echo "probe: text MISSING"; status=1; }
shot chosen.png

# 4. か again: the word chosen first.
key_tap 1082 524
[ -n "$word" ] && expect_re "ZWL OSK predictions serial=[0-9]+ reading=か count=[1-9][0-9]* first=$word\$"

# 5. A space ends the reading.
key_tap 1238 602
expect_text 'ZWL OSK reading end reason=other'
shot space.png

# 6. No ERROR.
lines
if grep -q 'ERROR' "$out/log-now.txt"; then echo "no-error: FAILED"; status=1; else echo "no-error: ok"; fi
guest "$stop_all" >/dev/null
echo "osk-predict: status $status (pictures in $out)"
exit $status
