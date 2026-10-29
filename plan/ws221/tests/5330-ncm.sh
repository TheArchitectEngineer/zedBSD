#!/bin/sh
# The RTL8156 (CDC NCM) of the Latitude 5330 under zedBSD, in one run of plan/tools/hw5330/5330-run.sh from the NVMe:
# writes IMAGE (plan/ws221/tests/config-5330-jumbo.mk) to the NVMe's zedBSD partitions, sets the VisionFive 2's en0 to
# MTU 9000 for the run (back to 1500 at the end, read back), starts plan/ws221/tests/perf-peer.py on this host for two
# connections, and has 5330-run.sh's EXTRA step run on the 5330's zedBSD:
#   the adapter's attach line (segment, NTB sizes, max_datagram and its read back, caps, mtu_max); ifconfig ue0;
#   tcp-perf send and recv against this host at MTU 1500 (SECONDS each, default 10); then, as root, ifconfig ue0
#   mtu at the adapter's mtu_max from its attach line (9000 at most) and ping -s to the VisionFive 2 at that size,
#   and at 1472;
#   net.tcpstats' first line and ifconfig ue0's counters.
# 5330-run.sh then takes its survey, reboots to Debian and compares.  Output: build/ws221-5330/NAME (fresh-out.sh):
# summary.txt (the lines above), extra.txt (the whole EXTRA output), run.txt (5330-run.sh), peer.log.
#   plan/ws221/tests/5330-ncm.sh IMAGE NAME [SECONDS]
# VF2_HOST (default kei@10.0.30.2), VF2_PASSWORD (default kei).  The 5330's Debian network settings are not touched.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:?usage: 5330-ncm.sh IMAGE NAME [SECONDS]}
name=${2:?usage: 5330-ncm.sh IMAGE NAME [SECONDS]}
seconds=${3:-10}
vf2=${VF2_HOST:-kei@10.0.30.2}
vf2_ip=${vf2#*@}
export SSHPASS="${VF2_PASSWORD:-kei}"
so="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8"
server_ip=$(ip route get 10.0.30.3 | sed -n 's/.* src \([0-9.]*\).*/\1/p' | head -1)
port=5311
. plan/tools/fresh-out.sh
fresh_out "build/ws221-5330/$name"
out=build/ws221-5330/$name
say() { echo "5330-ncm: $*" | tee -a "$out/summary.txt"; }
vf2_root() { printf '%s\n' "$SSHPASS" | timeout 60 sshpass -e ssh $so "$vf2" "sudo -S sh -c '$1'" 2>&1 | tr -d '\r'; }
vf2_mtu() { timeout 30 sshpass -e ssh $so "$vf2" 'ifconfig en0' 2>&1 | sed -n 's/.*mtu \([0-9]*\).*/\1/p' | head -1; }

timeout 60 sshpass -e scp $so build/vf2/bin/ifconfig "$vf2:/tmp/ifconfig" || { say "FAIL: copy of ifconfig to the VisionFive 2"; exit 1; }
trap 'vf2_root "/tmp/ifconfig en0 mtu 1500" > /dev/null; say "restored: VisionFive 2 mtu $(vf2_mtu)"' EXIT
vf2_root "/tmp/ifconfig en0 mtu 9000" > /dev/null
say "VisionFive 2 mtu $(vf2_mtu)"

: > "$out/peer.log"
timeout 1500 python3 -I plan/ws221/tests/perf-peer.py "$server_ip" "$port" "$out/peer.log" 2 > /dev/null 2>&1 &
peer=$!

extra="dmesg | grep -E 'usb-cdc-(ncm|ecm):'; echo ---; ifconfig ue0; echo ---;"
extra="$extra tcp-perf send $server_ip $port $seconds | tail -1; tcp-perf recv $server_ip $port $seconds | tail -1; echo ---;"
extra="$extra m=\$(dmesg | sed -n 's/.*usb-cdc-ncm: ue0 .*mtu_max=\\([0-9]*\\).*/\\1/p' | tail -1); echo mtu-max=\$m;"
extra="$extra [ \"\${m:-1500}\" -gt 9000 ] && m=9000; echo user | sudo -S ifconfig ue0 mtu \${m:-1500}; echo mtu-rc=\$?;"
extra="$extra ifconfig ue0 | head -1; s=\$(( \$(ifconfig ue0 | sed -n 's/.* mtu \\([0-9]*\\).*/\\1/p' | head -1) - 28 ));"
extra="$extra echo ping-size=\$s; ping -c 3 -W 3 -s \$s $vf2_ip; ping -c 3 -W 3 -s 1472 $vf2_ip; echo ---;"
extra="$extra sysctl net.tcpstats | head -1; ifconfig ue0"
# 5330-run.sh gives each zedBSD command T seconds (default 60); the EXTRA step needs the transfers and the pings.
T=$((seconds * 2 + 120)) EXTRA=$extra timeout 2400 sh plan/tools/hw5330/5330-run.sh --nvme-image "$image" "ws221-$name" > "$out/run.txt" 2>&1
status=$?
cp build/hw5330/logs/ws221-$name-extra.txt "$out/extra.txt" 2> /dev/null
kill "$peer" 2> /dev/null
grep -E '^5330-run: ' "$out/run.txt" | sed 's/^/5330-ncm: /' | tee -a "$out/summary.txt"
grep -E 'usb-cdc-(ncm|ecm):' "$out/extra.txt" | sed 's/^/5330-ncm: attach: /' | tee -a "$out/summary.txt"
grep -E '^TCP-PERF' "$out/extra.txt" | sed 's/^/5330-ncm: /' | tee -a "$out/summary.txt"
sed 's/^/5330-ncm: peer: /' "$out/peer.log" | tee -a "$out/summary.txt"
grep -E 'mtu-max=|mtu-rc=|ping-size=|bytes from|packets transmitted|sendto|ifconfig:' "$out/extra.txt" | sed 's/^/5330-ncm: /' | tee -a "$out/summary.txt"
grep -E 'RX packets|TX packets' "$out/extra.txt" | tail -2 | sed 's/^/5330-ncm: after: /' | tee -a "$out/summary.txt"
say "5330-run exit $status"
exit $status
