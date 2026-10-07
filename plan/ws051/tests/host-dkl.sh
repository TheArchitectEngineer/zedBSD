#!/bin/sh
# ws051-p003: builds and runs the host test of the Type-C PLL and PHY text (host-dkl-test.c) against the production
# display sources (plan/ws084/tests/display-host-lib.sh: every display/*.c, ASan/UBSan), with Linux v6.8.12's own
# DKL PLL calculation as the reference (host-dkl-linux.c over dkl-linux.inc, which this script takes out of
# plan/ws031/linux-parity/linux-reference/i915-src/display/ unchanged: the #define lines of intel_dkl_phy_regs.h and
# intel_mg_phy_regs.h, and icl_mg_pll_find_divisors, icl_calc_mg_pll_state and icl_ddi_mg_pll_get_freq of
# intel_dpll_mgr.c).  host-dkl-stubs.c stands in for the kernel services host-kernel.c lacks; perf.c is linked as
# production.
#   sh plan/ws051/tests/host-dkl.sh [OUT]   (default build/ws051-host/host-dkl)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
ref=plan/ws031/linux-parity/linux-reference/i915-src/display
OUT=${1:-build/ws051-host/host-dkl}
fresh_out "$OUT.linux"
inc=$OUT.linux/dkl-linux.inc
{
	for h in intel_dkl_phy_regs.h intel_mg_phy_regs.h; do
		awk '/^#define/ { keep = 1 } keep { print } keep && !/\\$/ { keep = 0 }' "$ref/$h" | grep -v '__INTEL_.*_REGS__'
	done
	for f in icl_mg_pll_find_divisors icl_calc_mg_pll_state icl_ddi_mg_pll_get_freq; do
		awk -v f="$f" '$0 ~ "^static int " f "\\(" { keep = 1 } keep { print } keep && /^}$/ { keep = 0 }' "$ref/intel_dpll_mgr.c"
	done
} > "$inc"
grep -c '^static int' "$inc" | grep -qx 3 || { echo "host-dkl.sh: the Linux functions were not found" >&2; exit 1; }
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c"
CC="${CC:-cc} -I$OUT.linux -Iplan/ws051/tests" i915_display_host_build "$OUT" plan/ws051/tests/host-dkl-test.c plan/ws051/tests/host-dkl-linux.c \
	plan/ws051/tests/host-dkl-stubs.c
"$OUT" "$@"
