#!/bin/sh
# ws245: the host tests of the xd display (display 12 and 13, src/drivers/gpu/i915/display/xd-*.c) on the own register
# model (src/drivers/gpu/i915/tests/display/xd-fake-regs.c), under ASan/UBSan, with the lock order record.
#   sh plan/ws245/tests/run-xd-host-tests.sh [OUT]   (default build/ws245-host)
# Each test is built with the display host library (every display source as production, host-kernel.c's stand-ins;
# plan/ws084/tests/display-host-lib.sh).  The tests:
#   host-xd-regs-test      the register headers against the tree's older headers (pairs of xd-regs-map.h)
#   host-xd-io-test        the register window: access, waits, the write record, the mailbox, the lock order
#   host-xd-platform-test  the platform descriptions: PCI ids, steppings, PCH ids, the description rows
#   host-xd-vbt-test       the VBT, OpRegion and EDID decode on made-up tables, against the old parser on Alder Lake-P
#   host-xd-survey-test    the survey of the first picture on the register model, and the start's record of the display
#                          writes (src/drivers/gpu/i915/mmio-record.c)
# The check against the Linux tree itself is plan/ws245/tests/check-xd-regs-linux.sh (needs the unpacked tree).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${1:-build/ws245-host}
mkdir -p "$OUT"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c src/drivers/gpu/i915/mmio-record.c ${I915_HOST_EXTRA_PRODUCTION:-}"
failed=0
for test in host-xd-regs-test host-xd-io-test host-xd-platform-test host-xd-vbt-test host-xd-survey-test; do
	test -f "$T/$test.c" || continue
	i915_display_host_build "$OUT/$test" "$T/$test.c" "$T/xd-fake-regs.c" plan/ws084/tests/native-decide-stubs.c
	if ! "$OUT/$test"; then
		failed=1
	fi
done
if [ "$failed" -ne 0 ]; then
	echo "run-xd-host-tests: FAILED" >&2
	exit 1
fi
echo "run-xd-host-tests: all passed"
