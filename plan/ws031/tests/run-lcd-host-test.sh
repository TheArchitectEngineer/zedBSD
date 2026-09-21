#!/bin/sh
# WS031: host build + run of the LCD-A first-slice test (display/state.c: the state calculation and the register words
# of the modeset writers, compared with Linux's values on the target), ASan/UBSan, from the repository root.
set -eu
cd "$(dirname "$0")/../../.."
. plan/ws031/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${TMPDIR:-/tmp}/ws031-lcd-host-test
i915_display_host_build "$OUT" "$T/host-lcd-test.c"
"$OUT" plan/ws031/display-ref "$@"
