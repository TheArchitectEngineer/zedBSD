#!/bin/sh
# Host check of the VisionFive 2's sound path (ws256-p019): the AXI DMA
# controller's descriptor chains (memory copy and cyclic), the PWM audio
# converter's settings and sample clock, on a fake register model (no
# emulator, no board).
#   plan/ws256/tests/vf2-audio/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/vf2-audio
out=$1
mkdir -p "$out"
dtc -q -I dts -O dtb -o "$out/vf2-audio.dtb" "$here/vf2-audio.dts"
cc -std=c99 -D_POSIX_C_SOURCE=200809L -include time.h -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/generic/fdt.c" \
	"$root/src/drivers/platform/visionfive2/vf2-syscon.c" \
	"$root/src/drivers/platform/visionfive2/vf2-crg.c" \
	"$root/src/drivers/platform/visionfive2/vf2-pinctrl.c" \
	"$root/src/drivers/dma/dw-axi-dmac.c" \
	"$root/src/drivers/platform/visionfive2/vf2-dmac.c" \
	"$root/src/drivers/platform/visionfive2/vf2-pwmdac.c" \
	"$here/vf2-audio-host-test.c" -o "$out/vf2-audio-host-test"
"$out/vf2-audio-host-test" "$out/vf2-audio.dtb"
