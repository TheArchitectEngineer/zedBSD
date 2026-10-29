#!/bin/sh
# ws291-p010a: mview's still view of 1 copy on the RTX 2070 under each of several kernels, run from the top of the tree:
#   sh plan/ws291/tests/rtx-depth-probe.sh OUT RESTORE VMUNIX...
# For each VMUNIX: put in (zedBSD restarted), mview's still view captured (OUT/<n>/shot.png and a 960-wide copy),
# the kernel's whole message buffer kept (OUT/<n>/dmesg.txt) and its lines of the executor, the 3D engine and the
# firmware printed.  At the end RESTORE is put back on the disk and Debian runs again.  OUT/summary.txt holds what
# is printed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-depth-probe.sh OUT RESTORE VMUNIX...}
restore=${2:?the kernel to put back}
shift 2
export GPU_PERF_MACHINE=rtx
M="sh plan/tools/gpu-perf/measure.sh"
KEY=${KEY:-plan/tmp/guest/id_ed25519}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$out"
summary=$out/summary.txt
: > "$summary"

say() {
	echo "$*" | tee -a "$summary"
}

n=0
for kernel in "$@"; do
	n=$((n + 1))
	dir=$out/$n
	mkdir -p "$dir"
	say "$n kernel $kernel $(cksum < "$kernel")"
	timeout 400 $M deploy --kernel "$kernel" > "$dir/deploy.txt" 2>&1 || { say "$n FAIL deploy"; continue; }
	timeout 120 $M shot "$dir/shot" 1 > "$dir/shot.txt" 2>&1
	grep -E 'MVIEW (DONE|FAILED)' "$dir/shot.txt" | sed "s/^/$n /" | tee -a "$summary"
	if [ -s "$dir/shot/shot.png" ]; then
		python3 -I -c 'import sys
from PIL import Image
im = Image.open(sys.argv[1])
im.thumbnail((960, 540))
im.save(sys.argv[2])' "$dir/shot/shot.png" "$dir/shot-small.png"
		say "$n shot $dir/shot/shot.png"
	fi
	ip=$($M zedbsd 2> /dev/null | tail -1)
	if [ -n "$ip" ]; then
		timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" dmesg > "$dir/dmesg.txt" 2>&1
		grep -E 'nvrtx: (vk|gr|rpc|executor|channel)|fault' "$dir/dmesg.txt" | grep -v 'before the tables' | tail -12 | sed "s/^/$n /" | tee -a "$summary"
	fi
done
ip=$($M zedbsd 2> /dev/null | tail -1)
if [ -n "$ip" ]; then
	timeout 60 scp -i "$KEY" $SSHOPT "$restore" "root@$ip:/tmp/vmunix.restore" &&
	    timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" 'cp /tmp/vmunix.restore /boot/boot0/vmunix && sync && cksum /boot/boot0/vmunix' |
	    sed 's/^/restored /' | tee -a "$summary"
fi
$M linux > "$out/linux.txt" 2>&1 || say "FAIL back to Debian"
say "end $(date +%H:%M)"
