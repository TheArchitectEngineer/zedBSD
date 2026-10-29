#!/bin/sh
# ws291-p011: one window on the RTX 2070: KERNEL put in, the gpu-perf bench LIST on zedBSD, RESTORE put back, Debian again.
#   sh plan/ws291/tests/rtx-bench.sh OUT KERNEL RESTORE LIST   (from the top of the tree, in the RDNA machine's lock)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=$1; kernel=$2; restore=$3; list=$4
export GPU_PERF_MACHINE=rtx
M="sh plan/tools/gpu-perf/measure.sh"
KEY=plan/tmp/guest/id_ed25519
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$out"
timeout 400 $M deploy --kernel "$kernel" > "$out/deploy.txt" 2>&1 || { echo "FAIL deploy"; }
echo "kernel $(cksum < "$kernel")"
timeout 1500 $M bench zedbsd "$out/bench" "$list" > "$out/bench.txt" 2>&1; echo "bench rc=$?"
cat "$out/bench.txt" | tail -30
ip=$($M zedbsd 2> /dev/null | tail -1)
if [ -n "$ip" ]; then
	timeout 60 scp -i "$KEY" $SSHOPT "$restore" "root@$ip:/tmp/vmunix.restore" &&
	    timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" 'cp /tmp/vmunix.restore /boot/boot0/vmunix && sync && cksum /boot/boot0/vmunix && grep -c "^nvrtx.vk.perf=" /boot/boot0/zedbsd.cfg' | sed 's/^/restored /'
fi
timeout 400 $M linux > "$out/linux.txt" 2>&1; echo "linux rc=$?"
