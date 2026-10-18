#!/bin/sh
# ws237-p003: host test of the Gen9 backlight's arithmetic (src/drivers/gpu/i915/display/xd-panel.c): the decode
# of the PCH PWM in both layouts and the brightness to duty mapping both ways, ASan/UBSan.
#   sh plan/ws237/tests/run-gen9-backlight-host-test.sh [OUT]   (default build/ws237-host/backlight)
# Built with the display host library (every display source as production, host-kernel.c's stand-ins); the node
# operations need the GPU and are dropped by the link.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${1:-build/ws237-host/backlight}
mkdir -p "$(dirname "$OUT")"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
i915_display_host_build "$OUT" "$T/host-gen9-backlight-test.c" plan/ws084/tests/native-decide-stubs.c
"$OUT"
