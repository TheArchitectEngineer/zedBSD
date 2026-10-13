#!/bin/sh
# ws220-p001: the host model of a TCP transfer.  Builds src/kern/net/tcp.c, socket.c, packet-buf.c, checksum.c and in6.c
# as the kernel builds them (freestanding, the kernel's headers) with shim.c and model.c, and runs the transfer
# scenarios: A sends to B over a modelled link; the model prints each interval's rate and both ends' state.
#   plan/ws220/tests/tcp-model/run.sh                 the scenarios against WS220's targets, a summary line each and PASS/FAIL
#   plan/ws220/tests/tcp-model/run.sh NAME...         only the scenarios named (for a fix phase's own, e.g. lan-wrap overlap)
#   plan/ws220/tests/tcp-model/run.sh -- ARGS...      one run of the model with ARGS (see model.c), every interval printed
# OUT= chooses the build folder (default build/ws220-host, nothing in it is removed: each build overwrites its files).
# MODEL_CFLAGS= adds compile flags for a what-if (MODEL_CFLAGS=-DCONFIG_TCP_SEND_QUEUE_MAX=44: a larger send ring).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../../.."
out=${OUT:-build/ws220-host}
mkdir -p "$out"
cc=${CC:-clang}
flags="-std=c11 -O2 -g -nostdlibinc -Iinclude -Isrc -I. -Iplan/ws220/tests/tcp-model -DKERN_UAPI_NATIVE -DKERN_KCRT_NATIVE
	-DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 -ffreestanding -fno-builtin -Wall -Wextra -Werror ${MODEL_CFLAGS:-}"
objects=
for source in src/kern/net/tcp.c src/kern/net/socket.c src/kern/net/packet-buf.c src/kern/net/checksum.c \
    src/kern/net/in6.c plan/ws220/tests/tcp-model/shim.c plan/ws220/tests/tcp-model/model.c; do
	object=$out/$(basename "$source" .c).o
	# shellcheck disable=SC2086
	$cc $flags -c "$source" -o "$object" || { echo "tcp-model: build of $source failed"; exit 2; }
	objects="$objects $object"
done
# shellcheck disable=SC2086
$cc -o "$out/tcp-model" $objects -lc || { echo "tcp-model: link failed"; exit 2; }

if [ "${1:-}" = "--" ]; then
	shift
	exec timeout 600 "$out/tcp-model" "$@"
fi

status=0
only=" $* "
# One scenario: its name, the least mean MB/s WS220 aims for in it (the stream must also arrive whole), and the model's
# arguments.  The targets are the model's (zedBSD at both ends, or a peer whose window does not move): p001 records which
# the code before the fix misses; the fix phases make them all pass.  --peer-window 65535 stands for a Linux peer.
scenario() {
	name=$1
	least=$2
	shift 2
	if [ "$only" != "  " ] && ! echo "$only" | grep -q " $name "; then
		return 0
	fi
	output=$(timeout 600 "$out/tcp-model" --quiet "$@")
	line=$(echo "$output" | grep '^tcp-model: summary')
	echo "$name: $line"
	if echo "$output" | grep -q '^tcpstats: FAIL'; then
		echo "$output" | grep '^tcpstats: FAIL' | sed 's/^/  /'
		status=1
	fi
	if echo "$output" | grep -q '^check: FAIL'; then
		echo "$output" | grep '^check: FAIL' | sed 's/^/  /'
		status=1
	fi
	complete=$(echo "$line" | sed -n 's/.*complete=\([01]\).*/\1/p')
	mean=$(echo "$line" | sed -n 's/.*mean_mbs=\([0-9.]*\).*/\1/p')
	if [ "$complete" != 1 ]; then
		echo "  FAIL: $name: the stream did not arrive whole"
		status=1
	elif ! awk -v a="$mean" -v b="$least" 'BEGIN { exit !(a >= b) }'; then
		echo "  FAIL: $name: mean $mean MB/s, below the target $least"
		status=1
	fi
}

# The clean LAN: gigabit, 0.2 ms round trip, a USB adapter's queue of 9 frames each way (no regression).
scenario lan-clean 30 --bytes 67108864
# 10 ms of round trip: the rate is the window over the round trip (64 KB: 6.4 MB/s).
scenario rtt-10ms 4 --bytes 16777216 --delay-us 5000
# (The resends of these and of slow-reader stay at most the counts of ws220-p005, --expect-retransmits-max: p004 review-1 G2
# took p003's; p005 made slow-reader's 8566 (p003 14246; the probes of its closed window, which no longer count as
# duplicates) and lan-loss-1's 85 with 2 timeouts.
# rtt-10ms-loss-0.1 loses nothing to its losses since p005 (no timeout); its rate is the ring's 8 KB a round trip, p006.)
# Losses on the data with a Linux peer's window: each must cost a round trip or so, not a timeout.
scenario lan-loss-0.1 10 --bytes 33554432 --loss-ab 0.1 --peer-window 65535 --limit-s 300 --expect-retransmits-max 30 \
    --expect-timeouts-max 0
scenario lan-loss-1 2 --bytes 8388608 --loss-ab 1 --peer-window 65535 --limit-s 300 --expect-retransmits-max 85 \
    --expect-timeouts-max 2
scenario rtt-10ms-loss-0.1 2 --bytes 8388608 --delay-us 5000 --loss-ab 0.1 --peer-window 65535 --limit-s 300 --expect-retransmits-max 10 \
    --expect-timeouts-max 0
# The same losses with zedBSD as the receiver (its window moves, which hides part of the sender's fault).
scenario lan-loss-1-zedbsd 2 --bytes 16777216 --loss-ab 1 --limit-s 300
# Losses on the acknowledgements only.
scenario lan-ackloss-1 30 --bytes 33554432 --loss-ba 1 --peer-window 65535 --expect-retransmits-max 0
# A's sequence numbers wrap 4 MiB into the transfer (a 1 GB transfer crosses the wrap one time in four).
scenario lan-wrap 10 --bytes 16777216 --wrap-before 4194304 --limit-s 120
# A reader slower than the link (2 MB/s): the window closes and opens again, the rate is the reader's.
scenario slow-reader 1.6 --bytes 16777216 --reader-kbps 16000 --expect-retransmits-max 8566
# ws220-p002: one data segment in twenty starts 100 bytes before B's receive_next (B takes the rest), also across the wrap.
scenario overlap 30 --bytes 33554432 --overlap-ab 5
scenario overlap-wrap 30 --bytes 16777216 --overlap-ab 5 --wrap-before 4194304 --limit-s 120
# ws220-p002: a blind reset within the window, one outside it and a SYN, 100 ms in: none ends the connection.
scenario blind-segments 30 --bytes 33554432 --blind-at-ms 100
# ws220-p003: a device queue shorter than the send ring (8): what the device refuses waits unsent, in order, and goes on the
# pass its transmit completion runs, instead of its writer sleeping 10 ms (before p003: 0.68, 0.49, 0.20, 0.10 MB/s).
scenario queue-7 30 --bytes 67108864 --queue 7
scenario queue-5 30 --bytes 67108864 --queue 5
scenario queue-2 30 --bytes 67108864 --queue 2
scenario queue-1 30 --bytes 33554432 --queue 1
# ws220-p003: two writers on one socket while the device refuses: no byte twice, none out of order (before p003: corrupt).
scenario two-writers 30 --bytes 33554432 --queue 3 --second-writer --expect-in-order
# ws220-p003: losses with a one-frame device queue: the timer's resends and the acknowledgements are refused too.
scenario queue-1-loss-1 2 --bytes 16777216 --queue 1 --loss-ab 1 --limit-s 300
# ws220-p003: A's neighbour is unresolved for 500 ms (IPv4 refuses with EAGAIN and asks ARP each time): TCP takes the
# segments as lost on the wire (review-1 G1), so ARP is asked once for each segment of the ring and then at the timer's
# resends (at most 12), and the transfer goes on after the timer's resend (a stall of about one RTO).
unresolved() {
	name=$1
	most=$2
	shift 2
	if [ "$only" != "  " ] && ! echo "$only" | grep -q " $name "; then
		return 0
	fi
	scenario "$@"
	refusals=$(echo "$line" | sed -n 's/.* unresolved_refusals=\([0-9]*\).*/\1/p')
	if [ -z "$refusals" ] || [ "$refusals" -gt "$most" ]; then
		echo "  FAIL: $name: $refusals refusals (ARP requests), more than $most"
		status=1
	fi
}
unresolved unresolved 12 unresolved 5 --bytes 16777216 --unresolved-at-ms 100 --unresolved-ms 500
# ws220-p003: the neighbour never resolves again: the connection is given up after R2 (100 s), with ARP asked only at the
# ring's segments and the timer's resends (review-1 G1: before, a retry every 10 ms for ever).
giveup_unresolved() {
	name=$1
	shift
	if [ "$only" != "  " ] && ! echo "$only" | grep -q " $name "; then
		return 0
	fi
	line=$(timeout 600 "$out/tcp-model" --quiet "$@" | grep '^tcp-model: summary')
	echo "$name: $line"
	seconds=$(echo "$line" | sed -n 's/.* seconds=\([0-9.]*\).*/\1/p')
	refusals=$(echo "$line" | sed -n 's/.* unresolved_refusals=\([0-9]*\).*/\1/p')
	if ! awk -v a="$seconds" 'BEGIN { exit !(a >= 100.5 && a <= 200) }'; then
		echo "  FAIL: $name: gave up after $seconds s, not between R2 (100 s) and 200 s"
		status=1
	fi
	if [ -z "$refusals" ] || [ "$refusals" -gt 20 ]; then
		echo "  FAIL: $name: $refusals refusals (ARP requests), more than 20"
		status=1
	fi
}
giveup_unresolved unresolved-giveup --bytes 1073741824 --unresolved-at-ms 500 --unresolved-ms 1000000 --limit-s 400
# ws220-p003: a connect whose neighbour never resolves gives up after R2 for a SYN (3 minutes), asking ARP at each resend.
connect_unresolved() {
	name=$1
	shift
	if [ "$only" != "  " ] && ! echo "$only" | grep -q " $name "; then
		return 0
	fi
	line=$(timeout 600 "$out/tcp-model" --quiet "$@" | grep '^tcp-model: summary')
	echo "$name: $line"
	seconds=$(echo "$line" | sed -n 's/.* after \([0-9.]*\) s.*/\1/p')
	refusals=$(echo "$line" | sed -n 's/.* unresolved_refusals=\([0-9]*\).*/\1/p')
	if ! awk -v a="$seconds" 'BEGIN { exit !(a >= 180 && a <= 300) }'; then
		echo "  FAIL: $name: gave up after $seconds s, not between R2 for a SYN (180 s) and 300 s"
		status=1
	fi
	if [ -z "$refusals" ] || [ "$refusals" -gt 20 ]; then
		echo "  FAIL: $name: $refusals refusals (ARP requests), more than 20"
		status=1
	fi
}
connect_unresolved connect-unresolved --connect-unresolved
# ws220-p004: the round-trip estimate (RFC 6298): A's SRTT on the LAN (0.2 ms, within a tick) and with 10 ms of round
# trip, and net.tcpstats gives it.
scenario rtt-estimate-lan 30 --bytes 16777216 --expect-srtt-us 0:2000
scenario rtt-estimate-10ms 0.5 --bytes 4194304 --delay-us 5000 --expect-srtt-us 9000:13000
# ws220-p004: A's first SYN is lost: the data starts with a timeout of 3 s (RFC 6298 section 5.7), and the first sample
# sets SRTT = R and RTTVAR = R / 2 (section 2.2; RTT 10 ms).
syn_retried() {
	name=$1
	shift
	if [ "$only" != "  " ] && ! echo "$only" | grep -q " $name "; then
		return 0
	fi
	scenario "$name" 0.5 "$@"
	connected=$(timeout 600 "$out/tcp-model" --quiet "$@" | grep '^tcp-model: connected')
	echo "$name: $connected"
	if ! echo "$connected" | grep -q ' rto_ms=3000 '; then
		echo "  FAIL: $name: the connection did not start with a 3 s timeout"
		status=1
	fi
}
syn_retried syn-retried --bytes 4194304 --delay-us 5000 --lose-first-syn --expect-first-srtt-us 9000:11000 \
    --expect-first-rttvar-us 4000:6000
# ws220-p004 review-1: the losses' stalls are what the timeouts explain (one RTO a loss, more only for a resend lost
# too or a loss before the backoff ended), the seed fixed (the counts are printed on the "stall:" line).
scenario loss-stall-0.1 0 --bytes 33554432 --loss-ab 0.1 --peer-window 65535 --limit-s 300 --expect-stall-rto
scenario loss-stall-1 0 --bytes 8388608 --loss-ab 1 --peer-window 65535 --limit-s 300 --expect-stall-rto
# ws220-p004 review-1: a peer that delays its acknowledgements 40 ms (Linux) or 200 ms (the longest), each segment alone
# (a writer with gaps) or in a stream: no spurious resend, and SRTT takes the delay in.
scenario delayed-ack-40 0 --bytes 262144 --writer-gap-ms 300 --ack-delay-ms 40 --expect-retransmits-max 0 \
    --expect-srtt-us 30000:50000
scenario delayed-ack-200 0 --bytes 262144 --writer-gap-ms 300 --ack-delay-ms 200 --expect-retransmits-max 0 \
    --expect-srtt-us 190000:210000 --expect-fast-retransmits 0:0
scenario delayed-ack-stream 30 --bytes 16777216 --ack-delay-ms 200 --expect-retransmits-max 0
# ws220-p004 review-1 G1: the round trip grows from 0.2 ms to 600 ms past the timeout: the backoff holds until a sample,
# the resends stay few, and SRTT follows (a Linux-like peer, whose window does not move).
scenario rtt-step 0 --bytes 1048576 --delay-step-at-ms 20 --delay-step-us 300000 --peer-window 65535 --limit-s 120 \
    --expect-retransmits-max 8 --expect-srtt-us 500000:700000 --expect-fast-retransmits 0:0
# ws220-p005: the same with zedBSD as the receiver (its window moves): no resend on a reopened window keeps the sample.
scenario rtt-step-zedbsd 0 --bytes 1048576 --delay-step-at-ms 20 --delay-step-us 300000 --limit-s 120 \
    --expect-retransmits-max 8 --expect-srtt-us 500000:700000 --expect-fast-retransmits 0:0
# ws220-p004 review-1 G2: B's reader stops for 5 s, B's window closes: A's probes double apart (at most 10, p003: 17739).
# ws220-p005: the duplicates of the closed window start no fast retransmit; one after the window opens (the hole of a
# segment B refused) may.
scenario zero-window 2.5 --bytes 16777216 --reader-pause-at-ms 100 --reader-pause-ms 5000 --expect-retransmits-max 10 \
    --expect-fast-retransmits 0:1
# ws220-p004 review-1 M1: A's route fails for 50 ms; a write taken back and written again is not timed from its first
# try (SRTT stays within 5 ms).
scenario no-route 10 --bytes 16777216 --no-route-at-ms 100 --no-route-ms 50 --expect-srtt-max-us 5000
# ws220-p005: NewReno (RFC 6582): two segments of one window lost: one fast retransmit, then the partial
# acknowledgement's resend, no timeout (a Linux-like peer and zedBSD); a whole window lost: one timeout, its partial
# acknowledgements' resends, and no fast retransmit from the duplicates the resends bring.
scenario two-holes 30 --bytes 1048576 --drop-ab 100,103 --peer-window 65535 --expect-fast-retransmits 1:1 \
    --expect-timeouts-max 0 --expect-retransmits-max 2
scenario two-holes-zedbsd 30 --bytes 1048576 --drop-ab 100,103 --expect-fast-retransmits 1:1 --expect-timeouts-max 0 \
    --expect-retransmits-max 2
scenario window-lost 2 --bytes 1048576 --drop-ab 100,101,102,103,104,105,106,107 --peer-window 65535 \
    --expect-fast-retransmits 0:0 --expect-timeouts-max 1 --expect-retransmits-max 8
# ws220-p005 review-1 G2: losses across the sequence numbers' wrap: the fast retransmits go on (recover is compared only
# inside a recovery).
scenario lan-wrap-loss 10 --bytes 16777216 --wrap-before 4194304 --loss-ab 0.1 --peer-window 65535 --limit-s 120 \
    --expect-fast-retransmits 1:100 --expect-timeouts-max 0
# ws220-p005 (M7, the user's decision 2026-10-13: as FreeBSD): B's reader stops for 300 s and B keeps answering the probes
# of its closed window: A is never given up, probes every 5 to 60 s (the persist timer), and the stream completes.
scenario persist-answering 0 --bytes 4194304 --reader-pause-at-ms 100 --reader-pause-ms 300000 --limit-s 400 \
    --expect-probe-gap-s 5:60 --expect-retransmits-max 16
# The same peer goes silent (the link loses everything from 2 s), or A has shut its side at 1 s (FIN_WAIT_1), or A sets
# TCP_MAXUNACKTIME 10: A drops the connection after the persist timer's 12 steps and min(2 h, RTO x 2559) of silence
# (about 9 minutes here), after 60 s of silence, or 10 s after its data last moved with a reset B takes (B closed, 0).
closes() {
	name=$1
	low=$2
	high=$3
	b_state=$4
	shift 4
	if [ "$only" != "  " ] && ! echo "$only" | grep -q " $name "; then
		return 0
	fi
	output=$(timeout 900 "$out/tcp-model" --quiet "$@")
	line=$(echo "$output" | grep '^close:')
	echo "$name: $line $(echo "$output" | grep '^persist:')"
	closed=$(echo "$line" | sed -n 's/.*a_closed_at_s=\([-0-9.]*\).*/\1/p')
	state=$(echo "$line" | sed -n 's/.* b_state=\([-0-9]*\).*/\1/p')
	if ! awk -v a="$closed" -v l="$low" -v h="$high" 'BEGIN { exit !(a >= l && a <= h) }'; then
		echo "  FAIL: $name: A closed at $closed s, not between $low and $high s"
		status=1
	fi
	if [ "$b_state" != "-" ] && [ "$state" != "$b_state" ]; then
		echo "  FAIL: $name: B is in state $state, not $b_state"
		status=1
	fi
	if echo "$output" | grep -q '^check: FAIL'; then
		echo "$output" | grep '^check: FAIL' | sed 's/^/  /'
		status=1
	fi
}
closes persist-silent 500 800 - --bytes 4194304 --reader-pause-at-ms 100 --reader-pause-ms 100000000 --blackhole-at-ms 2000 \
    --limit-s 1200 --expect-probe-gap-s 5:60
closes persist-closed 60 80 - --bytes 4194304 --reader-pause-at-ms 100 --reader-pause-ms 100000000 --blackhole-at-ms 2000 \
    --close-at-ms 1000 --limit-s 120
closes maxunacktime 10 11 0 --bytes 4194304 --reader-pause-at-ms 100 --reader-pause-ms 300000 --maxunacktime 10 --limit-s 400
# ws220-p002: the link loses everything 500 ms in: A gives up after R2 (at least 100 s; 1+2+4+8+16+32+60 s of resends).
giveup() {
	name=$1
	shift
	if [ "$only" != "  " ] && ! echo "$only" | grep -q " $name "; then
		return 0
	fi
	line=$(timeout 600 "$out/tcp-model" --quiet "$@" | grep '^tcp-model: summary')
	echo "$name: $line"
	seconds=$(echo "$line" | sed -n 's/.* seconds=\([0-9.]*\).*/\1/p')
	if ! awk -v a="$seconds" 'BEGIN { exit !(a >= 100.5 && a <= 200) }'; then
		echo "  FAIL: $name: gave up after $seconds s, not between R2 (100 s) and 200 s"
		status=1
	fi
}
giveup data-giveup --bytes 1073741824 --blackhole-at-ms 500 --limit-s 400
# ws220-p002: A's SYN is lost from the start: the connect gives up only after 3 minutes (R2 for a SYN).
scenario syn-giveup 0 --connect-blackhole

[ $status = 0 ] && echo "tcp-model: PASS" || echo "tcp-model: FAIL"
exit $status
