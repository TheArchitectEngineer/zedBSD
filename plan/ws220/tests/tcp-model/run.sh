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
# Losses on the data with a Linux peer's window: each must cost a round trip or so, not a timeout.
scenario lan-loss-0.1 10 --bytes 33554432 --loss-ab 0.1 --peer-window 65535 --limit-s 300
scenario lan-loss-1 2 --bytes 8388608 --loss-ab 1 --peer-window 65535 --limit-s 300
scenario rtt-10ms-loss-0.1 2 --bytes 8388608 --delay-us 5000 --loss-ab 0.1 --peer-window 65535 --limit-s 300
# The same losses with zedBSD as the receiver (its window moves, which hides part of the sender's fault).
scenario lan-loss-1-zedbsd 2 --bytes 16777216 --loss-ab 1 --limit-s 300
# Losses on the acknowledgements only.
scenario lan-ackloss-1 30 --bytes 33554432 --loss-ba 1 --peer-window 65535
# A's sequence numbers wrap 4 MiB into the transfer (a 1 GB transfer crosses the wrap one time in four).
scenario lan-wrap 10 --bytes 16777216 --wrap-before 4194304 --limit-s 120
# A reader slower than the link (2 MB/s): the window closes and opens again, the rate is the reader's.
scenario slow-reader 1.6 --bytes 16777216 --reader-kbps 16000
# ws220-p002: one data segment in twenty starts 100 bytes before B's receive_next (B takes the rest), also across the wrap.
scenario overlap 30 --bytes 33554432 --overlap-ab 5
scenario overlap-wrap 30 --bytes 16777216 --overlap-ab 5 --wrap-before 4194304 --limit-s 120
# ws220-p002: a blind reset within the window, one outside it and a SYN, 100 ms in: none ends the connection.
scenario blind-segments 30 --bytes 33554432 --blind-at-ms 100
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
