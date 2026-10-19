#!/bin/sh
# Host check of ws277-p005: the modes, the PLL table's choice, the
# timing words and the HDMI transmitter's writes.
#   plan/ws277/tests/p005/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws277/tests/p005
out=$1
mkdir -p "$out"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/dc8200-mode.c" \
	"$here/mode-host-test.c" -o "$out/mode-host-test"
"$out/mode-host-test"
cc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/dc8200-adopt.c" \
	"$root/src/drivers/gpu/powervr/dc8200-mode.c" \
	"$root/src/drivers/gpu/powervr/dc8200-hdmi.c" \
	"$root/src/drivers/gpu/powervr/dc8200.c" \
	"$here/own-host-test.c" -o "$out/own-host-test"
"$out/own-host-test" 1 "root=x display.mode=1280x720@60"
"$out/own-host-test" 0 "root=x"
