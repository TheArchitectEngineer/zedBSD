#!/bin/sh
# ws035-p116: the demonstration's walk-through on the Venus guest at the demo LCD's 1920x1280, a picture per step
# (OUTDIR/NN-name.png; with a PREFIX each is also copied to PREFIX-NN-name.png):
#   00 the loader's splash (the VGA console, QMP screendump), 01 the greeter, 02 the desktop after the login,
#   03 App Home (the top-left swipe), 04 Files (from App Home), 05 Terminal (from App Home, a command typed),
#   05b the X terminal (from App Home, ws035-p120), 06 Notes (the top-right swipe, a stroke drawn), 07 PDF Viewer (from App Home, a PDF opened with Ctrl+O),
#   08 Wiseview (the bottom edge's swipe), 09 the lock screen (App Home's Lock Screen), 10 unlocked,
#   11 the greeter again (App Home's Log Out).
# The pointer is driven through QMP like a finger or a pen: a press, moves and a release.
# ws035-p120: the demonstration logs in as the person kei ("Kei", no password; plan/ws035/demo/demo-accounts.sh):
# the session's log is /run/user/1000/session.log, the PDF goes to /home/kei/Documents, and the walk checks that
# Notes saved its notebook in /home/kei/Documents/Notes as kei, that the session has the group network, and that
# PDF Viewer opened over the fullscreen Notes leaves the system bar away (ZWL GLASS bar hidden).
#
#   plan/ws035/tests/build-demo-venus-image.sh build/amd64
#   VENUS_SIZE=1920x1280 plan/tools/files/files-guest.sh start build/amd64/hdd-image.img
#   plan/ws035/tests/demo-walk.sh [OUTDIR] [PREFIX]          (right after the start: step 00 is the boot)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-demo}
prefix=${2:-}
mkdir -p "$out"
width=1920
height=1280
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width $width --height $height "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.6; }
status=0
user=kei
uid=1000
home=/home/kei
session=/run/user/$uid/session.log

# Takes the Venus output as OUTDIR/NAME.png (and PREFIX-NAME.png).
shot() {
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1.png" --runtime "$GUEST_RUNTIME" >/dev/null || status=1
	[ -n "$prefix" ] && cp "$out/$1.png" "$prefix-$1.png"
	echo "shot $1"
}

# Waits until a guest log has a line matching a pattern (up to N seconds).
wait_log() {
	tries=0
	while [ $tries -lt "${3:-20}" ]; do
		found=$(guest "grep -cE '$2' $1 2>/dev/null" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && { echo "log: $2 ok"; return 0; }
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $2 MISSING"
	status=1
	return 1
}

# A stroke: a press at (x0, y0), n moves ms apart to (x1, y1), the release, then a wait.
stroke() {
	x0=$1; y0=$2; x1=$3; y1=$4; n=$5; ms=$6
	steps="move $x0 $y0 sleep 200 down sleep 30"
	i=1
	while [ $i -le "$n" ]; do
		steps="$steps move $((x0 + (x1 - x0) * i / n)) $((y0 + (y1 - y0) * i / n)) sleep $ms"
		i=$((i + 1))
	done
	pointer $steps up sleep "${7:-1500}"
}

# A tap (a click) and a wait.
tap() {
	pointer move "$1" "$2" sleep 250 down sleep 60 up sleep "${3:-1000}"
}

# The pointer out of the way, in the bottom-right corner.
park() {
	pointer move $((width - 12)) $((height - 12)) sleep 400
}

# App Home's icons: six a row, 144 px apart, centred; the first row at y 466.
icon_x() { echo $((width / 2 - 360 + $1 * 144)); }

# 00. The splash on the VGA console while the kernel starts.
python3 - "$GUEST_RUNTIME/qmp.sock" "$out/00-splash.png" <<'EOF' && [ -n "$prefix" ] && cp "$out/00-splash.png" "$prefix-00-splash.png"
import json, os, socket, sys
sys.path.insert(0, "plan/ws035/tests")
from ppm2png import read_ppm, write_png
connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
connection.connect(sys.argv[1])
stream = connection.makefile("rw")
stream.readline()
def call(command, **arguments):
	request = {"execute": command}
	if arguments:
		request["arguments"] = arguments
	stream.write(json.dumps(request) + "\n")
	stream.flush()
	while True:
		reply = json.loads(stream.readline())
		if "return" in reply or "error" in reply:
			return reply
call("qmp_capabilities")
ppm = sys.argv[2] + ".ppm"
if "error" in call("screendump", filename=ppm, format="ppm"):
	sys.exit(1)
w, h, pixels = read_ppm(ppm)
write_png(sys.argv[2], w, h, pixels)
os.unlink(ppm)
print("shot 00-splash")
EOF

# 01. The greeter.
wait_log /var/log/greeter.log 'ZWL GREETER open' 90
sleep 3
shot 01-greeter

# 02. The login (kei, no password: Enter): the desktop.
wait_log /var/log/greeter.log "ZWL GREETER open users=1 selected=$user" 5
keys '\n'
wait_log $session 'ZWL HANDOFF go=1' 30
wait_log /var/log/sessiond.log "SESSIOND SESSION start user=$user uid=$uid" 5
sleep 4
shot 02-desktop
guest "ls -ld $home $home/Documents; grep \"SESSIOND SESSION home=\" /var/log/sessiond.log" | sed 's/^/home: /'

# A PDF for step 07 in kei's Documents (the session made the folder): DEMO_PDF, or the quilt manual of the host.
pdf=${DEMO_PDF:-/usr/share/doc/quilt/quilt.pdf}
if [ -f "$pdf" ]; then
	timeout 60 python3 plan/tools/guest/guest.py put "$pdf" "$home/Documents/$(basename "$pdf")" </dev/null
	guest "chown $uid:$uid '$home/Documents/$(basename "$pdf")'" >/dev/null
fi

# 03. App Home: the swipe from the top-left corner.
stroke 4 4 240 240 10 30 1500
wait_log $session 'ZWL HOME opened' 10
shot 03-apphome

# 04. Files from App Home.
tap "$(icon_x 0)" 466 5000
park
shot 04-files

# 05. Terminal from App Home, a command typed.
stroke 4 4 240 240 10 30 1500
tap "$(icon_x 2)" 466 5000
keys 'id; uname -a; ls' '\n'
park
wait_log $session 'ZTERM START' 5
shot 05-terminal

# 05b. (ws035-p120) The X terminal from App Home (the X server starts with it), a command typed.
stroke 4 4 240 240 10 30 1500
tap "$(icon_x 0)" 618 12000
keys 'id' '\n'
park
guest "ps -A -o pid,args | grep -E '[z]term|[x]server'" | sed 's/^/x11: /'
shot 05b-xterminal

# 06. Notes: the swipe from the top-right corner (fullscreen), and a stroke on the page.
stroke $((width - 6)) 6 $((width - 166)) 166 8 40 8000
wait_log $session 'ZWL CORNER notes (launch|surface)' 10
pointer move 700 300 sleep 200 down sleep 30 move 740 380 sleep 20 move 760 420 sleep 20 move 800 340 sleep 20 \
    move 820 300 sleep 20 move 860 380 sleep 20 move 880 420 sleep 20 move 920 340 sleep 20 move 940 300 sleep 30 up sleep 800
park
shot 06-notes

# Notes saves the notebook NOTES_AUTOSAVE_IDLE_MS (5 s) after the stroke, as kei, in kei's Documents/Notes.
sleep 6
saved=$(guest "ls -l $home/Documents/Notes/*.pdf 2>/dev/null | head -1")
echo "notes: $saved"
echo "$saved" | grep -Eq "^-[rw-]+ +[0-9]+ +$user +$user .*note-[0-9-]+\.pdf" || { echo "notes: no notebook of kei's"; status=1; }

# 07. PDF Viewer from App Home; Ctrl+O, Documents, the first PDF.
guest "ls $home/Documents/*.pdf 2>/dev/null | head -1" | grep -q pdf || echo "note: no PDF in $home/Documents (put one there first)"
stroke 4 4 240 240 10 30 1500
tap "$(icon_x 3)" 466 5000
keys '<ctrl-o>'
sleep 1.5
keys '<down>' '\n'
sleep 1.5
keys '<down>' '\n'
sleep 5
park
wait_log $session 'ZWL GLASS bar hidden fullscreen=' 5
shot 07-pdfviewer

# 08. Wiseview: the swipe up from the bottom edge.
stroke $((width / 2)) $((height - 4)) $((width / 2)) $((height - 404)) 10 30 2000
park
shot 08-wiseview
keys '<esc>'
sleep 1.5

# 09. The lock screen from App Home's Lock Screen.
stroke 4 4 240 240 10 30 1500
tap "$(icon_x 1)" 618 2500
wait_log $session 'ZWL LOCK locked reason=home' 10
park
shot 09-lock

# 10. Unlocked (Enter: kei has no password).
keys '\n'
wait_log $session 'ZWL LOCK unlocked' 10
sleep 2
shot 10-unlocked

# 11. Log Out from App Home: the greeter again.
stroke 4 4 240 240 10 30 1500
tap "$(icon_x 2)" 618 1000
wait_log /var/log/sessiond.log "SESSIOND SESSION end user=$user" 30
sleep 3
shot 11-logout

[ $status = 0 ] && echo "demo-walk: done" || echo "demo-walk: some steps missed"
exit $status
