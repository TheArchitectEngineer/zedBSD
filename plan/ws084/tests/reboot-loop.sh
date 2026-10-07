#!/bin/sh
# ws084-p003 (L2): reboots the bare Latitude 5330 COUNT times over ssh and checks each boot took the firmware's display over
# and reached the desktop.  The machine runs the demo image from USB (plan/ws075/demo/build-demo-image.sh, whose root
# accepts plan/tmp/guest/id_ed25519); everything is asked of the machine over ssh (dmesg, ps), never read from a console.
#   plan/ws084/tests/reboot-loop.sh HOST COUNT OUTDIR
# Each boot n writes OUTDIR/boot<n>.dmesg and OUTDIR/boot<n>.ps and one line
#   REBOOT-LOOP n ok|fail seconds=S reason=...
# (S: the seconds until ssh answered again).  A boot is ok when its dmesg has "i915: N0 decision: PROCEED",
# "i915: takeover: rc=0", "still active 0x0" and "i915: resident display: picture up", has none of "pipe_off wait timed
# out", "LCD-B preflight: the display is not idle" and "resident display: not started", and ps shows the compositor
# (wayland).  When ssh does not come back within 300 s the loop stops there (reason=no-ssh).  The last line is
#   reboot-loop: OK/COUNT ok
# and the exit status is 0 only when every boot was ok.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
host=${1:?usage: reboot-loop.sh HOST COUNT OUTDIR}
count=${2:?usage: reboot-loop.sh HOST COUNT OUTDIR}
out=${3:?usage: reboot-loop.sh HOST COUNT OUTDIR}
mkdir -p "$out"
key=plan/tmp/guest/id_ed25519
remote() { timeout 30 ssh -i "$key" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 \
	-o BatchMode=yes -o LogLevel=ERROR "root@$host" "$@"; }

# How long to wait for the machine to go down, to come back, and for the session and the compositor to start.
down_limit=60
up_limit=300
settle=30

ok=0
n=1
while [ "$n" -le "$count" ]; do
	# The reboot; the connection may drop before ssh returns, which is not a failure.
	remote /sbin/reboot >/dev/null 2>&1

	# Waits for ssh to stop answering (the machine went down).
	waited=0
	while [ "$waited" -lt "$down_limit" ] && remote true >/dev/null 2>&1; do
		sleep 2
		waited=$((waited + 2))
	done

	# Waits for ssh to answer again, counting the seconds.
	seconds=0
	up=0
	while [ "$seconds" -lt "$up_limit" ]; do
		if remote true >/dev/null 2>&1; then
			up=1
			break
		fi
		sleep 5
		seconds=$((seconds + 5))
	done
	if [ "$up" -ne 1 ]; then
		echo "REBOOT-LOOP $n fail seconds=$seconds reason=no-ssh"
		break
	fi

	# The session and the compositor start, then the boot's log and processes are taken.
	sleep "$settle"
	remote dmesg > "$out/boot$n.dmesg" 2>&1
	remote ps -A -o pid,args > "$out/boot$n.ps" 2>&1

	# The checks of the boot, the first one missing named as the reason.
	reason=
	grep -q 'i915: N0 decision: PROCEED' "$out/boot$n.dmesg" || reason=${reason:-no-n0-proceed}
	grep -q 'i915: takeover: rc=0,' "$out/boot$n.dmesg" || reason=${reason:-no-takeover-rc0}
	grep -q 'i915: takeover: rc=0,.*still active 0x0' "$out/boot$n.dmesg" || reason=${reason:-pipe-still-active}
	grep -q 'i915: resident display: picture up' "$out/boot$n.dmesg" || reason=${reason:-no-picture-up}
	grep -q 'pipe_off wait timed out' "$out/boot$n.dmesg" && reason=${reason:-pipe-off-timeout}
	grep -q 'LCD-B preflight: the display is not idle' "$out/boot$n.dmesg" && reason=${reason:-preflight-not-idle}
	grep -q 'resident display: not started' "$out/boot$n.dmesg" && reason=${reason:-display-not-started}
	grep -qE '(^|[ /])wayland( |$)' "$out/boot$n.ps" || reason=${reason:-no-compositor}
	if [ -z "$reason" ]; then
		ok=$((ok + 1))
		echo "REBOOT-LOOP $n ok seconds=$seconds reason=none"
	else
		echo "REBOOT-LOOP $n fail seconds=$seconds reason=$reason"
	fi

	# The lines a reader compares between the boots.
	grep -E 'N0 decision|takeover:|resident display|pipe_off|LCD-B preflight|DC_off' "$out/boot$n.dmesg" | sed "s/^/  boot$n: /"
	n=$((n + 1))
done

echo "reboot-loop: $ok/$count ok"
[ "$ok" -eq "$count" ]
