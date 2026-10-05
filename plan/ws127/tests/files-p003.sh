#!/bin/sh
# ws127-p003: the trash at the top of another volume ($topdir/.Trash-$uid, F-041) on the Venus guest
# (plan/tools/files/build-files-image.sh; start the guest first, e.g. plan/tools/files/files-guest.sh start).
# zdesktop --glass at 1280x800, files at 1000x640; the sample home (/tmp/fhome) has the home trash, and a tmpfs
# mounted on /tmp/vol is the other volume, with Notes.txt in it.
#  1. Ctrl+A and Delete on /tmp/vol: TASK done kind=trash; Notes.txt is in /tmp/vol/.Trash-$uid/files, its record's
#     Path is relative to the volume's top (Path=Notes.txt), and nothing went to the home trash.
#  2. The Trash (sidebar) lists it (LOCATION kind=trash items=1): trash-volume.png; Put Back returns it to /tmp/vol
#     (TASK done kind=restore) and removes its record.
#  3. Ctrl+Z undoes the put back: in the volume's trash again (TASK done kind=trash).
#  4. In the Trash, Delete asks and Enter deletes it for good (TASK done kind=delete): the item and its record go.
#
#   sh plan/ws127/tests/files-p003.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws127-p003}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
. plan/tools/guest/zwl-clients.sh
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[f]iles" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[f]iles" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 5 ]; do
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

# Fails the run unless a guest test command succeeds.
expect_guest() {
	if guest "$1 && echo YES" | grep -q YES; then
		echo "guest: $2 ok"
	else
		echo "guest: $2 FAILED"
		status=1
	fi
}

# Clicks a point of the window's body (x, y from its top left) and waits.
click() {
	pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep "${3:-700}"
}
shot() {
	pointer move 1270 790 sleep 400
	check "$out/$1" >/dev/null
}

guest "$stop_all" >/dev/null
guest 'umount /tmp/vol >/dev/null 2>&1; rm -rf /tmp/vol /tmp/fhome /tmp/files.clipboard; sh /usr/share/files-tests/make-home.sh /tmp/fhome >/dev/null
mkdir -p /tmp/vol && mount -t tmpfs /tmp/vol && echo notes > /tmp/vol/Notes.txt && echo mounted' | tail -1
uid=$(guest 'id -u' | tail -1)
volume_trash=/tmp/vol/.Trash-$uid
home_trash=/tmp/fhome/.local/share/Trash
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4
HOME=/tmp/fhome /bin/files --token=f1 --timeout-s=500 --width=1000 --height=640 /tmp/vol > /tmp/f.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
zwl_app_clients
set -- $(guest "grep 'ZWL MAP client=$zc1 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p')
surface=${1:-0}; wx=${2:-0}; wy=${3:-0}
echo "files: surface $surface at $wx,$wy (uid $uid)"
expect_log /tmp/f.log 'ZFILES LOCATION kind=folder path=/tmp/vol items=1 error=0'

# 1. To the volume's trash.
click 600 400
keys '<ctrl-a>'
sleep 0.5
keys '<delete>'
sleep 1.5
expect_log /tmp/f.log 'ZFILES TASK done id=1 kind=trash state=done files=1 '
expect_guest "[ -f $volume_trash/files/Notes.txt ] && [ ! -e /tmp/vol/Notes.txt ]" 'Notes.txt is in the volume trash'
expect_guest "grep -qx 'Path=Notes.txt' $volume_trash/info/Notes.txt.trashinfo" 'the record path is relative to the volume top'
expect_guest "[ ! -e $home_trash/files/Notes.txt ]" 'nothing went to the home trash'

# 2. The Trash lists it; Put Back.
click 100 343
expect_log /tmp/f.log 'ZFILES LOCATION kind=trash path= items=1 error=0'
click 331 120
shot trash-volume.png
click 803 44 1200
expect_log /tmp/f.log 'ZFILES TASK done id=2 kind=restore state=done files=1 '
expect_guest "[ -f /tmp/vol/Notes.txt ] && [ ! -e $volume_trash/info/Notes.txt.trashinfo ]" 'put back to /tmp/vol, the record gone'

# 3. Undo the put back.
keys '<ctrl-z>'
sleep 1.5
expect_log /tmp/f.log 'ZFILES TASK done id=3 kind=trash state=done files=1 '
expect_guest "[ -f $volume_trash/files/Notes.txt ] && [ ! -e /tmp/vol/Notes.txt ]" 'undo put back: in the volume trash again'

# 4. Delete it for good from the Trash.
click 100 343
click 331 120
keys '<delete>'
sleep 1
shot delete-dialog.png
keys '<ret>'
sleep 1.5
expect_log /tmp/f.log 'ZFILES TASK done id=4 kind=delete state=done files=1 '
expect_guest "[ ! -e $volume_trash/files/Notes.txt ] && [ ! -e $volume_trash/info/Notes.txt.trashinfo ]" 'deleted with its record'

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
guest 'umount /tmp/vol >/dev/null 2>&1; echo unmounted' >/dev/null
[ $status = 0 ] && echo "files-p003: PASS" || echo "files-p003: FAIL"
exit $status
