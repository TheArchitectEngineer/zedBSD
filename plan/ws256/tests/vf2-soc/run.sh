#!/bin/sh
# Host check of the VisionFive 2's SoC drivers (clock and reset generators, power unit,
# pin controllers, system control blocks) and the generic device tree
# helpers they use, on fake registers (no emulator, no board).
#   plan/ws256/tests/vf2-soc/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/vf2-soc
out=$1
mkdir -p "$out"
dtc -q -I dts -O dtb -o "$out/vf2-soc.dtb" "$here/vf2-soc.dts"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/generic/fdt.c" \
	"$root/src/drivers/platform/visionfive2/vf2-syscon.c" \
	"$root/src/drivers/platform/visionfive2/vf2-crg.c" \
	"$root/src/drivers/platform/visionfive2/vf2-pinctrl.c" \
	"$root/src/drivers/platform/visionfive2/vf2-pmu.c" \
	"$here/vf2-soc-host-test.c" -o "$out/vf2-soc-host-test"
"$out/vf2-soc-host-test" "$out/vf2-soc.dtb"
