#!/bin/sh
# Reno against CUBIC on the VisionFive 2, the board sending to the 5330's Debian over the switch, optionally through a
# path the 5330 makes on its ingress (an ifb device with netem: delay, rate, loss, queue limit), which only the board's
# data takes (its acknowledgements come back straight).  The board runs the kernel already on it (put it there with
# plan/ws221/tests/vf2-tcp.sh --kernel); this copies tcp-perf to its /tmp and runs ROUNDS rounds of reno then cubic
# (tcp-perf --cc), each SECONDS long, against plan/ws221/tests/perf-peer.py on the 5330.  Judged from the programs' own
# lines over SSH, never the console.
#   plan/ws311/tests/vf2-cc.sh [--bin DIR] [--seconds N] [--rounds N] [--netem "ARGS"] NAME
# --netem ARGS: the netem qdisc's arguments on the 5330's ifb device (for example "delay 80ms rate 35mbit loss 0.004%
# limit 240"); with it the 5330's adapter's GRO is off for the run.  Before, the 5330 must have no ingress qdisc and no
# ifb module loaded; at the end (also on failure, trap EXIT) the ingress qdisc and the ifb device go, the module is
# unloaded, GRO is as it was, and what the 5330 shows then is read back into the summary.  Everything put on the 5330
# goes in one new directory there (mktemp -d), whose path the summary gives (nothing is removed there).
# The output is build/ws311-vf2/NAME (plan/tools/fresh-out.sh): summary.txt and each run's lines.  The last lines give
# each control's median, least and most Mbps and the ratio of the medians; "vf2-cc: PASS" when every run moved bytes.
# VF2_HOST (default kei@10.0.30.2), VF2_PASSWORD (default kei); PEER (default awe@10.0.30.3), PEER_IF
# (enx6c1ff706148a), PEER_ADDRESS (10.0.30.3).  Run it under flock /tmp/i915-hw.lock (the 5330 is shared).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
bin=build/vf2/bin
seconds=30
rounds=5
netem=
while [ $# -gt 1 ]; do
	case $1 in
	--bin) bin=$2 ;;
	--seconds) seconds=$2 ;;
	--rounds) rounds=$2 ;;
	--netem) netem=$2 ;;
	*) echo "vf2-cc: unknown option $1"; exit 2 ;;
	esac
	shift 2
done
name=${1:?usage: vf2-cc.sh [--bin DIR] [--seconds N] [--rounds N] [--netem ARGS] NAME}
host=${VF2_HOST:-kei@10.0.30.2}
export SSHPASS="${VF2_PASSWORD:-kei}"
peer=${PEER:-awe@10.0.30.3}
peer_if=${PEER_IF:-enx6c1ff706148a}
server_ip=${PEER_ADDRESS:-10.0.30.3}
port=5311
so="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8"
. plan/tools/fresh-out.sh
fresh_out "build/ws311-vf2/$name"
out=build/ws311-vf2/$name

say() { echo "vf2-cc: $*" | tee -a "$out/summary.txt"; }
board() { timeout "${T:-60}" sshpass -e ssh $so "$host" "$1" 2>&1 | tr -d '\r'; }
on_peer() { timeout "${T:-60}" ssh -o BatchMode=yes "$peer" "$1" 2>&1; }

# The 5330's state after a run: its qdiscs, any ifb device, the module, GRO.
peer_state() {
	on_peer "/usr/sbin/tc qdisc show dev $peer_if; ip -o link show type ifb 2>/dev/null | wc -l | sed 's/^/ifb devices: /';
	    grep -c '^ifb ' /proc/modules | sed 's/^/ifb module: /';
	    sudo -n /usr/sbin/ethtool -k $peer_if | grep generic-receive-offload" | tr '\n' ';'
}

# Takes the path away again: the ingress qdisc, the ifb device and module, and GRO as it was.
restore_peer() {
	[ -n "$netem" ] || return 0
	on_peer "sudo -n /usr/sbin/tc qdisc del dev $peer_if ingress 2>/dev/null; sudo -n ip link del ifb311 2>/dev/null;
	    sudo -n modprobe -r ifb 2>/dev/null; sudo -n /usr/sbin/ethtool -K $peer_if gro $gro_before" > /dev/null
	say "5330 after: $(peer_state)"
}

peer_dir=$(on_peer 'mktemp -d /tmp/vf2-cc.XXXXXX')
[ -n "$peer_dir" ] || { say "FAIL: no directory on the 5330"; exit 1; }
say "5330's files: $peer:$peer_dir"
timeout 60 scp -q plan/ws221/tests/perf-peer.py "$peer:$peer_dir/" || { say "FAIL: copy of the peer"; exit 1; }
timeout 60 sshpass -e scp $so "$bin/tcp-perf" "$bin/sysctl" "$host:/tmp/" || { say "FAIL: copy to the board"; exit 1; }
say "board: $(board 'uname -a')"
gro_before=on
if [ -n "$netem" ]; then
	before=$(peer_state)
	say "5330 before: $before"
	case $before in
	*ingress*|*"ifb devices: "[1-9]*|*"ifb module: "[1-9]*) say "FAIL: the 5330 already has an ingress qdisc or ifb"; exit 1 ;;
	esac
	case $before in
	*"generic-receive-offload: off"*) gro_before=off ;;
	esac
	trap restore_peer EXIT
	on_peer "sudo -n modprobe ifb numifbs=0 && sudo -n ip link add ifb311 type ifb && sudo -n ip link set ifb311 up &&
	    sudo -n /usr/sbin/ethtool -K $peer_if gro off &&
	    sudo -n /usr/sbin/tc qdisc add dev $peer_if handle ffff: ingress &&
	    sudo -n /usr/sbin/tc filter add dev $peer_if parent ffff: matchall action mirred egress redirect dev ifb311 &&
	    sudo -n /usr/sbin/tc qdisc add dev ifb311 root netem $netem && /usr/sbin/tc qdisc show dev ifb311" > "$out/netem.txt"
	grep -q netem "$out/netem.txt" || { say "FAIL: netem: $(tr '\n' ' ' < "$out/netem.txt")"; exit 1; }
	say "path: $(tr '\n' ' ' < "$out/netem.txt")"
fi
say "server=$server_ip seconds=$seconds rounds=$rounds"

status=0
round=1
while [ $round -le "$rounds" ]; do
	for cc in reno cubic; do
		T=$((seconds + 60)) on_peer "f=\$(mktemp $peer_dir/perf-peer.XXXXXX); python3 -I $peer_dir/perf-peer.py $server_ip $port \$f 1; cat \$f" \
		    > "$out/peer-$cc-$round.log" &
		job=$!
		sleep 1
		T=$((seconds + 40)) board "/tmp/tcp-perf --cc $cc send $server_ip $port $seconds; echo exit=\$?" > "$out/client-$cc-$round.txt"
		wait "$job"
		T=20 board "/tmp/sysctl net.tcpstats" > "$out/tcpstats-$cc-$round.txt"
		client=$(grep '^TCP-PERF' "$out/client-$cc-$round.txt")
		mbps=$(echo "$client" | sed -n 's/.* mbps=\([0-9.]*\).*/\1/p')
		echo "$cc $mbps" >> "$out/rates.txt"
		say "round $round $cc: ${client:-no result} | 5330 $(grep '^done' "$out/peer-$cc-$round.log")"
		[ -n "$mbps" ] || status=1
	done
	round=$((round + 1))
done

# Each control's median, least and most, and the ratio of the medians.
for cc in reno cubic; do
	awk -v c="$cc" '$1 == c { v[++n] = $2 } END { if (n == 0) { print c ": none"; exit }
	    for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (v[j] < v[i]) { t = v[i]; v[i] = v[j]; v[j] = t }
	    printf "%s: median=%s least=%s most=%s runs=%d\n", c, v[int((n + 1) / 2)], v[1], v[n], n }' "$out/rates.txt" |
	    while read -r line; do say "$line"; done
done
reno=$(awk '$1 == "reno" { v[++n] = $2 } END { for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (v[j] < v[i]) { t = v[i]; v[i] = v[j]; v[j] = t } print v[int((n + 1) / 2)] }' "$out/rates.txt")
cubic=$(awk '$1 == "cubic" { v[++n] = $2 } END { for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (v[j] < v[i]) { t = v[i]; v[i] = v[j]; v[j] = t } print v[int((n + 1) / 2)] }' "$out/rates.txt")
say "ratio cubic/reno of the medians: $(awk -v a="$cubic" -v b="$reno" 'BEGIN { if (b > 0) printf "%.3f", a / b }')"
[ $status = 0 ] && say PASS || say FAIL
exit $status
