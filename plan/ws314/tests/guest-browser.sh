#!/bin/sh
# WS314's browser test on zedBSD (QEMU, Venus): the browser, as the desktop program of a compositor of its own
# (/bin/wayland --testing --glass at 1280x800), opens a page of a server on the host (plan/ws314/tests/pages).  The
# page's two pictures come through libbrowser's loader on its network thread; a click on its link (QMP) goes to the
# second page, which is fetched without blocking (the network thread wakes the window through kl_app_post), and its
# picture and a request of its script (XMLHttpRequest) come the same way.  Checks the browser's own lines in its log in the guest
# (the first page's console line, the second page's NAVIGATE line, the request's console line), the server's log of
# the pictures asked for, and takes a picture of the screen through VNC.  The image is built first:
#   plan/tools/guest/test-image.sh plan/ws314/tests/config-amd64-browser.mk IMGBUILD
#   usage: plan/ws314/tests/guest-browser.sh OUT IMGBUILD [PORT]     (OUT under build/, e.g. build/p2-q/browser1)
# Prints one summary line per check and "ws314-browser: PASS" or "FAIL".  The QEMU console is not read.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: guest-browser.sh OUT IMGBUILD [PORT]}
img=${2:?}
port=${3:-28180}
top=$(pwd)
mkdir -p "$out/www"
cp plan/ws314/tests/pages/first.html plan/ws314/tests/pages/second.html plan/ws314/tests/pages/data.txt "$out/www/"
cp docs/howto/keiland-examples/01-app.png "$out/www/picture.png"
echo "$port" > "$out/port"
python3 -m http.server --bind 127.0.0.1 --directory "$out/www" "$port" > "$out/server.log" 2>&1 &
server=$!
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
# The Venus renderer and its environment as guest.sh sets them, with VNC on the Venus console for the picture.
VENUS_RENDERER=${VENUS_RENDERER:-/home/awe/zedBSD-claude1/build/ws035-sq-venus/install}
export LIBGL_ALWAYS_SOFTWARE=1 MESA_LOADER_DRIVER_OVERRIDE=zink
export VK_DRIVER_FILES="${VK_DRIVER_FILES:-/usr/share/vulkan/icd.d/lvp_icd.json}"
export RENDER_SERVER_EXEC_PATH="$VENUS_RENDERER/libexec/virgl_render_server"
export LD_LIBRARY_PATH="$VENUS_RENDERER/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
. plan/tools/guest/venus-hostmem.sh
g() { timeout "${G_TIMEOUT:-120}" python3 -I plan/tools/guest/guest.py "$@"; }
# Waits up to $3 seconds for a guest file to have a line matching a pattern; 0 when it came.
pointer() { timeout 60 python3 -I plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@" > /dev/null; }
expect() { G_TIMEOUT=$(($3 + 30)) g run "i=0; while ! grep -q '$2' $1 2>/dev/null && [ \$i -lt $(($3 * 2)) ]; do sleep 0.5; i=\$((i+1)); done; grep -q '$2' $1" > /dev/null 2>&1; }
failed=0
timeout 600 python3 -I plan/tools/guest/guest.py start "$img/hdd-image.img" --symbols "$img/vmunix" \
    --qemu "$top/plan/ws314/tests/qemu-unsafe.sh" \
    --qemu-extra "-object memory-backend-memfd,id=mem,size=8G,share=on -machine memory-backend=mem -device virtio-gpu-gl-pci,id=venus,venus=on,blob=on,hostmem=$VENUS_HOSTMEM,max_outputs=1 -display egl-headless,rendernode=/dev/dri/renderD128 -vnc unix:$GUEST_RUNTIME/vnc.sock,display=venus -device usb-tablet,bus=xhci.0,port=4" \
    > "$out/start.log" 2>&1
G_TIMEOUT=600 g wait --timeout 590 > "$out/wait.log" 2>&1 || { echo "ws314-browser: the guest did not answer"; failed=1; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[b]rowser" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[b]rowser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
first=FAIL
pictures=FAIL
second=FAIL
done_line=FAIL
if [ $failed = 0 ]; then
	sleep 20
	g run "$stop_all" > /dev/null 2>&1
	g put plan/ws314/tests/guest-browser-client.sh /tmp/ws314-browser-client.sh >> "$out/put.log" 2>&1 || failed=1
	g put "$out/port" /tmp/ws314-port >> "$out/put.log" 2>&1 || failed=1
	g run "chmod 755 /tmp/ws314-browser-client.sh; rm -rf /tmp/ws314run; mkdir -m 700 /tmp/ws314run; export XDG_RUNTIME_DIR=/tmp/ws314run HOME=/tmp/ws314home; mkdir -p \$HOME; rm -f /tmp/browser.log; /bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass --desktop-client=/tmp/ws314-browser-client.sh > /tmp/wayland.log 2>&1 </dev/null & echo started" > "$out/guest.out" 2>&1
	expect /tmp/browser.log 'ZBROWSER CONSOLE .*ws314-first' 120 && first=PASS
	# A click on the first page's link (its block fills the top of the page, below the window's titlebar).
	if expect /tmp/browser.log 'ZBROWSER READY' 60; then
		sleep 3
		set -- $(g run "grep 'KWL MAP client=' /tmp/wayland.log | tail -1" 2>/dev/null | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
		echo "browser at ${1:-0},${2:-0}" > "$out/browser-at.txt"
		pointer move $((${1:-0} + 298)) $((${2:-0} + 200)) sleep 150 move $((${1:-0} + 300)) $((${2:-0} + 200)) sleep 300 down sleep 60 up sleep 800
		# Once more on the link's words, should the first click only have given the window the pointer.
		expect /tmp/browser.log 'ZBROWSER NAVIGATE path=http://10.0.2.2:[0-9]*/second.html' 5 ||
		    pointer move $((${1:-0} + 148)) $((${2:-0} + 20)) sleep 150 move $((${1:-0} + 150)) $((${2:-0} + 20)) sleep 300 down sleep 60 up sleep 800
	fi
	expect /tmp/browser.log 'ZBROWSER NAVIGATE path=http://10.0.2.2:[0-9]*/second.html title=WS314 second' 60 && second=PASS
	expect /tmp/browser.log 'ZBROWSER CONSOLE .*ws314-done 200 ws314-data' 60 && done_line=PASS
	sleep 3
	grep -q 'GET /picture.png?a ' "$out/server.log" && grep -q 'GET /picture.png?b ' "$out/server.log" &&
	    grep -q 'GET /picture.png?c ' "$out/server.log" && pictures=PASS
	timeout 60 python3 -I plan/ws035/tests/zdesktop-shot.py "$out/browser.png" --runtime "$GUEST_RUNTIME" > "$out/shot.log" 2>&1
	for t in browser wayland; do
		g run "cat /tmp/$t.log" > "$out/$t.log" 2>&1
	done
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
kill "$server" 2>/dev/null
echo "first page (its script ran): $first"
echo "pictures asked through the network thread (both pages): $pictures"
echo "second page fetched without blocking: $second"
echo "its script's request answered: $done_line"
if [ -s "$out/browser.png" ]; then
	echo "screen: $out/browser.png"
else
	echo "screen: none ($(tail -1 "$out/shot.log" 2>/dev/null))"
fi
grep -q 'ZBROWSER ERROR' "$out/browser.log" 2>/dev/null && { echo "browser ERROR lines:"; grep 'ZBROWSER ERROR' "$out/browser.log"; }
[ $first = PASS ] && [ $pictures = PASS ] && [ $second = PASS ] && [ $done_line = PASS ] || failed=1
[ $failed = 0 ] && echo "ws314-browser: PASS" || echo "ws314-browser: FAIL"
