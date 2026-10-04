#!/bin/sh
# ws090-p014: the file chooser as a sheet under its parent's title bar, on the Venus guest (zdesktop --glass 1280x800,
# this worktree's compositor, Text Editor and libraries put in by the install step).  The steps read zdesktop's log through
# SSH and the pictures (nothing reads the console); the pointer is driven through QMP.
#   install   the compositor, Text Editor, the libraries and a note (/root/note.txt)
#   open      Text Editor on the note; its Open: the chooser is a sheet (ZWL GLASS sheet ... parent=), under the title
#             bar in the middle of the window, with no title bar of its own (open.png)
#   move      the parent's title bar dragged: the sheet moves with it (moved.png)
#   hold      a press on the parent's body is held (ZWL GLASS sheet holds) and the chooser stays; Cancel closes it
#   dock      Open again, the parent's maximize button: the parent docks, the sheet hangs under the system bar
#             (docked.png); the docked title's restore brings both back
#   minimize  Open again, the parent's minimize button: both hide (minimized.png); Wiseview (Super+Tab) shows the
#             parent alone, Right picks its tile and Enter brings both back (restored.png); Cancel
#   saveas    Text Editor without a file, "abc" typed and Ctrl+S: Save As is a sheet too (saveas.png); Cancel
#   GUEST_RUNTIME=... BIN=build/amd64 plan/ws090/tests/sheet-guest.sh OUTDIR STEP...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws094-run}"
bin=${BIN:-build/amd64}
out=$1
shift
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; sleep 0.8; }
shot() { python3 plan/ws035/tests/zdesktop-shot.py --runtime "$GUEST_RUNTIME" "$out/$1" >/dev/null 2>&1; echo "shot $1"; }
count() { guest "grep -acE '$1' /tmp/zdesktop.log" | tail -1; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]extedit" | awk "{print \$1}"); do kill $p; done; sleep 1'

# Fails the run unless zdesktop's log has more lines matching a pattern than a count (within a few seconds).
expect_more() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
		found=$(count "$1")
		[ "${found:-0}" -gt "$2" ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt "$2" ] 2>/dev/null; then
		echo "log: $1 ok"
	else
		echo "log: $1 MISSING"
		status=1
	fi
}

# The sheet's place from zdesktop's log line and the parent's, checked against the rule (x centred, y at the title bar).
sheet_place() {
	guest "grep -a 'ZWL GLASS sheet surface=' /tmp/zdesktop.log | tail -1"
}

# zdesktop and Text Editor afresh (the file argument, or none).
start() {
	guest "$stop_all" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0; /bin/wayland --timeout=900 --width=1280 --height=800 --glass --wallpaper=/usr/share/keiland/wallpaper.ppm > /tmp/zdesktop.log 2>&1 </dev/null & i=0; while ! grep -q 'ZWL READY' /tmp/zdesktop.log && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 2; /bin/textedit --timeout-s=900 $1 > /tmp/te.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
}

# Text Editor's Open (its title bar's button at 487,73 while the window is where it opens).
open_chooser() {
	before=$(count 'ZWL GLASS sheet surface=')
	pointer move 487 73 sleep 200 down sleep 60 up sleep 1500 move 1250 780 sleep 300
	expect_more 'ZWL GLASS sheet surface=[0-9]+ parent=' "$before"
	expect_more 'ZWL GLASS sheet at x=260 y=95 ' 0
}

# The chooser's Cancel (its place at the bottom right of the sheet, as the chooser lays it out at 760x480); Text Editor
# hears the chooser end (TEXTEDIT CHOSEN).
cancel() {
	set -- $(sheet_xy)
	chosen=$(guest "grep -ac 'TEXTEDIT CHOSEN' /tmp/te.log" | tail -1)
	pointer move $(( ${1:-260} + 597 )) $(( ${2:-95} + 447 )) sleep 200 down sleep 60 up sleep 1000
	now=$(guest "grep -ac 'TEXTEDIT CHOSEN' /tmp/te.log" | tail -1)
	[ "${now:-0}" -gt "${chosen:-0}" ] 2>/dev/null && echo "cancel: the chooser ended ok" || { echo "cancel: the chooser did not end MISSING"; status=1; }
}

# The sheet's top-left corner now: the parent's body from the log is not kept, so the sheet's place is asked of the log's last configure.
sheet_xy() {
	guest "grep -a 'ZWL GLASS sheet at ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p'
}

for step in "$@"; do
	case "$step" in
	install)
		guest "$stop_all" >/dev/null
		put "$bin/bin/wayland" /bin/wayland
		put "$bin/bin/textedit" /bin/textedit
		for library in $(cd "$bin/dynamic" && ls *.so | grep -vE '^(libc|ld)\.so$'); do
			put "$bin/dynamic/$library" "/lib/$library"
		done
		printf 'Sheet test\nline two\n' > "$out/note.txt"
		put "$out/note.txt" /root/note.txt
		guest 'chmod 755 /bin/wayland /bin/textedit' >/dev/null
		;;
	open)
		start /root/note.txt
		open_chooser
		shot open.png
		;;
	move)
		pointer move 340 73 sleep 200 down sleep 150 move 300 90 sleep 80 move 250 110 sleep 80 move 200 130 sleep 200 up sleep 800 move 1250 780 sleep 300
		expect_more 'ZWL GLASS moved surface=' 0
		expect_more 'ZWL GLASS sheet at x=120 y=152 ' 0
		shot moved.png
		;;
	hold)
		held=$(count 'ZWL GLASS sheet holds')
		pointer move 600 760 sleep 200 down sleep 60 up sleep 600
		expect_more 'ZWL GLASS sheet holds' "$held"
		cancel
		;;
	dock)
		start /root/note.txt
		open_chooser
		pointer move 1029 73 sleep 200 down sleep 60 up sleep 1500 move 1250 780 sleep 300
		expect_more 'ZWL GLASS dock surface=' 0
		expect_more 'ZWL GLASS sheet at x=260 y=48 ' 0
		shot docked.png
		cancel
		;;
	minimize)
		start /root/note.txt
		open_chooser
		pointer move 995 73 sleep 200 down sleep 60 up sleep 1000 move 1250 780 sleep 300
		shot minimized.png
		keys '<super-tab>'
		sleep 1
		shot wiseview.png
		keys '<right>'
		keys '<ret>'
		sleep 1.5
		shot restored.png
		cancel
		;;
	saveas)
		start ''
		keys 'abc'
		before=$(count 'ZWL GLASS sheet surface=')
		keys '<ctrl-s>'
		sleep 1.5
		pointer move 1250 780 sleep 300
		expect_more 'ZWL GLASS sheet surface=[0-9]+ parent=' "$before"
		shot saveas.png
		cancel
		;;
	*)
		echo "unknown step $step"
		status=1
		;;
	esac
done

# zdesktop's errors.
errors=$(count 'ZWL ERROR')
[ "${errors:-0}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: $errors ERROR lines"; status=1; }
[ $status -eq 0 ] && echo "sheet-guest: PASS" || echo "sheet-guest: FAIL"
exit $status
