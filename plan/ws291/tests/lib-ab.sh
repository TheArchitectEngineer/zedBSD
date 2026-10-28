#!/bin/sh
# ws291: the same bench runs and submission profiles with each of several builds of libvulkan (and a kernel) on the
# RDNA machine, in one command: for each LABEL=LIBVULKAN[,VMUNIX] the library (and the kernel) is put in (zedBSD
# restarted), the runs of LIST are benched (timing off) and the profiles of PROFILE_RUNS (libvulkan's submission
# timing) taken; printed: each label's table and the profiles' timing lines.  Long logs stay in OUT/LABEL/.
#
#     plan/ws291/tests/lib-ab.sh OUT LIST LABEL=LIBVULKAN[,VMUNIX]...
#
# PROFILE_RUNS: mview options of the profiled runs, one a line (default: subs-1000 and big-1000).
# Run from the top of the tree; every step has the time limits of measure.sh.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=$1
list=$2
shift 2
m=plan/tools/gpu-perf/measure.sh
runs=${PROFILE_RUNS:-"--draws=each --copies=1000 --submits=1000
--draws=each --copies=1000"}
mkdir -p "$out"
for pair in "$@"; do
	label=${pair%%=*}
	files=${pair#*=}
	lib=${files%%,*}
	kernel=
	[ "$lib" != "$files" ] && kernel=${files#*,}
	dir=$out/$label
	mkdir -p "$dir"
	if [ -n "$kernel" ]; then
		timeout 900 sh $m deploy --kernel "$kernel" "/lib/libvulkan.so=$lib" > "$dir/deploy.out" 2>&1
	else
		timeout 900 sh $m deploy "/lib/libvulkan.so=$lib" > "$dir/deploy.out" 2>&1
	fi
	echo "== $label deploy rc=$? ($(tail -1 "$dir/deploy.out"))"
	timeout 1800 sh $m bench zedbsd "$dir/bench" "$list" > "$dir/bench.out" 2>&1
	echo "== $label bench rc=$?"
	cat "$dir/bench/table.txt" 2>/dev/null
	n=0
	printf '%s\n' "$runs" | while read -r opts; do
		n=$((n + 1))
		timeout 300 sh $m profile "$dir/prof$n" --spin=8 --content=game --present=mailbox --images=4 $opts \
			> "$dir/prof$n.out" 2>&1
		echo "== $label profile $n ($opts) rc=$?"
		grep -E 'MVIEW (SPIN|FAILED)|VULKAN TIMING' "$dir/prof$n.out" | tail -2
	done
done
