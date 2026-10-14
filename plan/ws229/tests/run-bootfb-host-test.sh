#!/bin/sh
# BUG-310: host test of the boot framebuffer display's arithmetic (src/drivers/gpu/i915/display/bootfb.c):
# the framebuffer's checks and layout, the clip, the red/blue swap and the row copy, ASan/UBSan.
#   sh plan/ws229/tests/run-bootfb-host-test.sh [OUT]   (default build/bug310-host/test)
# Built with the display host library (every display source as production, host-kernel.c's stand-ins); the node
# operations need the GPU and are dropped by the link.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${1:-build/bug310-host/test}
mkdir -p "$(dirname "$OUT")"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
i915_display_host_build "$OUT" "$T/host-bootfb-test.c" plan/ws084/tests/native-decide-stubs.c
"$OUT"
