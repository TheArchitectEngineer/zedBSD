#!/bin/sh
# Simulates plio against plio-tb.v with Icarus Verilog (Debian iverilog) and
# prints PASS or FAIL.  The compiled model goes to BUILD (default
# build/zybo-pl/sim).
#   plan/ws286/tests/plio-tb.sh [BUILD]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname -- "$0")/../../.." && pwd)
build=${1:-$root/build/zybo-pl/sim}
mkdir -p "$build"
iverilog -g2005 -Wall -o "$build/plio-tb.vvp" \
	"$root/plan/ws286/tests/plio-tb.v" "$root/platform/armv7/zybo/pl/plio/plio.v"
out=$(timeout 60 vvp -n "$build/plio-tb.vvp")
printf '%s\n' "$out"
case "$out" in
*"PASS plio-tb"*) exit 0 ;;
*) exit 1 ;;
esac
