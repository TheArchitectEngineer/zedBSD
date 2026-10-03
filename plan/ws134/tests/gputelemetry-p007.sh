#!/bin/sh
# ws134-p007: the kernel's GPU telemetry (sysctl hw.gputelemetry) on a running zedBSD QEMU guest (an image of this tree's
# kernel with sysctl, for example the System Monitor's: plan/ws134/tests/build-monitor-image.sh BUILD, then
# plan/tools/files/files-guest.sh start BUILD/hdd-image.img).  The guest's GPU is Venus, whose driver keeps no
# telemetry, so the value is the header with no GPU: what QEMU can show of the layout.  The i915's entry (its busy
# time, its frequencies) is for the physical machine (the 5330), not here.  Over SSH, never the console:
#  1. sysctl hw.gputelemetry: "gpus=0" and no GPU line.
#  2. hw.cputimes and hw.diskstats still read beside it (the three leaves follow each other); sysctl -a is recorded,
#     not judged (its other names depend on the boot).
#
#   plan/ws134/tests/gputelemetry-p007.sh [OUTDIR]
# Prints "gputelemetry-p007: PASS" or "gputelemetry-p007: FAIL".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws134-p007}
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }

# 1. The value on Venus: the header only.
guest 'sysctl hw.gputelemetry; echo exit=$?' > "$out/telemetry.txt"
if grep -qx 'hw.gputelemetry: gpus=0' "$out/telemetry.txt" && grep -qx 'exit=0' "$out/telemetry.txt" &&
    [ "$(grep -c 'hw.gputelemetry: gpu=' "$out/telemetry.txt")" = 0 ]; then
	echo "ok: hw.gputelemetry: gpus=0 on Venus"
else
	echo "FAILED: hw.gputelemetry (see $out/telemetry.txt)"
	status=1
fi

# 2. The neighbours still read; sysctl -a recorded.
for name in hw.cputimes hw.diskstats; do
	guest "sysctl $name; echo exit=\$?" > "$out/$name.txt"
	if grep -q "^$name: " "$out/$name.txt" && grep -qx 'exit=0' "$out/$name.txt"; then
		echo "ok: $name reads"
	else
		echo "FAILED: $name (see $out/$name.txt)"
		status=1
	fi
done
guest 'sysctl -a; echo exit=$?' > "$out/all.txt"
echo "sysctl -a: $(tail -1 "$out/all.txt") (recorded, not judged)"

[ $status -eq 0 ] && echo "gputelemetry-p007: PASS" || echo "gputelemetry-p007: FAIL"
exit $status
