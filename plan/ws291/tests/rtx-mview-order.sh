#!/bin/sh
# ws291-p010a: whether mview's still view on the RTX 2070 depends on the runs before it, run from the top of the tree:
#   sh plan/ws291/tests/rtx-mview-order.sh OUT VMUNIX
# VMUNIX put in (zedBSD restarted), then a still view of 1 copy, a spin of 1 copy, a still view of 1 copy, a spin of
# 5000 copies, still views of 5000 and of 1 copy, the kernel's executor and fault lines, and Debian again.
# OUT/summary.txt holds what is printed; the pictures are OUT/<step>/shot.png with 960-wide copies OUT/<step>-small.png.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-mview-order.sh OUT VMUNIX}
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
	grep -E 'MVIEW (SPIN|FAILED)|result=' "$out/$name.txt" | sed "s/^/$name /" | tee -a "$summary"
}

shot() {
	name=$1
	timeout 120 $M shot "$out/$name" "$2" > "$out/$name.txt" 2>&1
	grep -E 'MVIEW (DONE|FAILED)' "$out/$name.txt" | sed "s/^/$name /" | tee -a "$summary"
	if [ -s "$out/$name/shot.png" ]; then
		python3 -I -c 'import sys
from PIL import Image
im = Image.open(sys.argv[1])
im.thumbnail((960, 540))
im.save(sys.argv[2])' "$out/$name/shot.png" "$out/$name-small.png"
		say "$name $out/$name-small.png"
	fi
}

$M deploy --kernel "$kernel" > "$out/deploy.txt" 2>&1 || { say "FAIL deploy"; exit 1; }
say "kernel $(cksum < "$kernel")"
shot a-shot1 1
run b-one --token=one --spin=6 --copies=1 --content=game --present=mailbox --images=4
shot c-shot1 1
run d-many --token=many --spin=6 --copies=5000 --content=game --present=mailbox --images=4
shot e-shot5000 5000
shot f-shot1 1
ip=$($M zedbsd 2> /dev/null | tail -1)
if [ -n "$ip" ]; then
	timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" 'dmesg | grep -E "nvrtx: (vk|gr)|full|fault|MMU" | grep -v "before the tables" | tail -16' > "$out/kernel.txt" 2>&1
	sed 's/^/kernel /' "$out/kernel.txt" | tee -a "$summary"
fi
[ "${KEEP_ZEDBSD:-0}" = 1 ] || $M linux > "$out/linux.txt" 2>&1 || say "FAIL back to Debian"
say "end $(date +%H:%M)"
