#!/bin/sh
# WS314's tests on zedBSD (QEMU, Venus): post-test.c, process-test.c and instance-test.c built for zedBSD against the
# image's libkeiland, and Terminal running a command (its shell's end closes it), each as the desktop program of a compositor of its own
# (/bin/wayland --testing).  The image is built first:
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
for t in post process instance; do
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
	for t in post process instance; do
		g put "$out/$t-test" "/tmp/$t-test" >> "$out/put.log" 2>&1 || failed=1
	done
	client='/tmp/post-test > /tmp/post.log 2>&1; /tmp/process-test > /tmp/process.log 2>&1; /tmp/instance-test > /tmp/instance.log 2>&1; /bin/terminal --command="echo zterm-$((40+2)) > /tmp/zterm.txt; exit 3" --timeout=30 > /tmp/terminal.log 2>&1; cat /tmp/zterm.txt >> /tmp/terminal.log; echo done > /tmp/ws314-done'
	G_TIMEOUT=400 g run "chmod 755 /tmp/*-test; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/ws314home; mkdir -p \$HOME; rm -f /tmp/wayland-0 /tmp/ws314-done; /bin/wayland --testing --timeout=300 '--desktop-client=sh -c \"$client\"' > /tmp/wayland.log 2>&1 </dev/null & i=0; while [ ! -e /tmp/ws314-done ] && [ \$i -lt 300 ]; do sleep 1; i=\$((i+1)); done; echo waited=\$i" > "$out/guest.out" 2>&1
	for t in post process instance terminal wayland; do
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
for t in post process instance; do
	echo "$t: $(tail -1 "$out/$t.log")"
	grep -q '^PASS' "$out/$t.log" || failed=1
done
if grep -q 'zterm-42' "$out/terminal.log" && grep -q 'reason=shell-exited' "$out/terminal.log"; then
	echo "terminal: the shell ran the command, and its end closed the terminal (reason=shell-exited)"
else
	echo "terminal: FAIL"
	failed=1
fi
[ $failed = 0 ] && echo "ws314-guest: PASS" || echo "ws314-guest: FAIL"
