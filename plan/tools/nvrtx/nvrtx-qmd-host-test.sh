#!/bin/sh
# Host test of the nvrtx compute launch of every generation (QMD 2.2 and 3.0,
# the compute classes and their launch methods).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/tools/nvrtx/nvrtx-qmd-host-test.sh   (from anywhere)
# The expected field positions are plan/tools/nvrtx/qmd-fields.h, written by
# qmd-fields-gen.py from open-gpu-doc.  The binary goes in a new directory
# below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/nvrtx-qmd.XXXXXX")
cc -std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I"$repo/include" -I"$repo/src" -I"$repo" -idirafter "$repo/libc/include" \
	-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-o "$out/qmd-test" \
	"$repo/plan/tools/nvrtx/nvrtx-qmd-host-test.c" \
	"$repo/src/drivers/gpu/nvrtx/compute-launch.c" "$repo/src/drivers/gpu/nvrtx/wire.c"
"$out/qmd-test"
