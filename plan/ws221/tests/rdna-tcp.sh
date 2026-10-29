#!/bin/sh
# The TCP throughput of the RDNA machine's RTL8127 (10.0.10.3, PCIe 10GbE) against this host's 10GbE, in one run, as
# plan/tools/rtl8127/rtl-link.sh LANTEST=2 does it: the NVMe's zedBSD kernel is saved and KERNEL put in, zedBSD started
# once with UEFI BootNext, rge0 brought up and the control interface ue0 taken down (with its connected route) so
# everything goes over rge0; tcp-perf and sysctl from BIN are copied to /tmp and each run of RUNS made against
# plan/ws221/tests/perf-peer.py on this host, with hw.cputimes, the driver's profile line and net.tcpstats taken
# before and after.  A kernel with the temporary PC sampler (the "pcprof:" lines that a read of hw.cputimes writes)
# gets its histogram turned into functions by plan/ws221/tests/pcprof-summary.py against KERNEL's symbols.
# Then zedBSD reboots into Debian (POWERCYCLE=1: the plug once when Debian does not come back) and the saved kernel goes
# back, read back and compared.  The caller holds the RDNA slot's lock.  Judged from the programs' own lines over
# SSH, never the console.
#   sh plan/ws221/tests/rdna-tcp.sh --kernel VMUNIX --bin DIR [--runs LIST] [--seconds N] [--chunk BYTES] NAME
# --runs: a comma list of send (the machine sends) and recv (it receives), default send,recv; --seconds each (20).
# The output is build/ws221-rdna/NAME (plan/tools/fresh-out.sh): summary.txt and the raw outputs.  Nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
kernel=
bin=
runs=send,recv
seconds=20
chunk=131072
while [ $# -gt 1 ]; do
	case $1 in
	--kernel) kernel=$2 ;;
	--bin) bin=$2 ;;
	--runs) runs=$2 ;;
	--seconds) seconds=$2 ;;
	--chunk) chunk=$2 ;;
	*) echo "rdna-tcp: unknown option $1"; exit 2 ;;
	esac
	shift 2
done
name=${1:?usage: rdna-tcp.sh --kernel VMUNIX --bin DIR [--runs LIST] [--seconds N] NAME}
[ -f "$kernel" ] && [ -x "$bin/tcp-perf" ] && [ -x "$bin/sysctl" ] || { echo "rdna-tcp: --kernel and --bin (tcp-perf, sysctl) are needed"; exit 2; }
. plan/tools/fresh-out.sh
mkdir -p build/ws221-rdna
fresh_out "build/ws221-rdna/$name"
out=build/ws221-rdna/$name
DEBIAN=${DEBIAN:-awe@10.0.10.3}
ZEDBSD_MAC=${ZEDBSD_MAC:-6c:1f:f7:1a:08:b6}
ZEDBSD_SUBNET=${ZEDBSD_SUBNET:-10.0.30}
ZEDBSD_BOOT=${ZEDBSD_BOOT:-0004}
IFACE=${IFACE:-rge0}
CONTROL=${CONTROL:-ue0}
POWERCYCLE=${POWERCYCLE:-0}
PEER_IP=${PEER_IP:-10.0.10.2}
PERF_PORT=${PERF_PORT:-5321}
PLUG=${PLUG:-AC276E42B252}
KEY=${KEY:-plan/tmp/guest/id_ed25519}
NV_BOOT=/dev/disk/by-partuuid/d335c6ec-77ea-486f-8a95-57f10a367f0a
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
summary=$out/summary.txt
: > "$summary"

say() {
	echo "rdna-tcp: $*" | tee -a "$summary"
}
deb() {
	timeout "${T:-60}" ssh $SSHOPT "$DEBIAN" "$@"
}
zs() {
	timeout "${T:-30}" ssh $SSHOPT -i "$KEY" "root@$1" "$2" < /dev/null
}
linux_up() {
	[ "$(T=15 deb uname -s 2> /dev/null)" = Linux ]
}
zedbsd_ip() {
	i=2
	while [ $i -lt 255 ]; do
		ping -c 1 -W 1 "$ZEDBSD_SUBNET.$i" > /dev/null 2>&1 &
		i=$((i + 1))
	done
	wait
	zip=$(ip neigh | awk -v mac="$ZEDBSD_MAC" '$0 ~ mac && $0 !~ /FAILED/ && $1 ~ /^[0-9.]+$/ {print $1; exit}')
	[ -n "$zip" ] || return 1
	[ "$(T=15 zs "$zip" "uname -s" 2> /dev/null)" = zedBSD ] || return 1
	echo "$zip"
}
nvme_kernel() {
	sh plan/ws291/tests/rdna-nvme.sh kernel "$1" > "$out/nvme-kernel.txt" 2>&1
	rc=$?
	tee -a "$summary" < "$out/nvme-kernel.txt"
	return $rc
}

linux_up || { say "FAIL: Debian does not answer"; exit 1; }
say "kernel $(sha256sum "$kernel" | cut -c1-16) runs $runs seconds $seconds chunk $chunk"
deb "sudo -n mkdir -p /mnt/ws221 && sudo -n mount -o ro $NV_BOOT /mnt/ws221 && cp /mnt/ws221/vmunix ws221-vmunix.saved; s=\$?; sudo -n umount /mnt/ws221; exit \$s" ||
	{ say "FAIL: could not save the NVMe kernel"; exit 1; }
T=120 timeout 120 scp $SSHOPT -q "$DEBIAN:ws221-vmunix.saved" "$out/vmunix.saved" || { say "FAIL: could not fetch the saved kernel"; exit 1; }
say "saved NVMe kernel $(sha256sum "$out/vmunix.saved" | cut -c1-16) cksum $(cksum < "$out/vmunix.saved")"
nvme_kernel "$kernel" || { say "FAIL: kernel install"; exit 1; }

say "Debian: BootNext $ZEDBSD_BOOT, reboot"
deb "sudo -n efibootmgr -q -n $ZEDBSD_BOOT && (sleep 1; sudo -n systemctl reboot) > /dev/null 2>&1 &"
sleep 40
zip=
end=$(($(date +%s) + 300))
while [ "$(date +%s)" -lt $end ]; do
	zip=$(zedbsd_ip) && break
	zip=
	sleep 5
done

if [ -n "$zip" ]; then
	say "zedBSD up at $zip: $(zs "$zip" "uname -v" 2>&1)"
	T=60 zs "$zip" "ifconfig $IFACE up" > "$out/ifconfig-up.txt" 2>&1
	rip=
	for wait in 5 10 10 10; do
		sleep "$wait"
		zs "$zip" "ifconfig $IFACE" > "$out/ifconfig-$IFACE.txt" 2>&1
		rip=$(awk '$1 == "inet" && $2 !~ /^127\./ {print $2; exit}' "$out/ifconfig-$IFACE.txt")
		[ -n "$rip" ] && grep -q 'media: 10000Mbps' "$out/ifconfig-$IFACE.txt" && break
	done
	say "$IFACE: $(grep -E 'media|inet ' "$out/ifconfig-$IFACE.txt" | tr -s ' \n' ' ')"
	if [ -n "$rip" ]; then
		# The control interface and its connected route go, so every reply takes IFACE's address.
		zs "$zip" "route show" > "$out/route-before.txt" 2>&1
		cnet=$(awk -v c="$CONTROL" '$NF == c && $1 != "default" && $3 ~ /C/ {print $1; exit}' "$out/route-before.txt")
		del=
		[ -n "$cnet" ] && del="; route delete -net $cnet -interface $CONTROL"
		T=10 zs "$zip" "(sleep 2; ifconfig $CONTROL down$del) > /dev/null 2>&1 &"
		sleep 8
		zip=$rip
		T=30 zs "$zip" "route show" > "$out/route-after.txt" 2>&1
		say "routes after $CONTROL down: $(tr -s ' \n' ' ' < "$out/route-after.txt")"
		timeout 60 ssh $SSHOPT -i "$KEY" "root@$rip" "cat > /tmp/tcp-perf && chmod 755 /tmp/tcp-perf" < "$bin/tcp-perf" &&
		    timeout 60 ssh $SSHOPT -i "$KEY" "root@$rip" "cat > /tmp/sysctl && chmod 755 /tmp/sysctl" < "$bin/sysctl"
		say "programs copied: exit $?"
		zs "$rip" "ifconfig $IFACE" > "$out/counters-before.txt" 2>&1
		count=$(echo "$runs" | tr ',' '\n' | grep -c .)
		: > "$out/peer.log"
		timeout $((count * (seconds + 60) + 120)) python3 -I plan/ws221/tests/perf-peer.py "$PEER_IP" "$PERF_PORT" "$out/peer.log" "$count" > /dev/null 2>&1 &
		peer=$!
		sleep 2
		n=0
		for dir in $(echo "$runs" | tr ',' ' '); do
			n=$((n + 1))
			run=$n-$dir
			zs "$rip" "/tmp/sysctl hw.cputimes; dmesg | grep 'rtl8125: $IFACE profile' | tail -1; /tmp/sysctl net.tcpstats" > "$out/before-$run.txt" 2>&1
			snmp0=$(awk '/^Tcp: [0-9]/ {print $13}' /proc/net/snmp)
			cp /proc/net/netstat "$out/netstat-$run-before.txt"
			# net.tcpstats every 2 s over the run, on the machine, read back after.
			T=$((seconds + 60)) zs "$rip" "(while :; do /tmp/sysctl net.tcpstats; sleep 2; done) > /tmp/tcpstats-$run.txt 2>&1 & s=\$!; /tmp/tcp-perf $dir $PEER_IP $PERF_PORT $seconds $chunk; kill \$s" > "$out/perf-$run.txt" 2>&1
			say "run $run: exit $? $(grep '^TCP-PERF' "$out/perf-$run.txt")"
			snmp1=$(awk '/^Tcp: [0-9]/ {print $13}' /proc/net/snmp)
			cp /proc/net/netstat "$out/netstat-$run-after.txt"
			sleep 2
			zs "$rip" "/tmp/sysctl hw.cputimes; dmesg | grep 'rtl8125: $IFACE profile' | tail -1" > "$out/after-$run.txt" 2>&1
			T=60 zs "$rip" "dmesg" > "$out/dmesg-$run.txt" 2>&1
			T=60 zs "$rip" "cat /tmp/tcpstats-$run.txt" > "$out/tcpstats-$run.txt" 2>&1
			python3 -I plan/tools/rtl8127/profile-summary.py "$dir" "$seconds" "$out/before-$run.txt" "$out/after-$run.txt" > "$out/profile-$run.txt" 2>&1
			say "  $(tr '\n' ' ' < "$out/profile-$run.txt")"
			say "  this host's RetransSegs over the run: $((snmp1 - snmp0)); TcpExt: $(python3 -I plan/ws221/tests/netstat-delta.py "$out/netstat-$run-before.txt" "$out/netstat-$run-after.txt")"
			if grep -q 'pcprof: begin' "$out/dmesg-$run.txt"; then
				python3 -I plan/ws221/tests/pcprof-summary.py "$kernel" "$out/dmesg-$run.txt" > "$out/pcprof-$run.txt" 2>&1
				say "  pcprof (out/pcprof-$run.txt): $(head -12 "$out/pcprof-$run.txt" | tr '\n' ';')"
			fi
		done
		wait "$peer"
		say "peer: $(tr '\n' ' ' < "$out/peer.log")"
		zs "$rip" "ifconfig $IFACE" > "$out/counters-after.txt" 2>&1
		say "$IFACE after: $(grep -E 'packets' "$out/counters-after.txt" | tr -s ' \n' ' ')"
	else
		say "FAIL: $IFACE has no address"
	fi
	say "zedBSD: reboot"
	T=10 zs "$zip" "(sleep 1; reboot) > /dev/null 2>&1 &"
else
	say "FAIL: zedBSD did not answer in 300 s"
fi

sleep 30
end=$(($(date +%s) + 400))
cycled=0
until linux_up; do
	if [ "$(date +%s)" -ge $end ]; then
		if [ "$POWERCYCLE" = 1 ] && [ $cycled = 0 ]; then
			say "Debian did not come back: power cycle of plug $PLUG"
			timeout 60 python3 -I plan/tools/switchbot.py cycle "$PLUG" 15 >> "$out/powercycle.txt" 2>&1
			say "power cycle: exit $?"
			cycled=1
			sleep 60
			end=$(($(date +%s) + 400))
			continue
		fi
		say "FAIL: Debian did not come back (power-cycle needed)"
		exit 1
	fi
	sleep 5
done
say "Debian is back"
nvme_kernel "$out/vmunix.saved" || { say "FAIL: saved kernel not put back"; exit 1; }
back=$(deb "sudo -n mount -o ro $NV_BOOT /mnt/ws221 && sha256sum /mnt/ws221/vmunix | cut -c1-64; sudo -n umount /mnt/ws221")
if [ "$back" = "$(sha256sum "$out/vmunix.saved" | cut -c1-64)" ]; then
	say "NVMe kernel is the saved one again ($(echo "$back" | cut -c1-16))"
else
	say "FAIL: NVMe kernel $back is not the saved one"
fi
say "RTX: $(deb "lspci -k -s 01:00.0 | sed -n 's/.*Kernel driver in use: //p'")"
say "done"
