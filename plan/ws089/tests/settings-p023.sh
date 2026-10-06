#!/bin/sh
# ws089-p023: the Storage page's folder analysis and Trash on the Venus guest of the Settings image
# (build-settings-image.sh), zdesktop --glass at 1280x800 and Settings as root (HOME=/root).  The test makes
# /root/p023 (a folder of 3 MB, one of 4000 small files) and a trash of two items, and removes them after.
#  1. Analyze (control 400) counts the home: "STORAGE done root=/root bytes=N"; N is du -skx /root's (in KB);
#     the largest folder's row is drawn (storage-done.png).
#  2. A click on p023's row analyses it ("STORAGE analyze root=/root/p023").
#  3. Analyze again and Stop at once: "STORAGE stopped" within a second, what was counted kept.
#  4. The Trash: its size ("STORAGE trash bytes=... items=2"), Empty Trash (402) asks, Empty (403) empties it
#     ("STORAGE emptied removed=5 errno=0": old.txt, Old and its inner.txt, and the two .trashinfo; files/ and info/
#     empty) (trash-empty.png).
# PASS: every "ok" line and the last line settings-p023: PASS.
#   plan/ws089/tests/settings-guest.sh start              (the guest must be up)
#   plan/ws089/tests/settings-p023.sh [OUTDIR]            (default build/ws089-shots/p023)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws089-shots/p023}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]ettings" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[s]ettings" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_desktop='export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q KWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1; echo started'
status=0
. plan/ws089/tests/settings-wait.sh
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "${3:-10}" ]; do
		found=$(guest "grep -caE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then echo "log: $2 ok"; else echo "log: $2 MISSING"; status=1; fi
}
shot() {
	pointer move 1270 790 sleep 300
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}
# Clicks a control by its index (the last place Settings logged for it), scrolling the page until it is in the window.
control() {
	index=$1
	pause=${2:-800}
	cx=0
	cy=9999
	for turn in 1 2 3 4 5; do
		place=$(guest "grep -a 'ZSETTINGS CONTROL index=$index ' /tmp/s.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
		set -- $place 0 0 0 0
		cx=$((wx + $1 + $3 / 2)); cy=$((wy + $2 + $4 / 2))
		[ "$2" -gt 0 ] 2>/dev/null && [ "$cy" -lt 760 ] && break
		pointer move $((wx + 700)) $((wy + 400)) sleep 200 wheel-down wheel-down wheel-down wheel-down wheel-down wheel-down wheel-down wheel-down sleep 1000
	done
	pointer move $((cx - 2)) "$cy" sleep 150 move "$cx" "$cy" sleep 300 down sleep 60 up sleep "$pause"
}

# The tree and the trash.
wait_guest
guest "$stop_all" >/dev/null
guest 'rm -rf /root/p023 /root/.local/share/Trash; mkdir -p /root/p023/big /root/p023/many /root/.local/share/Trash/files/Old /root/.local/share/Trash/info
dd if=/dev/zero of=/root/p023/big/blob bs=1024 count=3072 2>/dev/null
i=0; while [ $i -lt 4000 ]; do echo $i > /root/p023/many/f$i; i=$((i+1)); done
echo old > /root/.local/share/Trash/files/old.txt; echo inner > /root/.local/share/Trash/files/Old/inner.txt
printf "[Trash Info]\nPath=/root/old.txt\nDeletionDate=2026-10-05T08:00:00\n" > /root/.local/share/Trash/info/old.txt.trashinfo
printf "[Trash Info]\nPath=/root/Old\nDeletionDate=2026-10-05T08:00:00\n" > /root/.local/share/Trash/info/Old.trashinfo
sync; echo made' >/dev/null
du_kb=$(guest 'du -skx /root' | awk '{print $1}' | tail -1)
echo "du: ${du_kb:-?} KB"

# Settings on the Storage page.
guest "$start_desktop" >/dev/null
wait_desktop
guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/settings --timeout-s=600 storage > /tmp/s.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
find_window
expect_log /tmp/s.log 'STORAGE trash bytes=[0-9]+ items=2'

# 1. The home.
control 400 1500
expect_log /tmp/s.log 'STORAGE done root=/root bytes=[0-9]+' 30
bytes=$(guest "grep -a 'STORAGE done root=/root ' /tmp/s.log | tail -1" | sed -n 's/.* bytes=\([0-9]*\) .*/\1/p')
echo "analysis: ${bytes:-?} bytes"
[ -n "$bytes" ] && [ -n "$du_kb" ] && [ $(((bytes + 1023) / 1024)) -eq "$du_kb" ] && pass "the total is du's" || fail "the total is du's (${bytes:-?} bytes, ${du_kb:-?} KB)"
shot storage-done.png

# 2. Into p023 (its row: the first folder row whose name is p023 is the largest, row 410 or after).
row=$(guest "grep -a 'ZSETTINGS CONTROL index=41[0-9] ' /tmp/s.log | head -1" | sed -n 's/.*index=\(41[0-9]\) .*/\1/p')
control "${row:-410}" 1500
expect_log /tmp/s.log 'STORAGE analyze root=/root/' 10

# 3. Analyze and Stop at once.
control 400 50
control 400 500
expect_log /tmp/s.log 'STORAGE stopped root=' 5

# 4. The Trash emptied.
control 402 600
control 403 2000
expect_log /tmp/s.log 'STORAGE emptied removed=5 errno=0' 10
left=$(guest 'ls -A /root/.local/share/Trash/files /root/.local/share/Trash/info | grep -vc ":$\|^$"' | tail -1)
[ "${left:-1}" = 0 ] && pass "files/ and info/ are empty" || fail "files/ and info/ are empty (${left:-?} left)"
shot trash-empty.png

# Back as it was.
guest "$stop_all" >/dev/null
guest 'rm -rf /root/p023 /root/.local/share/Trash; echo removed' >/dev/null
guest 'grep -a "ZSETTINGS STORAGE" /tmp/s.log' > "$out/settings.log"
[ $status = 0 ] && echo "settings-p023: PASS" || echo "settings-p023: FAIL"
exit $status
