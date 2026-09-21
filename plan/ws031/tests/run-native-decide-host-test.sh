#!/bin/sh
# WS031: host test of the N0 decision rules (display/takeover.c: drv_i915_native_decide), ASan/UBSan.
set -e
cd "$(dirname "$0")/../../.."
. plan/ws031/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${TMPDIR:-/tmp}/ws031-native-decide-host-test
i915_display_host_build "$OUT" "$T/host-native-decide-test.c"
"$OUT"
