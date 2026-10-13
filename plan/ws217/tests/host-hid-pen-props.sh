#!/bin/sh
# ws217-p003: the host test of the properties a HID pen's device is registered with (src/drivers/generic/hid-input.c:
# INPUT_PROP_DIRECT for a pen on a display, INPUT_PROP_POINTER for a pen tablet).  The kernel's HID files are compiled
# freestanding as the kernel compiles them (as plan/ws143/tests/hid-input-host-test.sh does); the test uses the host's
# C library.  The user's touch LCD's two interfaces (BUG-267) are the fixtures from files.  Output in a new directory
# under build/tmp (nothing is removed here; Q1's plan/tools/q1-clean.sh removes old runs).
#   sh plan/ws217/tests/host-hid-pen-props.sh   (from the repository's top)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
. plan/tools/fresh-out.sh
fresh_out build/tmp/ws217-hid-pen-props
out=$fresh_dir
cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
kflags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc -fno-builtin -ffunction-sections \
	-fdata-sections -D__ZEDBSD__ -DKERN_USER_ABI_LP64 -Iinclude -Isrc"
hflags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement -Iinclude"
for file in hid-report hid-digitizer hid-touch hid-input; do
	$cc $kflags $extra -c src/drivers/generic/$file.c -o "$out/$file.o"
done
$cc $hflags $extra -c plan/ws217/tests/host-hid-pen-props.c -o "$out/host-hid-pen-props.o"
$cc $extra -Wl,--gc-sections "$out/host-hid-pen-props.o" "$out/hid-report.o" "$out/hid-digitizer.o" "$out/hid-touch.o" \
	"$out/hid-input.o" -o "$out/host-hid-pen-props"
timeout 60 "$out/host-hid-pen-props" plan/bugs/BUG-267/touchscreen-if0.rdesc plan/bugs/BUG-267/touchscreen-if1.rdesc
