#!/bin/sh
# ws237-p004: host test of the Gen9 mode set arithmetic (src/drivers/gpu/i915/display/gen9-mode.c): CD clock, mode,
# DP link and M/N, DPLL words, DDI buffer tables, training adjustments, watermarks and DDB, ASan/UBSan.
#   sh plan/ws237/tests/run-gen9-mode-host-test.sh [OUT]   (default build/ws237-host/mode)
# Built with the display host library (every display source as production, host-kernel.c's stand-ins); the node
# operations need the GPU and are dropped by the link.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${1:-build/ws237-host/mode}
mkdir -p "$(dirname "$OUT")"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
i915_display_host_build "$OUT" "$T/host-gen9-mode-test.c" plan/ws084/tests/native-decide-stubs.c
"$OUT"
