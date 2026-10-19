#!/bin/sh
# Host check of the 16550 receive driver and the serial console's byte
# translation, on a fake UART (no emulator).
#   plan/ws256/tests/serial/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/serial
out=$1
mkdir -p "$out"
dtc -q -I dts -O dtb -o "$out/virt.dtb" "$root/plan/ws256/tests/fdt/virt-like.dts"
dtc -q -I dts -O dtb -o "$out/dw-uart.dtb" "$here/dw-uart.dts"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" \
	"$root/src/drivers/serial/ns16550.c" "$root/src/drivers/generic/fdt.c" \
	"$root/src/drivers/generic/serial-console.c" \
	"$here/serial-host-test.c" -o "$out/serial-host-test"
"$out/serial-host-test" "$out/virt.dtb" "$out/dw-uart.dtb"
