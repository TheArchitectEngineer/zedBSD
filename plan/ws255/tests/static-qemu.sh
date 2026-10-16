#!/bin/sh
# ws255-p001: a test image with a static IPv4 answers SSH without DHCP.  The guest's ue0 is on QEMU's user network,
# whose DHCP server is always there; the check is that the guest never asks it: the image's net.conf
# (plan/ws255/tests/net-qemu.conf) is static, and the guest's system log, ue0, its routes and resolver are read over
# SSH.  Not judged from the console or serial log.
#   sh plan/ws255/tests/static-qemu.sh [--no-build] [OUTDIR]   (default build/ws255/static-qemu, a fresh run each time)
# Environment: BUILD (default build/amd64).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
. plan/tools/fresh-out.sh
build_image=y
if [ "${1:-}" = --no-build ]; then
	build_image=n
	shift
fi
name=${1:-build/ws255/static-qemu}
fresh_out "$name"
out=$fresh_dir
build=${BUILD:-build/amd64}
export GUEST_RUNTIME="$PWD/$out/guest"
guest="plan/tools/guest/guest.sh"

# The image: the SSH guest with the static net.conf copied over the harness's.
if [ $build_image = y ]; then
	flock /tmp/zedbsd-image-build.lock plan/tools/guest/test-image.sh plan/ws255/tests/config.mk "$build" \
		--file /etc/net.conf=plan/ws255/tests/net-qemu.conf ZEDBSD_TEST_IMAGE_TAG=ws255 >"$out/build.log" 2>&1
fi
image=$build/hdd-image.img
trap '"$guest" stop >/dev/null 2>&1 || true' EXIT

# The guest, and its SSH.  Every frame of ue0 is captured, to see that the guest sends no DHCP at all.
"$guest" start "$image" --qemu-extra "-object filter-dump,id=dump0,netdev=net0,file=$PWD/$out/ue0.pcap" \
	>"$out/start.log" 2>&1
ssh_ok=n
if timeout 300 "$guest" wait --timeout 240 >>"$out/start.log" 2>&1; then
	ssh_ok=y
fi

# The guest's record: the configuration it booted with, ue0, the routes, the resolver, and whether DHCP ran.
timeout 30 "$guest" run 'cat /etc/net.conf' >"$out/net.conf.txt" 2>&1 || true
timeout 30 "$guest" run 'ifconfig ue0' >"$out/ifconfig-ue0.txt" 2>&1 || true
timeout 30 "$guest" run 'route show' >"$out/routes.txt" 2>&1 || true
timeout 30 "$guest" run 'cat /etc/resolv.conf' >"$out/resolv.txt" 2>&1 || true
timeout 30 "$guest" run 'grep -E "networkd|dhcpc|net:" /var/log/messages' >"$out/messages-net.txt" 2>&1 || true
timeout 30 "$guest" run 'ls /var/db/dhcpc' >"$out/dhcpc-db.txt" 2>&1 || true
"$guest" screenshot "$out/screen.png" >/dev/null 2>&1 || true

# The judgement: SSH answered, ue0 has the static address, and the guest sent no DHCP (UDP to port 67).
"$guest" stop >/dev/null 2>&1 || true
static=n
grep -q '10\.0\.2\.15' "$out/ifconfig-ue0.txt" && static=y
dhcp=$(python3 -I plan/ws255/tests/pcap-dhcp.py "$out/ue0.pcap" 2>"$out/pcap.err" || echo error)
{
	# Recorded, not judged: net startup does not apply net.conf's default route and DNS at boot yet (ws255-p001).
	default=n
	grep -q '^default\|^0\.0\.0\.0' "$out/routes.txt" && default=y
	resolver=n
	grep -q '^nameserver' "$out/resolv.txt" && resolver=y
	echo "ssh=$ssh_ok static=$static dhcp-frames=$dhcp default-route=$default resolver=$resolver"
	if [ $ssh_ok = y ] && [ $static = y ] && [ "$dhcp" = 0 ]; then
		echo "WS255-Q001 static-qemu: PASS"
	else
		echo "WS255-Q001 static-qemu: FAIL"
	fi
} >"$out/result.txt"
cat "$out/result.txt"
echo "out: $out"
