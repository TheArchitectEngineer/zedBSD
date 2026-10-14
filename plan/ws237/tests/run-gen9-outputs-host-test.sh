#!/bin/sh
# ws237-p004: host test of the Gen9 external outputs (display/gen9-outputs.c) on the hardware model
# (gen9-fake-hw.c): latencies, DP and HDMI probe, lighting and darkening with the DDB order, flips, hotplug, ASan/UBSan.
#   sh plan/ws237/tests/run-gen9-outputs-host-test.sh [OUT]   (default build/ws237-host/outputs)
# Built with the display host library (every display source as production, host-kernel.c's stand-ins); the node
# operations need the GPU and are dropped by the link.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${1:-build/ws237-host/outputs}
mkdir -p "$(dirname "$OUT")"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
i915_display_host_build "$OUT" "$T/host-gen9-outputs-test.c" "$T/gen9-fake-hw.c" plan/ws084/tests/native-decide-stubs.c
"$OUT"
