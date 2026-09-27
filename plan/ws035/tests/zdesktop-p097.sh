#!/bin/sh
# ws035-p097: the quiet kernel log and the keyboard held from the console while a GPU display lease is held, on the
# Venus guest of the login image with kmsg=quiet (the image's zedbsd.cfg given logo=logo.ppm and kmsg=quiet; the text
# console is on the standard VGA, the compositor on the Venus display):
#  1. dmesg has the kernel's messages (boot: parameters ... kmsg=quiet, the HAL's A64 TIMER TICK), and the console
#     (console.png) shows the login prompt with no kernel message on it: the console was revealed by getty's read.
#  2. hold.png: while zdesktop holds the display lease, "hello" typed on the keyboard does not reach the console's
#     login prompt.
#  3. release.png: after zdesktop ends (the lease is released), "world" typed reaches it.
#
#   make the image: build-login-image.sh, then a copy with the two lines added to the ESP's zedbsd.cfg (mtools)
#   plan/tools/files/files-guest.sh start IMAGE-COPY
#   plan/ws035/tests/zdesktop-p097.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-p097}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.8; }
status=0

# Takes the text console's screen (the standard VGA) as PNG and its text (the boot test's reader).
console_shot() {
	python3 - "$GUEST_RUNTIME/qmp.sock" "$out/$1" <<'EOF'
import json, socket, sys
sys.path.insert(0, "plan/ws035/tests")
from ppm2png import read_ppm, write_png
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
f = s.makefile("rw")
f.readline()
for command in ({"execute": "qmp_capabilities"}, {"execute": "screendump", "arguments": {"filename": sys.argv[2] + ".ppm", "format": "ppm"}}):
	f.write(json.dumps(command) + "\n")
	f.flush()
	while True:
		reply = json.loads(f.readline())
		if "return" in reply or "error" in reply:
			break
width, height, pixels = read_ppm(sys.argv[2] + ".ppm")
write_png(sys.argv[2], width, height, pixels)
EOF
	rm -f "$out/$1.ppm"
}

# Fails the run unless a command's output matches a pattern.
expect_run() {
	output=$(guest "$1")
	if printf '%s\n' "$output" | grep -qE "$2"; then
		echo "run: $2 ok"
	else
		echo "run: $2 MISSING"
		status=1
	fi
}

# 1. dmesg, and the console.
expect_run 'dmesg' 'boot: parameters: .*kmsg=quiet'
expect_run 'dmesg' 'A64 TIMER TICK'
expect_run 'dmesg' 'vfs: runtime filesystems mounted'
console_shot console.png

# 2. zdesktop holds the display; what is typed is not the console's.
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; /bin/zdesktop --timeout=120 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
keys 'hello'
sleep 1
console_shot hold.png

# 3. zdesktop ends; what is typed is the console's again.
guest 'for p in $(ps -A -o pid,args | grep -E "[z]desktop( |$)" | awk "{print \$1}"); do kill $p; done; sleep 3' >/dev/null
keys 'world'
sleep 1
console_shot release.png

echo "zdesktop-p097: status=$status (the pictures are read by eye: hold.png without hello, release.png with world)"
exit $status
