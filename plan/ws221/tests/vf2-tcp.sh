#!/bin/sh
# The TCP throughput of the VisionFive 2 against this host, end to end, printing a short summary.  Optionally puts a
# kernel on the board (/boot/boot0/vmunix, the one before kept as vmunix.old, the board's first kernel kept once as
# vmunix.p3orig; read back and compared), power-cycles it through its SwitchBot plug and waits for SSH; then copies
# tcp-perf and sysctl to the board's /tmp and runs each transfer against plan/ws221/tests/perf-peer.py on this host,
# sampling the board's net.tcpstats and its adapter's counters every second.  Judged from the programs' own lines
# over SSH, never the console.
#   plan/ws221/tests/vf2-tcp.sh [--kernel VMUNIX] [--bin DIR] [--seconds N] [--runs LIST] [--chunk BYTES] [--mtu N]
#       NAME
# --bin: the directory with tcp-perf, sysctl and ifconfig for riscv64 (default build/vf2/bin,
# plan/ws221/tests/config-vf2-tcp.mk); ifconfig is used only from /tmp, for --mtu (the board's adapter set to N, as
# root, before the runs).  Each run also reports each CPU's busy share over the run (hw.cputimes).
# --runs: a comma list of send (the board sends) and recv (the board receives), default send,recv; --seconds each run
# (default 30).  serve-send and serve-recv (with --peer) measure the board's accepted connections: the board runs
# tcp-perf serve on the port and the peer connects with tcp-perf built there from the same source (/tmp/tcp-perf-linux),
# to the board's own address (over IPv6 its stable global one when ADDRESS is IPv6).  The output is build/ws221-vf2/NAME (plan/tools/fresh-out.sh): summary.txt, the client's and the
# peer's lines, the samples.  The last line is "vf2-tcp: PASS" when every run moved bytes in both ends' count.
# --address ADDRESS: the address of this host the peer listens on (default the source of the route to the board; an
# IPv6 one measures IPv6).  --peer USER@HOST,ADDRESS runs the peer on another Linux machine over SSH (its python3, /tmp/perf-peer.py) at ADDRESS
# instead of this host (2026-10-29: the 5330's Debian on the board's own switch; the path from the board to this host
# reorders frames).  --peer-if IF with --mtu sets that peer's adapter IF to the same MTU (sudo ip link, nothing
# kept); at the end the board's adapter and the peer's go back to 1500.  --ptb AT,MTU[/AT,MTU...] (with --peer, an IPv6
# ADDRESS): AT seconds into each run the peer sends the board a Packet Too Big for its own address with MTU
# (plan/ws221/tests/ptb6.py, as root), as a router on the path would; each run's line then gives the segment size the
# board ended with, the segment sizes it sent with over the run (the samples), and the rate before the first and after
# the last message.  VF2_HOST (default kei@10.0.30.2) and VF2_PASSWORD (default kei) reach the board; VF2_PLUG its plug.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
kernel=
bin=build/vf2/bin
seconds=30
runs=send,recv
chunk=131072
mtu=
ptb=
address=
peer_remote=
peer_if=
while [ $# -gt 1 ]; do
	case $1 in
	--kernel) kernel=$2 ;;
	--bin) bin=$2 ;;
	--seconds) seconds=$2 ;;
	--runs) runs=$2 ;;
	--chunk) chunk=$2 ;;
	--mtu) mtu=$2 ;;
	--peer) peer_remote=$2 ;;
	--peer-if) peer_if=$2 ;;
	--ptb) ptb=$2 ;;
	--address) address=$2 ;;
	*) echo "vf2-tcp: unknown option $1"; exit 2 ;;
	esac
	shift 2
done
name=${1:?usage: vf2-tcp.sh [--kernel VMUNIX] [--bin DIR] [--seconds N] [--runs LIST] NAME}
host=${VF2_HOST:-kei@10.0.30.2}
board_ip=${host#*@}
plug=${VF2_PLUG:-14C19FA80F86}
export SSHPASS="${VF2_PASSWORD:-kei}"
server_ip=$(ip route get "$board_ip" | sed -n 's/.* src \([0-9.]*\).*/\1/p' | head -1)
[ -n "$address" ] && server_ip=$address
peer_host=
if [ -n "$peer_remote" ]; then
	peer_host=${peer_remote%,*}
	server_ip=${peer_remote#*,}
	timeout 60 scp -q plan/ws221/tests/perf-peer.py plan/ws221/tests/ptb6.py "$peer_host:/tmp/" ||
	    { echo "vf2-tcp: FAIL: copy of the peer"; exit 1; }
	case $runs in
	*serve*)
		timeout 60 scp -q userland/tests/tcp-perf/main.c "$peer_host:/tmp/tcp-perf-main.c" &&
		    timeout 120 ssh -o BatchMode=yes "$peer_host" 'cc -O2 -o /tmp/tcp-perf-linux /tmp/tcp-perf-main.c' ||
		    { echo "vf2-tcp: FAIL: tcp-perf on the peer"; exit 1; }
		;;
	esac
fi
port=5301
so="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8"
. plan/tools/fresh-out.sh
fresh_out "build/ws221-vf2/$name"
out=build/ws221-vf2/$name

say() { echo "vf2-tcp: $*" | tee -a "$out/summary.txt"; }
board() { timeout "${T:-60}" sshpass -e ssh $so "$host" "$1" 2>&1 | tr -d '\r'; }
board_root() { printf '%s\n' "$SSHPASS" | timeout "${T:-60}" sshpass -e ssh $so "$host" "sudo -S sh -c '$1'" 2>&1 | tr -d '\r'; }

# Waits up to 3 minutes for the board's SSH; prints 0 when it answered.
wait_ssh() {
	start=$(date +%s)
	while [ $(($(date +%s) - start)) -lt 180 ]; do
		T=10 board 'uname -s' 2>/dev/null | grep -qi zedbsd && return 0
		sleep 5
	done
	return 1
}

if [ -n "$kernel" ]; then
	timeout 120 sshpass -e scp $so "$kernel" "$host:/tmp/vmunix.new" || { say "FAIL: copy of the kernel"; exit 1; }
	board_root 'cd /boot/boot0 && if [ ! -f vmunix.p3orig ]; then cp vmunix vmunix.p3orig; fi && cp vmunix vmunix.old && cp /tmp/vmunix.new vmunix && sync' > "$out/deploy.txt"
	timeout 120 sshpass -e scp $so "$host:/boot/boot0/vmunix" "$out/vmunix.back" > /dev/null 2>&1
	cmp -s "$kernel" "$out/vmunix.back" || { say "FAIL: the kernel read back differs"; exit 1; }
	say "kernel placed ($(sha256sum "$kernel" | cut -c1-16)), the one before is vmunix.old"
	T=20 board sync > /dev/null
	tries=0
	up=no
	while [ $tries -lt 3 ]; do
		tries=$((tries + 1))
		# A mark in /tmp that only a reboot takes away: SSH answering is not enough (2026-10-29: a cycle that did
		# not cut the power left the old kernel running).
		T=20 board 'echo up > /tmp/vf2-tcp.not-rebooted' > /dev/null
		python3 -I plan/tools/switchbot.py cycle "$plug" 10 > "$out/switchbot-$tries.txt" 2>&1
		sleep 20
		if wait_ssh; then
			mark=$(T=20 board 'cat /tmp/vf2-tcp.not-rebooted 2>/dev/null')
			if [ -z "$mark" ]; then
				up=yes
				break
			fi
			say "the board did not reboot on power cycle $tries ($(tr '\n' ' ' < "$out/switchbot-$tries.txt"))"
			continue
		fi
		say "no SSH 3 minutes after power cycle $tries"
	done
	[ $up = yes ] || { say "FAIL: the board did not come back after $tries power cycles"; exit 2; }
	say "board up after $tries power cycle(s)"
	sleep 15
fi

board 'uname -a' > "$out/uname.txt"
say "board: $(cat "$out/uname.txt")"
timeout 60 sshpass -e scp $so "$bin/tcp-perf" "$bin/sysctl" "$bin/ifconfig" "$host:/tmp/" || { say "FAIL: copy of the programs"; exit 1; }
iface=$(board 'route show' | awk '$1 == "default" || $1 == "0.0.0.0" {print $NF; exit}')
[ -n "$iface" ] || iface=en0
say "server=$server_ip board=$board_ip interface=$iface seconds=$seconds chunk=$chunk runs=$runs"
if [ -n "$mtu" ]; then
	trap 'board_root "/tmp/ifconfig $iface mtu 1500" > /dev/null 2>&1; [ -n "$peer_if" ] && [ -n "$peer_host" ] &&
	    timeout 60 ssh -o BatchMode=yes "$peer_host" "sudo ip link set $peer_if mtu 1500" > /dev/null 2>&1' EXIT
	if [ -n "$peer_if" ] && [ -n "$peer_host" ]; then
		timeout 60 ssh -o BatchMode=yes "$peer_host" "sudo ip link set $peer_if mtu $mtu; ip -o link show $peer_if" \
		    > "$out/peer-mtu.txt" 2>&1
		say "peer mtu: $(sed -n 's/.*\(mtu [0-9]*\).*/\1/p' "$out/peer-mtu.txt")"
	fi
	board_root "/tmp/ifconfig $iface mtu $mtu" > "$out/mtu.txt"
	say "mtu $mtu: $(tr '\n' ' ' < "$out/mtu.txt")"
fi
board "ifconfig $iface" > "$out/ifconfig-before.txt"
say "adapter: $(head -1 "$out/ifconfig-before.txt")"

status=0
for run in $(echo "$runs" | tr ',' ' '); do
	case $run in
	send|recv) ;;
	serve-send|serve-recv)
		[ -n "$peer_host" ] || { say "$run needs --peer"; status=1; continue; } ;;
	*) say "unknown run $run"; status=1; continue ;;
	esac
	: > "$out/peer-$run.log"
	case $run in
	serve-*)
		# The board listens; the peer connects once the samples run (below).
		;;
	*)
		if [ -n "$peer_host" ]; then
			timeout $((seconds + 60)) ssh -o BatchMode=yes "$peer_host" \
			    "f=\$(mktemp /tmp/perf-peer.XXXXXX); python3 -I /tmp/perf-peer.py $server_ip $port \$f 1; cat \$f" \
			    > "$out/peer-$run.log" 2>&1 &
		else
			timeout $((seconds + 60)) python3 -I plan/ws221/tests/perf-peer.py "$server_ip" "$port" "$out/peer-$run.log" 1 &
		fi
		peer=$!
		sleep 1
		;;
	esac
	board "nohup sh -c 'while :; do /tmp/sysctl net.tcpstats; sleep 1; done' > /tmp/ws221-samples-$run.txt 2>&1 < /dev/null & echo \$! > /tmp/ws221-sampler.pid" > /dev/null
	board '/tmp/sysctl hw.cputimes' > "$out/cpu-before-$run.txt"
	ptb_job=
	if [ -n "$ptb" ] && [ -n "$peer_host" ]; then
		# The board's end of the connection, as the peer sees it, gets the message about the peer's address.
		( last=0
		for message in $(echo "$ptb" | tr '/' ' '); do
			at=${message%,*}
			sleep $((at - last))
			last=$at
			timeout 30 ssh -o BatchMode=yes "$peer_host" \
			    "c=\$(ss -Htn state established '( sport = :$port )' | awk '{print \$4}' | head -1); a=\${c%]:*};
			    a=\${a#[}; p=\${c##*:}; sudo python3 -I /tmp/ptb6.py \$a $server_ip ${message#*,} \$p $port"
		done ) > "$out/ptb-$run.txt" 2>&1 &
		ptb_job=$!
	fi
	case $run in
	serve-*)
		# The board's address the peer connects to, and the one it listens on.
		case $server_ip in
		*:*)
			listen=::
			target=$(board "ifconfig $iface" | awk '/inet6/ && !/temporary/ && !/fe80/ && !/tentative/ {print $2; exit}') ;;
		*)
			listen=0.0.0.0
			target=$board_ip ;;
		esac
		board "nohup sh -c '/tmp/tcp-perf serve $listen $port $seconds $chunk; echo exit=\$?' > /tmp/ws221-serve.txt 2>&1 < /dev/null &" > /dev/null
		sleep 2
		direction=send
		[ $run = serve-send ] && direction=recv
		timeout $((seconds + 60)) ssh -o BatchMode=yes "$peer_host" "/tmp/tcp-perf-linux $direction $target $port $seconds $chunk" \
		    > "$out/peer-$run.log" 2>&1
		tries=0
		until board 'cat /tmp/ws221-serve.txt' > "$out/client-$run.txt" && grep -q '^exit=' "$out/client-$run.txt"; do
			tries=$((tries + 1))
			[ $tries -gt 20 ] && break
			sleep 1
		done
		peer=
		;;
	*)
		T=$((seconds + 40)) board "/tmp/tcp-perf $run $server_ip $port $seconds $chunk; echo exit=\$?" > "$out/client-$run.txt"
		;;
	esac
	[ -n "$ptb_job" ] && wait "$ptb_job"
	board '/tmp/sysctl hw.cputimes' > "$out/cpu-after-$run.txt"
	[ -n "$peer" ] && wait "$peer"
	board 'kill $(cat /tmp/ws221-sampler.pid)' > /dev/null
	timeout 60 sshpass -e scp $so "$host:/tmp/ws221-samples-$run.txt" "$out/samples-$run.txt" > /dev/null 2>&1
	client=$(grep '^TCP-PERF' "$out/client-$run.txt")
	peerline=$(grep -E '^(done|TCP-PERF)' "$out/peer-$run.log")
	say "$run: board ${client:-no result} | host ${peerline:-no result}"
	min=$(awk -F'mbps=' '/^t=/ {v = $2 + 0; if (n++ == 0 || v < m) m = v} END {if (n) printf "%.1f", m}' "$out/client-$run.txt")
	say "$run: slowest second ${min:-?} Mbps"
	tcp=$(grep ":$port " "$out/samples-$run.txt" | tail -1 | tr ' ' '\n' |
	    grep -E '^(send_mss|receive_mss|retransmit_segments|fast_retransmits|send_refused)=' | tr '\n' ' ')
	say "$run: board's connection at the end: ${tcp:-none sampled}"
	if [ -n "$ptb" ]; then
		first=$(echo "$ptb" | cut -d/ -f1)
		first=${first%,*}
		last=$(echo "$ptb" | tr '/' '\n' | tail -1)
		last=${last%,*}
		rates=$(awk -F'[= ]' -v first="$first" -v last="$last" '/^t=/ { if ($2 <= first) { b += $6; nb++ }
		    else if ($2 > last + 1) { a += $6; na++ } }
		    END { if (nb) printf "before %.1f ", b / nb; if (na) printf "after %.1f", a / na }' "$out/client-$run.txt")
		sizes=$(grep ":$port " "$out/samples-$run.txt" | tr ' ' '\n' | sed -n 's/^send_mss=//p' | uniq | tr '\n' ' ')
		say "$run: packet too big: $(cat "$out/ptb-$run.txt" | tr '\n' ' ')| send_mss over the run: $sizes| mean Mbps $rates"
	fi
	cpu=$(awk '/cpu=/ { for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
		c = v["cpu"]; t = v["user"] + v["system"] + v["idle"] + v["other"]
		if (FILENAME ~ /before/) { b[c] = v["user"] + v["system"]; bt[c] = t } else { a[c] = v["user"] + v["system"]; at[c] = t }
		} END { for (c = 0; c < 64; c++) if (c in a) { d = at[c] - bt[c]; p = 0; if (d > 0) p = (a[c] - b[c]) * 100 / d; printf "cpu%d=%.0f%% ", c, p } }' \
	    "$out/cpu-before-$run.txt" "$out/cpu-after-$run.txt")
	say "$run: busy $cpu"
	case $client in
	*bytes=0\ *|'') status=1 ;;
	esac
	[ -n "$peerline" ] || status=1
done

board "ifconfig $iface" > "$out/ifconfig-after.txt"
say "adapter after: $(grep -E 'RX packets|TX packets' "$out/ifconfig-after.txt" | tr -s ' ' | tr '\n' ';')"
[ $status = 0 ] && say PASS || say FAIL
