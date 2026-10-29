#!/bin/sh
# ws309: on QEMU's Venus guest with two heads (QEMU's D-Bus display, plan/tools/guest/venus-head.sh), the output
# moves to a display of another size and the glass look's wallpaper is made again at that size off the event loop:
# the picture is read and decoded by a thread of its own and drawn and blurred by the blur thread, while the frames
# go on with the old images (stretched).  The guest is started from IMAGE in OUT/runtime with VENUS_DISPLAY=dbus
# VENUS_OUTPUTS=2; head 0 is given 1280x800 and head 1 1024x768 (QEMU ignores a size equal to the last one a head was
# given, so head 0 needs its size before it can be unplugged); WAYLAND is put in as /bin/wayland and a compositor of
# its own (--testing --glass --log-frames --wallpaper=the picture) shows a wl_shm window drawing all along (wlshm).
# Head 0 unplugged: the output moves to display 1 at 1024x768; its capture (keiland-shot) is OUT/small.png.  Head 0
# plugged again and head 1 unplugged: the output moves back to display 0 at 1280x800; OUT/large.png.
# Prints "resize-guest: PASS" when, for each move:
#  - the compositor was ready and listed two displays (KWL OUTPUT displays count=2, first move only),
#  - the output moved and was fitted (KWL OUTPUT switch name=Venus virtual display N width=W height=H, and before it,
#    from the opening, KWL OUTPUT resized width=W height=H),
#  - the wallpaper was made again at that size by the threads (KWL GLASS resize width=W height=H ms=N images=...:
#    the form with images= is the blur thread's; the event loop's own form has no images=),
#  - at least one frame was composed between the fitting and that line (KWL COMPOSE: the old images were drawn
#    meanwhile, the event loop did not wait for the picture),
#  - the capture has the output's size and is a picture (not one colour: the samples' channels spread over 24 or more),
# and no KWL FAILED or VULKAN_ERROR line came and the compositor still runs.  The resize lines' ms= (from the request
# to the picture shown), the frames composed meanwhile and the worst pass_max_us of KWL PERF pass are printed, not
# judged.  The guest is ended by QEMU's monitor.
#   usage: plan/ws309/tests/resize-guest.sh OUT IMAGE WAYLAND
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: resize-guest.sh OUT IMAGE WAYLAND}
image=${2:?}
wl=${3:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-120}" python3 -I plan/tools/guest/guest.py "$@"; }
head_set() { timeout 30 sh plan/tools/guest/venus-head.sh "$1" "$2" >> "$out/heads.log" 2>&1; }
# Waits (up to SECONDS) for a line of the compositor's log matching a pattern; the log is kept as OUT/NAME.
wait_line() {
	waited=0
	while [ "$waited" -lt "$2" ]; do
		g run 'cat /tmp/resize.log' > "$out/$3" 2>/dev/null
		if grep -qE "$1" "$out/$3"; then
			return 0
		fi
		sleep 1
		waited=$((waited + 1))
	done
	echo "resize-guest: no line /$1/ within $2 s"
	return 1
}
failed=0
VENUS_DISPLAY=dbus VENUS_OUTPUTS=2 timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
G_TIMEOUT=300 g wait > "$out/wait.log" 2>&1 || { echo "resize-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]lshm" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]lshm" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	sh plan/tools/guest/venus-head.sh list > "$out/consoles.txt" 2>&1
	g run "$stop_all" > /dev/null 2>&1
	head_set 0 1280x800
	head_set 1 1024x768
	sleep 3
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	G_TIMEOUT=60 g run 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p010dev; mkdir -p $HOME; rm -f /tmp/wayland-0 /tmp/wlshm.log; nohup /bin/wayland --testing --glass --log-frames --timeout=300 --wallpaper=/usr/share/keiland/wallpaper.png > /tmp/resize.log 2>&1 </dev/null & sleep 5; nohup wlshm --size=300x200 --band=ff00ff00 --frames=6000 --token=moving > /tmp/wlshm.log 2>&1 </dev/null & echo started' > "$out/guest-start.out" 2>&1
	wait_line 'KWL OUTPUT displays count=2' 60 ready.log || failed=1
	sleep 3
	# The move to the smaller display.
	head_set 0 off
	wait_line 'KWL GLASS (resize width=1024 height=768|resize loader|resize errno|no wallpaper)' 30 small.log
	sleep 2
	G_TIMEOUT=90 g run 'timeout 60 keiland-shot /tmp/small.png > /tmp/shot-small.out 2>&1 </dev/null; echo shot=$?' > "$out/shot-small.out" 2>&1
	g get /tmp/small.png "$out/small.png" > /dev/null 2>&1
	# Head 0 back, then the move back to it.
	head_set 0 1280x800
	sleep 3
	head_set 1 off
	wait_line 'KWL GLASS (resize width=1280 height=800|resize loader|resize errno|no wallpaper)' 30 large.log
	sleep 2
	G_TIMEOUT=90 g run 'timeout 60 keiland-shot /tmp/large.png > /tmp/shot-large.out 2>&1 </dev/null; echo shot=$?' > "$out/shot-large.out" 2>&1
	g get /tmp/large.png "$out/large.png" > /dev/null 2>&1
	g run 'cat /tmp/resize.log' > "$out/resize.log" 2>&1
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
	print("resize-guest: the guest or the compositor did not start")
def fail(why):
	global ok
	ok = False
	print("resize-guest: " + why)
try:
	text = open(os.path.join(out, "resize.log"), errors="replace").read().splitlines()
except OSError:
	text = []
def find(pattern, start=0):
	for index in range(start, len(text)):
		if re.search(pattern, text[index]):
			return index
	return -1
if find(r"^KWL READY") < 0:
	fail("the compositor was not ready")
if find(r"KWL OUTPUT displays count=2") < 0:
	fail("the compositor did not list two displays")
if find(r"^KWL (FAILED|VULKAN_ERROR)") >= 0:
	fail("a KWL FAILED or VULKAN_ERROR line")
start = 0
for name, display, width, height in (("small", 1, 1024, 768), ("large", 0, 1280, 800)):
	# The fitting is logged inside the opening, before the move's own line.
	moved = find(r"KWL OUTPUT switch name=Venus virtual display %d width=%d height=%d" % (display, width, height), start)
	if moved < 0:
		fail("%s: the output did not move to display %d at %dx%d" % (name, display, width, height))
		continue
	fitted = -1
	for index in range(start, moved):
		if re.search(r"KWL OUTPUT resized width=%d height=%d" % (width, height), text[index]):
			fitted = index
	if fitted < 0:
		fail("%s: the desktop was not fitted to %dx%d" % (name, width, height))
		continue
	made = find(r"KWL GLASS resize width=%d height=%d ms=\d+" % (width, height), fitted)
	if made < 0:
		fail("%s: the wallpaper was not made again at %dx%d" % (name, width, height))
		continue
	line = text[made]
	if "images=" not in line:
		fail("%s: the wallpaper was made in the event loop (%s)" % (name, line))
	frames = sum(1 for index in range(fitted, made) if text[index].startswith("KWL COMPOSE frame="))
	print("resize-guest: %s: %s frames-meanwhile=%d" % (name, line, frames))
	if frames < 1:
		fail("%s: no frame was composed while the wallpaper was made" % name)
	start = made
	try:
		from PIL import Image
		picture = Image.open(os.path.join(out, name + ".png")).convert("RGB")
		if picture.size != (width, height):
			fail("%s: the capture is %dx%d, not %dx%d" % (name, picture.size[0], picture.size[1], width, height))
		low = [255, 255, 255]
		high = [0, 0, 0]
		for y in range(8, picture.size[1] - 8, 16):
			for x in range(8, picture.size[0] - 8, 16):
				pixel = picture.getpixel((x, y))
				for channel in range(3):
					low[channel] = min(low[channel], pixel[channel])
					high[channel] = max(high[channel], pixel[channel])
		spread = max(high[channel] - low[channel] for channel in range(3))
		print("resize-guest: %s: capture %dx%d spread=%d" % (name, picture.size[0], picture.size[1], spread))
		if spread < 24:
			fail("%s: the capture is one colour" % name)
	except Exception as error:
		fail("%s: the capture could not be read: %s" % (name, error))
passes = [line for line in text if line.startswith("KWL PERF pass")]
if passes:
	worst = max(int(re.search(r"pass_max_us=(\d+)", line).group(1)) for line in passes[1:] or passes)
	print("resize-guest: pass_max_us(max)=%d over %d KWL PERF pass lines" % (worst, len(passes)))
try:
	alive = open(os.path.join(out, "alive.out"), errors="replace").read().split()
	if not alive or int(alive[-1]) < 1:
		fail("the compositor is gone")
except (OSError, ValueError):
	fail("whether the compositor runs could not be read")
print("resize-guest: " + ("PASS" if ok else "FAIL"))
PY
