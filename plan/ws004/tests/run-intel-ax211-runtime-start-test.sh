#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# The runtime-session stop order table (BUG-158): drain, quiesce, reset,
# then bus-master disable, then DMA release.
set -eu
python3 "$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)/plan/tools/driver-fragments/prepare.py" --source src/drivers/wifi/intel-ax211/intel-ax211.c

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$test_dir/../../.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/zedbsd-intel-ax211-runtime-start.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM

cc=${CC:-cc}
# _DEFAULT_SOURCE: the host mapping of kcrt.h calls vsnprintf, which strict c89 does not declare.
warnings="-std=c89 -D_DEFAULT_SOURCE -pedantic -Wno-long-long -Wall -Wextra -Werror"
ax211="$repo_root/src/drivers/wifi/intel-ax211"
core="$repo_root/build/driver-fragments/src/drivers/intel-ax211.c"
sources="$core $ax211/intel-ax211-protocol.c $ax211/intel-ax211-init.c"
sources="$sources $ax211/intel-ax211-runtime.c"
sources="$sources $ax211/intel-ax211-runtime-start.c"
sources="$sources $test_dir/intel-ax211-runtime-start-test.c"

# Ordinary native gate.
# shellcheck disable=SC2086
$cc $warnings -O2 -I"$repo_root/include" $sources \
	-o "$build_dir/intel-ax211-runtime-start"
"$build_dir/intel-ax211-runtime-start"

# Memory-safety and undefined-behaviour gate.
# shellcheck disable=SC2086
$cc $warnings -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I"$repo_root/include" $sources \
	-o "$build_dir/intel-ax211-runtime-start-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$build_dir/intel-ax211-runtime-start-sanitize"

echo "intel ax211 runtime stop: ordinary, ASan/UBSan PASS"
