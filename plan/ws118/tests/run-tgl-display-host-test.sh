#!/bin/sh
# ws118-p006: host test of the Tiger Lake display answers (combo PHY predicate, P5 DPLL tables, CDCLK hooks, pipe
# scaler readout and disable), against the production display sources, ASan/UBSan.
#   sh plan/ws118/tests/run-tgl-display-host-test.sh [OUT]   (default build/ws118-tgl-display/test)
# Builds through plan/ws084/tests/display-host-lib.sh; perf.c and the N0 test's stand-ins link as there.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
OUT=${1:-build/ws118-tgl-display/test}
mkdir -p "$(dirname "$OUT")"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
i915_display_host_build "$OUT" plan/ws118/tests/host-tgl-display-test.c plan/ws084/tests/native-decide-stubs.c
"$OUT"
