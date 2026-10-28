#!/bin/sh
# ws291: the power guard of a long run on the RX 5600 XT machine, from the host at the top of the tree.  While the
# command runs, zedBSD's amdrdnactl power line is read every GUARD_SECONDS (20 by default); when it says the
# system-management firmware hung or the thermal fault, the temperature reached GUARD_TEMP (90 C), or the fan stands
# while the driver drives it (the firmware stops it below about 60 C by itself), the GPU's load
# (mview, vkhang, nggshape) is killed, the kernel's log is kept in OUT.dmesg, zedBSD is powered off and the command is
# stopped.  The guard's lines go to OUT.guard; it prints "guard: tripped ..." when it acted.
#   usage: plan/ws291/tests/guard.sh OUT COMMAND [ARG...]
# Each answer's clocks line (the memory clock, the hub's memory state, the underflow) is logged with the power line;
# a round without an answer is logged as "no answer (N)" (the machine is then judged by whoever watches the log).
# The machine's state that made the guard (2026-10-28): the firmware stopped answering, the fan stood at 0 rpm and the
# GPU kept working.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: guard.sh OUT COMMAND [ARG...]}
shift
top=$(pwd)
KEY=${KEY:-$top/plan/tmp/guest/id_ed25519}
ZEDBSD_MAC=${ZEDBSD_MAC:-6c:1f:f7:1a:08:b6}
seconds=${GUARD_SECONDS:-20}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$(dirname "$out")"
: > "$out.guard"

# The power line of the running zedBSD, or nothing when zedBSD does not answer.
power_line() {
	ip=$(ip neigh | awk -v mac="$ZEDBSD_MAC" '$0 ~ mac && $0 !~ /FAILED/ && $1 ~ /^[0-9.]+$/ {print $1; exit}')
	[ -n "$ip" ] || return 1
	timeout 20 ssh -i "$KEY" $SSHOPT "root@$ip" "amdrdnactl | grep '^power \|^clocks '" 2> /dev/null
}

"$@" &
child=$!
tripped=0
silent=0
while kill -0 $child 2> /dev/null; do
	sleep "$seconds"
	lines=$(power_line)
	line=$(echo "$lines" | grep '^power ')
	if [ -z "$line" ]; then
		silent=$((silent + 1))
		echo "$(date +%H:%M:%S) no answer ($silent)" >> "$out.guard"
		continue
	fi
	silent=0
	echo "$(date +%H:%M:%S) $(echo "$lines" | tr '\n' ' ')" >> "$out.guard"
	why=""
	case "$line" in
	*hung=1*) why="smu hung" ;;
	*fault=1*) why="thermal fault" ;;
	esac
	temp=$(echo "$line" | sed -n 's/.* temp=\([0-9]*\)C.*/\1/p')
	if [ -z "$why" ] && [ "${temp:-0}" -ge "${GUARD_TEMP:-90}" ]; then
		why="temperature ${temp}C"
	fi
	duty=$(echo "$line" | sed -n 's/.* duty=\([0-9]*\)%.*/\1/p')
	rpm=$(echo "$line" | sed -n 's/.* fan=\([0-9]*\)rpm.*/\1/p')
	if [ -z "$why" ] && [ "${duty:-0}" -gt 0 ] && [ "${rpm:-1}" = 0 ]; then
		why="fan stands at duty ${duty}%"
	fi
	[ -n "$why" ] || continue
	echo "guard: tripped at $(date +%H:%M:%S): $why ($line)" | tee -a "$out.guard"
	tripped=1
	ip=$(ip neigh | awk -v mac="$ZEDBSD_MAC" '$0 ~ mac && $0 !~ /FAILED/ && $1 ~ /^[0-9.]+$/ {print $1; exit}')
	timeout 20 ssh -i "$KEY" $SSHOPT "root@$ip" "pkill mview; pkill vkhang; pkill nggshape" 2> /dev/null
	timeout 20 ssh -i "$KEY" $SSHOPT "root@$ip" "dmesg" > "$out.dmesg" 2> /dev/null
	timeout 20 ssh -i "$KEY" $SSHOPT "root@$ip" "(sleep 2; poweroff) > /dev/null 2>&1 &" 2> /dev/null
	kill $child 2> /dev/null
	break
done
wait $child 2> /dev/null
exit $tripped
