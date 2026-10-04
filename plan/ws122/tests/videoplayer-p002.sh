#!/bin/sh
# ws122-p002: the simple video player on the Venus guest with a sound card (videoplayer-guest.sh), image of
# config-amd64-p002.mk with the sample (sample.mp4: 20 s, 320x240 MPEG-4 Part 2 at 25 fps, AAC 48 kHz stereo,
# a 440 Hz tone).  zdesktop --glass at 1280x800, audiod, the player at 960x600 on the sample.
#  1. Opens and plays: READY, AUDIO error=0 (audiod's stream), OPEN ... width=320 height=240 duration_ms=20000
#     video=mpeg4 audio=aac, OPENED error=0, PLAY, FRAMES shown=1 and shown=100 (playing.png).
#  2. Space pauses (PAUSE shown= time_ms=), Space 2 s later plays (PLAY shown= time_ms=): no picture and no time
#     went by between them.
#  3. Right goes forward 10 s: SEEK to_ms=, SEEK done to_ms= (seek.png).
#  4. The end: END reached, ENDED shown=.
#  5. Left goes back 10 s from the end (SEEK done; the player stands paused there); Ctrl+Q: DONE reason=close.
#  6. The sound: QEMU's recording of the HDA output ($GUEST_RUNTIME/sound.wav) has a peak above 1000.
# PASS: every "ok" line and the last line videoplayer-p002: PASS.
#
#   plan/ws122/tests/videoplayer-guest.sh start IMAGE    (the guest must be up, this image)
#   plan/ws122/tests/videoplayer-p002.sh [OUTDIR]        (default build/ws122-p002)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws122-run}"
export GUEST_RUNTIME
out=${1:-build/ws122-p002}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[v]ideoplayer|[a]udiod" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[v]ideoplayer|[a]udiod" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern (within TRIES seconds, default 5).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "${3:-5}" ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}

# The number of lines of the player's log matching a pattern.
count_log() {
	guest "grep -cE '$1' /tmp/v.log" | tail -1
}

# A picture of the screen with the pointer out of the way.
shot() {
	pointer move 1270 790 sleep 500
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp
/sbin/audiod > /tmp/a.log 2>&1 </dev/null & sleep 1
picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
rm -f /tmp/wayland-0; /bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4
/bin/videoplayer --timeout-s=300 /usr/share/videoplayer-tests/sample.mp4 > /tmp/v.log 2>&1 </dev/null & sleep 3; echo started' >/dev/null

# 1. Opened and playing.
expect_log /tmp/v.log 'READY width=960 height=600'
expect_log /tmp/v.log 'AUDIO error=0'
expect_log /tmp/v.log 'OPEN path=/usr/share/videoplayer-tests/sample.mp4 width=320 height=240 duration_ms=20000 video=mpeg4 audio=aac'
expect_log /tmp/v.log 'OPENED path=.* error=0'
expect_log /tmp/v.log 'VIDEOPLAYER PLAY shown=0 time_ms=0'
expect_log /tmp/v.log 'FRAMES shown=1 '
expect_log /tmp/v.log 'FRAMES shown=100 ' 8
shot playing.png

# 2. Pause and play: from the pause to the play 2 s later, the pictures shown and the clock stand still.
keys '<spc>'
expect_log /tmp/v.log 'VIDEOPLAYER PAUSE shown='
sleep 2
keys '<spc>'
expect_log /tmp/v.log 'VIDEOPLAYER PLAY shown=[1-9]'
set -- $(guest "grep -E 'VIDEOPLAYER (PAUSE|PLAY) shown=' /tmp/v.log | tail -2" |
    sed -n 's/.* shown=\([0-9]*\) time_ms=\([0-9]*\).*/\1 \2/p' | tr '\n' ' ')
if [ $# = 4 ] && [ $(($3 - $1)) -le 1 ] && [ $(($4 - $2)) -le 100 ]; then
	echo "pause: stood still (shown $1 -> $3, time_ms $2 -> $4) ok"
else
	echo "pause: did not stand still ($*) FAIL"
	status=1
fi

# 3. Forward 10 s.
keys '<right>'
expect_log /tmp/v.log 'SEEK to_ms=[0-9]+$'
expect_log /tmp/v.log 'SEEK done to_ms=[0-9]+'
sleep 1
shot seek.png

# 4. The end (at most 20 s more).
expect_log /tmp/v.log 'END reached' 20
expect_log /tmp/v.log 'ENDED shown=[0-9]+' 5

# 5. Back 10 s from the end; then quit.
seeks=$(count_log 'SEEK done')
keys '<left>'
sleep 2
seeks_after=$(count_log 'SEEK done')
[ "${seeks_after:-0}" -gt "${seeks:-0}" ] 2>/dev/null && echo "back: SEEK done ok" || { echo "back: SEEK done MISSING"; status=1; }
keys '<ctrl-q>'
expect_log /tmp/v.log 'DONE reason=close'

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
guest 'cat /tmp/v.log' > "$out/v.log"
guest 'cat /tmp/a.log' > "$out/a.log"

# 6. The sound reached the card.
if [ -s "$GUEST_RUNTIME/sound.wav" ]; then
	peak=$(python3 plan/ws035/tests/hda-wav-check.py peak "$GUEST_RUNTIME/sound.wav" 2>&1 | sed -n 's/^peak=//p')
	if [ "${peak:-0}" -gt 1000 ] 2>/dev/null; then
		echo "sound: peak=$peak ok"
	else
		echo "sound: peak=$peak FAIL"
		status=1
	fi
else
	echo "sound: no recording FAIL"
	status=1
fi
[ $status = 0 ] && echo "videoplayer-p002: PASS" || echo "videoplayer-p002: FAIL"
exit $status
