#!/bin/sh
# ws229-p005: reads one Gen9 laptop's run back over SSH (the image of plan/ws229/tests/gen9-images.sh, booted from USB
# by the user) and prints the lines the run is judged by.  The whole kernel log is kept beside them.
#
#   plan/ws229/tests/gen9-collect.sh HOST LABEL       e.g. gen9-collect.sh 10.0.30.7 kbl-r-8250u
#   SSH_USER (default kei) and SSH_PASS (default kei, given with sshpass when it is installed); OUT default
#   build/ws229-gen9/logs; WAIT (default 900) the seconds to wait for the run's verdict line, read again every 30 s.
#   The machine's log is /var/log/kernel.log (syslogd copies the kernel's ring there every 2 s); /run/dmesg.boot and
#   dmesg are the fallbacks.
#
# The run (src/drivers/gpu/i915/tests/execution/sequence.c) ends with "i915: gen9 summary: <scenario> PASS|FAIL|SKIP|
# NOT RUN" for eu, vkcs, vkx, vke1, vke2 and vkc and "i915: gen9: verdict PASS|FAIL (...)".  PASS needs the verdict
# PASS; a FAIL verdict, no verdict line within WAIT, or a panic is a FAIL, and the lines below say where.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
[ $# -eq 2 ] || { echo "usage: $0 HOST LABEL"; exit 2; }
host=$1
label=$2
cd "$(dirname -- "$0")/../../.."
out=${OUT:-build/ws229-gen9/logs}
mkdir -p "$out"
log=$out/gen9-$label.log
user=${SSH_USER:-kei}
wait_s=${WAIT:-900}
ssh_cmd="ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10"
if command -v sshpass > /dev/null 2>&1; then
	ssh_cmd="sshpass -p ${SSH_PASS:-kei} $ssh_cmd"
fi
waited=0
while :; do
	$ssh_cmd "$user@$host" 'cat /var/log/kernel.log 2>/dev/null || cat /run/dmesg.boot 2>/dev/null || dmesg' > "$log" ||
		{ echo "gen9-collect: no log from $user@$host"; exit 1; }
	grep -aq 'i915: gen9: verdict ' "$log" && break
	grep -aq 'panic' "$log" && break
	[ $waited -ge "$wait_s" ] && break
	sleep 30
	waited=$((waited + 30))
done
echo "gen9-collect: $(wc -l < "$log") lines in $log"
echo "--- the GPU"
grep -aE 'i915: .*(8086:|graphics_ver|gen9|Gen9|attach|absent|n0_lite|N0)' "$log" | grep -av 'i915: gen9' | cut -c1-200 | head -20
echo "--- the run"
grep -aE 'i915: (test |gen9[: ]|EU-TEST (PASS|FAIL|HANG|ERROR)|[a-z0-9]+: verdict)' "$log" | cut -c1-300 | head -60
echo "--- faults"
grep -anE 'panic|fault|GPU hang|wedged|did not complete' "$log" | grep -v expected_fault | cut -c1-200 | head -20
if grep -aq 'i915: gen9: verdict PASS' "$log"; then
	echo "gen9-collect: $label PASS"
	exit 0
fi
if grep -aq 'i915: gen9: verdict ' "$log"; then
	echo "gen9-collect: $label FAIL (the summary lines above say which)"
else
	echo "gen9-collect: $label FAIL (no verdict line after ${waited} s)"
fi
exit 1
