#!/bin/sh
# BUG-102: an ordinary user (kei, uid 1000) pings through the set-user-ID root ping.
# Needs a running WS073 guest (tests/g.sh start IMAGE) of an image with this tree's userland.
# As root it gives kei the harness key, then over SSH as kei it checks /bin/ping's mode, pings the guest's own
# loopback and the emulator's gateway, shows that a copy without the set-user-ID bit is refused (the bit is what
# grants the socket), and runs tests/setuid-drop.c installed set-user-ID root (the privilege is gone after
# setuid(getuid()) and cannot come back).  Prints PING-USER:PASS.
#   sh plan/ws073/tests/ping-user.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
root=$(pwd)
sysroot=$root/build/amd64/sysroot
out=build/ws073-p029/guest
runtime=${GUEST_RUNTIME:-$root/build/ws073-run}
cc="$root/build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot=$sysroot"
mkdir -p "$out"
$cc -nostdinc -I. -Iinclude -isystem "$sysroot/usr/include" -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC \
	-m64 -march=x86-64 -mno-red-zone -O2 -ffreestanding -fPIC -fno-builtin -fno-stack-protector -Wall -Wextra -Werror \
	-c plan/ws073/tests/setuid-drop.c -o "$out/setuid-drop.o"
$cc -m64 -nostdlib -pie -Wl,--no-relax -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code \
	-Wl,-z,stack-size=0x100000 -Wl,--dynamic-linker=/lib/ld.so \
	"$sysroot/usr/lib/crt1.o" "$out/setuid-drop.o" -Lbuild/amd64/dynamic -l:libc.so -o "$out/setuid-drop"
g() { timeout 300 sh plan/ws073/tests/g.sh "$@" </dev/null; }
g put "$out/setuid-drop" /var/tmp/setuid-drop
g run 'mkdir -p /home/kei/.ssh && cp /root/.ssh/authorized_keys /home/kei/.ssh/authorized_keys &&
	chown -R kei:kei /home/kei /home/kei/.ssh && chmod 700 /home/kei/.ssh && chmod 600 /home/kei/.ssh/authorized_keys &&
	chown root:wheel /var/tmp/setuid-drop && chmod 4755 /var/tmp/setuid-drop && ls -l /var/tmp/setuid-drop'
port=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["ssh_port"])' "$runtime/session.json")
kei() {
	timeout 120 ssh -i plan/tmp/guest/id_ed25519 -p "$port" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
		-o LogLevel=ERROR -o ConnectTimeout=5 -o BatchMode=yes kei@127.0.0.1 -- "$@" </dev/null
}
status=0
kei 'id; ls -l /bin/ping' | tee "$out/kei-id.txt"
grep -q 'uid=1000' "$out/kei-id.txt" || { echo "PING-USER:FAIL not kei"; status=1; }
grep -q '^-rwsr-xr-x .* root ' "$out/kei-id.txt" || { echo "PING-USER:FAIL /bin/ping is not set-user-ID root"; status=1; }
if kei 'ping -c 3 127.0.0.1' > "$out/kei-ping-lo.txt" 2>&1; then :; else echo "PING-USER:FAIL loopback"; status=1; fi
cat "$out/kei-ping-lo.txt"
if kei 'ping -c 3 10.0.2.2' > "$out/kei-ping-gw.txt" 2>&1; then :; else echo "PING-USER:FAIL gateway"; status=1; fi
cat "$out/kei-ping-gw.txt"
kei 'cp /bin/ping /var/tmp/ping-nosuid.$$ && /var/tmp/ping-nosuid.$$ -c 1 127.0.0.1; echo "exit $?"; rm -f /var/tmp/ping-nosuid.$$' \
	> "$out/kei-ping-nosuid.txt" 2>&1 || true
cat "$out/kei-ping-nosuid.txt"
grep -q 'socket: .*not permitted' "$out/kei-ping-nosuid.txt" || { echo "PING-USER:FAIL copy without the bit was not refused"; status=1; }
kei /var/tmp/setuid-drop > "$out/kei-setuid-drop.txt" 2>&1 || true
cat "$out/kei-setuid-drop.txt"
grep -q 'SETUID-DROP:PASS' "$out/kei-setuid-drop.txt" || { echo "PING-USER:FAIL privilege drop"; status=1; }
g run 'ping -c 2 127.0.0.1' > "$out/root-ping-lo.txt" 2>&1 || { echo "PING-USER:FAIL root loopback"; status=1; }
cat "$out/root-ping-lo.txt"
[ "$status" = 0 ] && echo "PING-USER:PASS"
exit "$status"
