#!/bin/sh
# Host check of the DesignWare SD/MMC driver and its JH7110 glue on a fake
# controller with a fake SD card (no emulator, no board): the bring-up
# command sequence, the command register's bits, the clock dividers, the
# FIFO's reads and writes, the recovery from data errors (BUG-340: the
# sample phase tuning and the fall back to the default speed), and the glue's
# clocks, reset, card detect and properties on a VisionFive 2-shaped tree.
#   plan/ws256/tests/dw-mmc/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/dw-mmc
out=$1
mkdir -p "$out"
dtc -q -I dts -O dtb -o "$out/vf2-mmc.dtb" "$here/vf2-mmc.dts"
cc -std=c99 -O2 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -include time.h \
	-I"$root/include" -I"$root/src" \
	"$root/src/drivers/generic/fdt.c" \
	"$root/src/drivers/platform/visionfive2/vf2-syscon.c" \
	"$root/src/drivers/platform/visionfive2/vf2-crg.c" \
	"$root/src/drivers/platform/visionfive2/vf2-pinctrl.c" \
	"$root/src/drivers/platform/visionfive2/vf2-mmc.c" \
	"$root/src/drivers/mmc/dw-mmc.c" \
	"$here/dw-mmc-host-test.c" -o "$out/dw-mmc-host-test"
"$out/dw-mmc-host-test" driver
"$out/dw-mmc-host-test" phase
"$out/dw-mmc-host-test" no-hook
"$out/dw-mmc-host-test" glue "$out/vf2-mmc.dtb"
