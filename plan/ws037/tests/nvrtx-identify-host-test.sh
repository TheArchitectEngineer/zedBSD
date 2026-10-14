#!/bin/sh
# Host test of the nvrtx chip identity decoding (ws037-p003).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws037/tests/nvrtx-identify-host-test.sh   (from the repository root)
# Builds src/drivers/gpu/nvrtx/identify.c with the host compiler next to a
# table of identity values and checks each decoding.  The binary goes in a
# new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/nvrtx-identify.XXXXXX")
cc -std=c99 -Wall -Wextra -Werror -I"$repo/include" -I"$repo/src" -I"$repo" \
	-o "$out/identify-test" \
	"$repo/plan/ws037/tests/nvrtx-identify-host-test.c" \
	"$repo/src/drivers/gpu/nvrtx/identify.c"
"$out/identify-test"
