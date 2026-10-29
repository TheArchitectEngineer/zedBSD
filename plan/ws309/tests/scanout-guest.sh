#!/bin/sh
# ws309-p012: the game mode's way in on QEMU's Venus guest: the display thread asks the backend whether the display
# can show a window straight, and the compositor goes on.  The guest is started from IMAGE in OUT/runtime; WAYLAND is
# put in as /bin/wayland ("image": the image's own is kept, for a comparison); a compositor of its own (--testing --log-frames) shows mview spinning a model in the game mode
# (--content=game --present=mailbox, 5 s), then mview's still view (fifo, 300 frames) captured by keiland-shot after
# 4 s (OUT/shot.png).  The guest is ended by QEMU's monitor.
# Prints "scanout-guest: PASS" when the compositor was ready, the game mode's question was answered (a KWL SCANOUT
# line for the spinning window: direct=1, or direct=0 reason=backend ... output=kept), the still view ran to its end
# (MVIEW DONE run=still), the capture is the output's size and a picture (the samples' channels spread over 24 or
# more), the compositor still runs, and no KWL FAILED or VULKAN_ERROR line came.  KWL SCANOUT, KWL DISPLAY change and
# KWL OUTPUT open/closed lines are printed.
#   usage: plan/ws309/tests/scanout-guest.sh OUT IMAGE WAYLAND
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: scanout-guest.sh OUT IMAGE WAYLAND}
image=${2:?}
wl=${3:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-120}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
G_TIMEOUT=300 g wait > "$out/wait.log" 2>&1 || { echo "scanout-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[m]view" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	if [ "$wl" != image ]; then
		g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	fi
	G_TIMEOUT=200 g run 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p012dev; mkdir -p $HOME; rm -f /tmp/wayland-0; /bin/wayland --testing --log-frames --timeout=300 > /tmp/scanout.log 2>&1 </dev/null & sleep 5; export WAYLAND_DISPLAY=wayland-0; timeout 60 mview --token=spin --spin=5 --copies=1 --content=game --present=mailbox > /tmp/mview-spin.log 2>&1 </dev/null; sleep 2; (timeout 90 mview --token=still --frames=300 --content=none --present=fifo --copies=1 > /tmp/mview-still.log 2>&1 </dev/null &); sleep 4; timeout 60 keiland-shot /tmp/shot.png > /tmp/shot.out 2>&1 </dev/null; echo shot=$?; sleep 1; i=0; while ps -A -o args | grep -q "[m]view" && [ $i -lt 600 ]; do sleep 0.1; i=$((i+1)); done; echo done' > "$out/guest-start.out" 2>&1
	g get /tmp/shot.png "$out/shot.png" > /dev/null 2>&1
	g run 'cat /tmp/scanout.log' > "$out/scanout.log" 2>&1
	g run 'cat /tmp/mview-spin.log /tmp/mview-still.log' > "$out/mview.log" 2>&1
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
	print("scanout-guest: the guest or the compositor did not start")
def fail(why):
	global ok
	ok = False
	print("scanout-guest: " + why)
def lines(name):
	try:
		return open(os.path.join(out, name), errors="replace").read().splitlines()
	except OSError:
		return []
text = lines("scanout.log")
for line in text:
	if re.match(r"KWL (SCANOUT|DISPLAY change|OUTPUT (open|closed)|MODE window)", line):
		print("scanout-guest: " + line)
if not any(line.startswith("KWL READY") for line in text):
	fail("the compositor was not ready")
if any(re.match(r"KWL (FAILED|VULKAN_ERROR)", line) for line in text):
	fail("a KWL FAILED or VULKAN_ERROR line")
if not any(re.match(r"KWL SCANOUT (direct=1 |direct=0 reason=backend .*output=kept)", line) for line in text):
	fail("the game mode's question was not answered")
mview = lines("mview.log")
if not any(line.startswith("MVIEW DONE run=still") for line in mview):
	fail("the still view did not run to its end (%s)" % [l for l in mview if "FAILED" in l or "DONE" in l])
alive = lines("alive.out")
if not alive or alive[-1].strip() != "1":
	fail("the compositor does not run any more")
try:
	from PIL import Image
	im = Image.open(os.path.join(out, "shot.png")).convert("RGB")
	w, h = im.size
	samples = [im.getpixel((x * w // 16, y * h // 16)) for x in range(16) for y in range(16)]
	spread = max(max(c) - min(c) for c in zip(*samples))
	print("scanout-guest: capture %dx%d spread=%d" % (w, h, spread))
	if spread < 24:
		fail("the capture is one colour")
except Exception as error:
	fail("no capture (%s)" % error)
print("scanout-guest: " + ("PASS" if ok else "FAIL"))
PY
