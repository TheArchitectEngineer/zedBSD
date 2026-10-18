#!/bin/sh
# ws237-p004: host test of the Gen9 mode set register sequences (display/xd-aux.c, xd-gmbus.c, xd-ddi.c, xd-output.c, xd-pipe.c, xd-dbuf.c, xd-hpd.c) on a
# hardware model: AUX/DPCD/EDID, GMBUS, power wells, DPLLs, DP training, DP and HDMI output on/off, watermarks, ASan/UBSan.
#   sh plan/ws237/tests/run-gen9-modeset-host-test.sh [OUT]   (default build/ws237-host/modeset)
# Built with the display host library (every display source as production, host-kernel.c's stand-ins); the node
# operations need the GPU and are dropped by the link.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${1:-build/ws237-host/modeset}
mkdir -p "$(dirname "$OUT")"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
i915_display_host_build "$OUT" "$T/host-gen9-modeset-test.c" "$T/gen9-fake-hw.c" plan/ws084/tests/native-decide-stubs.c
"$OUT"
