#!/bin/sh
# ws051-p005b: host test of the unplug of the firmware's own external output while it is lit (display/present.c,
# display.c), ASan/UBSan.
#   sh plan/ws051/tests/host-unplug-gop.sh [OUT]   (default build/ws051/unplug-gop)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
OUT=${1:-build/ws051/unplug-gop}
mkdir -p "$(dirname "$OUT")"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
i915_display_host_build "$OUT" plan/ws051/tests/host-unplug-gop.c plan/ws051/tests/host-unplug-gop-stubs.c
"$OUT"
