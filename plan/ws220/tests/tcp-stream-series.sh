#!/bin/sh
# ws220-p001: a long TCP transfer each way between a zedBSD machine and a Linux host, with the rate and both ends'
# TCP state recorded over time.  The machine samples `sysctl net.tcpstats` (its connection's windows, ring,
# retransmission timer and counts) and its adapter's `ifconfig` about once a second; the host samples `ss -tin` (its
# cwnd, ssthresh, rtt, retransmissions) as often.  plan/ws220/tests/analyze.py turns each direction's samples into
# series-DIRECTION.csv and a summary line (mean, lowest and median one-second rate, seconds below 1 MB/s, the longest
# run at 0, and the counts that tell why).  Judged over SSH, never the console.
#
#   plan/ws220/tests/tcp-stream-series.sh IMAGE [OPTION...]           QEMU: the SSH guest with a second usb-net (CDC
#       ECM) adapter on a host tap, bridged to the namespace zbs where the host's end runs at 10.77.0.1; the guest's
#       adapter is 10.77.0.2.  netem on the namespace's veth (to the guest) and on the bridge's veth (from it) adds
#       the delay and the loss.  Needs sudo (ip, tc, the namespace).  The image: plan/tools/guest/test-image.sh
#       plan/ws220/tests/config-amd64-tcp.mk BUILD (the SSH guest's config).
#   plan/ws220/tests/tcp-stream-series.sh ssh:USER@MACHINE --server-ip ADDRESS [OPTION...]   a real machine (the
#       5330, a Pi 4) on the LAN of this host, whose LAN address is ADDRESS; the machine is reached by SSH at MACHINE.
#       No netem: the loss and the delay are the LAN's.
# Options: --bytes N (each direction, default 1073741824), --direction down|up|both (default both: down is the
# machine receiving from the host's HTTP server, up is the machine sending: the host reads `dd if=/dev/zero` over
# SSH from it), --loss PERCENT and --delay MS (QEMU only, both ways), --timeout SECONDS (each direction, default 900),
# --out DIRECTORY (default build/ws220-series; each run makes a new directory behind it, plan/tools/fresh-out.sh,
# nothing on the host is removed).
# PASS: every direction moved all its bytes (the summaries are the measurement, judged by the reader against WS220's
# targets).  Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
target=${1:?usage: tcp-stream-series.sh IMAGE|ssh:USER@MACHINE [OPTION...]}
shift
bytes=1073741824
direction=both
loss=0
delay=0
limit=900
out=build/ws220-series
server_ip=
while [ $# -ge 2 ]; do
	case $1 in
	--bytes) bytes=$2 ;;
	--direction) direction=$2 ;;
	--loss) loss=$2 ;;
	--delay) delay=$2 ;;
	--timeout) limit=$2 ;;
	--out) out=$2 ;;
	--server-ip) server_ip=$2 ;;
	*) echo "tcp-stream-series: unknown option $1"; exit 2 ;;
	esac
	shift 2
done
. plan/tools/fresh-out.sh
fresh_out "$out"
key=plan/tmp/guest/id_ed25519
status=0
server_pid=
ssh_options="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ServerAliveInterval=10"

case $target in
ssh:*)
	mode=real
	machine=${target#ssh:}
	[ -n "$server_ip" ] || { echo "tcp-stream-series: a real machine needs --server-ip"; exit 2; }
	host_ip=$server_ip
	# The machine's own SSH keys let this host in; the guests' key is for QEMU.
	key_option=
	# shellcheck disable=SC2086
	remote() { timeout 300 ssh $ssh_options "$machine" "$1" 2>&1 | tr -d '\r'; }
	# The host side runs as this user, in the host's own network.
	host_run() { "$@"; }
	;;
*)
	mode=qemu
	image=$target
	host_ip=10.77.0.1
	key_option="-i $key"
	GUEST_RUNTIME=$(pwd)/$out/run
	export GUEST_RUNTIME
	remote() { timeout 300 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
	host_run() { sudo ip netns exec zbs "$@"; }
	;;
esac

# The host's segment taken down (QEMU): the server, the guest, the links and the namespace.
teardown() {
	[ -n "$server_pid" ] && sudo kill -INT "$server_pid" 2>/dev/null
	[ "$mode" = qemu ] || return 0
	python3 plan/tools/guest/guest.py stop >/dev/null 2>&1
	sudo ip link del zbstap0 2>/dev/null
	sudo ip link del zbsbr0 2>/dev/null
	sudo ip link del zbsv0 2>/dev/null
	sudo ip netns del zbs 2>/dev/null
}
trap teardown EXIT

# QEMU: the segment (a bridge with the tap and a veth into the namespace), the guest on it, and its address.
if [ "$mode" = qemu ]; then
	teardown
	sudo ip netns add zbs || exit 1
	sudo ip link add zbsbr0 type bridge
	sudo ip link set zbsbr0 up
	sudo ip tuntap add dev zbstap0 mode tap user "$(id -un)"
	sudo ip link set zbstap0 master zbsbr0 up
	sudo ip link add zbsv0 type veth peer name zbsv1
	sudo ip link set zbsv0 master zbsbr0 up
	sudo ip link set zbsv1 netns zbs
	sudo ip -n zbs link set lo up
	sudo ip -n zbs link set zbsv1 up
	sudo ip -n zbs addr add 10.77.0.1/24 dev zbsv1
	if [ "$loss" != 0 ] || [ "$delay" != 0 ]; then
		sudo ip netns exec zbs tc qdisc replace dev zbsv1 root netem delay "${delay}ms" loss "$loss%"
		sudo tc qdisc replace dev zbsv0 root netem delay "${delay}ms" loss "$loss%"
	fi
	python3 plan/tools/guest/guest.py start "$image" --qemu-extra "-netdev tap,id=net1,ifname=zbstap0,script=no,downscript=no \
-device usb-net,bus=xhci.0,port=5,id=ecm1,netdev=net1,mac=52:54:00:33:00:08" || exit 1
	python3 plan/tools/guest/guest.py wait --timeout 240 || { echo "FAIL: the guest did not come up"; exit 1; }
	iface=$(remote "ifconfig -a | awk '/^[a-z]+[0-9]+:/ {name = substr(\$1, 1, length(\$1) - 1)} /ether 52:54:00:33:00:08/ {print name}'" |
	    tail -1)
	[ -n "$iface" ] || { echo "FAIL: no second adapter"; exit 1; }
	# The address through networkd (net static), not ifconfig behind its back: the adapter is not in net.conf, so
	# networkd's DHCP may still be running on it; the request stops that DHCP, which would otherwise put back the
	# address it found and fall back to 169.254 over 10.77.0.2 (BUG-298).  The address is read again after
	# networkd's 10-second DHCP would have ended.
	remote "net static $iface ipv4 10.77.0.2 netmask 255.255.255.0 && ifconfig $iface up && sleep 2 && ping -c 3 10.77.0.1; sleep 10; ifconfig $iface" > "$out/setup.txt"
	grep -qE ' [1-3] packets received' "$out/setup.txt" || { cat "$out/setup.txt"; echo "FAIL: no way to 10.77.0.1"; exit 1; }
	grep -q 'inet 10\.77\.0\.2 ' "$out/setup.txt" || { cat "$out/setup.txt"; echo "FAIL: $iface lost 10.77.0.2 (BUG-298)"; exit 1; }
	machine_ip=10.77.0.2
	# The machine is reached from the namespace with the guests' key.
	machine=root@10.77.0.2
else
	machine_ip=${machine#*@}
	iface=$(remote "route show" | awk '$1 == "default" || $1 == "0.0.0.0" {print $NF; exit}')
fi
echo "mode=$mode interface=${iface:-?} machine=$machine_ip host=$host_ip bytes=$bytes loss=$loss% delay=${delay}ms" |
    tee "$out/settings.txt"
remote "uname -a; sysctl net.tcpstats | head -1" >> "$out/settings.txt"
grep -q 'net.tcpstats: time_ms=' "$out/settings.txt" || { echo "FAIL: the machine's kernel has no net.tcpstats"; exit 1; }

# The host's HTTP server for the download.
host_run python3 plan/ws220/tests/stream.py serve "$host_ip" 8080 "$(pwd)/$out/server-log.txt" > "$out/server.txt" 2>&1 &
server_pid=$!
sleep 1

# One direction: the samplers on both ends, the transfer, then the samples and their analysis.
measure() {
	which=$1
	echo "== $which"
	remote "nohup sh -c 'while :; do sysctl net.tcpstats; ifconfig ${iface:-ue0}; sleep 1; done' > /tmp/ws220-series-$which.txt 2>&1 < /dev/null & echo \$! > /tmp/ws220-sampler.pid" > /dev/null
	host_run sh -c "while :; do echo time=\$(date +%s.%N); ss -tin dst $machine_ip; sleep 1; done" > "$out/ss-$which.txt" 2>&1 &
	ss_pid=$!
	start=$(date +%s.%N)
	if [ "$which" = down ]; then
		remote "timeout $limit fetch -q -o /dev/null http://$host_ip:8080/$bytes; echo exit=\$?" > "$out/transfer-$which.txt"
		peer=$host_ip:8080
	else
		# shellcheck disable=SC2086
		host_run timeout "$limit" ssh $ssh_options $key_option "$machine" "dd if=/dev/zero bs=65536 count=$((bytes / 65536))" 2> "$out/transfer-$which.txt" |
		    python3 plan/ws220/tests/stream.py sink "$out/sink-log.txt"
		echo "exit=$?" >> "$out/transfer-$which.txt"
		peer="$host_ip:*"
	fi
	end=$(date +%s.%N)
	sleep 2
	kill "$ss_pid" 2>/dev/null
	sudo kill "$ss_pid" 2>/dev/null
	remote "kill \$(cat /tmp/ws220-sampler.pid)" > /dev/null
	if [ "$mode" = qemu ]; then
		python3 plan/tools/guest/guest.py get "/tmp/ws220-series-$which.txt" "$out/series-$which.txt" > /dev/null 2>&1
	else
		# shellcheck disable=SC2086
		scp $ssh_options "$machine:/tmp/ws220-series-$which.txt" "$out/series-$which.txt" > /dev/null 2>&1
	fi
	awk -v a="$start" -v b="$end" -v n="$bytes" 'BEGIN { printf "host clock: %.1f s, %.2f MB/s\n", b - a, n / 1048576 / (b - a) }' |
	    tee -a "$out/summary.txt"
	python3 plan/ws220/tests/analyze.py "$which" "$peer" "$out/series-$which.txt" "$out/ss-$which.txt" "$out/series-$which.csv" |
	    tee -a "$out/summary.txt"
	# Every byte moved: the download's fetch ended well, the upload's sink read them all.
	if [ "$which" = down ]; then
		grep -q '^exit=0' "$out/transfer-$which.txt" || { echo "FAIL: the download did not finish"; status=1; }
	else
		grep -q "^done .* bytes=$(( (bytes / 65536) * 65536 ))\$" "$out/sink-log.txt" || { echo "FAIL: the upload did not finish"; status=1; }
	fi
}

case $direction in
down) measure down ;;
up) measure up ;;
*) measure down; measure up ;;
esac

[ $status = 0 ] && echo "tcp-stream-series: PASS (the series are in $out)" || echo "tcp-stream-series: FAIL"
exit $status
