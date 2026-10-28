#!/bin/sh
# ws291-p010a: mview on the RTX 2070 (GPU_PERF_MACHINE=rtx) with the executor's depth, indexed draws, instances,
# mipmapped textures, uniform buffers and culling, run from the top of the tree:
#   sh plan/ws291/tests/rtx-mview-check.sh OUT VMUNIX
# VMUNIX put in (zedBSD restarted), then mview's default scene of 1 and 5000 copies (game content, mailbox), the
# per-pixel shading of 1 copy (its uniform buffer), mview's still views of 1 and 5000 copies captured, and the kernel's
# lines of a refused command, a full window or space and a GPU fault.  Debian runs again at the end.  OUT/summary.txt
# holds what is printed; the pictures are OUT/shot-1/shot.png and OUT/shot-5000/shot.png.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-mview-check.sh OUT VMUNIX}
kernel=${2:?the kernel}
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

run() {
	name=$1
	shift
	timeout 180 $M mview "$out/$name" "$@" > "$out/$name.txt" 2>&1
	grep -E 'MVIEW (PRESENT|SPIN|FAILED|DONE)|result=' "$out/$name.txt" | sed "s/^/$name /" | tee -a "$summary"
}

$M deploy --kernel "$kernel" > "$out/deploy.txt" 2>&1 || { say "FAIL deploy"; exit 1; }
say "kernel $(cksum < "$kernel")"
run one --token=one --spin=6 --copies=1 --content=game --present=mailbox --images=4
run many --token=many --spin=6 --copies=5000 --content=game --present=mailbox --images=4
run pixel --token=pixel --spin=4 --copies=1 --shading=pixel --content=game --present=mailbox --images=4
for copies in 1 5000; do
	timeout 120 $M shot "$out/shot-$copies" $copies > "$out/shot-$copies.txt" 2>&1
	grep -E 'MVIEW (DONE|FAILED)' "$out/shot-$copies.txt" | sed "s/^/shot-$copies /" | tee -a "$summary"
	[ -s "$out/shot-$copies/shot.png" ] && say "shot-$copies $(wc -c < "$out/shot-$copies/shot.png") bytes"
done
ip=$($M zedbsd 2> /dev/null | tail -1)
if [ -n "$ip" ]; then
	timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" 'dmesg | grep -E "nvrtx: vk|full|fault|MMU" | tail -16' > "$out/kernel.txt" 2>&1
	sed 's/^/kernel /' "$out/kernel.txt" | tee -a "$summary"
fi
$M linux > "$out/linux.txt" 2>&1 || say "FAIL back to Debian"
say "end $(date +%H:%M)"
