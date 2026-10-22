#!/bin/sh
# Prints the path of the Raspberry Pi 4 device tree (bcm2711-rpi-4-b.dtb) for the QEMU raspi4b tools.
#   sh plan/tools/rpi4-dtb.sh IMAGE
# The build puts it beside the image (ws233-p001).  An image built before that has none; then the
# file comes from a shallow, sparse clone of raspberrypi/firmware at the revision the build pins
# (platform/arm64/firmware.mk), kept in build/sources/raspberrypi-firmware.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
IMAGE=${1:?image}
BESIDE=$(dirname -- "$IMAGE")/bcm2711-rpi-4-b.dtb
if test -f "$BESIDE"; then
	printf '%s\n' "$BESIDE"
	exit 0
fi

ROOT=$(cd -- "$(dirname -- "$0")/../.." && pwd)
REVISION=$(sed -n 's/^override RPI4_BOOT_FIRMWARE_REVISION := *//p' "$ROOT/platform/arm64/firmware.mk")
CLONE=$ROOT/build/sources/raspberrypi-firmware/$REVISION
if ! test -f "$CLONE/boot/bcm2711-rpi-4-b.dtb"; then
	echo "rpi4-dtb: no device tree beside $IMAGE; shallow clone of raspberrypi/firmware $REVISION" >&2
	mkdir -p "$CLONE"
	git -C "$CLONE" init -q
	git -C "$CLONE" remote add origin https://github.com/raspberrypi/firmware.git 2>/dev/null || true
	git -C "$CLONE" sparse-checkout set --no-cone /boot/bcm2711-rpi-4-b.dtb
	git -C "$CLONE" fetch -q --depth 1 --filter=blob:none origin "$REVISION"
	git -C "$CLONE" checkout -q FETCH_HEAD
fi
printf '%s\n' "$CLONE/boot/bcm2711-rpi-4-b.dtb"
