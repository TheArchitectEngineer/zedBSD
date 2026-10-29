#!/bin/sh
# ws309-p012: two runs of rtx-bug353.sh on the RTX 2070 (10.0.10.3) in one hold of the machine, then the stick as it
# was.  Run from the top of the tree, holding /tmp/zedbsd-rdna.lock:
#   [LIBS=...] sh plan/ws309/tests/rtx-bug353-pair.sh OUT VMUNIX WAYLAND1 WAYLAND2 RESTORE
# rtx-bug353.sh runs with WAYLAND1 (KEEP=1, OUT/run1), then with WAYLAND2 (OUT/run2), which at its end puts back the
# stick's compositor and libraries and RESTORE (the NVMe kernel) and starts Debian.  The stick's kernels before the
# first run are printed.  OUT/summary.txt holds both runs' summaries.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-bug353-pair.sh OUT VMUNIX WAYLAND1 WAYLAND2 RESTORE}
kernel=${2:?the kernel}
wl1=${3:?the first compositor}
wl2=${4:?the second compositor}
restore=${5:?the kernel to put back}
M="sh plan/tools/gpu-perf/measure.sh"
KEY=${KEY:-plan/tmp/guest/id_ed25519}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$out"
summary=$out/summary.txt
: > "$summary"

say() {
	echo "$*" | tee -a "$summary"
}

z() {
	timeout 120 ssh -i "$KEY" $SSHOPT "root@$ip" "$@"
}

say "start $(date +%H:%M)"
ip=$($M zedbsd 2> /dev/null | tail -1)
[ -n "$ip" ] || { say "FAIL zedBSD did not come up"; exit 1; }
z 'cksum /boot/boot0/vmunix /boot/boot0/vmunix.old' > "$out/nvme.txt" 2>&1
say "nvme $(tr '\n' ' ' < "$out/nvme.txt")"
say "restore $(cksum < "$restore")"
KEEP=1 sh plan/ws309/tests/rtx-bug353.sh "$out/run1" "$kernel" "$wl1" "$restore" > "$out/run1.txt" 2>&1
sed 's/^/run1 /' "$out/run1/summary.txt" | grep -v 'KWL MODES' | tee -a "$summary"
sh plan/ws309/tests/rtx-bug353.sh "$out/run2" "$kernel" "$wl2" "$restore" > "$out/run2.txt" 2>&1
sed 's/^/run2 /' "$out/run2/summary.txt" | grep -v 'KWL MODES' | tee -a "$summary"
say "end $(date +%H:%M)"
