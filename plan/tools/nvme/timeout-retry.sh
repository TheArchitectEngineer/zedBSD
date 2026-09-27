#!/bin/sh
# ws072 (BUG-059): makes NVMe commands outlive the driver's timeout by
# throttling the second NVMe disk to a few KiB per second with QMP while
# several readers keep many commands queued, then lifts the throttle and
# reports what the readers saw.  Without a retry after the queue's recovery
# the readers fail with ETIMEDOUT (or EIO); with it they finish.
#   sh plan/tools/nvme/timeout-retry.sh IMAGE [BPS] [READERS] [SECONDS]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
bps=${2:-2048}
readers=${3:-12}
seconds=${4:-40}
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
export GUEST_RUNTIME=${GUEST_RUNTIME:-build/nvme-timeout-run}
guest=plan/tools/guest/guest.py
work=${WORK:-build/nvme-timeout}
rm -rf "$work"
mkdir -p "$work"
truncate -s 64M "$work/disk.img"
drive="-drive if=none,id=slow,file=$root/$work/disk.img,format=raw -device nvme,serial=ws072slow,drive=slow"

python3 $guest stop > /dev/null 2>&1
python3 $guest start --disk nvme --qemu-extra "$drive" "$image" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH"; exit 1; }
monitor=$GUEST_RUNTIME/qmp.sock

# Throttles the disk, then starts the readers, each on its own 4 MiB.
limit="{\"device\":\"slow\",\"bps\":$bps,\"bps_rd\":0,\"bps_wr\":0,\"iops\":0,\"iops_rd\":0,\"iops_wr\":0}"
python3 plan/tools/qmp.py "$monitor" block_set_io_throttle "$limit"
python3 $guest run "i=0; while [ \$i -lt $readers ]; do
	(dd if=/dev/nvme1n1 of=/dev/null bs=65536 count=64 skip=\$((i * 64)) > /tmp/r\$i.out 2>&1; echo \"exit \$?\" >> /tmp/r\$i.out) > /dev/null 2>&1 < /dev/null &
	i=\$((i + 1)); done; echo started"
sleep "$seconds"

# Lifts the throttle and collects what the readers saw.
free='{"device":"slow","bps":0,"bps_rd":0,"bps_wr":0,"iops":0,"iops_rd":0,"iops_wr":0}'
python3 plan/tools/qmp.py "$monitor" block_set_io_throttle "$free"
python3 $guest run "sleep 20; for f in /tmp/r*.out; do echo \"== \$f\"; cat \$f; done; dd if=/dev/nvme1n1 of=/dev/null bs=65536 count=16; echo after-exit \$?"
python3 $guest stop > /dev/null
