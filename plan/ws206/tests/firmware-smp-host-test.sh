#!/bin/sh
#
# zedBSD
# Copyright (C) 2026 Awe Morris
#
# SPDX-License-Identifier: Zlib
#
# Builds the firmware mailbox SMP host test with ThreadSanitizer and runs its
# two scenarios.  The guest is not started.
#
#   sh plan/ws206/tests/firmware-smp-host-test.sh OUT [DTB]
#
# OUT is a directory of the caller's own, such as build/<phase>/firmware-smp.
# DTB is the Raspberry Pi 4 device tree the firmware ships.

set -eu

REPO=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${1:?usage: firmware-smp-host-test.sh OUT [DTB]}
DTB=${2:-$REPO/vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb}
CC=${CC:-cc}

mkdir -p "$OUT"

# Builds the unchanged driver with the model, under ThreadSanitizer.
"$CC" -D_POSIX_C_SOURCE=200809L -I"$REPO/include" -I"$REPO/src" -I"$REPO" \
	-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=thread -pthread \
	"$REPO/plan/ws206/tests/firmware-smp-host-test.c" \
	"$REPO/src/drivers/platform/rpi4/rpi4-firmware.c" \
	"$REPO/src/drivers/generic/fdt.c" \
	-fsanitize=thread -pthread -o "$OUT/firmware-smp-host-test"

# Any ThreadSanitizer report fails the run.
TSAN_OPTIONS="halt_on_error=1 exitcode=66 ${TSAN_OPTIONS:-}"
export TSAN_OPTIONS

"$OUT/firmware-smp-host-test" race "$DTB"
"$OUT/firmware-smp-host-test" retry "$DTB"
echo "firmware-smp: PASS"
