#!/bin/sh
# Host test of the nvrtx renderer's recovery decisions that touch no hardware (src/drivers/gpu/nvrtx/recover.c):
# the sessions' channel numbers, and (once there) the culprit of a stalled graphics engine on a model of its scheduler.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws250/tests/nvrtx-recover-host-test.sh   (from the repository root)
# The binary goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/nvrtx-recover.XXXXXX")
cc -std=gnu11 -Wall -Wextra -Werror -I"$repo/include" -I"$repo/src" -I"$repo" \
	-o "$out/recover-test" \
	"$repo/plan/ws250/tests/nvrtx-recover-host-test.c" \
	"$repo/src/drivers/gpu/nvrtx/recover.c"
"$out/recover-test"
