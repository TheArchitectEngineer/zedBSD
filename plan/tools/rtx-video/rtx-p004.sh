#!/bin/sh
# The video decoder on the RTX 2070 machine in one command, inside the machine's time slot and lock: the video decoder's
# checks (rtx-video-check.sh, nvrtx.video=1), the recovery's hang checks without the line (plan/ws250/tests/
# rtx-hang-check.sh, the same kernel), then the machine given back: RESTORE put in as zedBSD's kernel and checked,
# Debian running with the RTX on vfio-pci (plan/ws242/tests/rtx-regress.sh restore=).  OUT/summary.txt has the
# three summaries.
#   usage: plan/tools/rtx-video/rtx-p004.sh OUT VMUNIX RESTORE [video|hang|restore]...   (from the top of the tree;
#          no step names: all three)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
OUT=${1:?usage: rtx-p004.sh OUT VMUNIX RESTORE [STEP]...}
VMUNIX=${2:?usage: rtx-p004.sh OUT VMUNIX RESTORE [STEP]...}
RESTORE=${3:?usage: rtx-p004.sh OUT VMUNIX RESTORE [STEP]...}
shift 3
STEPS=${*:-video hang restore}
export GPU_PERF_MACHINE=${GPU_PERF_MACHINE:-rtx}
export PERF_BUILD=${PERF_BUILD:-build/rtx-video}
mkdir -p "$OUT"
SUMMARY=$OUT/summary.txt
: > "$SUMMARY"

say() {
	echo "$(date +%H:%M:%S) $*" | tee -a "$SUMMARY"
}

for step in $STEPS; do
	say "step $step"
	case $step in
	video)
		timeout 1800 sh plan/tools/rtx-video/rtx-video-check.sh "$OUT/video" "$VMUNIX" > "$OUT/video.out" 2>&1
		sed 's/^/    /' "$OUT/video/summary.txt" >> "$SUMMARY"
		;;
	hang)
		timeout 1800 sh plan/ws250/tests/rtx-hang-check.sh "$OUT/hang" "$VMUNIX" > "$OUT/hang.out" 2>&1
		sed 's/^/    /' "$OUT/hang/summary.txt" >> "$SUMMARY"
		;;
	restore)
		timeout 1200 sh plan/ws242/tests/rtx-regress.sh "$OUT/restore" "$VMUNIX" "restore=$RESTORE" > "$OUT/restore.out" 2>&1
		sed 's/^/    /' "$OUT/restore/summary.txt" >> "$SUMMARY"
		;;
	*)
		say "unknown step $step"
		;;
	esac
done
cat "$SUMMARY"
