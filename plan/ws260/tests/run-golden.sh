#!/bin/sh
# The golden records of the display 9 code: the register writes of every scene of the Gen9 host tests on the register
# model (gen9-fake-hw.c), and the table of the display 9 arithmetic (host-gen9-mode-table.c), compared with the records
# kept in plan/ws260/tests/golden/.  Each stage of the display 9 rewrite runs this: every difference is either a fault
# or an intended change that the stage's phase.md explains.
#   sh plan/ws260/tests/run-golden.sh [OUT]           compare (default OUT build/tmp/ws260-golden)
#   sh plan/ws260/tests/run-golden.sh --record [OUT]  write the records into plan/ws260/tests/golden/
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
record=0
if [ "${1:-}" = "--record" ]; then
	record=1
	shift
fi
T=src/drivers/gpu/i915/tests/display
G=plan/ws260/tests/golden
OUT=${1:-build/tmp/ws260-golden}
fresh_out "$OUT"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
i915_display_host_build "$OUT/modeset" "$T/host-gen9-modeset-test.c" "$T/gen9-fake-hw.c" plan/ws084/tests/native-decide-stubs.c
i915_display_host_build "$OUT/outputs" "$T/host-gen9-outputs-test.c" "$T/gen9-fake-hw.c" plan/ws084/tests/native-decide-stubs.c
i915_display_host_build "$OUT/golden" "$T/host-gen9-golden-test.c" "$T/gen9-fake-hw.c" plan/ws260/tests/golden-stubs.c
i915_display_host_build "$OUT/table" "$T/host-gen9-mode-table.c" plan/ws084/tests/native-decide-stubs.c
"$OUT/modeset" "$OUT/modeset.txt"
"$OUT/outputs" "$OUT/outputs.txt"
"$OUT/golden" "$OUT/backlight.txt"
"$OUT/table" "$OUT/table.txt"
if [ "$record" = 1 ]; then
	mkdir -p "$G"
	for f in modeset outputs backlight; do
		cp "$OUT/$f.txt" "$G/$f.txt"
	done
	gzip -n -9 -c "$OUT/table.txt" > "$G/table.txt.gz"
	echo "golden: recorded into $G"
	exit 0
fi
failed=0
gzip -dc "$G/table.txt.gz" > "$OUT/table.golden.txt"
for f in modeset outputs backlight table; do
	golden=$G/$f.txt
	if [ "$f" = table ]; then
		golden=$OUT/table.golden.txt
	fi
	if cmp -s "$golden" "$OUT/$f.txt"; then
		echo "golden: $f same ($(wc -l < "$OUT/$f.txt") lines)"
	else
		echo "golden: $f DIFFERS"
		diff "$golden" "$OUT/$f.txt" | head -40
		failed=1
	fi
done
exit $failed
