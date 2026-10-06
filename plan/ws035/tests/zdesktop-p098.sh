#!/bin/sh
# ws035-p098: the default graphical boot on the Venus guest of the graphical login image
# (plan/ws035/tests/build-login-image.sh BUILD graphical; zedbsd.cfg has logo=logo.ppm kmsg=quiet login=graphical):
#  1. The machine boots to the greeter by itself: init held getty_console back (replaced by greeter), sessiond
#     started the greeter (sysctl kern.boot.login is graphical); greeter.png (the Venus display), and console.png
#     (the text console on the standard VGA: still the boot logo, nothing written on it).
#  2. root (the only account; empty password) logs in with Enter: session.png, the session's zdesktop runs as root.
#  3. App Home's Log Out ends it and the greeter comes back: again.png.
#
#   plan/tools/files/files-guest.sh start build/amd64/hdd-graphical.img
#   plan/ws035/tests/zdesktop-p098.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-p098}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.8; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width "$width" --height "$height" "$GUEST_RUNTIME/qmp.sock" "$@"; }
status=0

# Fails the run unless a guest file has a line matching a pattern (within some seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "${3:-10}" ]; do
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

# Fails the run unless a command's output matches a pattern.
expect_run() {
	output=$(guest "$1")
	if printf '%s\n' "$output" | grep -qE "$2"; then
		echo "run: $2 ok"
	else
		echo "run: $2 MISSING ($output)"
		status=1
	fi
}

# Takes the text console's screen (the standard VGA) as PNG.
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

# 1. The greeter at boot.
expect_run 'sysctl kern.boot.login' 'kern.boot.login: graphical'
expect_run 'service status 2>&1; ps -A -o user,args' 'sessiond'
expect_log /var/log/sessiond.log 'GREETER start pid=[0-9]+ uid=78' 30
expect_log /var/log/greeter.log 'KWL GREETER open users=1 selected=root' 30
getty=$(guest 'ps -A -o args' | grep -c '^/sbin/getty')
if [ "${getty:-0}" -eq 0 ]; then
	echo "run: no getty (replaced by the greeter) ok"
else
	echo "run: getty runs: NOT replaced"
	status=1
fi
set -- $(guest "grep 'KWL OUTPUT open' /var/log/greeter.log | tail -1" | sed -n 's/.*width=\([0-9]*\) height=\([0-9]*\).*/\1 \2/p')
width=${1:-1280}; height=${2:-800}
sleep 4
pointer move $((width - 4)) $((height / 3)) sleep 400
check "$out/greeter.png" >/dev/null
console_shot console.png

# 2. root logs in (empty password).
keys '\n'
expect_log /var/log/sessiond.log 'AUTH ok user=root uid=0' 15
expect_log /var/log/sessiond.log 'SESSION start user=root' 15
expect_log /run/user/0/session.log 'KWL READY socket=/run/user/0/wayland-0' 20
sleep 3
check "$out/session.png" >/dev/null

# 3. Log Out, and the greeter again.
pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
set -- $(guest "grep 'KWL HOME icon name=\"Log Out\"' /run/user/0/session.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
if [ -n "${1:-}" ]; then
	pointer move "$1" "$2" sleep 400 down sleep 60 up sleep 500
else
	echo "no Log Out icon"
	status=1
fi
expect_log /var/log/sessiond.log 'SESSION end user=root' 20
sleep 7
pointer move $((width - 4)) $((height / 3)) sleep 400
check "$out/again.png" >/dev/null
guest "cat /var/log/sessiond.log" > "$out/sessiond.log"

echo "zdesktop-p098: status=$status"
exit $status
