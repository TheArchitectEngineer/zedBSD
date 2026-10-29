#!/bin/sh
# ws291-p011: the executor's submissions without the wait for the GPU, on the RTX 2070, run from the top of the tree:
#   sh plan/ws291/tests/rtx-async-check.sh OUT RESTORE VMUNIX [hang]
# VMUNIX put in (zedBSD restarted), mview's still view of 1 copy captured on the fresh session (OUT/shot-1), mview's
# scene of 1 and 5000 copies (game content, mailbox) without the counts, then the driver's counts on (nvrtx.vk.perf=1,
# zedBSD restarted) for mview's scene of 5000 copies with libvulkan's submission timing and the driver's perf lines,
# with "hang" the hang check (vkhang) after it, the counts off, RESTORE put back and Debian again.  OUT/summary.txt holds what is
# printed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-async-check.sh OUT RESTORE VMUNIX [hang]}
restore=${2:?the kernel to put back}
kernel=${3:?the kernel}
hang=${4:-}
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
	grep -E 'MVIEW (SPIN|FAILED|DONE)|result=' "$out/$name.txt" | sed "s/^/$name /" | tee -a "$summary"
}

timeout 400 $M deploy --kernel "$kernel" > "$out/deploy.txt" 2>&1 || { say "FAIL deploy"; exit 1; }
say "kernel $(cksum < "$kernel")"
# The still view first: a still view after a spin in the same boot does not show mview's window.
timeout 120 $M shot "$out/shot-1" 1 > "$out/shot-1.txt" 2>&1
grep -E 'MVIEW (DONE|FAILED)|diff|pixels' "$out/shot-1.txt" | sed 's/^/shot-1 /' | tee -a "$summary"
[ -s "$out/shot-1/shot.png" ] && say "shot-1 $out/shot-1/shot.png"
run one --token=one --spin=6 --copies=1 --content=game --present=mailbox --images=4
run many --token=many --spin=12 --copies=5000 --content=game --present=mailbox --images=4
timeout 400 $M timing on > "$out/timing-on.txt" 2>&1 || say "FAIL timing on"
ip=$($M zedbsd 2> /dev/null | tail -1)
if [ -n "$ip" ]; then
	timeout 120 ssh -i "$KEY" $SSHOPT "root@$ip" 'U=$(awk -F: "\$3 == 1000 { print \$1; exit }" /etc/passwd)
n=$(dmesg | wc -l)
su $U -c "env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 VULKAN_ZEDBSD_SUBMIT_TIMING=1 timeout 60 /bin/mview --token=prof --spin=12 --content=game --present=mailbox --images=4 --copies=5000" 2>&1 | grep -E "MVIEW (SPIN|STAGES|FAILED)|VULKAN TIMING" | tail -6
dmesg | tail -n +$n | grep -E "perf|window full|gr after|executor|lost" | tail -20' > "$out/scene.txt" 2>&1
	sed 's/^/scene /' "$out/scene.txt" | tee -a "$summary"
fi
if [ "$hang" = hang ]; then
	timeout 300 $M hang "$out/hang" > "$out/hang.txt" 2>&1
	sed 's/^/hang /' "$out/hang.txt" | tail -20 | tee -a "$summary"
fi
timeout 400 $M timing off > "$out/timing-off.txt" 2>&1 || say "FAIL timing off"
ip=$($M zedbsd 2> /dev/null | tail -1)
if [ -n "$ip" ]; then
	timeout 60 scp -i "$KEY" $SSHOPT "$restore" "root@$ip:/tmp/vmunix.restore" &&
	    timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" 'cp /tmp/vmunix.restore /boot/boot0/vmunix && sync && cksum /boot/boot0/vmunix && grep -c "^nvrtx.vk.perf=" /boot/boot0/zedbsd.cfg' |
	    sed 's/^/restored /' | tee -a "$summary"
fi
$M linux > "$out/linux.txt" 2>&1 || say "FAIL back to Debian"
say "end $(date +%H:%M)"
