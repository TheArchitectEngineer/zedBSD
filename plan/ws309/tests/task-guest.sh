#!/bin/sh
# ws309: on QEMU's Venus guest, the render thread's tasks: wl_shm windows (wlshm) made and closed one after another,
# with a cursor of their own changing shape under the test pointer (aat-input), each new image's move to its layout
# going to the render thread as a task.  The guest is started from IMAGE in OUT/runtime; WAYLAND is put in as
# /bin/wayland; a compositor of its own (--testing --glass --log-frames) shows the windows, the screen is captured
# with a last window held (OUT/task.png) and the compositor's and the windows' lines are collected.  The guest is
# ended by QEMU's monitor.
# Prints "task-guest: PASS" when the compositor was ready, tasks completed (KWL TASK done, every result=0), every
# wlshm drew its frames (WLSHM DONE), the held window's colour (0x2040c0) covers most of the middle of the capture,
# no KWL FAILED or VULKAN_ERROR line came, and KWL PERF pass was told (its pass_max_us is printed).
#   usage: plan/ws309/tests/task-guest.sh OUT IMAGE WAYLAND
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: task-guest.sh OUT IMAGE WAYLAND}
image=${2:?}
wl=${3:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-300}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
g wait > "$out/wait.log" 2>&1 || { echo "task-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]lshm" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]lshm" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	# Windows of three sizes, each with a cursor that changes every 2 frames while the pointer is on it; the pointer
	# moves over the middle of the screen, where a window is placed.
	G_TIMEOUT=200 g run 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p008dev; mkdir -p $HOME; rm -f /tmp/wayland-0; /bin/wayland --testing --glass --log-frames --timeout=300 --width=1280 --height=800 > /tmp/task.log 2>&1 </dev/null & sleep 5; aat-input start --width 1280 --height 800 > /tmp/aat.log 2>&1; for size in 400x300 640x480 300x200 400x300 800x500; do wlshm --size=$size --band=ff00ff00 --cursor=ffff0000 --cursor-every=2 --frames=90 --token=s$size >> /tmp/wlshm.log 2>&1 </dev/null & sleep 1; for step in 1 2 3 4 5 6; do aat-input move-to $((600 + step * 7)) $((380 + step * 5)) >/dev/null 2>&1; sleep 0.3; done; wait $!; done; wlshm --size=400x300 --color=ff2040c0 --hold --token=hold >> /tmp/wlshm.log 2>&1 </dev/null & sleep 6; echo started' > "$out/guest-start.out" 2>&1
	python3 -I plan/ws035/tests/zdesktop-check.py "$out/task.png" --runtime "$GUEST_RUNTIME" > "$out/check.log" 2>&1
	G_TIMEOUT=60 g run 'echo "== compositor"; grep -E "KWL (READY|THREADS|FAILED|VULKAN_ERROR|PERF pass|CURSOR_PLANE)" /tmp/task.log; echo "== graves"; grep "KWL GRAVE" /tmp/task.log | awk -F= "{s+=\$2} END {print s+0}"; echo "== tasks"; grep -c "KWL TASK done" /tmp/task.log; grep "KWL TASK done" /tmp/task.log | grep -vc "result=0$"; echo "== windows"; grep -E "WLSHM (DONE|FAILED|ENTER)" /tmp/wlshm.log' > "$out/guest.out" 2>&1
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
cat "$out/guest-start.out" "$out/guest.out"
python3 -I - "$out/guest.out" "$out/task.png" <<'PY'
import re, sys
text = open(sys.argv[1], errors="replace").read().splitlines()
ok = True
def fail(why):
	global ok
	ok = False
	print("task-guest: " + why)
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
tasks = section("tasks")
windows = section("windows")
if not any(line.startswith("KWL READY") for line in compositor):
	fail("the compositor was not ready")
if any(re.match(r"KWL (FAILED|VULKAN_ERROR)", line) for line in compositor):
	fail("a KWL FAILED or VULKAN_ERROR line")
if len(tasks) < 2 or not tasks[0].strip().isdigit() or int(tasks[0]) == 0:
	fail("no task completed")
elif tasks[1].strip() != "0":
	fail("a task failed")
done = [line for line in windows if line.startswith("WLSHM DONE")]
if len(done) != 6:
	fail("%d of 6 windows drew their frames" % len(done))
if any(line.startswith("WLSHM FAILED") for line in windows):
	fail("a window failed")
passes = [line for line in compositor if line.startswith("KWL PERF pass")]
if not passes:
	fail("no KWL PERF pass line")
else:
	worst = max(float(re.search(r"pass_max_us=(\d+)", line).group(1)) for line in passes)
	graves = section("graves")
	print("task-guest: tasks=%s graves=%s pass_max_us(max)=%.0f" % (tasks[0] if tasks else "?", graves[0] if graves else "?", worst))
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
	print("task-guest: held window colour %d of %d samples" % (near, total))
	if near * 2 < total:
		fail("the held window's colour is not in the middle of the capture")
except Exception as error:
	fail("the capture could not be read: %s" % error)
print("task-guest: " + ("PASS" if ok else "FAIL"))
PY
