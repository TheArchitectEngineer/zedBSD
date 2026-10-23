#!/bin/sh
# Checks the whole JTAG boot path on the ZYBO without a zedBSD kernel: builds
# jtag-hello.S as an 8 MiB raw image (code + seeded random padding) and as a
# small ELF with a 4 MiB initrd, boots each with jtag-boot.sh, and expects on
# the live UART the DTB's address, its FDT magic, and the sum of the image as
# the CPU reads it back from DDR (computed here on the host).
#   plan/ws286/tests/jtag-hello.sh [OUT]       (default build/zybo-hello)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname -- "$0")/../../.." && pwd)
out=${1:-$root/build/zybo-hello}
src=$root/plan/ws286/tests/jtag-hello.S
cc="${CROSS_COMPILE:-arm-linux-gnueabihf-}gcc -nostdlib -static -no-pie -Wl,--build-id=none"
mkdir -p "$out"

$cc -DIMAGE_SIZE=0x800000 -Ttext=0x00100000 -o "$out/hello-raw.elf" "$src"
"${CROSS_COMPILE:-arm-linux-gnueabihf-}objcopy" -O binary -j .text "$out/hello-raw.elf" "$out/hello-raw.code"
$cc -Wl,-N -Wl,--no-warn-rwx-segments -DIMAGE_SIZE=0x100 -Ttext=0x00100000 \
	-o "$out/hello.elf" "$src"
"${CROSS_COMPILE:-arm-linux-gnueabihf-}objcopy" -O binary -j .text "$out/hello.elf" "$out/hello.code"

# Pads the raw image to 8 MiB, makes the initrd, and prints the two sums.
sums=$(python3 -I - "$out" <<'EOF'
import random, struct, sys
out = sys.argv[1]
rng = random.Random(286002)
def word_sum(data):
	return sum(struct.unpack("<%dI" % (len(data) // 4), data)) & 0xffffffff
code = open(out + "/hello-raw.code", "rb").read()
raw = code + rng.randbytes(8 * 1024 * 1024 - len(code))
open(out + "/hello-raw.bin", "wb").write(raw)
open(out + "/initrd.bin", "wb").write(rng.randbytes(4 * 1024 * 1024))
small = open(out + "/hello.code", "rb").read()[:256]
print("%08x %08x" % (word_sum(raw), word_sum(small)))
EOF
)
raw_sum=${sums% *} elf_sum=${sums#* }

boot=$root/plan/tools/zybo/jtag-boot.sh
status=0
"$boot" "$out/hello-raw.bin" --append 'jtag-hello raw' --timeout 120 \
	--expect "JTAG HELLO r2=01f00000 fdt=ok sum=$raw_sum" || status=1
"$boot" "$out/hello.elf" --initrd "$out/initrd.bin" --append 'jtag-hello elf' --timeout 120 \
	--expect "JTAG HELLO r2=01f00000 fdt=ok sum=$elf_sum" || status=1
[ "$status" -eq 0 ] && echo "jtag-hello: PASS" || echo "jtag-hello: FAIL"
exit "$status"
