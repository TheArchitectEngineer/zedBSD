#!/bin/sh
# ws309-p012: two runs of rtx-bug353.sh on the RTX 2070 (10.0.10.3) in one hold of the machine, then the stick as it
# was.  Run from the top of the tree, holding /tmp/zedbsd-rdna.lock:
#   [LIBS=...] sh plan/ws309/tests/rtx-bug353-pair.sh OUT VMUNIX WAYLAND1 WAYLAND2
# First the NVMe kernel to put back is fetched (OUT/restore/vmunix): the stick's /boot/boot0/vmunix, or its
# vmunix.old when the one in place is VMUNIX itself (a run before left it there).  Then rtx-bug353.sh runs with
# WAYLAND1 (KEEP=1, OUT/run1) and with WAYLAND2 (OUT/run2), which at its end puts back the stick's compositor and
# libraries and that kernel and starts Debian.  OUT/summary.txt holds both runs' summaries.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-bug353-pair.sh OUT VMUNIX WAYLAND1 WAYLAND2}
kernel=${2:?the kernel}
wl1=${3:?the first compositor}
wl2=${4:?the second compositor}
M="sh plan/tools/gpu-perf/measure.sh"
KEY=${KEY:-plan/tmp/guest/id_ed25519}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$out/restore"
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
z 'cksum /boot/boot0/vmunix /boot/boot0/vmunix.old' > "$out/restore/nvme.txt" 2>&1
say "nvme $(tr '\n' ' ' < "$out/restore/nvme.txt")"
want=$(cksum < "$kernel" | awk '{print $1}')
now=$(awk '$3 == "/boot/boot0/vmunix" {print $1}' "$out/restore/nvme.txt")
from=/boot/boot0/vmunix
[ "$now" = "$want" ] && from=/boot/boot0/vmunix.old
z "cat $from" > "$out/restore/vmunix"
got=$(cksum < "$out/restore/vmunix" | awk '{print $1}')
expect=$(awk -v f="$from" '$3 == f {print $1}' "$out/restore/nvme.txt")
if [ -z "$expect" ] || [ "$got" != "$expect" ]; then
	say "FAIL the kernel to put back was not fetched ($from: $got, expected $expect)"
	exit 1
fi
say "restore $from $(cksum < "$out/restore/vmunix")"

KEEP=1 sh plan/ws309/tests/rtx-bug353.sh "$out/run1" "$kernel" "$wl1" "$out/restore/vmunix" > "$out/run1.txt" 2>&1
sed 's/^/run1 /' "$out/run1/summary.txt" | grep -v 'KWL MODES' | tee -a "$summary"
sh plan/ws309/tests/rtx-bug353.sh "$out/run2" "$kernel" "$wl2" "$out/restore/vmunix" > "$out/run2.txt" 2>&1
sed 's/^/run2 /' "$out/run2/summary.txt" | grep -v 'KWL MODES' | tee -a "$summary"
say "end $(date +%H:%M)"
