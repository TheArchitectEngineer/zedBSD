#!/bin/sh
# TCP both ways between a QEMU guest and this host, over the guest's user network (slirp: the host is 10.0.2.2 to the
# guest, and fec0::2 over IPv6, both reaching the host's loopback): builds the image (config-amd64-tcp.mk) unless IMAGE is given, starts the SSH guest, runs tcp-perf send and
# recv against plan/ws221/tests/perf-peer.py on the host's 127.0.0.1, each SECONDS long (default 20), checks that the
# guest's SSH still answers, and prints net.tcpstats.  The rate is the emulator's, not a measurement of the stack; the
# run checks that the transfers complete and the connection behaves.
#   plan/ws221/tests/qemu-tcp.sh [IMAGE [SECONDS [FAMILIES]]]
# FAMILIES is a comma list of inet and inet6 (default inet); each family runs a send and a recv.  An empty IMAGE builds.
# The guest runs in build/ws221-qemu/run (GUEST_RUNTIME), the image is built in build/ws221-tcp.  The last line is
# "qemu-tcp: PASS" when both transfers moved bytes in both ends' count and SSH answered after them.  Judged over SSH and
# the host's peer, never the console.  Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:-}
seconds=${2:-20}
families=${3:-inet}
if [ -z "$image" ]; then
	flock /tmp/zedbsd-image-build.lock timeout 3600 sh plan/tools/guest/test-image.sh plan/ws221/tests/config-amd64-tcp.mk \
	    build/ws221-tcp > build/ws221-tcp.log 2>&1 || { echo "qemu-tcp: FAIL (image build, build/ws221-tcp.log)"; exit 1; }
	image=build/ws221-tcp/hdd-image.img
fi
GUEST_RUNTIME=$(pwd)/build/ws221-qemu/run
export GUEST_RUNTIME
out=build/ws221-qemu
mkdir -p "$out"
guest() { timeout "${T:-120}" python3 plan/tools/guest/guest.py "$@"; }
trap 'guest stop > /dev/null 2>&1' EXIT
guest start "$image" || { echo "qemu-tcp: FAIL (start)"; exit 1; }
T=300 guest wait --timeout 240 || { echo "qemu-tcp: FAIL (no SSH)"; exit 1; }
port=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
status=0
for family in $(echo "$families" | tr ',' ' '); do
	case $family in
	inet) listen=127.0.0.1 target=10.0.2.2 ;;
	inet6) listen=::1 target=fec0::2 ;;
	*) echo "qemu-tcp: unknown family $family"; status=1; continue ;;
	esac
	for run in send recv; do
		: > "$out/peer-$family-$run.log"
		timeout $((seconds + 60)) python3 -I plan/ws221/tests/perf-peer.py "$listen" "$port" "$out/peer-$family-$run.log" 1 &
		peer=$!
		sleep 1
		T=$((seconds + 60)) guest run "tcp-perf $run $target $port $seconds" > "$out/client-$family-$run.txt" 2>&1
		wait "$peer"
		client=$(grep '^TCP-PERF' "$out/client-$family-$run.txt" | tr -d '\r')
		peerline=$(grep '^done' "$out/peer-$family-$run.log")
		echo "$family $run: guest ${client:-no result} | host ${peerline:-no result}"
		case $client in
		*bytes=0\ *|'') status=1 ;;
		esac
		[ -n "$peerline" ] || status=1
	done
done
after=$(guest run 'uname -s; sysctl net.tcpstats | head -3' 2>&1 | tr -d '\r')
echo "$after"
echo "$after" | grep -q zedBSD || { echo "qemu-tcp: SSH did not answer after the transfers"; status=1; }
[ $status = 0 ] && echo "qemu-tcp: PASS" || echo "qemu-tcp: FAIL"
exit $status
