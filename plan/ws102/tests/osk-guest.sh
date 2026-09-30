#!/bin/sh
# ws102: the on-screen keyboard (userland/desktop/wayland/keyboard.c) on the Venus guest.  The running guest gets this
# worktree's compositor (BIN); zdesktop --glass at 1280x800 without a client.  The steps read zdesktop's log through SSH
# and the pictures (nothing reads the console).  The pointer is driven through QMP, the fingers with /bin/touchinject
# (the pen image's injected touch screen, 1280x800 screen pixels).
#   install   the compositor and its libraries into the guest
#   start     zdesktop --glass 1280x800 (started again by each step that needs a fresh one)
#   pointer   (p002) the pointer's swipes: the bottom-right corner's opens the flick panel (318x766 at 962,34, the right column;
#             flick-open.png), its close key closes it; the same swipe twice opens and closes it; the bottom-left
#             corner's opens the QWERTY panel (1280x336 at 0,464, the bottom row; qwerty-open.png) and the bottom-right's then changes
#             to the flick panel
#   edges     (p002, D3) the corners do not take the other gestures' strokes: a straight-up stroke from the bottom-right
#             corner opens nothing; the bottom edge's swipe up in the middle still opens Wiseview; a swipe right from
#             the left edge just above the corner still switches the desktop
#   flick     (p003) the flick panel's keys (72 px at 968,488 and every 78 px): a tap on あ, a flick left on か (き),
#             a flick up held on な (the petals, petals.png; ぬ), the face key to the alpha face (abc: a, flick up c) and
#             the number face (1, 2 flicked down >), back to kana; a finger's flick right on あ (え)
#   send      (p004) what the keys type reaches the focused application: in Text Editor (/root/osk.txt) the alpha face's
#             a i u e o, the case key (O) and the number face's 1 2 3 as keys, saved by Ctrl+S: the file is
#             "aiueO123"; in ime-probe (a text input) the kana face's あ い う え お and か with the voice key (が)
#             as commits: its text is "あいうえおかが" with a deletion of 3 bytes before が; in wltest (no text input)
#             a kana is refused; in Text Editor a kana (WS090's text input) is also tried, and noted
#   close     (p005) the title band dragged 100 px right closes the flick panel, 100 px down the QWERTY panel; App
#             Home and Wiseview close an open panel (the lock screen needs a session's compositor: not checked here)
#   large     (p005; the guest started with VENUS_SIZE=1920x1080, OSK_WIDTH=1920 OSK_HEIGHT=1080) the flick panel is
#             414x1046 at 1506,34 (keys 96 px), the QWERTY panel 1920x453 at 0,627 (large-flick.png, large-qwerty.png)
#   qwerty    (p006) the QWERTY panel: the 30 characters "Hello, World! Kei 2026 (a+b)=c" (capitals by Shift, symbols
#             by Shift on the digits and by the symbols face) tapped into Text Editor within 6 s (5 characters a second,
#             qwerty-plan.py), saved by Ctrl+S: the file is the text; Shift twice locks it (ABC typed as capitals);
#             an arrow key moves the caret (qwerty.png, qwerty-symbols.png)
#   hand      (p008) the QWERTY panel's band button (1144,472 84x28) opens the handwriting face (writing area 962x288 at
#             6,506): two strokes of the pointer and one of a finger are drawn (hand.png), the stub recognizes them
#             600 ms after the last (3 candidates, あ first, its note); every frame that draws new points does so within
#             17 ms of their input (the lag logged by zdesktop); the candidate あ is sent to ime-probe; clear empties the
#             ink; the band button goes back to the keys
#   extra     (p020) the QWERTY panel's extra keys' row: in Text Editor "bc", Home, "a", End, "|" (from the extra
#             row), Tab: "abc|<tab>"; then Ctrl (held for one key) and a select all, "z" replaces it: the file is "z";
#             Alt then Esc lets go of Alt with the key (extra.png)
#   touch     (p002; the pen image) 10 injected swipes from the bottom-right corner open and close the panel 10 times
#             (5 opens, 5 closes); 10 straight-up strokes from the corner open nothing
#   OUTDIR is the first argument:  GUEST_RUNTIME=... BIN=build/ws102-amd64 plan/ws102/tests/osk-guest.sh OUTDIR STEP...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws102-run}"
bin=${BIN:-build/ws102-amd64}
out=$1
shift
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width "${OSK_WIDTH:-1280}" --height "${OSK_HEIGHT:-800}" "$GUEST_RUNTIME/qmp.sock" "$@"; }
shot() {
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	echo "shot $1"
}
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[f]iles|[w]ltest" | awk "{print \$1}"); do kill $p; done; sleep 1'

# Reads the last line of a guest file, again when the read comes back empty (an SSH read can fail now and then).
read_file() {
	tries=0
	line=
	while [ $tries -lt 4 ]; do
		line=$(guest "cat $1" | tail -1)
		[ -n "$line" ] && break
		tries=$((tries + 1))
		sleep 1
	done
	printf '%s\n' "$line"
}

# Counts a log's lines matching a pattern.
count() {
	found=$(guest "grep -acE '$1' /tmp/zdesktop.log" | tail -1)
	echo "${found:-0}"
}

# Fails the run unless zdesktop's log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
		found=$(count "$1")
		[ "$found" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "$found" -gt 0 ] 2>/dev/null; then
		echo "log: $1 ok"
	else
		echo "log: $1 MISSING"
		status=1
	fi
}

# Fails the run unless zdesktop's keyboard lines have a fixed text (compared on the host: UTF-8 through the guest's
# shell is not reliable).
expect_text() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
		guest "grep -a 'ZWL OSK' /tmp/zdesktop.log" > "$out/osk-now.txt"
		found=$(grep -cF "$1" "$out/osk-now.txt")
		[ "$found" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "$found" -gt 0 ] 2>/dev/null; then
		echo "text: $1 ok"
	else
		echo "text: $1 MISSING"
		status=1
	fi
}

# Fails the run unless a pattern's count is exactly the one given.
expect_count() {
	found=$(count "$1")
	if [ "$found" = "$2" ]; then
		echo "count: $1 = $2 ok"
	else
		echo "count: $1 = $found (expected $2) MISSING"
		status=1
	fi
}

# Starts zdesktop afresh: glass, 1280x800 (OSK_WIDTH and OSK_HEIGHT for another size), no client.
compositor() {
	guest "$stop_all" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=${OSK_WIDTH:-1280} --height=${OSK_HEIGHT:-800} --glass \$picture > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q 'ZWL OSK zone' /tmp/zdesktop.log && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 1; echo started" >/dev/null
}

# Replays a touch script (screen pixels of 1280x800).
touch_replay() {
	printf '%s\n' "$2" > "$out/$1.script"
	put "$out/$1.script" "/tmp/$1.script"
	result=$(guest "/bin/touchinject /tmp/$1.script 2>&1; echo replay=\$?")
	printf '%s\n' "$result" | grep -q '^replay=0$' || { echo "touchinject $1: FAILED"; status=1; }
}

# Reads the QWERTY keys' places from zdesktop's log once (qkey_tap uses them).
qkey_refresh() {
	tries=0
	while [ $tries -lt 5 ]; do
		guest "grep -a 'ZWL OSK qrect' /tmp/zdesktop.log" > "$out/qrect-now.txt"
		grep -q 'ZWL OSK qrect' "$out/qrect-now.txt" && break
		tries=$((tries + 1))
		sleep 1
	done
}

# A tap (twice quickly when the second argument is 2) on the QWERTY key with a label, at its latest place read by
# qkey_refresh; a label not found fails the run and taps nothing.
qkey_tap() {
	place=$(awk -v l="label=$1" '$NF == l' "$out/qrect-now.txt" | tail -1 | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
	taps=${2:-1}
	if [ -z "$place" ]; then
		echo "qkey $1: no place MISSING"
		status=1
		return
	fi
	set -- $place
	if [ "$taps" = 2 ]; then
		pointer move $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 )) sleep 150 down sleep 40 up sleep 120 down sleep 40 up sleep 400
	else
		pointer move $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 )) sleep 150 down sleep 60 up sleep 400
	fi
}

# A tap on a key, and a flick of one (dx, dy) in steps.
key_tap() { pointer move "$1" "$2" sleep 150 down sleep 60 up sleep 350; }
key_flick() {
	pointer move "$1" "$2" sleep 150 down sleep 50 move $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 )) sleep 50 move $(( $1 + $3 )) $(( $2 + $4 )) sleep 80 up sleep 350
}

# A swipe of the pointer from one point to another in steps, the button held.
swipe() {
	pointer move "$1" "$2" sleep 200 down sleep 80 move $(( ($1 + $3) / 2 )) $(( ($2 + $4) / 2 )) sleep 60 move "$3" "$4" sleep 120 up sleep 700
}

for step in "$@"; do
	case "$step" in
	install)
		put "$bin/bin/wayland" /bin/wayland
		put userland/desktop/fonts/DroidSansFallbackFull.ttf /usr/share/fonts/keiland-fallback.ttf
		for program in textedit ime-probe wltest; do
			[ -f "$bin/bin/$program" ] && put "$bin/bin/$program" "/bin/$program"
		done
		guest 'chmod 755 /bin/textedit /bin/ime-probe /bin/wltest 2>/dev/null' >/dev/null
		for library in libkeiland libkeiui libvulkan libtruetype libwayland-client; do
			[ -f "$bin/dynamic/$library.so" ] && put "$bin/dynamic/$library.so" "/lib/$library.so"
		done
		guest 'chmod 755 /bin/wayland' >/dev/null
		;;
	start)
		compositor
		expect_log 'ZWL OSK zone kind=flick x=1252 y=772 size=28'
		expect_log 'ZWL OSK zone kind=qwerty x=0 y=772 size=28'
		;;
	pointer)
		compositor
		# The bottom-right corner's swipe up and left: the flick panel.
		swipe 1272 792 1130 650
		expect_log 'ZWL OSK press corner=flick source=pointer x=1272 y=792'
		expect_log 'ZWL OSK armed corner=flick'
		expect_log 'ZWL OSK commit corner=flick via=(distance|flick)'
		expect_log 'ZWL OSK open kind=flick x=962 y=34 width=318 height=766'
		pointer move 700 300 sleep 400
		shot flick-open.png
		# Its close key (1228,442 28x28).
		pointer move 1254 56 sleep 200 down sleep 60 up sleep 600
		expect_log 'ZWL OSK close kind=flick reason=key'
		# The same swipe twice: open, then closed by the gesture.
		swipe 1272 792 1130 650
		swipe 1272 792 1130 650
		expect_log 'ZWL OSK close kind=flick reason=gesture'
		# The bottom-left corner's swipe up and right: the QWERTY panel.
		swipe 6 792 150 650
		expect_log 'ZWL OSK press corner=qwerty source=pointer x=6 y=792'
		expect_log 'ZWL OSK open kind=qwerty x=0 y=464 width=1280 height=336'
		pointer move 700 200 sleep 400
		shot qwerty-open.png
		# The bottom-right corner's swipe changes to the flick panel; its close key closes it.
		swipe 1272 792 1130 650
		expect_count 'ZWL OSK open kind=flick' 3
		pointer move 1254 56 sleep 200 down sleep 60 up sleep 600
		expect_count 'ZWL OSK close kind=flick reason=key' 2
		;;
	flick)
		compositor
		swipe 1272 792 1130 650
		expect_log 'ZWL OSK open kind=flick x=962 y=34'
		# A tap on あ, a flick left on か.
		pointer move 1004 524 sleep 200 down sleep 80 up sleep 500
		expect_text 'ZWL OSK key face=kana row=0 column=0 dir=center action=0 text=あ'
		pointer move 1082 524 sleep 200 down sleep 60 move 1062 524 sleep 60 move 1042 526 sleep 80 up sleep 500
		expect_text 'ZWL OSK key face=kana row=0 column=1 dir=left action=0 text=き'
		# A flick up held on な: its petals, then ぬ.
		pointer move 1082 602 sleep 200 down sleep 60 move 1082 587 sleep 60 move 1083 568 sleep 600
		shot petals.png
		pointer up sleep 500
		expect_text 'ZWL OSK key face=kana row=1 column=1 dir=up action=0 text=ぬ'
		# The face key: the alpha face; abc tapped (a) and flicked up (c).
		pointer move 1238 758 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK face name=alpha'
		pointer move 1082 524 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK key face=alpha row=0 column=1 dir=center action=0 text=a'
		pointer move 1082 524 sleep 200 down sleep 60 move 1082 507 sleep 60 move 1082 490 sleep 80 up sleep 500
		expect_log 'ZWL OSK key face=alpha row=0 column=1 dir=up action=0 text=c'
		pointer move 700 300 sleep 400
		shot alpha.png
		# The number face: 1 tapped, 2 flicked down (>), back to kana.
		pointer move 1238 758 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK face name=number'
		pointer move 1004 524 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK key face=number row=0 column=0 dir=center action=0 text=1'
		pointer move 1082 524 sleep 200 down sleep 60 move 1082 542 sleep 60 move 1082 562 sleep 80 up sleep 500
		expect_log 'ZWL OSK key face=number row=0 column=1 dir=down action=0 text=>'
		pointer move 1238 758 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK face name=kana'
		# A finger's flick right on あ.
		touch_replay flick-right 'size 1279 799 2
wait 2600
down 1 1004 524
swipe 40 0 8 16
up 1
hold 800'
		expect_text 'ZWL OSK key face=kana row=0 column=0 dir=right action=0 text=え'
		;;
	send)
		compositor
		# Text Editor on an empty file, on top with the keyboard.
		guest 'rm -f /root/osk.txt /tmp/te.log; touch /root/osk.txt' >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=600 /root/osk.txt > /tmp/te.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
		expect_log 'ZWL MAP client='
		swipe 1272 792 1130 650
		expect_log 'ZWL OSK open kind=flick'
		# The alpha face: a (abc), i (ghi up), u (tuv left), e (def left), o (mno up), then the case key (O).
		key_tap 1238 758
		key_tap 1082 524
		key_flick 1004 602 0 -30
		key_flick 1082 680 -30 0
		key_flick 1160 524 -30 0
		key_flick 1160 602 0 -30
		key_tap 1004 758
		# The number face: 1 2 3.
		key_tap 1238 758
		key_tap 1004 524
		key_tap 1082 524
		key_tap 1160 524
		expect_log 'ZWL OSK send via=key code=30 shift=0'
		expect_log 'ZWL OSK send via=key code=24 shift=1'
		expect_log 'ZWL OSK send via=key code=4 shift=0'
		pointer move 400 300 sleep 300
		shot send-textedit.png
		# Saved with the physical keyboard's Ctrl+S; the file read back.
		python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" '<ctrl-s>' >/dev/null
		sleep 2
		saved=$(read_file /root/osk.txt)
		[ "$saved" = "aiueO123" ] && echo "textedit: aiueO123 ok" || { echo "textedit: ($saved) MISSING"; status=1; }
		# ime-probe (a text input) on top: the kana face (the face key once more), あいうえお, か and the voice key.
		guest 'for p in $(ps -A -o pid,args | grep "[t]extedit" | awk "{print \$1}"); do kill $p; done; rm -f /tmp/ime-probe.log' >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/ime-probe --log=/tmp/ime-probe.log --seconds=300 > /dev/null 2>&1 </dev/null & sleep 4; echo started" >/dev/null
		key_tap 1238 758
		key_tap 1004 524
		key_flick 1004 524 -30 0
		key_flick 1004 524 0 -30
		key_flick 1004 524 30 0
		key_flick 1004 524 0 30
		key_tap 1082 524
		key_tap 1004 758
		sleep 1
		guest 'cat /tmp/ime-probe.log' > "$out/ime-probe.log"
		grep -qF 'PROBE TEXT text=あいうえおかが' "$out/ime-probe.log" && echo "ime-probe: あいうえおかが ok" || { echo "ime-probe: text MISSING"; status=1; }
		grep -qF 'PROBE DELETE before=3 after=0' "$out/ime-probe.log" && echo "ime-probe: delete 3 before が ok" || { echo "ime-probe: delete MISSING"; status=1; }
		expect_text 'ZWL OSK send via=commit text=が before=3'
		shot send-probe.png
		# wltest (no text input) on top: a kana is refused.
		guest 'for p in $(ps -A -o pid,args | grep "[i]me-probe" | awk "{print \$1}"); do kill $p; done' >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp; /bin/wltest --windowed --frames=3600 > /dev/null 2>&1 </dev/null & sleep 4; echo started" >/dev/null
		key_tap 1004 524
		expect_log 'ZWL OSK refused reason=no-text-input'
		guest 'for p in $(ps -A -o pid,args | grep "[w]ltest" | awk "{print \$1}"); do kill $p; done' >/dev/null
		# (noted, not required in L1) Text Editor with WS090's text input: あい, saved.
		guest 'rm -f /root/osk2.txt; touch /root/osk2.txt' >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=600 /root/osk2.txt > /tmp/te2.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
		key_tap 1004 524
		key_flick 1004 524 -30 0
		python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" '<ctrl-s>' >/dev/null
		sleep 2
		guest 'cat /root/osk2.txt' > "$out/osk2.txt"
		grep -qF 'あい' "$out/osk2.txt" && echo "note: Text Editor took あい by its text input" || echo "note: Text Editor did not take the kana ($(cat "$out/osk2.txt"))"
		;;
	close)
		compositor
		# The flick panel's band dragged right, the QWERTY panel's band dragged down.
		swipe 1272 792 1130 650
		pointer move 1000 52 sleep 200 down sleep 60 move 1050 52 sleep 60 move 1100 52 sleep 80 up sleep 600
		expect_log 'ZWL OSK close kind=flick reason=swipe'
		swipe 6 792 150 650
		pointer move 400 480 sleep 200 down sleep 60 move 400 530 sleep 60 move 400 580 sleep 80 up sleep 600
		expect_log 'ZWL OSK close kind=qwerty reason=swipe'
		# App Home (the launcher) closes the panel; Home closes by Esc.
		swipe 1272 792 1130 650
		pointer move 20 17 sleep 200 down sleep 60 up sleep 1200
		expect_log 'ZWL OSK close kind=flick reason=home'
		python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" '<esc>' >/dev/null
		sleep 1.5
		# Wiseview (the bottom edge's swipe) closes it; a press closes Wiseview.
		swipe 1272 792 1130 650
		expect_count 'ZWL OSK open kind=flick' 3
		swipe 640 796 640 480
		expect_log 'ZWL OSK close kind=flick reason=wiseview'
		pointer move 640 400 sleep 200 down sleep 60 up sleep 1200
		# (The lock screen, Super+L, locks only a session's compositor (--session, sessiond); this one is not.)
		;;
	large)
		compositor
		swipe 1912 1072 1770 930
		expect_log 'ZWL OSK open kind=flick x=1506 y=34 width=414 height=1046'
		pointer move 900 400 sleep 400
		shot large-flick.png
		swipe 6 1072 150 930
		expect_log 'ZWL OSK open kind=qwerty x=0 y=627 width=1920 height=453'
		pointer move 900 300 sleep 400
		shot large-qwerty.png
		;;
	qwerty)
		compositor
		guest 'rm -f /root/q.txt; touch /root/q.txt' >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=600 /root/q.txt > /tmp/te.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
		expect_log 'ZWL MAP client='
		swipe 6 792 150 650
		expect_log 'ZWL OSK open kind=qwerty'
		# The symbols face and back, so that both faces' keys are in the log.
		qkey_refresh
		qkey_tap '?123'
		expect_log 'ZWL OSK qface name=symbols'
		pointer move 700 200 sleep 300
		shot qwerty-symbols.png
		qkey_refresh
		qkey_tap 'ABC'
		expect_log 'ZWL OSK qface name=letters'
		# The 30 characters within 6 s.
		text='Hello, World! Kei 2026 (a+b)=c'
		guest "grep -a 'ZWL OSK' /tmp/zdesktop.log" > "$out/qwerty-keys.txt"
		plan=$(python3 plan/ws102/tests/qwerty-plan.py "$out/qwerty-keys.txt" "$text" 4800) || { echo "qwerty-plan: FAILED"; status=1; }
		before=$(count 'ZWL OSK qkey')
		pointer $plan
		sleep 1
		python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" '<ctrl-s>' >/dev/null
		sleep 2
		saved=$(read_file /root/q.txt)
		[ "$saved" = "$text" ] && echo "qwerty: typed \"$text\" ok" || { echo "qwerty: ($saved) MISSING"; status=1; }
		guest "grep -a 'ZWL OSK qkey' /tmp/zdesktop.log" > "$out/qwerty-qkeys.txt"
		first=$(sed -n "$((before + 1))p" "$out/qwerty-qkeys.txt" | sed -n 's/.* ms=\([0-9]*\).*/\1/p')
		last=$(tail -1 "$out/qwerty-qkeys.txt" | sed -n 's/.* ms=\([0-9]*\).*/\1/p')
		elapsed=$(( ${last:-0} - ${first:-0} ))
		[ "$elapsed" -gt 0 ] && [ "$elapsed" -le 6000 ] && echo "qwerty: 30 characters in $elapsed ms (5 a second or faster) ok" || { echo "qwerty: $elapsed ms MISSING"; status=1; }
		pointer move 700 200 sleep 300
		shot qwerty.png
		# Shift twice locks it: A B C as capitals; once more unlocks it.
		qkey_refresh
		qkey_tap 'Shift' 2
		expect_log 'ZWL OSK shift state=2'
		qkey_tap 'a'
		qkey_tap 'b'
		qkey_tap 'c'
		qkey_tap 'Shift'
		expect_log 'ZWL OSK shift state=0'
		# The left arrow (key 105).
		qkey_tap '←'
		expect_log 'ZWL OSK send via=key code=105 shift=0'
		python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" '<ctrl-s>' >/dev/null
		sleep 2
		saved=$(read_file /root/q.txt)
		[ "$saved" = "${text}ABC" ] && echo "qwerty: Shift locked: ABC ok" || { echo "qwerty: ($saved) MISSING"; status=1; }
		;;
	hand)
		compositor
		guest 'rm -f /tmp/ime-probe.log' >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/ime-probe --log=/tmp/ime-probe.log --seconds=300 > /dev/null 2>&1 </dev/null & sleep 4; echo started" >/dev/null
		swipe 6 792 150 650
		expect_log 'ZWL OSK open kind=qwerty x=0 y=464 width=1280 height=336'
		pointer move 1186 486 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK hand on area=6,506,962,288'
		# Two strokes of the pointer, moving every 16 ms.
		pointer move 200 580 sleep 150 down sleep 16 move 220 590 sleep 16 move 240 600 sleep 16 move 260 612 sleep 16 move 280 625 sleep 16 \
			move 300 640 sleep 16 move 320 655 sleep 16 move 340 668 sleep 16 move 360 680 sleep 16 move 380 690 sleep 16 move 400 700 sleep 60 up sleep 150
		pointer move 420 560 sleep 150 down sleep 16 move 418 580 sleep 16 move 416 600 sleep 16 move 414 620 sleep 16 move 412 640 sleep 16 \
			move 410 660 sleep 16 move 408 680 sleep 16 move 406 700 sleep 16 move 404 720 sleep 16 move 402 740 sleep 60 up sleep 150
		# A stroke of a finger.
		touch_replay stroke 'size 1279 799 2
wait 2600
down 1 500 600
swipe 150 60 15 16
up 1
hold 1500'
		expect_log 'ZWL OSK hand stroke-end strokes=3 '
		expect_text 'ZWL OSK hand recognize strokes=3 '
		expect_text 'candidates=3 first=あ note=認識はまだ'
		pointer move 700 300 sleep 300
		shot hand.png
		# Every frame that drew new points came within one frame of their input: its lag is shorter than the time since
		# the frame before (the guest's frame interval; 17 ms would need 60 frames a second, which this guest's
		# software Vulkan does not draw -- the numbers are kept for the record).
		guest "grep -a 'ZWL OSK hand frame' /tmp/zdesktop.log" > "$out/hand-frames.txt"
		frames=$(grep -c 'lag_ms=' "$out/hand-frames.txt")
		late=$(sed -n 's/.*lag_ms=\([0-9]*\) gap_ms=\([0-9]*\).*/\1 \2/p' "$out/hand-frames.txt" | awk '$1 > $2 { n++ } END { print n + 0 }')
		worst=$(sed -n 's/.*lag_ms=\([0-9]*\).*/\1/p' "$out/hand-frames.txt" | sort -n | tail -1)
		gaps=$(sed -n 's/.*gap_ms=\([0-9]*\).*/\1/p' "$out/hand-frames.txt" | sort -n | awk '{ a[NR] = $1 } END { print a[int((NR + 1) / 2)] }')
		[ "${frames:-0}" -ge 5 ] && [ "${late:-1}" = 0 ] && echo "hand: $frames frames, each within one frame of its input (worst lag $worst ms, median frame interval $gaps ms) ok" || { echo "hand: $frames frames, $late late (worst lag $worst ms, median interval $gaps ms) MISSING"; status=1; }
		# The candidate あ (1010,591) to ime-probe; the ink is cleared.
		pointer move 1022 577 sleep 200 down sleep 60 up sleep 800
		guest 'cat /tmp/ime-probe.log' > "$out/ime-probe-hand.log"
		grep -qF 'PROBE TEXT text=あ' "$out/ime-probe-hand.log" && echo "hand: あ sent ok" || { echo "hand: あ MISSING"; status=1; }
		# A stroke, then clear (1035,668): no recognition follows.
		recognized=$(count 'ZWL OSK hand recognize')
		pointer move 300 600 sleep 150 down sleep 16 move 330 610 sleep 16 move 360 620 sleep 60 up sleep 150
		pointer move 1047 665 sleep 200 down sleep 60 up sleep 1200
		expect_log 'ZWL OSK hand clear'
		expect_count 'ZWL OSK hand recognize' "$recognized"
		# Back to the keys.
		pointer move 1186 486 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK hand off'
		;;
	extra)
		compositor
		guest 'rm -f /root/x.txt; touch /root/x.txt' >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=600 /root/x.txt > /tmp/te.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
		expect_log 'ZWL MAP client='
		swipe 6 792 150 650
		expect_log 'ZWL OSK open kind=qwerty'
		qkey_refresh
		qkey_tap 'b'
		qkey_tap 'c'
		qkey_tap 'Home'
		qkey_tap 'a'
		qkey_tap 'End'
		qkey_tap '|'
		qkey_tap 'Tab'
		expect_log 'ZWL OSK send via=key code=102 shift=0 held=0'
		expect_log 'ZWL OSK send via=key code=43 shift=1 held=0'
		expect_log 'ZWL OSK send via=key code=15 shift=0 held=0'
		python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" '<ctrl-s>' >/dev/null
		sleep 2
		guest 'cat /root/x.txt' > "$out/x1.txt"
		printf 'abc|\t' > "$out/x1.want"
		cmp -s "$out/x1.txt" "$out/x1.want" && echo "extra: abc|<tab> ok" || { echo "extra: ($(od -c "$out/x1.txt" | head -2 | tr '\n' ' ')) MISSING"; status=1; }
		pointer move 700 200 sleep 300
		shot extra.png
		# Ctrl held for one key: Ctrl+A selects all; z replaces it; Ctrl is let go after the one key.
		qkey_tap 'Ctrl'
		expect_log 'ZWL OSK held=4'
		qkey_tap 'a'
		expect_log 'ZWL OSK send via=key code=30 shift=0 held=4'
		qkey_tap 'z'
		expect_log 'ZWL OSK send via=key code=44 shift=0 held=0'
		python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" '<ctrl-s>' >/dev/null
		sleep 2
		saved=$(read_file /root/x.txt)
		[ "$saved" = "z" ] && echo "extra: Ctrl+A then z: z ok" || { echo "extra: ($saved) MISSING"; status=1; }
		# Alt twice lets go of it; Alt then Esc sends Esc with Alt once.
		qkey_tap 'Alt'
		qkey_tap 'Alt'
		expect_count 'ZWL OSK held=0' 1
		qkey_tap 'Alt'
		qkey_tap 'Esc'
		expect_log 'ZWL OSK send via=key code=1 shift=0 held=8'
		;;
	maxshot)
		# (a picture for the user, 2026-09-30) Text Editor docked (maximized) by a double click on its title bar, lines
		# typed on the QWERTY panel, the panel left open; the work area (p007) is not done, so the panel covers the
		# window's bottom.
		compositor
		guest 'rm -f /root/m.txt; touch /root/m.txt' >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=600 /root/m.txt > /tmp/te.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
		expect_log 'ZWL MAP client='
		set -- $(guest "grep -a 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
		wx=${1:-0}; wy=${2:-0}
		pointer move $((wx + 200)) $((wy - 38)) sleep 200 down sleep 40 up sleep 120 down sleep 40 up sleep 1500
		expect_log 'ZWL GLASS dock'
		swipe 6 $(( ${OSK_HEIGHT:-800} - 8 )) 150 $(( ${OSK_HEIGHT:-800} - 150 ))
		expect_log 'ZWL OSK open kind=qwerty'
		qkey_refresh
		qkey_tap '?123'
		qkey_refresh
		qkey_tap 'ABC'
		qkey_refresh
		plan=$(python3 plan/ws102/tests/qwerty-plan.py "$out/qrect-now.txt" 'Hello from Kei!
The on-screen keyboard is part of the desktop.
Symbols: (a+b)*2 = c; x/y - 1 >= 0 ~ ok' 30000) || { echo "qwerty-plan: FAILED"; status=1; }
		pointer $plan
		sleep 1
		pointer move $(( ${OSK_WIDTH:-1280} / 2 )) 300 sleep 400
		shot "maximized-qwerty-${OSK_WIDTH:-1280}x${OSK_HEIGHT:-800}.png"
		;;
	maxflick)
		# (a picture for the user, 2026-09-30) Text Editor docked by a double click on its title bar; the flick panel
		# (the right column) types こんにちは, a new line, kei and 2026; the panel left open (the work area, p007, is not
		# done: the panel covers the window's right side).
		compositor
		guest 'rm -f /root/f.txt; touch /root/f.txt' >/dev/null
		guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=600 /root/f.txt > /tmp/te.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
		expect_log 'ZWL MAP client='
		set -- $(guest "grep -a 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
		wx=${1:-0}; wy=${2:-0}
		pointer move $((wx + 200)) $((wy - 38)) sleep 200 down sleep 40 up sleep 120 down sleep 40 up sleep 1500
		expect_log 'ZWL GLASS dock'
		width=${OSK_WIDTH:-1280}
		height=${OSK_HEIGHT:-800}
		swipe $((width - 8)) $((height - 8)) $((width - 150)) $((height - 150))
		expect_log 'ZWL OSK open kind=flick'
		# The flick keys' middles: a key's side, the step between keys, the first column and row (the panel's bottom).
		fkey=$((height / 11)); [ $fkey -lt 64 ] && fkey=64; [ $fkey -gt 96 ] && fkey=96
		fstep=$((fkey + 6))
		fx=$((width - 4 * fkey - 30 + 6 + fkey / 2))
		fy=$((height - 4 * fstep + fkey / 2))
		fl=$((fkey / 2))
		# こ (か down), ん (わ up), に (な left), ち (た left), は (は), a new line.
		key_flick $((fx + fstep)) $fy 0 $fl
		key_flick $((fx + fstep)) $((fy + 3 * fstep)) 0 -$fl
		key_flick $((fx + fstep)) $((fy + fstep)) -$fl 0
		key_flick $fx $((fy + fstep)) -$fl 0
		key_tap $((fx + 2 * fstep)) $((fy + fstep))
		key_tap $((fx + 3 * fstep)) $((fy + 2 * fstep))
		# The alpha face: k (jkl left), e (def left), i (ghi up), a space.
		key_tap $((fx + 3 * fstep)) $((fy + 3 * fstep))
		key_flick $((fx + fstep)) $((fy + fstep)) -$fl 0
		key_flick $((fx + 2 * fstep)) $fy -$fl 0
		key_flick $fx $((fy + fstep)) 0 -$fl
		key_tap $((fx + 3 * fstep)) $((fy + fstep))
		# The number face: 2 0 2 6.
		key_tap $((fx + 3 * fstep)) $((fy + 3 * fstep))
		key_tap $((fx + fstep)) $fy
		key_tap $((fx + fstep)) $((fy + 3 * fstep))
		key_tap $((fx + fstep)) $fy
		key_tap $((fx + 2 * fstep)) $((fy + fstep))
		sleep 1
		pointer move $((width / 3)) 300 sleep 400
		shot "maximized-flick-${width}x${height}.png"
		;;
	edges)
		compositor
		# A straight-up stroke from the bottom-right corner: no panel, no Wiseview.
		swipe 1272 792 1272 560
		expect_log 'ZWL OSK cancel reason=(unarmed|direction)'
		expect_count 'ZWL OSK open' 0
		expect_count 'ZWL WISEVIEW open' 0
		# The bottom edge's swipe up in the middle opens Wiseview; a press in the middle closes it.
		swipe 640 796 640 480
		expect_log 'ZWL WISEVIEW open windows='
		shot wiseview.png
		pointer move 640 400 sleep 200 down sleep 60 up sleep 900
		expect_count 'ZWL OSK press' 1
		# A swipe right from the left edge above the corner switches the desktop.
		pointer move 6 700 sleep 200 down sleep 80 move 60 700 sleep 60 move 200 700 sleep 60 move 420 700 sleep 120 up sleep 1200
		expect_log 'ZWL GLASS desktop swipe'
		expect_count 'ZWL OSK press' 1
		;;
	touch)
		compositor
		# Ten swipes from the bottom-right corner: open, close, ... (the same swipe toggles).
		script='size 1279 799 2
wait 2600'
		i=0
		while [ $i -lt 10 ]; do
			script="$script
down 1 1272 792
swipe -150 -150 12 16
up 1
wait 400"
			i=$((i + 1))
		done
		touch_replay swipes "$script
hold 800"
		expect_count 'ZWL OSK press corner=flick source=touch' 10
		expect_count 'ZWL OSK commit corner=flick' 10
		expect_count 'ZWL OSK open kind=flick' 5
		expect_count 'ZWL OSK close kind=flick reason=gesture' 5
		# Ten straight-up strokes from the corner: nothing opens.
		script='size 1279 799 2
wait 2600'
		i=0
		while [ $i -lt 10 ]; do
			script="$script
down 1 1272 792
swipe 0 -200 12 16
up 1
wait 400"
			i=$((i + 1))
		done
		touch_replay straight "$script
hold 800"
		expect_count 'ZWL OSK press corner=flick source=touch' 20
		expect_count 'ZWL OSK open kind=flick' 5
		;;
	stop)
		guest "$stop_all" >/dev/null
		;;
	*)
		echo "unknown step $step"
		status=1
		;;
	esac
done
errors=$(count 'ERROR')
[ "$errors" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest 'grep -a "ZWL OSK" /tmp/zdesktop.log' > "$out/osk-log.txt"
[ $status = 0 ] && echo "osk-guest: PASS" || echo "osk-guest: FAIL"
exit $status
