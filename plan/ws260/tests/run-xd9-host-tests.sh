#!/bin/sh
# The host tests of display 9 in the xd display code (src/drivers/gpu/i915/display/xd-*.c with the display 9 rows of
# intel/display-tables-xd9.h), under ASan/UBSan, each built with the display host library.
#   sh plan/ws260/tests/run-xd9-host-tests.sh [OUT]   (default build/tmp/ws260-xd9)
#   host-xd-display9-test  the display 9 rows: every Gen9 id in one row of its platform and power class, the south
#                          display's family, the rows' shape
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${1:-build/tmp/ws260-xd9}
fresh_out "$OUT"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
failed=0
for test in host-xd-display9-test; do
	i915_display_host_build "$OUT/$test" "$T/$test.c" "$T/gen9-fake-hw.c" plan/ws084/tests/native-decide-stubs.c
	if ! "$OUT/$test"; then
		failed=1
	fi
done
if [ "$failed" = 0 ]; then
	echo "run-xd9-host-tests: all passed"
fi
exit $failed
