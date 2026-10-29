#!/bin/sh
# ws309-p012: BUG-353 on the RTX 2070 (10.0.10.3, nvrtx): after mview ran in the game mode (mailbox, the display shown
# straight), a later mview window (fifo) is not on the screen.  Run from the top of the tree:
#   [LIB=libvulkan.so] [KEEP=1] sh plan/ws309/tests/rtx-bug353.sh OUT VMUNIX WAYLAND RESTORE
# The stick's /bin/wayland and /lib/libvulkan.so are kept as *.p012old (once); LIB is put in as /lib/libvulkan.so; WAYLAND is loaded once on the stick (its usage line:
# the stick's libraries have what it needs), renamed over /bin/wayland, and VMUNIX put in (zedBSD restarted), then
# the steps of plan/ws291/tests/rtx-mview-order.sh: a still view of 1 copy (a), a spin of 1 copy in the game mode (b),
# a still view of 1 copy (c), a spin of 5000 copies (d), still views of 5000 (e) and of 1 copy (f).  Each step's lines
# of the session's compositor log (/run/user/1000/session.log) are OUT/<step>-session.log.  At the end the stick's
# compositor and RESTORE (the NVMe kernel) are put back and checked by cksum, and Debian runs again (its RTX functions
# on vfio-pci are printed); KEEP=1 leaves the stick as the run made it (zedBSD running), for a second run.
# Prints, for each still view, the pixels 16 or more levels off the Linux reference (over15; the 1-copy views a, c and
# f are judged: under 1000 is the window shown), the compositor's game mode and display lines of b and d, and
# "rtx-bug353: PASS" when a, c and f show the window and no KWL FAILED or VULKAN_ERROR line came; e is printed only
# (no Linux reference of 5000 copies: OUT/e-shot5000-small.png is looked at).  OUT/summary.txt holds what is printed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: rtx-bug353.sh OUT VMUNIX WAYLAND RESTORE}
kernel=${2:?the kernel}
wl=${3:?the compositor}
restore=${4:?the kernel to put back}
export GPU_PERF_MACHINE=rtx
M="sh plan/tools/gpu-perf/measure.sh"
KEY=${KEY:-plan/tmp/guest/id_ed25519}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
SESSION=/run/user/1000/session.log
mkdir -p "$out"
summary=$out/summary.txt
: > "$summary"
failed=0

say() {
	echo "$*" | tee -a "$summary"
}

z() {
	timeout "${ZT:-60}" ssh -i "$KEY" $SSHOPT "root@$ip" "$@"
}

# The session log's length before a step, and its lines of the step after it.
mark() {
	lines=$(z "wc -l < $SESSION" 2> /dev/null | tr -d ' ')
	[ -n "$lines" ] || lines=0
}
collect() {
	z "tail -n +$((lines + 1)) $SESSION" > "$out/$1-session.log" 2>&1
	grep -E 'KWL (SCANOUT|MODE|DISPLAY change|OUTPUT (open|closed)|FAILED|VULKAN_ERROR)' "$out/$1-session.log" | sed "s/^/$1 /" | tee -a "$summary"
	if grep -qE 'KWL (FAILED|VULKAN_ERROR)' "$out/$1-session.log"; then
		failed=1
	fi
}

run() {
	name=$1
	shift
	mark
	timeout 180 $M mview "$out/$name" "$@" > "$out/$name.txt" 2>&1
	grep -E 'MVIEW (SPIN|FAILED)' "$out/$name.txt" | sed "s/^/$name /" | tee -a "$summary"
	collect "$name"
}

shot() {
	name=$1
	mark
	timeout 120 $M shot "$out/$name" "$2" > "$out/$name.txt" 2>&1
	grep -E 'MVIEW (DONE|FAILED)' "$out/$name.txt" | sed "s/^/$name /" | tee -a "$summary"
	over=$(sed -n 's/^over15 //p' "$out/$name.txt")
	say "$name over15=${over:--}"
	if [ "$3" = judge ] && { [ -z "$over" ] || [ "$over" -ge 1000 ]; }; then
		say "$name: the window is not on the screen"
		failed=1
	fi
	if [ -s "$out/$name/shot.png" ]; then
		python3 -I -c 'import sys
from PIL import Image
im = Image.open(sys.argv[1])
im.thumbnail((960, 540))
im.save(sys.argv[2])' "$out/$name/shot.png" "$out/$name-small.png"
	fi
	collect "$name"
}

ip=$($M zedbsd 2> /dev/null | tail -1)
[ -n "$ip" ] || { say "FAIL zedBSD did not come up"; exit 1; }
z "test -f /bin/wayland.p012old || cp -p /bin/wayland /bin/wayland.p012old; test -f /lib/libvulkan.so.p012old || cp -p /lib/libvulkan.so /lib/libvulkan.so.p012old; sync; cksum /bin/wayland.p012old /lib/libvulkan.so.p012old" > "$out/stick-old.txt" 2>&1
sed -n 1p "$out/stick-old.txt" > "$out/wayland-old.txt"
say "stick compositor and libvulkan $(tr '\n' ' ' < "$out/stick-old.txt")"
# LIB: a libvulkan.so put in with the kernel (the kernel's driver and its user side from one tree); renamed over the old one.
if [ -n "${LIB:-}" ]; then
	timeout 120 scp -O -i "$KEY" $SSHOPT "$LIB" "root@$ip:/tmp/libvulkan.p012" > /dev/null 2>&1 &&
	    z 'cp /tmp/libvulkan.p012 /lib/libvulkan.so.p012new && mv /lib/libvulkan.so.p012new /lib/libvulkan.so && sync' ||
	    { say "FAIL libvulkan was not put in"; failed=1; }
	say "libvulkan $(cksum < "$LIB")"
fi
# The compositor runs: the new one is renamed over it (the running one keeps its file), then the kernel and the restart.
timeout 120 scp -O -i "$KEY" $SSHOPT "$wl" "root@$ip:/tmp/wayland.p012" > /dev/null 2>&1 &&
    z 'chmod 755 /tmp/wayland.p012; /tmp/wayland.p012 --no-such-option 2>&1 | head -1' > "$out/wayland-check.txt" 2>&1
if ! grep -q '^usage: wayland' "$out/wayland-check.txt"; then
	say "FAIL the compositor does not load on the stick: $(head -1 "$out/wayland-check.txt")"
	failed=1
fi
if [ $failed = 0 ]; then
	z 'cp /tmp/wayland.p012 /bin/wayland.p012new && chmod 755 /bin/wayland.p012new && mv /bin/wayland.p012new /bin/wayland && sync' ||
	    { say "FAIL the compositor was not put in"; failed=1; }
fi
[ $failed = 0 ] && { $M deploy --kernel "$kernel" > "$out/deploy.txt" 2>&1 || { say "FAIL deploy"; failed=1; }; }
ip=$($M zedbsd 2> /dev/null | tail -1)
if [ $failed = 0 ] && [ -n "$ip" ]; then
	say "kernel $(cksum < "$kernel") compositor $(cksum < "$wl")"
	say "on the stick: $(z 'cksum /bin/wayland /boot/boot0/vmunix' 2>&1 | tr '\n' ' ')"
	shot a-shot1 1 judge
	run b-one --token=one --spin=6 --copies=1 --content=game --present=mailbox --images=4
	shot c-shot1 1 judge
	run d-many --token=many --spin=6 --copies=5000 --content=game --present=mailbox --images=4
	shot e-shot5000 5000 print
	shot f-shot1 1 judge
	z "dmesg | grep -E 'nvrtx: (vk|gr)|fault|MMU' | grep -v 'before the tables' | tail -16" > "$out/kernel.txt" 2>&1
	sed 's/^/kernel /' "$out/kernel.txt" | tee -a "$summary"
fi

# The stick as it was: its compositor and the NVMe kernel, checked (KEEP=1: left as it is, for a second run).
ip=$($M zedbsd 2> /dev/null | tail -1)
if [ "${KEEP:-0}" = 1 ]; then
	say "kept: the stick has the compositor and the kernel of this run"
	say "end $(date +%H:%M)"
	[ $failed = 0 ] && say "rtx-bug353: PASS" || say "rtx-bug353: FAIL"
	exit 0
fi
if [ -n "$ip" ]; then
	timeout 120 scp -O -i "$KEY" $SSHOPT "$restore" "root@$ip:/tmp/vmunix.restore" > /dev/null 2>&1
	z 'cp -p /bin/wayland.p012old /bin/wayland.p012new && mv /bin/wayland.p012new /bin/wayland && cp -p /lib/libvulkan.so.p012old /lib/libvulkan.so.p012new && mv /lib/libvulkan.so.p012new /lib/libvulkan.so && cp /tmp/vmunix.restore /boot/boot0/vmunix && sync && cksum /bin/wayland /lib/libvulkan.so /boot/boot0/vmunix' > "$out/restored.txt" 2>&1
	say "restored $(tr '\n' ' ' < "$out/restored.txt")"
	say "expected $(sed 's/ \/.*//' "$out/stick-old.txt" | tr '\n' ' ') $(cksum < "$restore")"
else
	say "FAIL the stick was not put back: zedBSD did not answer"
	failed=1
fi
$M linux > "$out/linux.txt" 2>&1 || say "FAIL back to Debian"
say "debian $(timeout 60 ssh $SSHOPT awe@10.0.10.3 "lspci -k -d 10de: | grep -c 'in use: vfio-pci'" 2>&1) RTX functions on vfio-pci"
say "end $(date +%H:%M)"
if [ $failed = 0 ]; then
	say "rtx-bug353: PASS"
else
	say "rtx-bug353: FAIL"
fi
