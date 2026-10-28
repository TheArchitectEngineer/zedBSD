#!/bin/sh
# gpu-perf amdrdna: the short check of the memory clock that follows the load (amdrdna.smu.memory=2) on the RX 5600 XT machine,
# from the host at the top of the tree, under the power guard (guard.sh): CYCLES times mview's scene of COPIES copies
# for LOAD seconds (MODE game: game mode, mailbox; window: composed, FIFO), then IDLE seconds; amdrdnactl's clocks line
# read every second the whole time with the count of the kernel's underflow marks so far (OUT/clocks.log).  Prints each cycle's memory clocks under load and at idle, the
# fps, and the kernel's "memory display" lines (the display step and the underflows the power watch counted).
#   usage: plan/tools/gpu-perf/amdrdna/dpm-check.sh OUT [CYCLES [LOAD [IDLE [MODE [COPIES]]]]]   (default 4 6 12 game 1500)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: dpm-check.sh OUT [CYCLES [LOAD [IDLE [MODE [COPIES]]]]]}
cycles=${2:-4}
load=${3:-6}
idle=${4:-12}
mode=${5:-game}
copies=${6:-1500}
m=plan/tools/gpu-perf/measure.sh
mkdir -p "$out"
if [ -z "${DPM_GUARDED:-}" ]; then
	DPM_GUARDED=1 exec sh plan/tools/gpu-perf/guard.sh "$out/dpm" sh "$0" "$out" "$cycles" "$load" "$idle" "$mode" "$copies"
fi
top=$(pwd)
. plan/tools/gpu-perf/conf.sh
KEY=${KEY:-$top/plan/tmp/guest/id_ed25519}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
ip=$(ip neigh | awk -v mac="$ZEDBSD_MAC" '$0 ~ mac && $0 !~ /FAILED/ && $1 ~ /^[0-9.]+$/ {print $1; exit}')
[ -n "$ip" ] || { echo "dpm-check: zedBSD does not answer"; exit 1; }
zs() {
	timeout 20 ssh -i "$KEY" $SSHOPT "root@$ip" "$@"
}
if [ "$mode" = game ]; then
	opts="--content=game --present=mailbox --images=4"
else
	opts="--present=fifo"
fi
# The clocks line every second, on zedBSD, for the whole run.
total=$((cycles * (load + idle + 8) + 10))
zs "i=0; while [ \$i -lt $total ]; do echo \"\$(date +%H:%M:%S) \$(amdrdnactl | grep '^clocks ') marks=\$(dmesg | grep -c 'memory display: underflow')\"; sleep 1; i=\$((i + 1)); done > /tmp/dpm-clocks.log 2>&1 < /dev/null &"
sleep 5
: > "$out/cycles.log"
cycle=0
while [ $cycle -lt "$cycles" ]; do
	cycle=$((cycle + 1))
	start=$(zs date +%H:%M:%S)
	timeout 300 sh "$m" mview "$out/cycle-$cycle" --spin="$load" $opts --copies="$copies" > "$out/cycle-$cycle.out" 2>&1
	fps=$(sed -n 's/.*MVIEW SPIN.* fps=\([0-9.]*\).*/\1/p' "$out/cycle-$cycle.out" | tail -1)
	end=$(zs date +%H:%M:%S)
	sleep "$idle"
	echo "$cycle $start $end ${fps:-none}" >> "$out/cycles.log"
done
sleep 3
zs "cat /tmp/dpm-clocks.log" > "$out/clocks.log"
zs "dmesg | grep 'memory display'" > "$out/memory-display.log"
# Each cycle: the memory clocks seen while mview ran, then in the idle after it.
while read -r cycle start end fps; do
	under=$(awk -v s="$start" -v e="$end" '$1 >= s && $1 <= e {for (i = 2; i <= NF; i++) if ($i ~ /^mem=/) print $i}' "$out/clocks.log" | sort | uniq -c | tr '\n' ' ')
	after=$(awk -v e="$end" -v n="$cycle" '$1 > e {for (i = 2; i <= NF; i++) if ($i ~ /^mem=/) print $i}' "$out/clocks.log" | head -"$idle" | sort | uniq -c | tr '\n' ' ')
	echo "cycle $cycle fps ${fps}: load [$under] idle [$after]"
done < "$out/cycles.log"
grep -c 'underflow' "$out/memory-display.log" | sed 's/^/underflow marks: /'
tail -3 "$out/memory-display.log"
