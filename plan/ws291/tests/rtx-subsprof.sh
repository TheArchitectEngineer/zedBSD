#!/bin/sh
# ws291-p011: libvulkan's submission timing (VULKAN_ZEDBSD_SUBMIT_TIMING) of mview subs-100 and big-1000 on the RTX 2070 with KERNEL,
# RESTORE put back, Debian again.  Needs build/ws291/usb/bin/mview.
#   sh plan/ws291/tests/rtx-subsprof.sh OUT KERNEL RESTORE   (from the top of the tree, in the RDNA machine's lock)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=$1; kernel=$2; restore=$3
export GPU_PERF_MACHINE=rtx
M="sh plan/tools/gpu-perf/measure.sh"
KEY=plan/tmp/guest/id_ed25519
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$out"
timeout 400 $M deploy --kernel "$kernel" > "$out/deploy.txt" 2>&1 || echo "FAIL deploy"
ip=$($M zedbsd 2> /dev/null | tail -1)
timeout 60 scp -i "$KEY" $SSHOPT build/ws291/usb/bin/mview "root@$ip:/tmp/mview" > /dev/null 2>&1
timeout 120 ssh -i "$KEY" $SSHOPT "root@$ip" 'U=$(awk -F: "\$3 == 1000 { print \$1; exit }" /etc/passwd)
for a in "--copies=100 --submits=100" "--copies=1000"; do
su $U -c "env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 VULKAN_ZEDBSD_SUBMIT_TIMING=1 timeout 40 /tmp/mview --token=p --spin=6 --content=game --present=mailbox --images=4 --draws=each $a" 2>&1 | grep -E "MVIEW (SPIN|STAGES|FAILED)|VULKAN TIMING" | tail -4
done' > "$out/prof.txt" 2>&1
cat "$out/prof.txt"
timeout 60 scp -i "$KEY" $SSHOPT "$restore" "root@$ip:/tmp/vmunix.restore" &&
    timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" 'cp /tmp/vmunix.restore /boot/boot0/vmunix && sync && cksum /boot/boot0/vmunix' | sed 's/^/restored /'
timeout 400 $M linux > "$out/linux.txt" 2>&1; echo "linux rc=$?"
