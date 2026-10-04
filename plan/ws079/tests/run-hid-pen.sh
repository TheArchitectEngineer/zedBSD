#!/bin/sh
# Builds and runs the host test of the USB HID pen (ws079-p002).
# The driver files are compiled freestanding like the kernel; the test itself
# uses the host C library.
# Usage: plan/ws079/tests/run-hid-pen.sh [build-dir]
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws079-p002-host}
mkdir -p "$out"

cc=${CC:-clang}
kflags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc \
	-fno-builtin -ffunction-sections -fdata-sections -D__ZEDBSD__ \
	-DKERN_USER_ABI_LP64 -I $root/include -I $root/src"
$cc $kflags -c "$root/src/drivers/generic/hid-report.c" -o "$out/hid-report.o"
$cc $kflags -c "$root/src/drivers/generic/hid-digitizer.c" -o "$out/hid-digitizer.o"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I "$root/include" \
	-c "$root/plan/ws079/tests/host-hid-pen.c" -o "$out/host-hid-pen.o"
$cc -Wl,--gc-sections "$out/host-hid-pen.o" "$out/hid-report.o" \
	"$out/hid-digitizer.o" -o "$out/host-hid-pen"
"$out/host-hid-pen"
