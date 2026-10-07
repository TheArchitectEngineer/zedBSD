#!/bin/sh
# ws051-p004b: builds and runs the host test of an external DP display's link (host-dpext-link.c over
# drv_i915_lcd_compute_dp_ext() of display/state.c) against the production display sources
# (plan/ws084/tests/display-host-lib.sh, ASan/UBSan).  host-dkl-stubs.c stands in for the kernel services
# host-kernel.c lacks.
#   sh plan/ws051/tests/host-dpext-link.sh [OUT]   (default build/ws051-host/host-dpext-link)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
OUT=${1:-build/ws051-host/host-dpext-link}
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c"
i915_display_host_build "$OUT" plan/ws051/tests/host-dpext-link.c plan/ws051/tests/host-dkl-stubs.c
"$OUT" "$@"
