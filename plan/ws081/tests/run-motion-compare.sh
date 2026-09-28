#!/bin/sh
# Builds and runs the comparison of touch resampling, prediction and fling
# velocity methods (ws081-p001, plan/ws081/design.md section 3).  The program is
# deterministic; its output is kept next to it as motion-compare.out.
# Usage: plan/ws081/tests/run-motion-compare.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws081-p001}
mkdir -p "$out"

cc=${CC:-clang}
$cc -std=gnu11 -O2 -Wall -Wextra -Werror "$root/plan/ws081/tests/motion-compare.c" \
	-lm -o "$out/motion-compare"
"$out/motion-compare" > "$out/motion-compare.out"

# Tells whether the output still matches the one the design quotes.
if cmp -s "$out/motion-compare.out" "$root/plan/ws081/tests/motion-compare.out"; then
	echo "motion-compare: output matches plan/ws081/tests/motion-compare.out"
else
	echo "motion-compare: output differs from plan/ws081/tests/motion-compare.out ($out/motion-compare.out)"
	exit 1
fi
