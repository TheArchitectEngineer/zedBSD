#!/bin/sh
# Host test of the nvrtx chip identity decoding, family records, product table and boot choice.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/tools/nvrtx/nvrtx-identify-host-test.sh   (from the repository root)
# Builds src/drivers/gpu/nvrtx/identify.c and products.c with the host compiler
# next to tables of expected values and checks them.  The binary goes in a
# new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/nvrtx-identify.XXXXXX")
cc -std=c99 -Wall -Wextra -Werror -I"$repo/include" -I"$repo/src" -I"$repo" \
	-o "$out/identify-test" \
	"$repo/plan/tools/nvrtx/nvrtx-identify-host-test.c" \
	"$repo/src/drivers/gpu/nvrtx/identify.c" \
	"$repo/src/drivers/gpu/nvrtx/products.c"
"$out/identify-test"
