#!/bin/sh
# ws245-p002: checks every value of the xd register headers (src/drivers/gpu/i915/intel/display-regs-xd*.h) against
# the Linux v6.8.12 register headers it was taken from, through the pair list tests/display/xd-regs-map.h.
#   sh plan/ws245/tests/check-xd-regs-linux.sh [LINUX_TREE] [OUT]
# LINUX_TREE is an unpacked linux-6.8.12 (default build/ws229-p001/linux/linux-6.8.12, verified by WS229 p001);
# the Linux headers are only read.  Every pair must find its Linux name: the count of pairs is printed and a pair
# missing from the Linux side fails the check.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/tools/fresh-out.sh
LINUX=${1:-build/ws229-p001/linux/linux-6.8.12}
OUT=${2:-build/ws245-p002/check-xd-regs-linux}
test -f "$LINUX/drivers/gpu/drm/i915/i915_reg.h" || { echo "no Linux tree at $LINUX" >&2; exit 2; }
mkdir -p "$(dirname "$OUT")"
fresh_out "$OUT.objects"
MAP=src/drivers/gpu/i915/tests/display/xd-regs-map.h
${CC:-cc} -std=gnu11 -O0 -Wall -Wno-unused-macros \
	-Iplan/ws245/tests/linux-stub -I"$LINUX/drivers/gpu/drm/i915" -Isrc/drivers/gpu/i915 \
	-Isrc/drivers/gpu/i915/tests/display \
	-o "$OUT.objects/check" plan/ws245/tests/check-xd-regs-linux.c
"$OUT.objects/check"
# Every pair of the list was compiled: the list has as many pairs as the check counted.
pairs=$(grep -c '^I915_XD_MAP_' "$MAP")
counted=$("$OUT.objects/check" | sed -n 's/^check_xd_regs_linux: \([0-9]*\) pairs matched, \([0-9]*\) differ$/\1 \2/p' | awk '{print $1 + $2}')
if [ "$pairs" != "$counted" ]; then
	echo "check_xd_regs_linux: the list has $pairs pairs but $counted were compared (a Linux name is missing)" >&2
	exit 1
fi
echo "check_xd_regs_linux: all $pairs pairs of the list compared"
