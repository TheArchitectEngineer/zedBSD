#!/bin/sh
# Debian's side of the depth-32 random read on the Latitude 5330, with the block layer's own account of each run:
# the reads, the mean time a request spent between issue and completion (read_ticks), the mean number in flight
# (time_in_queue over the wall time), and the NVMe interrupts per queue.  Runs on the build host; the 5330 must be
# in Debian.  Takes /tmp/i915-hw.lock.  Nothing is written to the disk and nothing is removed.
#   [SETTLE=1] sh plan/ws308/tests/debian-qdstat.sh RANDREAD_LINUX [SECONDS]
# Prints the randread line and one "qdstat:" line per run, and ends with "debian-qdstat: done".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
RR=${1:?randread-linux}
SECONDS_RUN=${2:-15}
HOST=solaris10-man
cd "$(dirname "$0")/../../.."

lin() {
	timeout "${TO:-120}" ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "$1"
}

exec 9> /tmp/i915-hw.lock
flock 9
lin hostname | grep -q chaos || { echo "debian-qdstat: FAIL: Debian is not up"; exit 1; }
TO=60 timeout 60 scp -q "$RR" "$HOST:ws245/randread-qdstat" || { echo "debian-qdstat: FAIL: copy"; exit 1; }
TO=60 timeout 60 scp -q plan/ws308/tests/qdstat-remote.sh "$HOST:ws245/qdstat-remote.sh" || { echo "debian-qdstat: FAIL: copy"; exit 1; }
# SETTLE=1 first trims and rests the drive as nvme-bench-5330.sh does before each mode.
if [ "${SETTLE:-0}" = 1 ]; then
	TO=600 lin 'sudo -n fstrim -av' > /dev/null
	sleep "${PRE_REST:-60}"
fi
for run in "1 direct notime" "32 direct notime" "32 notime" "32 direct procs notime"; do
	TO=$((SECONDS_RUN + 60)) lin "sudo -n sh \$HOME/ws245/qdstat-remote.sh \$HOME/ws245/randread-qdstat $SECONDS_RUN $run"
done
echo "debian-qdstat: done"
