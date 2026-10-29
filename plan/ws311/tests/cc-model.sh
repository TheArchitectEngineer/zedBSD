#!/bin/sh
# Reno against CUBIC in the host model of TCP (plan/ws220/tests/tcp-model, its big build): each scenario with five seeds
# for each, the median of the steady rate (from 20 s, after the first slow start), and the ratio CUBIC / Reno against
# the expectation.  Timed runs of 300 s of the model's clock.  Prints a line per scenario and the last line
# "cc-model: PASS" when every expectation holds.
#   plan/ws311/tests/cc-model.sh [NAME...]      OUT= the build folder (default build/ws311-host), JOBS= runs at once (10)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${OUT:-build/ws311-host}
jobs=${JOBS:-10}
OUT=$out sh plan/ws220/tests/tcp-model/run.sh none > /dev/null || { echo "cc-model: build failed"; exit 2; }
model=$out/big/tcp-model
runs=$out/cc-runs
mkdir -p "$runs"
only=" $* "
status=0
seeds="1 2 3 4 5"
common="--run-s 300 --measure-from-s 20 --send-buffer 1048576 --receive-buffer 1048576 --write-bytes 65536 --quiet"

# Runs one scenario's ten runs at once (JOBS at a time), each into its own file.
run_all() {
	name=$1
	shift
	for cc in reno cubic; do
		for seed in $seeds; do
			echo "$cc $seed"
		done
	done | xargs -P "$jobs" -n 2 sh -c 'timeout 1200 "$0" --cc "$1" --seed "$2" '"$common $*"' > "'"$runs/$name"'-$1-$2.txt" 2>&1' "$model"
}

# The median of the steady rates of one control's five runs.
median() {
	for seed in $seeds; do
		sed -n 's/^steady: from_s=[0-9.]* mbs=\([0-9.]*\)$/\1/p' "$runs/$1-$2-$seed.txt"
	done | sort -n | awk '{ v[NR] = $1 } END { if (NR == 0) print "none"; else print v[int((NR + 1) / 2)] }'
}

# A scenario: its name, the least CUBIC / Reno ratio it expects (- for none, recorded only), and the model's arguments.
ratio_scenario() {
	name=$1
	least=$2
	shift 2
	if [ "$only" != "  " ] && ! echo "$only" | grep -q " $name "; then
		return 0
	fi
	run_all "$name" "$@"
	reno=$(median "$name" reno)
	cubic=$(median "$name" cubic)
	ratio=$(awk -v a="$cubic" -v b="$reno" 'BEGIN { if (b > 0) printf "%.3f", a / b; else print "none" }')
	failed=$(cat "$runs/$name"-*.txt | grep -c 'check: FAIL\|tcpstats: FAIL')
	verdict=recorded
	if [ "$least" != - ]; then
		if awk -v r="$ratio" -v l="$least" 'BEGIN { exit !(r != "none" && r >= l) }'; then
			verdict=PASS
		else
			verdict=FAIL
			status=1
		fi
	fi
	[ "$failed" = 0 ] || { verdict="$verdict, $failed check failures"; status=1; }
	echo "$name: reno_median_mbs=$reno cubic_median_mbs=$cubic ratio=$ratio least=$least: $verdict"
}

# The gigabit LAN (no router): the same.
ratio_scenario cc-lan 0.99 --run-s 30 --measure-from-s 2
# Reno-friendly region (RFC 9438 section 5.2: below about 56 ms at 1e-4): 100 Mbps, 20 ms, queue of one BDP, 0.01 %.
ratio_scenario cc-20ms-loss 0.97 --delay-us 10000 --bottleneck-mbps 100 --bottleneck-queue 250000 --loss-ab 0.01
# CUBIC's region as the board's path 2 (35 Mbps, 80 ms, queue of one BDP, 0.004 %): recorded, the judgement is p004's.
ratio_scenario cc-80ms-loss - --delay-us 40000 --bottleneck-mbps 35 --bottleneck-queue 350000 --loss-ab 0.004

# Two connections on the 20 ms router, the second 5 s in: Jain's index of their mean rates, and the second's share.
fair_scenario() {
	name=$1
	first=$2
	second_cc=$3
	jain_least=$4
	share_least=$5
	shift 5
	if [ "$only" != "  " ] && ! echo "$only" | grep -q " $name "; then
		return 0
	fi
	for seed in $seeds; do
		echo "$seed"
	done | xargs -P "$jobs" -n 1 sh -c 'timeout 1200 "$0" --cc '"$first"' --second-flow-cc '"$second_cc"' --seed "$1" '"$common $*"' > "'"$runs/$name"'-$1.txt" 2>&1' "$model"
	jain=$(for seed in $seeds; do sed -n 's/^fair: .* jain=\([0-9.]*\) .*/\1/p' "$runs/$name-$seed.txt"; done | sort -n |
	    awk '{ v[NR] = $1 } END { print v[int((NR + 1) / 2)] }')
	share=$(for seed in $seeds; do sed -n 's/^fair: .* second_share=\([0-9.]*\) .*/\1/p' "$runs/$name-$seed.txt"; done | sort -n |
	    awk '{ v[NR] = $1 } END { print v[int((NR + 1) / 2)] }')
	verdict=PASS
	awk -v j="$jain" -v l="$jain_least" -v s="$share" -v m="$share_least" 'BEGIN { exit !(j >= l && s >= m) }' ||
	    { verdict=FAIL; status=1; }
	echo "$name: jain_median=$jain second_share_median=$share least=$jain_least/$share_least: $verdict"
}
fair_scenario fair-cubic-cubic cubic cubic 0.95 0 --delay-us 10000 --bottleneck-mbps 100 --bottleneck-queue 250000 \
    --loss-ab 0.01 --second-flow-at-ms 5000 --fair-skip-s 15
fair_scenario fair-cubic-reno cubic reno 0 0.25 --delay-us 10000 --bottleneck-mbps 100 --bottleneck-queue 250000 \
    --loss-ab 0.01 --second-flow-at-ms 5000 --fair-skip-s 15

[ $status = 0 ] && echo "cc-model: PASS" || echo "cc-model: FAIL"
exit $status
