#!/bin/sh
# The nvrtx regression on the RTX 2070 machine (GPU_PERF_MACHINE=rtx, 10.0.10.3: Debian, zedBSD started once with
# UEFI BootNext), step by step, each step's logs in OUT/STEP and one line per step in OUT/summary.txt:
#   hang            plan/ws250/tests/rtx-hang-check.sh OUT/hang VMUNIX: the kernel put in, mview scene-5000 (the
#                   control's fps), the hang scenarios, the desktop shot; it ends in Debian
#   boot            zedBSD started: the driver's lines (nvrtx:), the session's socket, a desktop shot
#   stop=STAGE      nvrtx.stop=STAGE on the boot line (no nvrtx.fail line), zedBSD restarted: as boot
#   fail=POINT      nvrtx.fail=POINT on the boot line (no nvrtx.stop line), zedBSD restarted: as boot
#   warm            the boot line without nvrtx.stop and nvrtx.fail, zedBSD restarted (through Debian, a warm
#                   reboot): as boot (after a fail, the region left asks for a power cycle)
#   cycle           Debian, the machine's power cycled (SwitchBot plug AC276E42B252), zedBSD started: as boot
#   restore=VMUNIX  the boot line without the test lines, VMUNIX put in, Debian: the kernel's cksum on zedBSD and
#                   the RTX's driver on Debian
#   usage: plan/ws242/tests/rtx-regress.sh OUT VMUNIX STEP...   (from the top of the tree)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
OUT=${1:?usage: rtx-regress.sh OUT VMUNIX STEP...}
VMUNIX=${2:?usage: rtx-regress.sh OUT VMUNIX STEP...}
shift 2
export GPU_PERF_MACHINE=${GPU_PERF_MACHINE:-rtx}
MEASURE="sh plan/tools/gpu-perf/measure.sh"
top=$(pwd)
. plan/tools/gpu-perf/conf.sh
KEY=${KEY:-$top/plan/tmp/guest/id_ed25519}
PLUG=${PLUG:-AC276E42B252}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$OUT" "$top/build/tmp"
ZSSHOPT="$SSHOPT -o ControlMaster=auto -o ControlPath=$top/build/tmp/ssh-%h -o ControlPersist=600"
SUMMARY=$OUT/summary.txt

say() {
	echo "$(date +%H:%M:%S) $*" | tee -a "$SUMMARY"
}

zs() {
	zip=$1
	shift
	timeout "${ZS_TIMEOUT:-60}" ssh -i "$KEY" $ZSSHOPT "root@$zip" "$@"
}

deb() {
	timeout "${ZS_TIMEOUT:-60}" ssh $SSHOPT "$DEBIAN" "$@"
}

# look NAME: the running zedBSD's driver lines, session and desktop shot in OUT/NAME, one summary line.
look() {
	lname=$1
	ldir=$OUT/$lname
	mkdir -p "$ldir"
	lip=$($MEASURE zedbsd 2> "$ldir/zedbsd.log") || { say "$lname FAIL zedBSD not up (ssh)"; return 1; }
	zs "$lip" "uname -v; grep -v '^#' /boot/boot0/zedbsd.cfg; cksum < /boot/boot0/vmunix" > "$ldir/system.txt" 2>&1 < /dev/null
	zs "$lip" "dmesg | grep 'nvrtx'" > "$ldir/nvrtx.log" 2>&1 < /dev/null
	zs "$lip" "test -S /run/user/1000/wayland-0 && echo session-socket; ls /dev | grep -iE 'gpu|fb|dri' | tr '\\n' ' '" > "$ldir/session.txt" 2>&1 < /dev/null
	ZS_TIMEOUT=30 zs "$lip" "env XDG_RUNTIME_DIR=/run/user/1000 keiland-shot /tmp/look.png > /dev/null 2>&1; cat /tmp/look.png" > "$ldir/shot.png" 2> /dev/null < /dev/null
	say "$lname zedBSD $lip | kernel $(tail -1 "$ldir/system.txt" | cut -d' ' -f1) | $(grep -cE 'session-socket' "$ldir/session.txt") session | shot $(wc -c < "$ldir/shot.png") bytes"
	grep -E 'gsp-init (ok|fail)|frts (ok|stop|fail)|booter (ok|fail)|takeover|skipped|fail on purpose|bus master off|not yet seen|GSP stays up|unload|layout (ok|fail|skipped)|gpu-device' "$ldir/nvrtx.log" | cut -c1-200 | sed 's/^/    /' | tee -a "$SUMMARY"
}

# setcfg [NAME=VALUE]: the running zedBSD's boot line without the nvrtx.stop and nvrtx.fail lines, then the line given.
setcfg() {
	cip=$($MEASURE zedbsd 2>> "$OUT/measure.log") || { say "FAIL zedBSD not up for the boot line"; return 1; }
	zs "$cip" "sed -i -e '/^nvrtx\\.stop=/d' -e '/^nvrtx\\.fail=/d' /boot/boot0/zedbsd.cfg && sync" < /dev/null || return 1
	if [ -n "${1:-}" ]; then
		zs "$cip" "echo '$1' >> /boot/boot0/zedbsd.cfg && sync" < /dev/null || return 1
	fi
	say "boot line: $(zs "$cip" "grep -E '^nvrtx\\.' /boot/boot0/zedbsd.cfg" < /dev/null | tr '\n' ' ')"
}

# restart: zedBSD through Debian (a warm reboot each way).
restart() {
	$MEASURE linux >> "$OUT/measure.log" 2>&1 || { say "FAIL Debian not up"; return 1; }
}

for step in "$@"; do
	say "step $step"
	case $step in
	hang)
		sh plan/ws250/tests/rtx-hang-check.sh "$OUT/hang" "$VMUNIX" > "$OUT/hang.out" 2>&1
		sed 's/^/    /' "$OUT/hang/summary.txt" | tee -a "$SUMMARY"
		;;
	boot)
		look boot
		;;
	stop=*|fail=*)
		setcfg "nvrtx.$step" && restart && look "$step"
		;;
	warm)
		setcfg && restart && look warm
		;;
	cycle)
		restart || continue
		python3 -I plan/tools/switchbot.py cycle "$PLUG" > "$OUT/switchbot.log" 2>&1
		say "power cycled: $(tail -1 "$OUT/switchbot.log")"
		sleep 60
		end=$(($(date +%s) + 300))
		until [ "$(ZS_TIMEOUT=15 deb uname -s 2> /dev/null)" = Linux ] || [ "$(date +%s)" -ge $end ]; do sleep 10; done
		look cycle
		;;
	restore=*)
		back=${step#restore=}
		setcfg
		$MEASURE deploy --kernel "$back" > "$OUT/restore.log" 2>&1 || say "FAIL deploy $back"
		rip=$($MEASURE zedbsd 2>> "$OUT/measure.log")
		say "restore: zedBSD kernel $(zs "$rip" "cksum < /boot/boot0/vmunix" < /dev/null | cut -d' ' -f1), wanted $(cksum < "$back" | cut -d' ' -f1)"
		restart
		say "Debian: $(deb "uname -r; lspci -nnk -d 10de: | grep 'driver in use' | sort | uniq -c | tr -s ' '; efibootmgr | grep BootOrder" | tr '\n' ' ')"
		;;
	*)
		say "unknown step $step"
		;;
	esac
done
say "where: $($MEASURE where 2> /dev/null)"
