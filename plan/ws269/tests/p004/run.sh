#!/bin/sh
# Host check of ws269-p004: the device window's page leaves for the
# uncached alias, and the VisionFive 2's GPU bring-up (power management
# unit, GPU clocks and resets, the PowerVR early stage) on fake registers.
#   plan/ws269/tests/p004/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws269/tests/p004
out=$1
mkdir -p "$out"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src/hal/riscv64" \
	"$root/src/hal/riscv64/window.c" \
	"$here/window-host-test.c" -o "$out/window-host-test"
"$out/window-host-test"
dtc -q -I dts -O dtb -o "$out/vf2-gpu.dtb" "$here/vf2-gpu.dts"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	-DCONFIG_DRIVER_POWERVR=1 \
	"$root/src/drivers/generic/fdt.c" \
	"$root/src/drivers/platform/visionfive2/vf2-syscon.c" \
	"$root/src/drivers/platform/visionfive2/vf2-crg.c" \
	"$root/src/drivers/platform/visionfive2/vf2-pmu.c" \
	"$root/src/drivers/platform/visionfive2/vf2-gpu.c" \
	"$root/src/drivers/gpu/powervr/powervr.c" \
	"$here/vf2-gpu-host-test.c" -o "$out/vf2-gpu-host-test"
for mode in ok unknown mismatch; do
	"$out/vf2-gpu-host-test" "$out/vf2-gpu.dtb" $mode
done
