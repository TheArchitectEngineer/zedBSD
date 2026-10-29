#!/bin/sh
# ws291-p010: the desktop's frames on the RTX 2070 with their parts, run from the top of the tree:
#   sh plan/ws291/tests/rtx-desk-profile.sh OUT RESTORE VMUNIX [perf]
# VMUNIX put in (zedBSD restarted), frame-ops on a fresh session (OUT/desk: the table and every frame of launch, hover
# and drag), then with "perf" the driver's counts on (nvrtx.vk.perf=1, zedBSD restarted) and frame-ops again
# (OUT/perf: the same, and the driver's perf lines of the run with the session's clock at each step), the counts off,
# RESTORE put back and Debian again.  OUT/summary.txt holds what is printed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-desk-profile.sh OUT RESTORE VMUNIX [perf]}
restore=${2:?the kernel to put back}
kernel=${3:?the kernel}
perf=${4:-}
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

# desk NAME: frame-ops on a fresh session into OUT/NAME, its table and the frames of the slow steps.
desk() {
	timeout 600 $M desktop "$out/$1" > "$out/$1.txt" 2>&1 || say "$1 FAIL desktop"
	sed "s/^/$1 /" "$out/$1/table.txt" | tee -a "$summary"
	python3 -I plan/tools/frame-ops/frame-times.py "$out/$1/session.log" "$out/$1/marks" --frames > "$out/$1/frames.txt" 2>&1
}

timeout 400 $M deploy --kernel "$kernel" > "$out/deploy.txt" 2>&1 || { say "FAIL deploy"; exit 1; }
say "kernel $(cksum < "$kernel")"
desk desk
if [ "$perf" = perf ]; then
	timeout 400 $M timing on > "$out/timing-on.txt" 2>&1 || say "FAIL timing on"
	desk perf
	ip=$($M zedbsd 2> /dev/null | tail -1)
	if [ -n "$ip" ]; then
		timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" 'dmesg | grep -E "perf|full|lost|takeover fail"' > "$out/perf/kernel.txt" 2>&1
		grep -cE 'perf [0-9]+ms' "$out/perf/kernel.txt" | sed 's/^/perf periods /' | tee -a "$summary"
	fi
	timeout 400 $M timing off > "$out/timing-off.txt" 2>&1 || say "FAIL timing off"
fi
ip=$($M zedbsd 2> /dev/null | tail -1)
if [ -n "$ip" ]; then
	timeout 60 scp -i "$KEY" $SSHOPT "$restore" "root@$ip:/tmp/vmunix.restore" &&
	    timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" 'cp /tmp/vmunix.restore /boot/boot0/vmunix && sync && cksum /boot/boot0/vmunix && grep -c "^nvrtx.vk.perf=" /boot/boot0/zedbsd.cfg' |
	    sed 's/^/restored /' | tee -a "$summary"
fi
timeout 300 $M linux > "$out/linux.txt" 2>&1 || say "FAIL back to Debian"
say "end $(date +%H:%M)"
