#!/bin/sh
# ws071: runs the guest tests of the phases given (default p002 to p009, p012 to p014, p017 (p015 deleted 2026-10-06), p010) one after another and
# prints each one's verdict and its failed checks.  The guest must be up (files-guest.sh start).
#
#   plan/tools/files/files-regress.sh [OUTDIR] [PHASE...]      (PHASE like p003)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws071-regress}
[ $# -gt 0 ] && shift
phases=${*:-p002 p003 p004 p005 p006 p007 p012 p008 p014 p013 p009 p017 p010}
failed=0
mkdir -p "$(dirname -- "$out")"
for phase in $phases; do
	timeout 600 sh "plan/tools/files/files-$phase.sh" "$out/$phase" > "$out-$phase.log" 2>&1
	grep -E "MISSING|FAIL|PASS" "$out-$phase.log"
	grep -q "files-$phase: PASS" "$out-$phase.log" || failed=1
done
[ $failed = 0 ] && echo "files-regress: PASS ($phases)" || echo "files-regress: FAIL"
exit $failed
