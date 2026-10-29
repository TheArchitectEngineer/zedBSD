#!/bin/sh
# ws291-p010: the RTX 2070's work channel space after it grew to 3.5 GiB, on the RDNA machine with the RTX in
# (GPU_PERF_MACHINE=rtx), run from the top of the tree:
#   sh plan/ws291/tests/rtx-va-check.sh OUT VMUNIX
# VMUNIX put in (zedBSD restarted), frame-ops on that session, then the clients of the frame-ops' end (the Terminal stays
# open) and mview in it: mview's first failing call (the space was full here before: vkCreateSwapchainKHR; with room it
# goes on to the depth format nvrtx lacks) and the kernel's lines of a full space or window.  Debian runs again at the
# end.  OUT/summary.txt holds what is printed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-va-check.sh OUT VMUNIX}
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

$M deploy --kernel "$kernel" > "$out/deploy.txt" 2>&1 || { say "FAIL deploy"; exit 1; }
say "kernel $(cksum < "$kernel")"
$M desktop --keep "$out/desk" > "$out/desk.txt" 2>&1
python3 -I plan/tools/frame-ops/frame-times.py "$out/desk/session.log" "$out/desk/marks" > "$out/desk/table.txt" 2>&1
sed 's/^/desk /' "$out/desk/table.txt" | tee -a "$summary"
ip=$($M zedbsd 2> /dev/null | tail -1)
[ -n "$ip" ] || { say "FAIL no zedBSD"; exit 1; }
timeout 120 ssh -i "$KEY" $SSHOPT "root@$ip" 'U=$(awk -F: "\$3 == 1000 { print \$1; exit }" /etc/passwd)
E="env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0"
for run in 1 2 3; do
	su $U -c "$E timeout 20 /bin/mview --token=va$run --spin=4 --copies=1 --content=game --present=mailbox --images=4" 2>&1 | grep -E "MVIEW (SPIN|FAILED|DONE)"
done
dmesg | grep -E "work-channel|full" | tail -8' > "$out/probe.txt" 2>&1
sed 's/^/probe /' "$out/probe.txt" | tee -a "$summary"
$M linux > "$out/linux.txt" 2>&1 || say "FAIL back to Debian"
say "end $(date +%H:%M)"
