#!/bin/sh
# ws245-p004: checks the VBT layout table of the xd display (src/drivers/gpu/i915/intel/xd-vbt-layout.h) against the
# Linux v6.8.12 VBT definitions (drivers/gpu/drm/i915/display/intel_vbt_defs.h, intel_bios.h): the place and mask of
# every field the xd code reads, the structure sizes and the code values.
#   sh plan/ws245/tests/check-xd-vbt-layout-linux.sh [LINUX_TREE] [OUT]
# LINUX_TREE is an unpacked linux-6.8.12 (default build/ws229-p001/linux/linux-6.8.12); its headers are only read.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/tools/fresh-out.sh
LINUX=${1:-build/ws229-p001/linux/linux-6.8.12}
OUT=${2:-build/ws245-p004/check-xd-vbt-layout-linux}
test -f "$LINUX/drivers/gpu/drm/i915/display/intel_vbt_defs.h" || { echo "no Linux tree at $LINUX" >&2; exit 2; }
mkdir -p "$(dirname "$OUT")"
fresh_out "$OUT.objects"
${CC:-cc} -std=gnu11 -O0 -Wall -Wno-unused-macros \
	-Iplan/ws245/tests/linux-stub -I"$LINUX/drivers/gpu/drm/i915" -Isrc/drivers/gpu/i915 \
	-o "$OUT.objects/check" plan/ws245/tests/check-xd-vbt-layout-linux.c
"$OUT.objects/check"
