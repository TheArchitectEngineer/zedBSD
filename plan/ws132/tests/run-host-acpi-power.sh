#!/bin/sh
# Builds and runs the host test of the ACPI power devices (ws132-p002):
# src/drivers/acpi/acpi-power.c compiled freestanding like the kernel, the
# test (a stand-in namespace of a laptop) with the host C library and the
# sanitizers.
# Usage: plan/ws132/tests/run-host-acpi-power.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws132-host}
mkdir -p "$out"

cc=${CC:-clang}
san="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc \
	-fno-builtin -D__ZEDBSD__ -DKERN_USER_ABI_LP64 -DCONFIG_DRIVER_ACPI=1 $san \
	-I "$root/include" -I "$root/src" \
	-c "$root/src/drivers/acpi/acpi-power.c" -o "$out/acpi-power.o"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror $san -I "$root/include" \
	-c "$root/plan/ws132/tests/host-acpi-power.c" -o "$out/host-acpi-power.o"
$cc $san "$out/host-acpi-power.o" "$out/acpi-power.o" -o "$out/host-acpi-power"
"$out/host-acpi-power"
