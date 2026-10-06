#!/bin/sh
# ws139-p002: measures the ledger's P-01, P-02 and P-03 in E2 (the Latitude 5330's own QEMU with Venus on the host's
# i915), twice, with p001's perf-run.sh.  Run on centris (the development host) by the tester (U2: T1/T2), never by
# the implementer.  It sends the committed tree's scripts and the image to the 5330 (ssh alias solaris10-man), runs
# plan/ws139/tests/e2-remote.sh there under centris' /tmp/i915-hw.lock (one hour at most), and brings out-1 and out-2
# back to OUT.
#
#   plan/ws139/tests/e2-run.sh IMAGE OUT      (IMAGE: plan/ws139/tests/build-perf-image.sh's hdd-image.img)
#
# The scripts sent are the committed ones (git archive HEAD), so plan/ws139 must have no uncommitted change.  Nothing
# is deleted: each run gets a new directory ~/ws139-e2.XXXXXX on the 5330, which the script prints at the end for
# Q1's clean-up (2026-10-06 user: deleting is Q1's step).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
[ $# -eq 2 ] || { echo "usage: e2-run.sh IMAGE OUT" >&2; exit 2; }
image=$1
out=$2
host=${I915_HOST:-solaris10-man}

# The scripts sent must be the committed ones.
changed=$(git status --porcelain plan/ws139)
if [ -n "$changed" ]; then
	echo "e2-run: FAIL plan/ws139 has uncommitted changes (git archive sends only the committed version)" >&2
	exit 1
fi
commit=$(git rev-parse HEAD)
mkdir -p "$out"
sha256sum "$image" > "$out/image.sha256"

# A new directory on the 5330 for this run.
remote=$(ssh "$host" 'mktemp -d "$HOME/ws139-e2.XXXXXX"')
echo "e2-run: remote directory $host:$remote"

# The scripts perf-run.sh and the steps it calls use, and the guest's SSH key (tracked in git).
git archive HEAD plan/ws139 plan/ws099/tests plan/ws095/tests plan/ws134/tests plan/ws089/tests plan/ws035/tests \
	plan/ws014/tests plan/tools/guest plan/tools/files plan/tools/qmp.py plan/tmp/guest |
	ssh "$host" "tar -x -C '$remote'"

# The image.
scp -q "$image" "$host:$remote/image.img"

# The measurement, under the hardware's lock on centris (passthrough tests wait meanwhile).
status=0
flock /tmp/i915-hw.lock timeout 3600 ssh "$host" "cd '$remote' && PERF_COMMIT=$commit sh plan/ws139/tests/e2-remote.sh" || status=$?

# What it measured, back here.
scp -q -r "$host:$remote/out-1" "$host:$remote/out-2" "$out/" || status=1
echo "e2-run: commit $commit, results in $out, remote directory $host:$remote (for Q1's clean-up), status=$status"
exit $status
