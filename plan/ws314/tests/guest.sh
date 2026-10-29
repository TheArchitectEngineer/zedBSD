#!/bin/sh
# WS314's tests on zedBSD (QEMU, Venus): post-test.c, process-test.c, launch-test.c and instance-test.c built for zedBSD
# against the image's libkeiland, Terminal running a command (its shell's end closes it), a Terminal's new window
# (Ctrl+Shift+N: kl_launch), Files opening a text in Text Editor (a double click: kl_launch) and a picture's thumbnail
# (keiland-preview's child), each as the desktop
# program of a compositor of its own (/bin/wayland --testing at 1280x800).  The image is built first:
#   plan/tools/guest/test-image.sh plan/ws314/tests/config-amd64-guest.mk IMGBUILD
#   usage: plan/ws314/tests/guest.sh OUT IMGBUILD       (OUT under build/, e.g. build/p2-q/run1)
# Prints one summary line per test and "ws314-guest: PASS" or "FAIL".  The QEMU console is not read.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: guest.sh OUT IMGBUILD}
img=${2:?}
top=$(pwd)
sysroot=$top/build/amd64/sysroot
mkdir -p "$out"
for t in post process launch instance; do
	build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -nostdinc -I. -Iinclude \
	    -isystem "$sysroot/usr/include" -Iuserland/desktop/include -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 \
	    -DKERN_DYNAMIC_LIBC -m64 -march=x86-64 -mno-red-zone -Os -ffreestanding -fPIC -fno-builtin \
	    -fno-stack-protector -Wall -Wextra -Werror -c "plan/ws314/tests/$t-test.c" -o "$out/$t-test.o" ||
	    { echo "ws314-guest: compile $t failed"; exit 1; }
	build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -m64 -nostdlib -pie -Wl,--no-relax \
	    -Wl,--hash-style=gnu,-z,now,-z,relro,-z,separate-code -Wl,-z,stack-size=0x100000,--allow-shlib-undefined \
	    -Wl,--dynamic-linker=/lib/ld.so "$sysroot/usr/lib/crt1.o" "$out/$t-test.o" \
	    -L"$img/dynamic" -Wl,-rpath-link,"$img/dynamic" -l:libkeiland.so -l:libwayland-client.so -l:libc.so \
	    -o "$out/$t-test" || { echo "ws314-guest: link $t failed"; exit 1; }
done
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
# The Venus renderer and its environment as plan/ws035/tests/zdesktop-guest.sh sets them (the renderer is the main
# checkout's build, read only), the guest started by the harness with QEMU's boot disk cache unsafe.
VENUS_RENDERER=${VENUS_RENDERER:-/home/awe/zedBSD-claude1/build/ws035-sq-venus/install}
export LIBGL_ALWAYS_SOFTWARE=1 MESA_LOADER_DRIVER_OVERRIDE=zink
export VK_DRIVER_FILES="${VK_DRIVER_FILES:-/usr/share/vulkan/icd.d/lvp_icd.json}"
export RENDER_SERVER_EXEC_PATH="$VENUS_RENDERER/libexec/virgl_render_server"
export LD_LIBRARY_PATH="$VENUS_RENDERER/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
. plan/tools/guest/venus-hostmem.sh
g() { timeout "${G_TIMEOUT:-120}" python3 -I plan/tools/guest/guest.py "$@"; }
keys() { timeout 60 python3 -I plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" > /dev/null; }
pointer() { timeout 60 python3 -I plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@" > /dev/null; }
# Waits up to $3 seconds for a guest file to have a line matching a pattern; 0 when it came.
expect() { G_TIMEOUT=$(($3 + 30)) g run "i=0; while ! grep -q '$2' $1 2>/dev/null && [ \$i -lt $(($3 * 2)) ]; do sleep 0.5; i=\$((i+1)); done; grep -q '$2' $1" > /dev/null 2>&1; }
failed=0
timeout 600 python3 -I plan/tools/guest/guest.py start "$img/hdd-image.img" --symbols "$img/vmunix" \
    --qemu "$top/plan/ws314/tests/qemu-unsafe.sh" \
    --qemu-extra "-object memory-backend-memfd,id=mem,size=8G,share=on -machine memory-backend=mem -device virtio-gpu-gl-pci,id=venus,venus=on,blob=on,hostmem=$VENUS_HOSTMEM,max_outputs=1 -display egl-headless,rendernode=/dev/dri/renderD128 -device usb-tablet,bus=xhci.0,port=4" \
    > "$out/start.log" 2>&1
G_TIMEOUT=600 g wait --timeout 590 > "$out/wait.log" 2>&1 || { echo "ws314-guest: the guest did not answer"; failed=1; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	sleep 20
	g run "$stop_all" > /dev/null 2>&1
	for t in post process launch instance; do
		g put "$out/$t-test" "/tmp/$t-test" >> "$out/put.log" 2>&1 || failed=1
	done
	g put plan/ws314/tests/guest-client.sh /tmp/ws314-client.sh >> "$out/put.log" 2>&1 || failed=1
	g put docs/howto/keiland-examples/01-app.png /tmp/ws314-picture.png >> "$out/put.log" 2>&1 || failed=1
	# Without the input method: a Terminal's keys reach the menus' shortcuts straight from the seat.
	g run 'if [ -e /usr/libexec/keiland-ime ]; then mv /usr/libexec/keiland-ime /usr/libexec/keiland-ime.off; fi' >> "$out/put.log" 2>&1
	G_TIMEOUT=400 g run "chmod 755 /tmp/*-test /tmp/ws314-client.sh; rm -rf /tmp/ws314run; mkdir -m 700 /tmp/ws314run; export XDG_RUNTIME_DIR=/tmp/ws314run HOME=/tmp/ws314home; mkdir -p \$HOME; rm -f /tmp/ws314-done /tmp/ws314-files; /bin/wayland --testing --timeout=900 --width=1280 --height=800 --desktop-client=/tmp/ws314-client.sh > /tmp/wayland.log 2>&1 </dev/null & i=0; while [ ! -e /tmp/ws314-done ] && [ \$i -lt 300 ]; do sleep 1; i=\$((i+1)); done; echo waited=\$i" > "$out/guest.out" 2>&1
	# A new window of the Terminal t1 (its log has the new one's too: a launch writes where its starter does).
	newwindow=FAIL
	if expect /tmp/t1.log 'ZTERM START run=t1 ' 60; then
		sleep 3
		# A click in its window gives it the keyboard.
		set -- $(g run "grep 'KWL MAP client=' /tmp/wayland.log | tail -1" 2>/dev/null | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
		echo "t1 at ${1:-0},${2:-0}" > "$out/t1-at.txt"
		pointer move $((${1:-0} + 200)) $((${2:-0} + 200)) sleep 300 down sleep 60 up sleep 800
		keys '<ctrl-shift-n>'
		expect /tmp/t1.log 'ZTERM START run=t1-new ' 60 && newwindow=PASS
	fi
	echo "newwindow=$newwindow" > "$out/newwindow.txt"
	g run 'for p in $(ps -A -o pid,args | grep "[t]erminal --token=t1" | awk "{print \$1}"); do kill $p; done' > /dev/null 2>&1
	# Files on the folder of one text, in the list view; a double click on its row opens it in Text Editor.
	opened=FAIL
	g run 'touch /tmp/ws314-files' > /dev/null 2>&1
	if expect /tmp/files.log 'ZFILES LOCATION kind=folder path=/tmp/ws314demo items=2 error=0' 90; then
		sleep 3
		keys '<ctrl-2>'
		sleep 2
		set -- $(g run "grep 'KWL MAP client=' /tmp/wayland.log | tail -1" 2>/dev/null | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
		wx=${1:-0}; wy=${2:-0}
		echo "files at $wx,$wy" > "$out/files-at.txt"
		pointer move $((wx + 400)) $((wy + 114)) sleep 300 down sleep 50 up sleep 80 down sleep 50 up sleep 900
		if expect /tmp/files.log 'ZFILES LAUNCH name=Text Editor command=/bin/textedit ./tmp/ws314demo/notes.txt.$' 30; then
			sleep 5
			g run 'ps -A -o args' > "$out/ps.txt" 2>&1
			grep -q '[t]extedit' "$out/ps.txt" && opened=PASS
		fi
	fi
	echo "files-open=$opened" > "$out/files-open.txt"
	# The picture's thumbnail, made by keiland-preview's child (child=1).
	thumb=FAIL
	expect /tmp/files.log 'ZFILES THUMB path=/tmp/ws314demo/picture.png error=0 .* status=0 child=1' 30 && thumb=PASS
	echo "thumbnail=$thumb" > "$out/thumbnail.txt"
	for t in post process launch instance terminal t1 files wayland; do
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
cat "$out/guest.out"
for t in post process launch instance; do
	echo "$t: $(tail -1 "$out/$t.log")"
	grep -q '^PASS' "$out/$t.log" || failed=1
done
if grep -q 'zterm-42' "$out/terminal.log" && grep -q 'reason=shell-exited' "$out/terminal.log"; then
	echo "terminal: the shell ran the command, and its end closed the terminal (reason=shell-exited)"
else
	echo "terminal: FAIL"
	failed=1
fi
echo "terminal new window: $(cat "$out/newwindow.txt" 2>/dev/null)"
grep -q PASS "$out/newwindow.txt" 2>/dev/null || failed=1
echo "files open: $(cat "$out/files-open.txt" 2>/dev/null)"
grep -q PASS "$out/files-open.txt" 2>/dev/null || failed=1
echo "files thumbnail: $(cat "$out/thumbnail.txt" 2>/dev/null)"
grep -q PASS "$out/thumbnail.txt" 2>/dev/null || failed=1
[ $failed = 0 ] && echo "ws314-guest: PASS" || echo "ws314-guest: FAIL"
