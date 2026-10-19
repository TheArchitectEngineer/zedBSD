#!/bin/sh
# Host check of ws277-p002: the decision whether the DC8200's screen as
# the loader left it can be taken over.
#   plan/ws277/tests/p002/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws277/tests/p002
out=$1
mkdir -p "$out"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/dc8200-adopt.c" \
	"$here/adopt-host-test.c" -o "$out/adopt-host-test"
"$out/adopt-host-test"
