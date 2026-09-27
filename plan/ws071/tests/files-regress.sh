#!/bin/sh
# ws071: runs the guest tests of the phases given (default p002 to p007) one after another and
# prints each one's verdict and its failed checks.  The guest must be up (files-guest.sh start).
#
#   plan/ws071/tests/files-regress.sh [OUTDIR] [PHASE...]      (PHASE like p003)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws071-regress}
[ $# -gt 0 ] && shift
phases=${*:-p002 p003 p004 p005 p006 p007}
failed=0
mkdir -p "$(dirname -- "$out")"
for phase in $phases; do
	timeout 600 sh "plan/ws071/tests/files-$phase.sh" "$out/$phase" > "$out-$phase.log" 2>&1
	grep -E "MISSING|FAIL|PASS" "$out-$phase.log"
	grep -q "files-$phase: PASS" "$out-$phase.log" || failed=1
done
[ $failed = 0 ] && echo "files-regress: PASS ($phases)" || echo "files-regress: FAIL"
exit $failed
