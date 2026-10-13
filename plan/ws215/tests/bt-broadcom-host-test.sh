#!/bin/sh
# The host test of bluetoothd's Broadcom start on a UART (ws215-p005): builds userland/base/bluetoothd's broadcom.c,
# session.c and hci.c with the host's compiler (under ASan and UBSan) and runs plan/ws215/tests/bt-broadcom-host-test.c
# (the pure parts, the count of failed starts, a scripted Broadcom controller on a UART with the kernel's ioctls).  The
# synthetic patch files go in a new folder of the build.  With the brcmbt-firmware package's verified cache (BRCMBT_CACHE,
# or the default path below), the real BCM4345C0.hcd is checked and loaded into the scripted controller as well.
# usage: plan/ws215/tests/bt-broadcom-host-test.sh   (from the repository's top; OUT= to choose the build folder)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
OUT=${OUT:-build/ws215-bt-broadcom-host}
mkdir -p "$OUT"
patches=$(mktemp -d "$OUT/patches.XXXXXX")
flags="-std=gnu11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wdeclaration-after-statement -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iinclude -I."
cc $flags -o "$OUT/bt-broadcom-host-test" plan/ws215/tests/bt-broadcom-host-test.c \
	userland/base/bluetoothd/broadcom.c userland/base/bluetoothd/session.c userland/base/bluetoothd/hci.c \
	userland/base/bluetoothd/intel.c userland/base/bluetoothd/acl.c -lpthread
brcmbt=${BRCMBT_CACHE:-build/sources/firmware/brcmbt/adb4d55c8f193ff16ece74dd72fd6e8801632103-dc85ccedc9c973682fbcf4d628ca61174bcc3120}
if [ -f "$brcmbt/BCM4345C0.hcd" ]; then
	timeout 120 "$OUT/bt-broadcom-host-test" "$patches" "$brcmbt"
else
	timeout 120 "$OUT/bt-broadcom-host-test" "$patches"
fi
