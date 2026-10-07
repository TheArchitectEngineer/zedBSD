#!/bin/sh
# ws051-p004b: builds and runs the host test of the takeover's Type-C readout objects (host-n1-tc.c: the registry's
# walks, the disable walk over a state's own encoders, the DPCD routing of an object's own AUX channel) against the
# production display sources (plan/ws084/tests/display-host-lib.sh, ASan/UBSan).  host-dkl-stubs.c stands in for the
# kernel services host-kernel.c lacks.
#   sh plan/ws051/tests/host-n1-tc.sh [OUT]   (default build/ws051-host/host-n1-tc)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
OUT=${1:-build/ws051-host/host-n1-tc}
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c"
i915_display_host_build "$OUT" plan/ws051/tests/host-n1-tc.c plan/ws051/tests/host-dkl-stubs.c
"$OUT" "$@"
