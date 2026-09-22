#!/bin/sh
# WS031 p014 A0: host test of the capture display's layout (display/capture.c, I915_TEST_CAPTURE): the header
# offsets the host harness parses, the slot rotation and ready protocol, and the mode and frame checks, ASan/UBSan.
set -e
cd "$(dirname "$0")/../../.."
. plan/ws031/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${TMPDIR:-/tmp}/ws031-capture-host-test
i915_display_host_build "$OUT" "$T/host-capture-test.c"
"$OUT"
