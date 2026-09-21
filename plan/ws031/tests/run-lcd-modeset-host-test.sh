#!/bin/sh
# WS031: host integration test of the one-screen LCD modeset (display/modeset.c and the modeset environment, the
# resident eDP of display/dp-sink.c) on the register / sink models tests/display/lcd-fake-hw.c and dp-fake-hw.c,
# ASan/UBSan, from the repository root.
set -eu
cd "$(dirname "$0")/../../.."
. plan/ws031/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${TMPDIR:-/tmp}/ws031-lcd-modeset-host-test
i915_display_host_build "$OUT" "$T/host-lcd-modeset-test.c" "$T/lcd-fake-hw.c" "$T/dp-fake-hw.c"
"$OUT" plan/ws031/display-ref "$@"
