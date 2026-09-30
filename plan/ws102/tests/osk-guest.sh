#!/bin/sh
# ws102: the on-screen keyboard (userland/desktop/wayland/keyboard.c) on the Venus guest.  The running guest gets this
# worktree's compositor (BIN); zdesktop --glass at 1280x800 without a client.  The steps read zdesktop's log through SSH
# and the pictures (nothing reads the console).  The pointer is driven through QMP, the fingers with /bin/touchinject
# (the pen image's injected touch screen, 1280x800 screen pixels).
#   install   the compositor and its libraries into the guest
#   start     zdesktop --glass 1280x800 (started again by each step that needs a fresh one)
#   pointer   (p002) the pointer's swipes: the bottom-right corner's opens the flick panel (318x354 at 950,434;
#             flick-open.png), its close key closes it; the same swipe twice opens and closes it; the bottom-left
#             corner's opens the QWERTY panel (1256x304 at 12,484; qwerty-open.png) and the bottom-right's then changes
#             to the flick panel
#   edges     (p002, D3) the corners do not take the other gestures' strokes: a straight-up stroke from the bottom-right
#             corner opens nothing; the bottom edge's swipe up in the middle still opens Wiseview; a swipe right from
#             the left edge just above the corner still switches the desktop
#   flick     (p003) the flick panel's keys (72 px at 956,476 and every 78 px): a tap on あ, a flick left on か (き),
#             a flick up held on な (the petals, petals.png; ぬ), the face key to the alpha face (abc: a, flick up c) and
#             the number face (1, 2 flicked down >), back to kana; a finger's flick right on あ (え)
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
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
shot() {
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	echo "shot $1"
}
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[f]iles|[w]ltest" | awk "{print \$1}"); do kill $p; done; sleep 1'

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

# Starts zdesktop afresh: glass, 1280x800, no client.
compositor() {
	guest "$stop_all" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass \$picture > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q 'ZWL OSK zone' /tmp/zdesktop.log && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 1; echo started" >/dev/null
}

# Replays a touch script (screen pixels of 1280x800).
touch_replay() {
	printf '%s\n' "$2" > "$out/$1.script"
	put "$out/$1.script" "/tmp/$1.script"
	result=$(guest "/bin/touchinject /tmp/$1.script 2>&1; echo replay=\$?")
	printf '%s\n' "$result" | grep -q '^replay=0$' || { echo "touchinject $1: FAILED"; status=1; }
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
		for library in libkeiland libvulkan libtruetype libwayland-client; do
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
		expect_log 'ZWL OSK open kind=flick x=950 y=434 width=318 height=354'
		pointer move 700 300 sleep 400
		shot flick-open.png
		# Its close key (1228,442 28x28).
		pointer move 1242 456 sleep 200 down sleep 60 up sleep 600
		expect_log 'ZWL OSK close kind=flick reason=key'
		# The same swipe twice: open, then closed by the gesture.
		swipe 1272 792 1130 650
		swipe 1272 792 1130 650
		expect_log 'ZWL OSK close kind=flick reason=gesture'
		# The bottom-left corner's swipe up and right: the QWERTY panel.
		swipe 6 792 150 650
		expect_log 'ZWL OSK press corner=qwerty source=pointer x=6 y=792'
		expect_log 'ZWL OSK open kind=qwerty x=12 y=484 width=1256 height=304'
		pointer move 700 200 sleep 400
		shot qwerty-open.png
		# The bottom-right corner's swipe changes to the flick panel; its close key closes it.
		swipe 1272 792 1130 650
		expect_count 'ZWL OSK open kind=flick' 3
		pointer move 1242 456 sleep 200 down sleep 60 up sleep 600
		expect_count 'ZWL OSK close kind=flick reason=key' 2
		;;
	flick)
		compositor
		swipe 1272 792 1130 650
		expect_log 'ZWL OSK open kind=flick x=950 y=434'
		# A tap on あ, a flick left on か.
		pointer move 992 512 sleep 200 down sleep 80 up sleep 500
		expect_text 'ZWL OSK key face=kana row=0 column=0 dir=center action=0 text=あ'
		pointer move 1070 512 sleep 200 down sleep 60 move 1050 512 sleep 60 move 1030 514 sleep 80 up sleep 500
		expect_text 'ZWL OSK key face=kana row=0 column=1 dir=left action=0 text=き'
		# A flick up held on な: its petals, then ぬ.
		pointer move 1070 590 sleep 200 down sleep 60 move 1070 575 sleep 60 move 1071 556 sleep 600
		shot petals.png
		pointer up sleep 500
		expect_text 'ZWL OSK key face=kana row=1 column=1 dir=up action=0 text=ぬ'
		# The face key: the alpha face; abc tapped (a) and flicked up (c).
		pointer move 1226 746 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK face name=alpha'
		pointer move 1070 512 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK key face=alpha row=0 column=1 dir=center action=0 text=a'
		pointer move 1070 512 sleep 200 down sleep 60 move 1070 495 sleep 60 move 1070 478 sleep 80 up sleep 500
		expect_log 'ZWL OSK key face=alpha row=0 column=1 dir=up action=0 text=c'
		pointer move 700 300 sleep 400
		shot alpha.png
		# The number face: 1 tapped, 2 flicked down (>), back to kana.
		pointer move 1226 746 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK face name=number'
		pointer move 992 512 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK key face=number row=0 column=0 dir=center action=0 text=1'
		pointer move 1070 512 sleep 200 down sleep 60 move 1070 530 sleep 60 move 1070 550 sleep 80 up sleep 500
		expect_log 'ZWL OSK key face=number row=0 column=1 dir=down action=0 text=>'
		pointer move 1226 746 sleep 200 down sleep 60 up sleep 500
		expect_log 'ZWL OSK face name=kana'
		# A finger's flick right on あ.
		touch_replay flick-right 'size 1279 799 2
wait 2600
down 1 992 512
swipe 40 0 8 16
up 1
hold 800'
		expect_text 'ZWL OSK key face=kana row=0 column=0 dir=right action=0 text=え'
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
