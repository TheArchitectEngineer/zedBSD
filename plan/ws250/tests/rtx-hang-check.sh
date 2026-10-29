#!/bin/sh
# The hang checks of the nvrtx recovery on the RTX 2070 machine (GPU_PERF_MACHINE=rtx, 10.0.10.3, zedBSD started once
# with UEFI BootNext), in one command: the kernel deployed, then the scenarios, each with a time limit, the logs kept
# in OUT, a summary printed (OUT/summary.txt).
#   S0  mview scene-5000 for 12 s alone, and vkhang --loops (the control): fps, VKHANG ok, the session channel lines
#   S2  vkhang (a draw that never ends) while mview runs 12 s: vkhang loses its device, mview runs to its end
#   S3  S2's vkhang five times, then vkhang --loops: each lost, the channel numbers, the control ok
#   S4  two vkhang at once while mview runs: both lost, mview to its end
#   S5  vkhang killed 0.5 s in (its close while its draw never ends): it ends within 5 s, then vkhang --loops ok
#   S1  (last, with --probe) nvrtx.kill=0 on the boot line: vkhang while mview runs, 10 s watched, nothing stopped;
#       the GSP's own reports and mview's frames are recorded; zedBSD is then restarted without the line
# A desktop shot (keiland-shot) is taken after S2 and at the end.
#   usage: plan/ws250/tests/rtx-hang-check.sh OUT VMUNIX [--probe]   (from the top of the tree)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
OUT=${1:?usage: rtx-hang-check.sh OUT VMUNIX [--probe]}
VMUNIX=${2:?usage: rtx-hang-check.sh OUT VMUNIX [--probe]}
PROBE=${3:-}
export GPU_PERF_MACHINE=${GPU_PERF_MACHINE:-rtx}
MEASURE=plan/tools/gpu-perf/measure.sh
top=$(pwd)
. plan/tools/gpu-perf/conf.sh
KEY=${KEY:-$top/plan/tmp/guest/id_ed25519}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$OUT" "$top/build/tmp"
ZSSHOPT="$SSHOPT -o ControlMaster=auto -o ControlPath=$top/build/tmp/ssh-%h -o ControlPersist=600"
SUMMARY=$OUT/summary.txt
: > "$SUMMARY"
MVIEW_RUN="--spin=12 --content=game --present=mailbox --copies=5000"
AS_USER='su $(awk -F: '"'"'$3 == 1000 { print $1; exit }'"'"' /etc/passwd) -c'
ENVS="env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0"

say() {
	echo "$*" | tee -a "$SUMMARY"
}

# zs IP COMMAND: a command on zedBSD as root, at most ZS_TIMEOUT seconds.
zs() {
	zip=$1
	shift
	timeout "${ZS_TIMEOUT:-60}" ssh -i "$KEY" $ZSSHOPT "root@$zip" "$@"
}

# run NAME LIMIT: the script on stdin run on zedBSD as root (at most LIMIT seconds), its output in OUT/NAME.log.
run() {
	rname=$1
	rlimit=$2
	ZS_TIMEOUT=$rlimit zs "$IP" "cat > /tmp/ws250-$rname.sh && timeout $rlimit sh /tmp/ws250-$rname.sh" > "$OUT/$rname.log" 2>&1
}

# The kernel's lines of the recovery since a scenario began.
KLINES="channel [0-9]+|lost|stopped|stuck|isolat|fault|session channel|shares the work channel|executor fail"

# The kernel in place and zedBSD up.
"$MEASURE" deploy --kernel "$VMUNIX" > "$OUT/deploy.log" 2>&1 || { say "FAIL deploy (deploy.log)"; exit 1; }
IP=$("$MEASURE" zedbsd 2> "$OUT/zedbsd.log") || { say "FAIL zedbsd not up"; exit 1; }
say "zedbsd $IP kernel $(cksum < "$VMUNIX" | cut -d' ' -f1)"
timeout 120 scp -O -i "$KEY" $ZSSHOPT "$USB_VKHANG" "$USB_MVIEW" "root@$IP:/tmp/" > /dev/null 2>&1 || { say "FAIL copy vkhang mview"; exit 1; }
zs "$IP" "chmod 755 /tmp/vkhang /tmp/mview" < /dev/null

# S0: the control.
run s0 120 <<EOS
n=\$(dmesg | wc -l)
$AS_USER "$ENVS /tmp/mview --token=s0 $MVIEW_RUN" 2>&1 | grep -E 'MVIEW (SPIN|STAGES|FAILED)'
$AS_USER "$ENVS /tmp/vkhang --loops" 2>&1
dmesg | tail -n +\$n | grep -E '$KLINES'
EOS
say "S0 $(grep -E 'MVIEW SPIN' "$OUT/s0.log" | tail -1) | $(grep -E 'VKHANG' "$OUT/s0.log" | tail -1) | $(grep -c 'session channel' "$OUT/s0.log") session channel lines"

# S2: vkhang while mview runs.
run s2 120 <<EOS
n=\$(dmesg | wc -l)
$AS_USER "$ENVS /tmp/mview --token=s2 $MVIEW_RUN" > /tmp/s2-mview.log 2>&1 &
sleep 3
$AS_USER "$ENVS /tmp/vkhang" 2>&1
wait
grep -E 'MVIEW (SPIN|STAGES|FAILED)' /tmp/s2-mview.log
$AS_USER "$ENVS /tmp/vkhang --loops" 2>&1
env XDG_RUNTIME_DIR=/run/user/1000 keiland-shot /tmp/s2.png > /dev/null 2>&1
dmesg | tail -n +\$n | grep -E '$KLINES'
EOS
ZS_TIMEOUT=60 zs "$IP" "cat /tmp/s2.png" > "$OUT/s2.png" 2> /dev/null < /dev/null
say "S2 $(grep -E 'VKHANG' "$OUT/s2.log" | head -1) | $(grep -E 'MVIEW SPIN|MVIEW FAILED' "$OUT/s2.log" | tail -1) | control $(grep -E 'VKHANG' "$OUT/s2.log" | tail -1) | $(grep -cE 'stopped' "$OUT/s2.log") stopped, $(grep -cE 'stuck' "$OUT/s2.log") stuck"

# S3: five times.
run s3 180 <<EOS
n=\$(dmesg | wc -l)
for i in 1 2 3 4 5; do
	$AS_USER "$ENVS /tmp/vkhang" 2>&1
done
$AS_USER "$ENVS /tmp/vkhang --loops" 2>&1
dmesg | tail -n +\$n | grep -E '$KLINES'
EOS
say "S3 $(grep -c 'VKHANG lost' "$OUT/s3.log") lost of 5 | control $(grep -E 'VKHANG' "$OUT/s3.log" | tail -1) | channels $(grep -oE 'channel [0-9]+ stopped' "$OUT/s3.log" | cut -d' ' -f2 | tr '\n' ' ')"

# S4: two at once while mview runs.
run s4 120 <<EOS
n=\$(dmesg | wc -l)
$AS_USER "$ENVS /tmp/mview --token=s4 $MVIEW_RUN" > /tmp/s4-mview.log 2>&1 &
sleep 3
$AS_USER "$ENVS /tmp/vkhang" > /tmp/s4-a.log 2>&1 &
$AS_USER "$ENVS /tmp/vkhang" > /tmp/s4-b.log 2>&1
wait
cat /tmp/s4-a.log /tmp/s4-b.log
grep -E 'MVIEW (SPIN|STAGES|FAILED)' /tmp/s4-mview.log
dmesg | tail -n +\$n | grep -E '$KLINES'
EOS
say "S4 $(grep -c 'VKHANG lost' "$OUT/s4.log") lost of 2 | $(grep -E 'MVIEW SPIN|MVIEW FAILED' "$OUT/s4.log" | tail -1)"

# S5: closed while its draw never ends.
run s5 60 <<EOS
n=\$(dmesg | wc -l)
$AS_USER "$ENVS /tmp/vkhang" > /tmp/s5.log 2>&1 &
sleep 0.5
pkill -9 vkhang
start=\$(date +%s)
while pgrep vkhang > /dev/null && [ \$((\$(date +%s) - start)) -lt 20 ]; do sleep 0.2; done
echo "S5ENDED after=\$((\$(date +%s) - start))s still=\$(pgrep vkhang | wc -l)"
$AS_USER "$ENVS /tmp/vkhang --loops" 2>&1
dmesg | tail -n +\$n | grep -E '$KLINES'
EOS
say "S5 $(grep S5ENDED "$OUT/s5.log") | control $(grep -E 'VKHANG' "$OUT/s5.log" | tail -1)"

# The desktop at the end.
ZS_TIMEOUT=30 zs "$IP" "env XDG_RUNTIME_DIR=/run/user/1000 keiland-shot /tmp/end.png > /dev/null 2>&1; cat /tmp/end.png" > "$OUT/end.png" 2> /dev/null < /dev/null
say "shots: $OUT/s2.png $OUT/end.png ($(wc -c < "$OUT/end.png") bytes at the end)"

# S1: the GSP left alone (nvrtx.kill=0), then the line taken out again.
if [ "$PROBE" = --probe ]; then
	"$MEASURE" cfg nvrtx.kill 0 > "$OUT/cfg-probe.log" 2>&1 || { say "FAIL cfg nvrtx.kill=0"; exit 1; }
	IP=$("$MEASURE" zedbsd 2>> "$OUT/zedbsd.log") || { say "FAIL zedbsd not up for S1"; exit 1; }
	timeout 120 scp -O -i "$KEY" $ZSSHOPT "$USB_VKHANG" "$USB_MVIEW" "root@$IP:/tmp/" > /dev/null 2>&1
	zs "$IP" "chmod 755 /tmp/vkhang /tmp/mview" < /dev/null
	run s1 90 <<EOS
n=\$(dmesg | wc -l)
$AS_USER "$ENVS /tmp/mview --token=s1 $MVIEW_RUN" > /tmp/s1-mview.log 2>&1 &
sleep 2
$AS_USER "$ENVS /tmp/vkhang" > /tmp/s1-hang.log 2>&1 &
sleep 10
grep -cE 'MVIEW FRAME' /tmp/s1-mview.log
dmesg | tail -n +\$n | grep -E '$KLINES|channel-stop channel error'
EOS
	say "S1 $(grep -c 'channel error' "$OUT/s1.log") GSP reports | $(grep -E 'lost' "$OUT/s1.log" | head -1)"
	"$MEASURE" cfg nvrtx.kill > "$OUT/cfg-back.log" 2>&1 || say "FAIL cfg back (nvrtx.kill line still on the boot line)"
fi

# Debian back, the RTX on vfio-pci again.
"$MEASURE" linux > "$OUT/linux.log" 2>&1 || say "FAIL back to Debian"
say "done: $OUT"
