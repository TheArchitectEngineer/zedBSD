#!/bin/sh
# ws291-p010: the RTX 2070's CPU window (BAR1) before and after blobs take their place in it only when the CPU first
# reaches them, on the RDNA machine with the RTX in (GPU_PERF_MACHINE=rtx), run from the top of the tree:
#   sh plan/ws291/tests/rtx-window-ab.sh OUT EAGER_VMUNIX LAZY_VMUNIX
# For each kernel (EAGER: every blob mapped at its creation, LAZY: mapped when the CPU first needs it), with the
# driver's counts on (nvrtx.vk.perf=1): frame-ops on a fresh session, then the clients that failed (the Terminal,
# mview in a window, keiland-shot) and the kernel's window lines (perf: window, window full).  Then, the counts off,
# LAZY's desktop again and the 3D scenes of nvk-scene.list.  Debian runs again at the end.  OUT/summary.txt holds
# what is printed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-window-ab.sh OUT EAGER_VMUNIX LAZY_VMUNIX}
eager=${2:?the eager kernel}
lazy=${3:?the lazy kernel}
export GPU_PERF_MACHINE=rtx
# The zedBSD mview the bench copies in (USB_MVIEW, as measure.sh takes it).
export USB_MVIEW=${USB_MVIEW:?the zedBSD mview to copy in}
M="sh plan/tools/gpu-perf/measure.sh"
KEY=${KEY:-plan/tmp/guest/id_ed25519}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$out"
summary=$out/summary.txt
: > "$summary"

say() {
	echo "$*" | tee -a "$summary"
}
zs() {
	timeout "${ZS_TIMEOUT:-90}" ssh -i "$KEY" $SSHOPT "root@$1" "$2"
}

# probe LABEL IP: the clients that failed, each at most 20 s, and the kernel's window lines.
probe() {
	label=$1
	ip=$2
	zs "$ip" 'U=$(awk -F: "\$3 == 1000 { print \$1; exit }" /etc/passwd)
E="env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0"
(su $U -c "$E timeout 8 /bin/terminal" > /tmp/probe-term.log 2>&1 &)
sleep 9
grep -h FAILED /tmp/probe-term.log | head -3 | sed "s/^/terminal: /"
grep -q FAILED /tmp/probe-term.log || echo "terminal: no FAILED line"
su $U -c "$E timeout 20 /bin/mview --token=probe --spin=4 --copies=1 --content=game --present=mailbox" 2>&1 | grep -E "MVIEW (SPIN|FAILED|DONE)" | sed "s/^/mview: /"
env XDG_RUNTIME_DIR=/run/user/1000 timeout 20 keiland-shot /tmp/probe.png 2>&1 | head -2 | sed "s/^/shot: /"
ls -la /tmp/probe.png 2>/dev/null | sed "s/^/shot: /"
dmesg | grep -E "window full|perf: window" | tail -6' > "$out/$label-probe.txt" 2>&1
	sed "s/^/$label /" "$out/$label-probe.txt" | tee -a "$summary"
	timeout 60 scp -O -i "$KEY" $SSHOPT "root@$ip:/tmp/probe.png" "$out/$label-probe.png" > /dev/null 2>&1
}

# run LABEL VMUNIX: the kernel put in (zedBSD restarted), frame-ops, the probes.
run() {
	label=$1
	kernel=$2
	$M deploy --kernel "$kernel" > "$out/$label-deploy.txt" 2>&1 || { say "$label: FAIL deploy"; return 1; }
	say "$label: kernel $(cksum < "$kernel")"
	$M desktop --keep "$out/$label-desk" > "$out/$label-desk.txt" 2>&1
	python3 -I plan/tools/frame-ops/frame-times.py "$out/$label-desk/session.log" "$out/$label-desk/marks" > "$out/$label-desk/table.txt" 2>&1
	sed "s/^/$label desk /" "$out/$label-desk/table.txt" | tee -a "$summary"
	ip=$($M zedbsd 2> /dev/null | tail -1)
	[ -n "$ip" ] || { say "$label: FAIL no zedBSD"; return 1; }
	probe "$label" "$ip"
}

$M timing on > "$out/timing-on.txt" 2>&1 || say "FAIL timing on"
run eager "$eager"
run lazy "$lazy"
$M timing off > "$out/timing-off.txt" 2>&1 || say "FAIL timing off"
$M desktop --keep "$out/lazy-nocount-desk" > "$out/lazy-nocount-desk.txt" 2>&1
python3 -I plan/tools/frame-ops/frame-times.py "$out/lazy-nocount-desk/session.log" "$out/lazy-nocount-desk/marks" > "$out/lazy-nocount-desk/table.txt" 2>&1
sed "s/^/lazy-nocount desk /" "$out/lazy-nocount-desk/table.txt" | tee -a "$summary"
$M bench zedbsd "$out/bench" plan/ws291/tests/nvk-scene.list > "$out/bench.txt" 2>&1
sed "s/^/bench /" "$out/bench/table.txt" | tee -a "$summary"
grep -h 'KWL SCANOUT' "$out"/bench/*.txt 2>/dev/null | sed -E 's/ at_ms=[0-9]+//' | sort | uniq -c | head -5 | sed 's/^/bench scanout /' | tee -a "$summary"
$M linux > "$out/linux.txt" 2>&1 || say "FAIL back to Debian"
say "end $(date +%H:%M)"
