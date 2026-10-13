#!/bin/sh
# ws220-p001, p002: long TCP transfers between a zedBSD machine and a Linux host, with the rate and both ends' TCP
# state recorded over time.  The machine samples `sysctl net.tcpstats` (its connection's windows, ring, retransmission
# timer and counts) and its adapter's `ifconfig` about once a second; the host samples `ss -tin` of the transfer's
# port (its cwnd, ssthresh, rtt, retransmissions) as often, and captures the transfer's headers (tcpdump), which
# shows stalls of a fraction of a second that a one-second sample cannot.  plan/ws220/tests/analyze.py turns each
# run into series-RUN.csv, a summary line from the samples and a wire line from the capture.  Judged over SSH and the
# host's capture, never the console.
#
#   plan/ws220/tests/tcp-stream-series.sh IMAGE [OPTION...]           QEMU: the SSH guest with a second usb-net (CDC
#       ECM) adapter on a host tap, bridged to the namespace zbs where the host's end runs at 10.77.0.1; the guest's
#       adapter is 10.77.0.2.  The tap, the bridge and both veths have TSO, GSO and GRO off, so netem on the
#       namespace's veth (to the guest) and on the bridge's veth (from it) drops single segments rather than the
#       64 KB blocks of a GSO packet.  Needs sudo (ip, tc, ethtool, tcpdump, the namespace).  The image:
#       plan/tools/guest/test-image.sh plan/ws220/tests/config-amd64-tcp.mk BUILD (the SSH guest, sysctl and
#       tcp-stream); config-amd64-tcp-wrap.mk starts the guest's sequence numbers 16 MiB before the 2^32 wrap.
#   plan/ws220/tests/tcp-stream-series.sh ssh:USER@MACHINE --server-ip ADDRESS [OPTION...]   a real machine (the
#       5330, a Pi 4) on the LAN of this host, whose LAN address is ADDRESS; the machine is reached by SSH at MACHINE.
#       No netem: the loss and the delay are the LAN's.  The host's adapter is captured as it is (its offloads are
#       not changed); the up run needs /bin/tcp-stream on the machine (userland/tests/tcp-stream).
# Runs (--runs, a comma list, default down,down-disk,up; the symptom is on the machine's receiving side, so the
# downloads come first):
#   down       the machine receives: fetch from the host's HTTP server to /dev/null
#   down-disk  the same to a file on the machine's disk (/root/ws220-down.bin, overwritten each time; --disk-bytes,
#              default 268435456), so a reader slower than the link closes and opens the window
#   up         the machine sends: tcp-stream's blocking writes to the host's listener on 8081
#   up-ssh     the machine sends through its sshd (`dd if=/dev/zero` read by the host over SSH; non-blocking writes,
#              and sshd's cipher bounds the rate)
# Options: --bytes N (each run, default 1073741824), --runs LIST (--direction down|up|both is the old form: both is
# down,down-disk,up), --loss PERCENT and --delay MS (QEMU only, both ways), --timeout SECONDS (each run, default
# 900), --capture-iface IFACE (real machine: the host's adapter to capture on; default the route's), --out DIRECTORY
# (default build/ws220-series; each run makes a new directory behind it, plan/tools/fresh-out.sh, nothing on the host
# is removed).  A capture of a 1 GiB run is about 140 MB (80 bytes of each frame).
# PASS: every run moved all its bytes (the summaries are the measurement, judged by the reader against WS220's
# targets).  Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
target=${1:?usage: tcp-stream-series.sh IMAGE|ssh:USER@MACHINE [OPTION...]}
shift
bytes=1073741824
disk_bytes=268435456
runs=down,down-disk,up
loss=0
delay=0
limit=900
out=build/ws220-series
server_ip=
capture_iface=
while [ $# -ge 2 ]; do
	case $1 in
	--bytes) bytes=$2 ;;
	--disk-bytes) disk_bytes=$2 ;;
	--runs) runs=$2 ;;
	--direction)
		case $2 in
		both) runs=down,down-disk,up ;;
		*) runs=$2 ;;
		esac
		;;
	--loss) loss=$2 ;;
	--delay) delay=$2 ;;
	--timeout) limit=$2 ;;
	--out) out=$2 ;;
	--server-ip) server_ip=$2 ;;
	--capture-iface) capture_iface=$2 ;;
	*) echo "tcp-stream-series: unknown option $1"; exit 2 ;;
	esac
	shift 2
done
for run in $(echo "$runs" | tr ',' ' '); do
	case $run in
	down|down-disk|up|up-ssh) ;;
	*) echo "tcp-stream-series: unknown run $run"; exit 2 ;;
	esac
done
for tool in tcpdump ethtool; do
	[ -x "/usr/sbin/$tool" ] || [ -x "/usr/bin/$tool" ] || { echo "tcp-stream-series: the host needs $tool (sudo apt-get install $tool)"; exit 2; }
done
. plan/tools/fresh-out.sh
fresh_out "$out"
key=plan/tmp/guest/id_ed25519
status=0
# The host's background processes are stopped by the pid each writes to a file in the output itself (the capture's
# tcpdump, the HTTP server, the ss sampler): $! is only the shell that started them under sudo, and a background
# shell ignores SIGINT, so a signal to it reached neither tcpdump nor the server (T1-574: the runs hung at the end
# and servers piled up on 10.77.0.1:8080).
capture_pidfile=
server_pidfile=
ss_pidfile=
ssh_options="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ServerAliveInterval=10"
http_port=8080
listen_port=8081

case $target in
ssh:*)
	mode=real
	machine=${target#ssh:}
	[ -n "$server_ip" ] || { echo "tcp-stream-series: a real machine needs --server-ip"; exit 2; }
	host_ip=$server_ip
	# The machine's own SSH keys let this host in; the guests' key is for QEMU.
	key_option=
	# shellcheck disable=SC2086
	remote() { timeout "$((limit + 120))" ssh $ssh_options "$machine" "$1" 2>&1 | tr -d '\r'; }
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
	remote() { timeout "$((limit + 120))" python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
	host_run() { sudo ip netns exec zbs "$@"; }
	;;
esac

# Stops the process whose pid is in FILE with SIGTERM (tcpdump then writes out its capture), waits up to 10 s for it to
# end and then kills it.  No file, or an empty one, stops nothing.
stop_pidfile() {
	[ -n "$1" ] && [ -s "$1" ] || return 0
	pid=$(cat "$1")
	sudo kill -TERM "$pid" 2>/dev/null || return 0
	tries=0
	while sudo kill -0 "$pid" 2>/dev/null && [ $tries -lt 100 ]; do
		sleep 0.1
		tries=$((tries + 1))
	done
	sudo kill -KILL "$pid" 2>/dev/null
	return 0
}

# The host's segment taken down: the capture, the sampler and the server, then (QEMU) the guest, the links and the
# namespace.
teardown() {
	stop_pidfile "$capture_pidfile"
	stop_pidfile "$ss_pidfile"
	stop_pidfile "$server_pidfile"
	[ "$mode" = qemu ] || return 0
	python3 plan/tools/guest/guest.py stop >/dev/null 2>&1
	sudo ip link del zbstap0 2>/dev/null
	sudo ip link del zbsbr0 2>/dev/null
	sudo ip link del zbsv0 2>/dev/null
	sudo ip netns del zbs 2>/dev/null
}
trap teardown EXIT

# Turns TSO, GSO and GRO off on one interface (in the namespace when the second word is zbs); a feature the device
# does not have is left as it is.
offload_off() {
	if [ "${2:-}" = zbs ]; then
		sudo ip netns exec zbs ethtool -K "$1" tso off gso off gro off 2>/dev/null
	else
		sudo ethtool -K "$1" tso off gso off gro off 2>/dev/null
	fi
	return 0
}

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
	# The host's TCP sends segments of the MSS, not GSO blocks netem would drop whole (review S3).
	offload_off zbsv1 zbs
	offload_off zbsv0
	offload_off zbsbr0
	offload_off zbstap0
	if [ "$loss" != 0 ] || [ "$delay" != 0 ]; then
		sudo ip netns exec zbs tc qdisc replace dev zbsv1 root netem delay "${delay}ms" loss "$loss%"
		sudo tc qdisc replace dev zbsv0 root netem delay "${delay}ms" loss "$loss%"
	fi
	python3 plan/tools/guest/guest.py start "$image" --qemu-extra "-netdev tap,id=net1,ifname=zbstap0,script=no,downscript=no \
-device usb-net,bus=xhci.0,port=5,id=ecm1,netdev=net1,mac=52:54:00:33:00:08" || exit 1
	python3 plan/tools/guest/guest.py wait --timeout 240 || { echo "FAIL: the guest did not come up"; exit 1; }
	# The tap exists once QEMU has opened it; its offloads are turned off again then.
	offload_off zbstap0
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
	# capture FILE PORT PIDFILE: tcpdump itself writes PIDFILE (the shell execs it), so stop_pidfile reaches it.
	capture() {
		sudo ip netns exec zbs sh -c 'echo $$ > "$3"; exec tcpdump -i zbsv1 -s 80 -w "$1" "tcp port $2"' capture \
		    "$1" "$2" "$3" > "$1.log" 2>&1
	}
else
	machine_ip=${machine#*@}
	iface=$(remote "route show" | awk '$1 == "default" || $1 == "0.0.0.0" {print $NF; exit}')
	[ -n "$capture_iface" ] || capture_iface=$(ip route get "$machine_ip" | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -1)
	# capture FILE PORT PIDFILE: tcpdump itself writes PIDFILE (the shell execs it), so stop_pidfile reaches it.
	capture() {
		sudo sh -c 'echo $$ > "$3"; exec tcpdump -i "$4" -s 80 -w "$1" "host $5 and tcp port $2"' capture \
		    "$1" "$2" "$3" "$capture_iface" "$machine_ip" > "$1.log" 2>&1
	}
fi
echo "mode=$mode interface=${iface:-?} machine=$machine_ip host=$host_ip bytes=$bytes disk_bytes=$disk_bytes runs=$runs" \
    "loss=$loss% delay=${delay}ms capture=${capture_iface:-zbsv1}" | tee "$out/settings.txt"
remote "uname -a; sysctl net.tcpstats | head -1" >> "$out/settings.txt"
grep -q 'net.tcpstats: time_ms=' "$out/settings.txt" || { echo "FAIL: the machine's kernel has no net.tcpstats"; exit 1; }
# The offloads in force, which say whether netem dropped single segments.
if [ "$mode" = qemu ]; then
	for device in zbstap0 zbsbr0 zbsv0; do
		echo "== ethtool -k $device"
		sudo ethtool -k "$device" 2>&1 | grep -E 'tcp-segmentation-offload|generic-segmentation-offload|generic-receive-offload'
	done >> "$out/settings.txt"
	{
		echo "== ethtool -k zbsv1 (namespace)"
		sudo ip netns exec zbs ethtool -k zbsv1 2>&1 | grep -E 'tcp-segmentation-offload|generic-segmentation-offload|generic-receive-offload'
	} >> "$out/settings.txt"
else
	{
		echo "== ethtool -k $capture_iface (not changed)"
		ethtool -k "$capture_iface" 2>&1 | grep -E 'tcp-segmentation-offload|generic-segmentation-offload|generic-receive-offload'
	} >> "$out/settings.txt"
fi

# The host's HTTP server for the downloads.
# The host's HTTP server for the downloads, which writes its own pid (the shell execs python) for teardown.
server_pidfile=$(pwd)/$out/server.pid
host_run sh -c 'echo $$ > "$1"; shift; exec "$@"' server "$server_pidfile" \
    python3 plan/ws220/tests/stream.py serve "$host_ip" "$http_port" "$(pwd)/$out/server-log.txt" > "$out/server.txt" 2>&1 &
sleep 1

# One run: the samplers on both ends and the capture, the transfer, then the samples and their analysis.
measure() {
	which=$1
	echo "== $which"
	direction=down
	port=$http_port
	run_bytes=$bytes
	case $which in
	down-disk)
		run_bytes=$disk_bytes
		[ "$run_bytes" -le "$bytes" ] || run_bytes=$bytes
		;;
	up) direction=up; port=$listen_port ;;
	up-ssh) direction=up; port=22 ;;
	esac
	remote "nohup sh -c 'while :; do sysctl net.tcpstats; ifconfig ${iface:-ue0}; sleep 1; done' > /tmp/ws220-series-$which.txt 2>&1 < /dev/null & echo \$! > /tmp/ws220-sampler.pid" > /dev/null
	ss_pidfile=$(pwd)/$out/ss-$which.pid
	host_run sh -c "echo \$\$ > '$ss_pidfile'; while :; do echo time=\$(date +%s.%N); ss -tin dst $machine_ip '( sport = :$port or dport = :$port )'; sleep 1; done" > "$out/ss-$which.txt" 2>&1 &
	capture_pidfile=$(pwd)/$out/wire-$which.pid
	capture "$(pwd)/$out/wire-$which.pcap" "$port" "$capture_pidfile" &
	capture_started=$!
	sleep 1
	start=$(date +%s.%N)
	case $which in
	down)
		remote "timeout $limit fetch -q -o /dev/null http://$host_ip:$http_port/$run_bytes; echo exit=\$?" > "$out/transfer-$which.txt"
		;;
	down-disk)
		remote "timeout $limit fetch -q -o /root/ws220-down.bin http://$host_ip:$http_port/$run_bytes; echo exit=\$?; ls -l /root/ws220-down.bin" > "$out/transfer-$which.txt"
		;;
	up)
		host_run timeout "$((limit + 60))" python3 plan/ws220/tests/stream.py listen "$host_ip" "$listen_port" \
		    "$(pwd)/$out/sink-log-$which.txt" > "$out/listen-$which.txt" 2>&1 &
		listen_pid=$!
		sleep 1
		remote "timeout $limit tcp-stream $host_ip $listen_port $run_bytes; echo exit=\$?" > "$out/transfer-$which.txt"
		wait "$listen_pid"
		;;
	up-ssh)
		run_bytes=$(( (bytes / 65536) * 65536 ))
		# shellcheck disable=SC2086
		host_run timeout "$limit" ssh $ssh_options $key_option "$machine" "dd if=/dev/zero bs=65536 count=$((bytes / 65536))" 2> "$out/transfer-$which.txt" |
		    python3 plan/ws220/tests/stream.py sink "$out/sink-log-$which.txt"
		echo "exit=$?" >> "$out/transfer-$which.txt"
		;;
	esac
	end=$(date +%s.%N)
	sleep 2
	# tcpdump writes out the capture and ends, and with it the shell that started it; then the sampler.
	stop_pidfile "$capture_pidfile"
	wait "$capture_started" 2>/dev/null
	capture_pidfile=
	stop_pidfile "$ss_pidfile"
	ss_pidfile=
	remote "kill \$(cat /tmp/ws220-sampler.pid)" > /dev/null
	if [ "$mode" = qemu ]; then
		python3 plan/tools/guest/guest.py get "/tmp/ws220-series-$which.txt" "$out/series-$which.txt" > /dev/null 2>&1
	else
		# shellcheck disable=SC2086
		scp $ssh_options "$machine:/tmp/ws220-series-$which.txt" "$out/series-$which.txt" > /dev/null 2>&1
	fi
	echo "== $which" >> "$out/summary.txt"
	awk -v a="$start" -v b="$end" -v n="$run_bytes" 'BEGIN { printf "host clock: %.1f s, %.2f MB/s\n", b - a, n / 1048576 / (b - a) }' |
	    tee -a "$out/summary.txt"
	# The transfer's port on the host is known except over SSH, where the machine's port 22 sends the data.
	peer="$host_ip:$port"
	port_option="--port $port"
	if [ "$which" = up-ssh ]; then
		peer="$host_ip:*"
		port_option="--sender-port 22"
	fi
	# shellcheck disable=SC2086
	python3 plan/ws220/tests/analyze.py "$direction" "$peer" "$out/series-$which.txt" "$out/ss-$which.txt" "$out/series-$which.csv" \
	    --pcap "$out/wire-$which.pcap" $port_option | tee -a "$out/summary.txt"
	# Every byte moved: the download's fetch ended well, the upload's listener or sink read them all.
	case $which in
	down|down-disk)
		grep -q '^exit=0' "$out/transfer-$which.txt" || { echo "FAIL: $which did not finish"; status=1; }
		;;
	*)
		grep -q "^done .* bytes=$run_bytes\$" "$out/sink-log-$which.txt" || { echo "FAIL: $which did not finish"; status=1; }
		;;
	esac
}

for run in $(echo "$runs" | tr ',' ' '); do
	measure "$run"
done

[ $status = 0 ] && echo "tcp-stream-series: PASS (the series are in $out)" || echo "tcp-stream-series: FAIL"
exit $status
