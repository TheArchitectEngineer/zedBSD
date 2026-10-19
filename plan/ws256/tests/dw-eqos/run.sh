#!/bin/sh
# Host check of the DesignWare Ethernet QoS driver, the clause-22 and
# YT8531 PHY drivers and the VisionFive 2's Ethernet glue on a fake
# controller with a fake PHY (no emulator, no board): the reset, the bus
# and queue settings, MDIO, the PHY's settings and link resolution, the
# rings (transmit, receive, wrap, full ring, refill), the interrupt, open
# and close, and the glue's clocks, resets, interface select and board
# versions.
#   plan/ws256/tests/dw-eqos/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/dw-eqos
out=$1
mkdir -p "$out"
for board in v13b v12a; do
	dtc -q -i "$here" -I dts -O dtb -o "$out/vf2-eqos-$board.dtb" "$here/vf2-eqos-$board.dts"
done
flags="-std=c11 -D_POSIX_C_SOURCE=200809L -I$here/include -I$root/include -I$root -I$root/src -Wall -Wextra -Werror"
sources="$root/src/drivers/generic/fdt.c \
	$root/src/drivers/platform/visionfive2/vf2-syscon.c \
	$root/src/drivers/platform/visionfive2/vf2-crg.c \
	$root/src/drivers/platform/visionfive2/vf2-pinctrl.c \
	$root/src/drivers/platform/visionfive2/vf2-ethernet.c \
	$root/src/drivers/ethernet/dw-eqos.c \
	$root/src/drivers/ethernet/mii-phy.c \
	$root/src/drivers/ethernet/yt8531.c \
	$here/dw-eqos-host-test.c"
# shellcheck disable=SC2086
cc $flags -O2 $sources -o "$out/dw-eqos-host-test"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $sources -o "$out/dw-eqos-host-test-sanitize"
for program in dw-eqos-host-test dw-eqos-host-test-sanitize; do
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$out/$program" driver
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$out/$program" failures
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$out/$program" generic
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$out/$program" glue-v13b "$out/vf2-eqos-v13b.dtb"
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$out/$program" glue-v12a "$out/vf2-eqos-v12a.dtb"
done
echo "dw-eqos: ordinary, ASan/UBSan PASS"
