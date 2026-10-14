#!/bin/sh
# ws237-p002: host test of the Gen9 flip display's arithmetic (src/drivers/gpu/i915/display/gen9-flip.c): the readout
# of the firmware's display registers (the SV7, the accepted variants and every refusal), the port and pixel clocks,
# the refresh and the display window's size, ASan/UBSan.
#   sh plan/ws237/tests/run-gen9-flip-host-test.sh [OUT]   (default build/ws237-host/test)
# Built with the display host library (every display source as production, host-kernel.c's stand-ins); the node
# operations need the GPU and are dropped by the link.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${1:-build/ws237-host/test}
mkdir -p "$(dirname "$OUT")"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
i915_display_host_build "$OUT" "$T/host-gen9-flip-test.c" plan/ws084/tests/native-decide-stubs.c
"$OUT"
