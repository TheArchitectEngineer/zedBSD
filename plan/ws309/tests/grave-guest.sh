#!/bin/sh
# ws309: on QEMU's Venus guest, the images let go while drawings use them (the graves): wl_shm windows (wlshm) made
# and closed one after another (each client's end gives its frames' holds back without waiting, and its surface's
# image waits for the frames given before), with a cursor of their own changing shape under the test pointer
# (aat-input), while the wallpaper is chosen during the session (keiland-settings: the picture, then the landscape),
# which draws into new images and lets the old ones go after the frames in flight.  The guest is started from IMAGE
# in OUT/runtime; WAYLAND is put in as /bin/wayland; a compositor of its own (--testing --glass --log-frames) shows
# the windows, the screen is captured with a last window held (OUT/grave.png) and the compositor's and the windows'
# lines are collected.  The guest is ended by QEMU's monitor.
# Prints "grave-guest: PASS" when the compositor was ready, both wallpapers were shown (KWL GLASS wallpaper path=...
# and path=-), every client's cleanup was told (KWL CLEANUP done), every wlshm drew its frames (WLSHM DONE), the held
# window's colour (0x2040c0) covers most of the middle of the capture, and no KWL FAILED or VULKAN_ERROR line came.  The worst pass_max_us and vk_outside_max_us of KWL PERF
# pass after the first window (the start is excluded), the worst cleanup's quiesce=, the number of images destroyed
# from their graves (KWL GRAVE: it depends on whether a frame was in flight when an image was let go, so it may be 0)
# and the wallpaper lines (their ms= from the request to the picture shown, and images=, the same images or new ones)
# are printed, not judged.
#   usage: plan/ws309/tests/grave-guest.sh OUT IMAGE WAYLAND
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: grave-guest.sh OUT IMAGE WAYLAND}
image=${2:?}
wl=${3:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-300}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
g wait > "$out/wait.log" 2>&1 || { echo "grave-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]lshm|[k]eiland-settings" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]lshm" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	# Windows of three sizes; the wallpaper is chosen while the second window draws and set back to the landscape
	# while the fourth does.
	G_TIMEOUT=240 g run 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p009dev; mkdir -p $HOME; rm -f /tmp/wayland-0 /tmp/settings.log; /bin/wayland --testing --glass --log-frames --timeout=300 --width=1280 --height=800 > /tmp/grave.log 2>&1 </dev/null & sleep 5; aat-input start --width 1280 --height 800 > /tmp/aat.log 2>&1; turn=0; for size in 400x300 640x480 300x200 400x300 800x500; do turn=$((turn+1)); wlshm --size=$size --band=ff00ff00 --cursor=ffff0000 --cursor-every=2 --frames=90 --token=s$size >> /tmp/wlshm.log 2>&1 </dev/null & sleep 1; [ $turn = 2 ] && keiland-settings set wallpaper /usr/share/keiland/wallpaper.png >> /tmp/settings.log 2>&1; [ $turn = 4 ] && keiland-settings reset wallpaper >> /tmp/settings.log 2>&1; for step in 1 2 3 4 5 6; do aat-input move-to $((600 + step * 7)) $((380 + step * 5)) >/dev/null 2>&1; sleep 0.3; done; wait $!; done; wlshm --size=400x300 --color=ff2040c0 --hold --token=hold >> /tmp/wlshm.log 2>&1 </dev/null & sleep 6; echo started' > "$out/guest-start.out" 2>&1
	python3 -I plan/ws035/tests/zdesktop-check.py "$out/grave.png" --runtime "$GUEST_RUNTIME" > "$out/check.log" 2>&1
	G_TIMEOUT=60 g run 'echo "== compositor"; grep -E "KWL (READY|THREADS|FAILED|VULKAN_ERROR|PERF pass)" /tmp/grave.log; echo "== graves"; grep "KWL GRAVE" /tmp/grave.log | awk -F= "{s+=\$2} END {print s+0}"; echo "== wallpaper"; grep -E "KWL GLASS (wallpaper path=|no wallpaper)" /tmp/grave.log; echo "== cleanup"; grep "KWL CLEANUP done" /tmp/grave.log; echo "== windows"; grep -E "WLSHM (DONE|FAILED|ENTER)" /tmp/wlshm.log; echo "== settings"; cat /tmp/settings.log' > "$out/guest.out" 2>&1
	g run "aat-input stop >/dev/null 2>&1; $stop_all" > /dev/null 2>&1
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
python3 -I - "$out/guest.out" "$out/grave.png" <<'PY'
import re, sys
text = open(sys.argv[1], errors="replace").read().splitlines()
ok = True
def fail(why):
	global ok
	ok = False
	print("grave-guest: " + why)
def section(name):
	try:
		start = text.index("== " + name) + 1
	except ValueError:
		return []
	end = start
	while end < len(text) and not text[end].startswith("== "):
		end += 1
	return text[start:end]
compositor = section("compositor")
graves = section("graves")
wallpaper = section("wallpaper")
cleanup = section("cleanup")
windows = section("windows")
if not any(line.startswith("KWL READY") for line in compositor):
	fail("the compositor was not ready")
if any(re.match(r"KWL (FAILED|VULKAN_ERROR)", line) for line in compositor):
	fail("a KWL FAILED or VULKAN_ERROR line")
if not any("KWL GLASS wallpaper path=/usr/share/keiland/wallpaper.png" in line for line in wallpaper):
	fail("the chosen wallpaper was not shown")
if not any("KWL GLASS wallpaper path=- " in line for line in wallpaper):
	fail("the landscape was not shown again")
if any("no wallpaper" in line for line in wallpaper):
	fail("a wallpaper could not be shown")
if len(cleanup) < 5:
	fail("%d of 5 closed windows' cleanups told" % len(cleanup))
done = [line for line in windows if line.startswith("WLSHM DONE")]
if len(done) != 6:
	fail("%d of 6 windows drew their frames" % len(done))
if any(line.startswith("WLSHM FAILED") for line in windows):
	fail("a window failed")
passes = [line for line in compositor if line.startswith("KWL PERF pass")]
if len(passes) < 2:
	fail("fewer than two KWL PERF pass lines")
else:
	worst = max(float(re.search(r"pass_max_us=(\d+)", line).group(1)) for line in passes[1:])
	outside = max(float(re.search(r"vk_outside_max_us=(\d+)", line).group(1)) for line in passes[1:])
	quiesce = max([int(re.search(r"quiesce=(\d+)", line).group(1)) for line in cleanup] or [-1])
	print("grave-guest: graves=%s pass_max_us(max)=%.0f vk_outside_max_us(max)=%.0f cleanup_quiesce_ms(max)=%d" % (graves[0] if graves else "?", worst, outside, quiesce))
for line in wallpaper:
	print("grave-guest: " + line)
try:
	from PIL import Image
	picture = Image.open(sys.argv[2]).convert("RGB")
	width, height = picture.size
	near = 0
	total = 0
	for y in range(height // 2 - 100, height // 2 + 100, 4):
		for x in range(width // 2 - 150, width // 2 + 150, 4):
			r, g, b = picture.getpixel((x, y))
			total += 1
			if abs(r - 0x20) <= 12 and abs(g - 0x40) <= 12 and abs(b - 0xc0) <= 12:
				near += 1
	print("grave-guest: held window colour %d of %d samples" % (near, total))
	if near * 2 < total:
		fail("the held window's colour is not in the middle of the capture")
except Exception as error:
	fail("the capture could not be read: %s" % error)
print("grave-guest: " + ("PASS" if ok else "FAIL"))
PY
