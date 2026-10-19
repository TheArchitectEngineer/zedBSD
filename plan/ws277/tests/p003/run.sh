#!/bin/sh
# Host check of the fatal stop's return of the DC8200's plane (ws277-p003
# ahead of time, R4): the probe writes nothing, the fatal stop marks the
# controller fatal and writes the probed plane back in the commit order,
# and the commit lock is tried a bounded number of times.
#   plan/ws277/tests/p003/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws277/tests/p003
out=$1
mkdir -p "$out"
cc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/dc8200-adopt.c" \
	"$root/src/drivers/gpu/powervr/dc8200.c" \
	"$here/fatal-host-test.c" -o "$out/fatal-host-test"
"$out/fatal-host-test"
