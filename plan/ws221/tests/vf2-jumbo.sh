#!/bin/sh
# Jumbo frames on the VisionFive 2 against the 5330's Debian on the same switch, with the kernel the board has now:
# sets the board's en0 and the 5330's adapter to MTU 9000 for the run (both back to 1500 at the end, the 5330's only
# with ip link, nothing kept), then over SSH:
#   1. ping -s 8972 from the board to the 5330 comes back at 8980 bytes;
#   2. a UDP socket on the board that reads nothing (udp-hold) gets 10 datagrams of 8000 bytes and then 5 of 1000
#      bytes from the 5330: it holds 2 of the jumbo ones and all the small ones (7, under the socket's 8 datagrams),
#      and the pool's jumbo packets are all free afterwards (net.tcpstats jumbo_used=0, jumbo_alloc_failures
#      unchanged);
#   3. over IPv6 (the 5330's global address on the adapter): ping -s 8952 from the board comes back at 8960 bytes,
#      ping -s 8953 (a packet of 9001 bytes) is refused on the board (IPv6 makes no fragments) and none goes out, and
#      the 5330's ping -6 -M do -s 8952 to the board's stable address gets its 3 replies.
#   plan/ws221/tests/vf2-jumbo.sh [--bin DIR] NAME
# --bin: ping, ifconfig, sysctl and udp-hold for riscv64 (default build/vf2/bin, plan/ws221/tests/config-vf2-tcp.mk),
# run from the board's /tmp.  The output is build/ws221-vf2/NAME (plan/tools/fresh-out.sh); the last line is
# "vf2-jumbo: PASS" when every check held.  VF2_HOST (default kei@10.0.30.2), VF2_PASSWORD (default kei),
# PEER (default awe@10.0.30.3), PEER_ADDRESS (default 10.0.30.3) and PEER_IF (default enx6c1ff706148a).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
bin=build/vf2/bin
while [ $# -gt 1 ]; do
	case $1 in
	--bin) bin=$2 ;;
	*) echo "vf2-jumbo: unknown option $1"; exit 2 ;;
	esac
	shift 2
done
name=${1:?usage: vf2-jumbo.sh [--bin DIR] NAME}
host=${VF2_HOST:-kei@10.0.30.2}
board_ip=${host#*@}
peer=${PEER:-awe@10.0.30.3}
peer_address=${PEER_ADDRESS:-10.0.30.3}
peer_if=${PEER_IF:-enx6c1ff706148a}
port=5309
export SSHPASS="${VF2_PASSWORD:-kei}"
so="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8"
. plan/tools/fresh-out.sh
fresh_out "build/ws221-vf2/$name"
out=build/ws221-vf2/$name

say() { echo "vf2-jumbo: $*" | tee -a "$out/summary.txt"; }
board() { timeout "${T:-60}" sshpass -e ssh $so "$host" "$1" 2>&1 | tr -d '\r'; }
board_root() { printf '%s\n' "$SSHPASS" | timeout "${T:-60}" sshpass -e ssh $so "$host" "sudo -S sh -c '$1'" 2>&1 | tr -d '\r'; }
peer_run() { timeout "${T:-60}" ssh -o BatchMode=yes -o ConnectTimeout=8 "$peer" "$1" 2>&1; }
status=0
check() {
	if [ "$2" = 0 ]; then say "ok: $1"; else say "FAIL: $1"; status=1; fi
}
restore() {
	board_root "/tmp/ifconfig en0 mtu 1500" > /dev/null 2>&1
	peer_run "sudo ip link set $peer_if mtu 1500" > /dev/null 2>&1
	say "restored: board $(board 'ifconfig en0' | sed -n 's/.*\(mtu [0-9]*\).*/\1/p' | head -1), peer $(peer_run "ip -o link show $peer_if" | sed -n 's/.*\(mtu [0-9]*\).*/\1/p')"
}

board 'uname -a' > "$out/uname.txt"
say "board: $(cat "$out/uname.txt")"
timeout 60 sshpass -e scp $so "$bin/ping" "$bin/ifconfig" "$bin/sysctl" "$bin/udp-hold" "$host:/tmp/" || { say "FAIL: copy of the programs"; exit 1; }
trap restore EXIT
peer_run "sudo ip link set $peer_if mtu 9000" > "$out/peer-mtu.txt"
board_root "/tmp/ifconfig en0 mtu 9000" > "$out/board-mtu.txt"
board_mtu=$(board 'ifconfig en0' | sed -n 's/.*mtu \([0-9]*\).*/\1/p' | head -1)
peer_mtu=$(peer_run "ip -o link show $peer_if" | sed -n 's/.*mtu \([0-9]*\).*/\1/p')
say "mtu: board $board_mtu, peer $peer_mtu"
[ "$board_mtu" = 9000 ] && [ "$peer_mtu" = 9000 ]; check "both ends at 9000" $?
sleep 2

# 1. A jumbo echo.
board_root "/tmp/ping -c 3 -W 3 -s 8972 $peer_address" > "$out/ping.txt"
grep -c '^8980 bytes from' "$out/ping.txt" | grep -qx 3; check "ping -s 8972: 3 replies of 8980 bytes" $?

# 2. The per-socket jumbo limit.
before=$(board '/tmp/sysctl net.tcpstats | head -1')
echo "$before" > "$out/tcpstats-before.txt"
T=60 board "/tmp/udp-hold $port 15" > "$out/hold.txt" &
holder=$!
sleep 4
peer_run "python3 -c '
import socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for length in [8000] * 10 + [1000] * 5:
	s.sendto(bytes(length), (\"$board_ip\", $port))
	time.sleep(0.01)
print(\"sent\")
'" > "$out/peer-send.txt"
wait $holder
after=$(board '/tmp/sysctl net.tcpstats | head -1')
echo "$after" > "$out/tcpstats-after.txt"
say "peer: $(tr '\n' ' ' < "$out/peer-send.txt")"
say "hold: $(grep '^UDP-HOLD' "$out/hold.txt")"
say "pool after: $(echo "$after" | grep -o 'jumbo_total=.*')"
grep -q '^UDP-HOLD .* queued=7 bytes=21000 largest=8000$' "$out/hold.txt"; check "2 jumbo and 5 small datagrams held" $?
echo "$after" | grep -q 'jumbo_used=0 '; check "no jumbo packet left taken" $?
f0=$(echo "$before" | sed -n 's/.*jumbo_alloc_failures=\([0-9]*\).*/\1/p')
f1=$(echo "$after" | sed -n 's/.*jumbo_alloc_failures=\([0-9]*\).*/\1/p')
[ -n "$f0" ] && [ "$f0" = "$f1" ]; check "no jumbo allocation failure ($f0 -> $f1)" $?

# 3. IPv6 jumbo echoes, and the size past the MTU refused.
peer6=$(peer_run "ip -6 -o addr show dev $peer_if scope global" | awk '!/temporary/ {print $4}' | cut -d/ -f1 | head -1)
board6=$(board 'ifconfig en0' | awk '/inet6/ && !/temporary/ && !/fe80/ && !/tentative/ {print $2; exit}')
say "ipv6: board $board6, peer $peer6"
board_root "/tmp/ping -c 3 -W 3 -s 8952 $peer6" > "$out/ping6.txt"
grep -c '^8960 bytes from' "$out/ping6.txt" | grep -qx 3; check "ping -s 8952 over IPv6: 3 replies of 8960 bytes" $?
board_root "/tmp/ping -c 2 -W 2 -s 8953 $peer6" > "$out/ping6-over.txt"
replies=$(grep -c 'bytes from' "$out/ping6-over.txt")
say "ping -s 8953 over IPv6: $(grep -v '^\[sudo\]' "$out/ping6-over.txt" | head -2 | tr '\n' ' ')"
[ "$replies" = 0 ]; check "ping -s 8953 over IPv6: no reply (no fragment made)" $?
peer_run "ping -6 -c 3 -W 3 -M do -s 8952 $board6" > "$out/peer-ping6.txt"
grep -q '3 received' "$out/peer-ping6.txt"; check "the 5330's ping -6 -M do -s 8952: 3 replies" $?

[ $status = 0 ] && say "PASS" || say "FAIL"
exit $status
