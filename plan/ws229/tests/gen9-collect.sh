#!/bin/sh
# ws229-p005: reads one Gen9 laptop's run back over SSH (the image of plan/ws229/tests/gen9-images.sh, booted from USB
# by the user) and prints the lines the scenario is judged by.  The whole kernel log is kept beside them.
#
#   plan/ws229/tests/gen9-collect.sh HOST SCENARIO LABEL      e.g. gen9-collect.sh 10.0.30.7 eu cfl-8th
#   SSH_USER (default kei) and SSH_PASS (default kei, given with sshpass when it is installed); OUT default
#   build/ws229-gen9/logs.  The machine's log is /var/log/kernel.log (syslogd copies the kernel's ring there every 2 s);
#   /run/dmesg.boot and dmesg are the fallbacks.
#
# Verdicts: eu: "i915: test eu: PASS" and "i915: EU-TEST PASS:"; vkcs, vkx, vke1, vke2, vkc: "i915: <name>: verdict PASS".
# Anything else (FAIL, HANG, a missing line after 3 minutes, a panic) is a FAIL; the log lines say where.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
[ $# -eq 3 ] || { echo "usage: $0 HOST SCENARIO LABEL"; exit 2; }
host=$1
scenario=$2
label=$3
cd "$(dirname -- "$0")/../../.."
out=${OUT:-build/ws229-gen9/logs}
mkdir -p "$out"
log=$out/$scenario-$label.log
user=${SSH_USER:-kei}
ssh_cmd="ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10"
if command -v sshpass > /dev/null 2>&1; then
	ssh_cmd="sshpass -p ${SSH_PASS:-kei} $ssh_cmd"
fi
$ssh_cmd "$user@$host" 'cat /var/log/kernel.log 2>/dev/null || cat /run/dmesg.boot 2>/dev/null || dmesg' > "$log" ||
	{ echo "gen9-collect: no log from $user@$host"; exit 1; }
echo "gen9-collect: $(wc -l < "$log") lines in $log"
echo "--- the GPU"
grep -aE 'i915: .*(8086:|graphics_ver|gen9|Gen9|attach|absent|n0_lite|N0)' "$log" | cut -c1-200 | head -20
echo "--- the scenario"
grep -aE 'i915: (test |ktest|MCR-PROBE (summary|skipped))|i915: .*(verdict|[A-Z0-9-]+ (PASS|FAIL|HANG|ERROR)[:( ]|[A-Z0-9-]+ cleanup|[A-Z0-9-]+ release:)' \
	"$log" | cut -c1-300 | head -100
echo "--- faults"
grep -anE 'panic|fault|GPU hang|wedged' "$log" | grep -v expected_fault | cut -c1-200 | head -20
case $scenario in
eu) grep -aq 'i915: test eu: PASS' "$log" && grep -aq 'i915: EU-TEST PASS:' "$log" ;;
*) grep -aq "i915: $scenario: verdict PASS" "$log" ;;
esac
if [ $? -eq 0 ]; then
	echo "gen9-collect: $scenario on $label PASS"
	exit 0
fi
echo "gen9-collect: $scenario on $label FAIL (no PASS verdict line)"
exit 1
