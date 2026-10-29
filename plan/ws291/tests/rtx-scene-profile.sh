#!/bin/sh
# ws291-p011: where the frame of mview's scene goes on the RTX 2070, run from the top of the tree:
#   sh plan/ws291/tests/rtx-scene-profile.sh OUT RESTORE VMUNIX [COPIES]
# VMUNIX put in, mview's still view of COPIES copies captured on the fresh session (OUT/shot/shot.png), then the
# driver's counts on (nvrtx.vk.perf=1 on the boot line, zedBSD restarted), mview's scene of
# COPIES (default 5000) copies for 12 s with libvulkan's submission timing, the driver's perf lines of the run (every
# 5 s: runs, GPU time, frames and flips), then the counts off, RESTORE put back and Debian again.
# OUT/summary.txt holds what is printed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-scene-profile.sh OUT RESTORE VMUNIX [COPIES]}
restore=${2:?the kernel to put back}
kernel=${3:?the kernel}
copies=${4:-5000}
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

timeout 400 $M deploy --kernel "$kernel" > "$out/deploy.txt" 2>&1 || { say "FAIL deploy"; exit 1; }
say "kernel $(cksum < "$kernel")"
# The still view first: a still view after a spin in the same boot does not show mview's window.
timeout 120 $M shot "$out/shot" "$copies" > "$out/shot.txt" 2>&1
grep -E 'MVIEW (DONE|FAILED)' "$out/shot.txt" | sed 's/^/shot /' | tee -a "$summary"
if [ -s "$out/shot/shot.png" ]; then
	python3 -I -c 'import sys
from PIL import Image
im = Image.open(sys.argv[1])
im.thumbnail((960, 540))
im.save(sys.argv[2])' "$out/shot/shot.png" "$out/shot-small.png"
	say "shot $out/shot-small.png"
fi
timeout 400 $M timing on > "$out/timing-on.txt" 2>&1 || say "FAIL timing on"
ip=$($M zedbsd 2> /dev/null | tail -1)
if [ -n "$ip" ]; then
	timeout 120 ssh -i "$KEY" $SSHOPT "root@$ip" 'U=$(awk -F: "\$3 == 1000 { print \$1; exit }" /etc/passwd)
n=$(dmesg | wc -l)
su $U -c "env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 VULKAN_ZEDBSD_SUBMIT_TIMING=1 timeout 60 /bin/mview --token=prof --spin=12 --content=game --present=mailbox --images=4 --copies='"$copies"'" 2>&1 | grep -E "MVIEW (SPIN|STAGES|FAILED)|VULKAN TIMING" | tail -6
dmesg | tail -n +$n | grep -E "perf|window full|gr after" | tail -16' > "$out/scene.txt" 2>&1
	sed 's/^/scene /' "$out/scene.txt" | tee -a "$summary"
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
