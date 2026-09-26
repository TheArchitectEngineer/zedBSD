#!/bin/sh
# ws063: cuts the machine off while the root (journal on by default)
# is being changed, boots the disk it left, and checks that a file made
# durable by sync survived and that the root partition checks clean after
# the replay and a sync.
#   sh plan/tools/ufs/root-crash.sh IMAGE SECONDS   (an SSH guest image with the native layout)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
seconds=$2
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
export GUEST_RUNTIME=${GUEST_RUNTIME:-build/ufs-root-crash-run}
guest=plan/tools/guest/guest.py
work=${WORK:-build/ufs-root-crash}
rm -rf "$work"
mkdir -p "$work"

# Makes a durable file, then churns names on the root until cut off.
python3 $guest stop > /dev/null 2>&1
python3 $guest start --disk nvme "$image" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH"; exit 1; }
python3 $guest run 'echo durable-content > /root/keep.txt && sync &&
	(nohup sh -c "mkdir -p /root/churn; i=0; while :; do echo \$i > /root/churn/f\$i; rm -f /root/churn/f\$((i - 50)); i=\$((i + 1)); done" > /dev/null 2>&1 < /dev/null &)'
sleep "$seconds"
python3 $guest stop > /dev/null
cp --reflink=auto "$GUEST_RUNTIME/disk.img" "$work/crashed.img"

# Boots what the crash left: the root's mount replays its journal.
python3 $guest start --disk nvme "$work/crashed.img" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH after the crash"; exit 1; }
python3 $guest run 'cat /root/keep.txt; echo churn $(ls /root/churn | wc -l); rm -rf /root/churn; sync'
python3 $guest stop > /dev/null

# Checks the root partition (the second GPT partition, 1 MiB + 64 MiB in).
dd if="$GUEST_RUNTIME/disk.img" of="$work/root.ufs" bs=1048576 skip=65 count=1024 status=none
python3 plan/tools/ufs/check-volume.py "$work/root.ufs" | tail -1
