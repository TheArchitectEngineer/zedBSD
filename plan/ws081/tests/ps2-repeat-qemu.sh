#!/bin/sh
# BUG-191: the PS/2 keyboard's typematic on a QEMU guest (q35's i8042; plan/tools/guest/guest.py start).  QEMU's PS/2
# keyboard sends a make code for every key-down QMP gives it, as a real keyboard's typematic sends one while a key
# is held.  QMP's keys go to the keyboard added last, which is guest.py's usb-kbd (T1-151: the PS/2 keyboard heard
# nothing), so the run takes the usb-kbd out first (device_del by its QOM path under /machine/peripheral-anon).
# The run:
#  1. ps2keys (ps2keys.c, built here and put into the guest over SSH) reads the PS/2 keyboard's input device.
#  2. Through QMP: the usb-kbd removed; A down five times 100 ms apart, then A up; later A down and up once more.
#  3. ps2keys' counts for A: press=2 repeat=4 release=2 (before BUG-191's fix: press=6 repeat=0).
# The image is any Kei image with the kernel under test (no desktop is needed).
#   plan/ws081/tests/ps2-repeat-qemu.sh IMAGE [BUILD] [OUTDIR]
#     BUILD: the build whose sysroot and libc.so link ps2keys (default build/amd64; build/amd64/sysroot must exist).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:?usage: ps2-repeat-qemu.sh IMAGE [BUILD] [OUTDIR]}
build=${2:-build/amd64}
out=${3:-build/ws081-shots/ps2repeat}
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws081-ps2-run}"
export GUEST_RUNTIME
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
status=0

# The probe, for Kei.
sysroot=$(pwd)/build/amd64/sysroot
[ -d "$build/sysroot" ] && sysroot=$(pwd)/$build/sysroot
build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -nostdinc -I. -Iinclude -isystem "$sysroot/usr/include" \
    -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC -m64 -march=x86-64 -mno-red-zone -O2 -ffreestanding -fPIC \
    -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -c plan/ws081/tests/ps2keys.c -o "$out/ps2keys.o" || { echo "ps2-repeat-qemu: FAIL (build)"; exit 1; }
build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -m64 -nostdlib -pie -Wl,--no-relax \
    -Wl,--hash-style=sysv,-z,now,-z,relro -Wl,--allow-shlib-undefined -Wl,--dynamic-linker=/lib/ld.so \
    "$sysroot/usr/lib/crt1.o" "$out/ps2keys.o" -L"$build/dynamic" -Wl,-rpath-link,"$build/dynamic" \
    -l:libc.so -o "$out/ps2keys" || { echo "ps2-repeat-qemu: FAIL (link)"; exit 1; }

# The guest.
python3 plan/tools/guest/guest.py stop >/dev/null 2>&1
timeout 120 python3 plan/tools/guest/guest.py start "$image" >/dev/null 2>&1
timeout 260 python3 plan/tools/guest/guest.py wait --timeout 240 >/dev/null 2>&1 || { echo "ps2-repeat-qemu: FAIL (no SSH)"; exit 1; }
timeout 60 python3 plan/tools/guest/guest.py put "$out/ps2keys" /tmp/ps2keys >/dev/null 2>&1
guest 'chmod 755 /tmp/ps2keys; (/tmp/ps2keys --seconds=10 > /tmp/ps2keys.log 2>&1 &); sleep 2; cat /tmp/ps2keys.log'

# A held (five makes), let go; A pressed and let go once more.
python3 - "$GUEST_RUNTIME/qmp.sock" <<'PYEOF'
import json, socket, sys, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
f = s.makefile("rw")
f.readline()
def send(command, arguments=None):
	f.write(json.dumps({"execute": command, "arguments": arguments or {}}) + "\n")
	f.flush()
	while True:
		reply = json.loads(f.readline())
		if "return" in reply or "error" in reply:
			return reply
def key(down):
	send("input-send-event", {"events": [{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": "a"}}}]})
send("qmp_capabilities")
# The usb-kbd goes, so that the keys reach the PS/2 keyboard.
listed = send("qom-list", {"path": "/machine/peripheral-anon"})
for child in listed.get("return", []):
	if child.get("type") == "child<usb-kbd>":
		print("removing", child["name"], send("device_del", {"id": "/machine/peripheral-anon/" + child["name"]}))
time.sleep(2)
for index in range(5):
	key(True)
	time.sleep(0.1)
key(False)
time.sleep(0.5)
key(True)
time.sleep(0.1)
key(False)
PYEOF
sleep 10
guest 'cat /tmp/ps2keys.log' > "$out/ps2keys.log"
cat "$out/ps2keys.log"
line=$(grep '^PS2KEYS device=' "$out/ps2keys.log" | tail -1)
value() { echo "$line" | sed -n "s/.* $1=\([-0-9]*\).*/\1/p"; }
[ "$(value press)" = 2 ] && echo "two presses: ok" || { echo "two presses: FAIL ($(value press))"; status=1; }
[ "$(value repeat)" = 4 ] && echo "four repeats: ok" || { echo "four repeats: FAIL ($(value repeat))"; status=1; }
[ "$(value release)" = 2 ] && echo "two releases: ok" || { echo "two releases: FAIL ($(value release))"; status=1; }
python3 plan/tools/guest/guest.py stop >/dev/null 2>&1
[ $status = 0 ] && echo "ps2-repeat-qemu: PASS" || echo "ps2-repeat-qemu: FAIL"
exit $status
