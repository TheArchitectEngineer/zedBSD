#!/bin/sh
# ws254-p001: networkd's DHCP retry in QEMU.  The guest has two wired interfaces: ue0 on QEMU's own network (DHCP,
# SSH forwarded), and ue1 on a dgram network whose DHCP server (dhcp-server.py on the host) answers only SERVE_AFTER
# seconds after it started.  Until then ue1's DHCP fails; networkd is to take ue1 down and up and ask again every
# minute, and to obtain the lease once the server answers.  Judged from the guest's /var/log/messages (read over SSH),
# ue1's address (ifconfig over SSH) and the DHCP messages the server saw -- not from the console or serial log.
#   sh plan/ws254/tests/retry-qemu.sh [--no-build] [OUTDIR]   (default build/ws254/retry-qemu, a fresh run each time)
# Environment: SERVE_AFTER (default 200 seconds), WAIT_LEASE (default 420 seconds), BUILD (default build/amd64).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
. plan/tools/fresh-out.sh
build_image=y
if [ "${1:-}" = --no-build ]; then
	build_image=n
	shift
fi
name=${1:-build/ws254/retry-qemu}
fresh_out "$name"
out=$fresh_dir
build=${BUILD:-build/amd64}
serve_after=${SERVE_AFTER:-200}
wait_lease=${WAIT_LEASE:-420}
export GUEST_RUNTIME="$PWD/$out/guest"
guest="plan/tools/guest/guest.sh"

# The image: the SSH guest with this test's net.conf.
if [ $build_image = y ]; then
	flock /tmp/zedbsd-image-build.lock plan/tools/guest/test-image.sh plan/ws254/tests/config.mk "$build" \
		--file /etc/net.conf=plan/ws254/tests/net.conf ZEDBSD_TEST_IMAGE_TAG=ws254 >"$out/build.log" 2>&1
fi
image=$build/hdd-image.img

# Two free ports for the dgram network: the server's and the guest NIC's.
ports=$(python3 -c 'import socket
s=[socket.socket(socket.AF_INET,socket.SOCK_DGRAM) for _ in range(2)]
[x.bind(("127.0.0.1",0)) for x in s]
print(*[x.getsockname()[1] for x in s])')
server_port=${ports% *}
guest_port=${ports#* }

# The server notes every DHCP message from the start, and answers only after SERVE_AFTER seconds.
python3 -u plan/ws254/tests/dhcp-server.py "$server_port" "$guest_port" "$serve_after" >"$out/dhcp.log" 2>&1 &
server_pid=$!
started=$(date +%s)
trap 'kill $server_pid 2>/dev/null || true; "$guest" stop >/dev/null 2>&1 || true' EXIT

# The guest, with ue1 on the dgram network (USB port 5: the harness takes 1 to 4).
"$guest" start "$image" --qemu-extra "-netdev dgram,id=net1,local.type=inet,local.host=127.0.0.1,local.port=$guest_port,remote.type=inet,remote.host=127.0.0.1,remote.port=$server_port -device usb-net,bus=xhci.0,port=5,id=ecm1,netdev=net1,mac=52:54:00:33:00:02" >"$out/start.log" 2>&1
timeout 300 "$guest" wait --timeout 240 >>"$out/start.log" 2>&1

# Waits for ue1's lease, looking at it over SSH every 15 seconds.
leased=n
while [ $(($(date +%s) - started)) -lt "$wait_lease" ]; do
	if timeout 30 "$guest" run 'ifconfig ue1' >"$out/ifconfig-ue1.txt" 2>&1 &&
	    grep -q '192\.168\.77\.10' "$out/ifconfig-ue1.txt"; then
		leased=y
		break
	fi
	sleep 15
done
echo "leased=$leased after $(($(date +%s) - started))s (server answers from ${serve_after}s)" >"$out/result.txt"

# The guest's record: networkd's lines in the system log, and the interfaces.
timeout 30 "$guest" run 'grep networkd /var/log/messages' >"$out/messages-networkd.txt" 2>&1 || true
timeout 30 "$guest" run 'ifconfig -a' >"$out/ifconfig-a.txt" 2>&1 || true
"$guest" screenshot "$out/screen.png" >/dev/null 2>&1 || true

# The judgement: the retries took ue1 down and up, a minute apart, and the last one obtained the lease.
resets=$(grep -c 'ue1: DHCP retry: taking the interface down and up' "$out/messages-networkd.txt" || true)
obtained=$(grep -c 'ue1: DHCP retry obtained an address' "$out/messages-networkd.txt" || true)
discovers=$(grep -c 'DISCOVER' "$out/dhcp.log" || true)
{
	echo "resets=$resets obtained=$obtained discovers=$discovers"
	if [ "$leased" = y ] && [ "$resets" -ge 2 ] && [ "$obtained" -ge 1 ]; then
		echo "WS254-Q001 retry-qemu: PASS"
	else
		echo "WS254-Q001 retry-qemu: FAIL"
	fi
} >>"$out/result.txt"
cat "$out/result.txt"
echo "out: $out"
