#!/bin/sh
# WS031: host test of the OpRegion VBT locator (display/vbt.c: drv_i915_opregion_locate_vbt) against the target
# laptop's OpRegion dump, ASan/UBSan.
set -e
cd "$(dirname "$0")/../../.."
. plan/ws031/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${TMPDIR:-/tmp}/ws031-opregion-host-test
i915_display_host_build "$OUT" "$T/host-opregion-test.c"
"$OUT"
