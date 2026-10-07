#!/bin/sh
# ws084-p004: host test of the N0 decision rules (display/takeover.c: drv_i915_native_decide), ASan/UBSan.
# Restored from the WS031 runner removed in 1e867fbf.
#   sh plan/ws084/tests/run-native-decide-host-test.sh [OUT]   (default build/ws084-native-decide/test)
# perf.c (present.c's counters) is linked as production; native-decide-stubs.c stands in for the services
# host-kernel.c lacks.  I915_HOST_EXTRA_PRODUCTION names more production sources to link.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${1:-build/ws084-native-decide/test}
mkdir -p "$(dirname "$OUT")"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
i915_display_host_build "$OUT" "$T/host-native-decide-test.c" plan/ws084/tests/native-decide-stubs.c
"$OUT"
