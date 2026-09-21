#!/bin/sh
# WS031: host build + run of the eDP first-stage test (display/dp-sink.c, panel.c, aux.c, edid-read.c on the register
# model tests/display/dp-fake-hw.c), ASan/UBSan, from the repository root.
set -eu
cd "$(dirname "$0")/../../.."
. plan/ws031/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${TMPDIR:-/tmp}/ws031-dp-host-test
i915_display_host_build "$OUT" "$T/host-dp-test.c" "$T/dp-fake-hw.c"
"$OUT" plan/ws031/display-ref "$@"
