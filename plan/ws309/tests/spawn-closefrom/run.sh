#!/bin/sh
# Test of libc's posix_spawn_file_actions_addclosefrom_np on zedBSD (QEMU guest): spawn-closefrom.c is linked
# statically against this tree's libc (build/amd64/sysroot/usr/lib/crt0.o and libc.o, which the caller builds:
# make ZEDBSD_CONFIG=plan/ws309/tests/config-amd64-p1.mk build/amd64/sysroot/usr/lib/libc.o), put into the guest
# started from IMAGE, and run there.  Prints its "spawn-closefrom: PASS" or the failures.
#   usage: plan/ws309/tests/spawn-closefrom/run.sh OUT IMAGE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: run.sh OUT IMAGE}
image=${2:?}
top=$(pwd)
sysroot=$top/build/amd64/sysroot
mkdir -p "$out"
build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -nostdinc -isystem "$sysroot/usr/include" \
    -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -m64 -march=x86-64 -mno-red-zone -ffreestanding -fno-pic -fno-pie \
    -fno-stack-protector -fno-builtin -ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror \
    -c plan/ws309/tests/spawn-closefrom/spawn-closefrom.c -o "$out/spawn-closefrom.o" || { echo "spawn-closefrom: compile failed"; exit 1; }
build/llvm/bin/ld.lld -m elf_x86_64 --gc-sections -nostdlib -static -z max-page-size=4096 -z stack-size=0x100000 \
    -T platform/amd64/user.ld "$sysroot/usr/lib/crt0.o" "$sysroot/usr/lib/libc.o" "$out/spawn-closefrom.o" \
    -o "$out/spawn-closefrom" || { echo "spawn-closefrom: link failed"; exit 1; }
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-120}" python3 -I plan/tools/guest/guest.py "$@"; }
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
if G_TIMEOUT=300 g wait > "$out/wait.log" 2>&1; then
	g put "$out/spawn-closefrom" /tmp/spawn-closefrom > "$out/put.log" 2>&1
	g run 'chmod 755 /tmp/spawn-closefrom; ls -l /tmp/spawn-closefrom; /tmp/spawn-closefrom; echo status=$?' > "$out/guest.out" 2>&1
	cat "$out/guest.out"
else
	echo "spawn-closefrom: the guest did not answer"
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
