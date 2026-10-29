#!/bin/sh
# ws309-p012: on QEMU's Venus guest, the output turned by the compositor to another angle of another logical size
# (the display's 1280x800 mode, 90 degrees: 800x1280) has the glass look's wallpaper made again at that size off the
# event loop (read and decoded by a thread of its own, drawn and blurred by the blur thread), while the frames go on
# with the old images (stretched); the turn parks the render thread (its park drains) and no longer waits for the
# device.  The guest is started from IMAGE in OUT/runtime; WAYLAND is put in as /bin/wayland and a compositor of its
# own (--testing --glass --log-frames --rotate-policy=any --wallpaper=the picture) shows a wl_shm window drawing all
# along (wlshm).  keiland-shot's request ROTATE 90 turns it (its capture OUT/turned.png), ROTATE 0 turns it back
# (OUT/back.png).
# Prints "rotate-guest: PASS" when, for each turn:
#  - the compositor turned (KWL OUTPUT rotate key=... rotation=R ... width=W height=H ... by=user),
#  - the wallpaper was made again at that size by the threads (KWL GLASS resize width=W height=H ms=N images=...,
#    after the turn's line),
#  - at least one frame was composed between the turn and that line (KWL COMPOSE frame=),
#  - the capture is a picture (the samples' channels spread over 24 or more),
# and no KWL FAILED or VULKAN_ERROR line came and the compositor still runs.  The turns' times line (KWL OUTPUT rotate
# times), the resize lines' ms= and the worst pass_max_us of KWL PERF pass are printed, not judged.  The guest is
# ended by QEMU's monitor.
#   usage: plan/ws309/tests/rotate-guest.sh OUT IMAGE WAYLAND
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rotate-guest.sh OUT IMAGE WAYLAND}
image=${2:?}
wl=${3:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-120}" python3 -I plan/tools/guest/guest.py "$@"; }
# Waits (up to SECONDS) for a line of the compositor's log matching a pattern; the log is kept as OUT/NAME.
wait_line() {
	waited=0
	while [ "$waited" -lt "$2" ]; do
		g run 'cat /tmp/rotate.log' > "$out/$3" 2>/dev/null
		if grep -qE "$1" "$out/$3"; then
			return 0
		fi
		sleep 1
		waited=$((waited + 1))
	done
	echo "rotate-guest: no line /$1/ within $2 s"
	return 1
}
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
G_TIMEOUT=300 g wait > "$out/wait.log" 2>&1 || { echo "rotate-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]lshm" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]lshm" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	G_TIMEOUT=60 g run 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p010dev; mkdir -p $HOME; rm -f /tmp/wayland-0 /tmp/wlshm.log; nohup /bin/wayland --testing --glass --log-frames --rotate-policy=any --timeout=300 --width=1280 --height=800 --wallpaper=/usr/share/keiland/wallpaper.png > /tmp/rotate.log 2>&1 </dev/null & sleep 5; nohup wlshm --size=300x200 --band=ff00ff00 --frames=6000 --token=moving > /tmp/wlshm.log 2>&1 </dev/null & echo started' > "$out/guest-start.out" 2>&1
	wait_line '^KWL READY' 60 ready.log || failed=1
	sleep 5
	# The turn to 90 degrees, then back to 0.
	G_TIMEOUT=60 g run 'XDG_RUNTIME_DIR=/tmp timeout 30 keiland-shot --request "ROTATE 90"' > "$out/rotate90.out" 2>&1
	wait_line 'KWL GLASS (resize width=800 height=1280|resize loader|resize errno|no wallpaper)' 30 turned.log
	sleep 2
	G_TIMEOUT=90 g run 'XDG_RUNTIME_DIR=/tmp timeout 60 keiland-shot /tmp/turned.png > /tmp/shot-turned.out 2>&1 </dev/null; echo shot=$?' > "$out/shot-turned.out" 2>&1
	g get /tmp/turned.png "$out/turned.png" > /dev/null 2>&1
	G_TIMEOUT=60 g run 'XDG_RUNTIME_DIR=/tmp timeout 30 keiland-shot --request "ROTATE 0"' > "$out/rotate0.out" 2>&1
	sleep 1
	wait_line 'KWL OUTPUT rotate key=.* rotation=0 .*by=user' 30 back-turn.log
	wait_line 'KWL GLASS resize width=1280 height=800 .*images=|KWL GLASS resize (loader|errno)' 30 back.log
	sleep 2
	G_TIMEOUT=90 g run 'XDG_RUNTIME_DIR=/tmp timeout 60 keiland-shot /tmp/back.png > /tmp/shot-back.out 2>&1 </dev/null; echo shot=$?' > "$out/shot-back.out" 2>&1
	g get /tmp/back.png "$out/back.png" > /dev/null 2>&1
	g run 'cat /tmp/rotate.log' > "$out/rotate.log" 2>&1
	g run 'ps -A -o args | grep -cE "^/bin/wayland( |$)"' > "$out/alive.out" 2>&1
	g run "$stop_all" > /dev/null 2>&1
fi
python3 -I - "$GUEST_RUNTIME/qmp.sock" <<'PY' > "$out/quit.log" 2>&1
import socket, sys
s = socket.socket(socket.AF_UNIX)
s.settimeout(10)
s.connect(sys.argv[1])
s.recv(4096)
s.sendall(b'{"execute":"qmp_capabilities"}\n')
s.recv(4096)
s.sendall(b'{"execute":"quit"}\n')
print(s.recv(4096))
PY
if [ -s "$GUEST_RUNTIME/dbus.pid" ]; then
	kill "$(cat "$GUEST_RUNTIME/dbus.pid")" 2>/dev/null
fi
python3 -I - "$out" "$failed" <<'PY'
import os, re, sys
out = sys.argv[1]
ok = sys.argv[2] == "0"
if not ok:
	print("rotate-guest: the guest or the compositor did not start")
def fail(why):
	global ok
	ok = False
	print("rotate-guest: " + why)
try:
	text = open(os.path.join(out, "rotate.log"), errors="replace").read().splitlines()
except OSError:
	text = []
def find(pattern, start=0):
	for index in range(start, len(text)):
		if re.search(pattern, text[index]):
			return index
	return -1
if find(r"^KWL READY") < 0:
	fail("the compositor was not ready")
if find(r"^KWL (FAILED|VULKAN_ERROR)") >= 0:
	fail("a KWL FAILED or VULKAN_ERROR line")
for line in text:
	if line.startswith("KWL OUTPUT rotate times"):
		print("rotate-guest: " + line)
start = 0
for name, rotation, width, height in (("turned", 90, 800, 1280), ("back", 0, 1280, 800)):
	turned = find(r"KWL OUTPUT rotate key=.* rotation=%d .*width=%d height=%d .*by=user" % (rotation, width, height), start)
	if turned < 0:
		fail("%s: the output did not turn to %d (%dx%d)" % (name, rotation, width, height))
		continue
	made = find(r"KWL GLASS resize width=%d height=%d ms=\d+" % (width, height), turned)
	if made < 0:
		fail("%s: the wallpaper was not made again at %dx%d" % (name, width, height))
		continue
	line = text[made]
	if "images=" not in line:
		fail("%s: the wallpaper was made in the event loop (%s)" % (name, line))
	frames = sum(1 for index in range(turned, made) if text[index].startswith("KWL COMPOSE frame="))
	print("rotate-guest: %s: %s frames-meanwhile=%d" % (name, line, frames))
	if frames < 1:
		fail("%s: no frame was composed while the wallpaper was made" % name)
	start = made
	try:
		from PIL import Image
		picture = Image.open(os.path.join(out, name + ".png")).convert("RGB")
		low = [255, 255, 255]
		high = [0, 0, 0]
		for y in range(8, picture.size[1] - 8, 16):
			for x in range(8, picture.size[0] - 8, 16):
				pixel = picture.getpixel((x, y))
				for channel in range(3):
					low[channel] = min(low[channel], pixel[channel])
					high[channel] = max(high[channel], pixel[channel])
		spread = max(high[channel] - low[channel] for channel in range(3))
		print("rotate-guest: %s: capture %dx%d spread=%d" % (name, picture.size[0], picture.size[1], spread))
		if spread < 24:
			fail("%s: the capture is one colour" % name)
	except Exception as error:
		fail("%s: the capture could not be read: %s" % (name, error))
passes = [line for line in text if line.startswith("KWL PERF pass")]
if passes:
	worst = max(int(re.search(r"pass_max_us=(\d+)", line).group(1)) for line in passes[1:] or passes)
	print("rotate-guest: pass_max_us(max)=%d over %d KWL PERF pass lines" % (worst, len(passes)))
try:
	alive = open(os.path.join(out, "alive.out"), errors="replace").read().split()
	if not alive or int(alive[-1]) < 1:
		fail("the compositor is gone")
except (OSError, ValueError):
	fail("whether the compositor runs could not be read")
print("rotate-guest: " + ("PASS" if ok else "FAIL"))
PY
