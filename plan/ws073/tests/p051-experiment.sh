#!/bin/sh
# ws073-p051 (BUG-135): measures the stalls of stat() in the guest under a host disk load, as ws073-p045 did:
# two writers (fsync, replace) and a nap probe run beside a stat probe on a cached file (fsprobe.c), while the
# host writes and fdatasyncs 1 GiB files in a loop on the disk that holds the image.  With VMUNIX_DWARF (a
# kernel built with ZEDBSD_KERNEL_LTO_CFLAGS="-g -fno-omit-frame-pointer", the one in IMAGE), the UFS mount's
# held locks and their owners' stacks are sampled through the gdbstub every half second (p045-locks.py).
#   sh plan/ws073/tests/p051-experiment.sh IMAGE OUTDIR FSPROBE [VMUNIX_DWARF]
# env: RUN_SECONDS (default 45), GUEST_RUNTIME (default build/p9-run), LOAD_DIR (default OUTDIR), LOAD_JOBS (how
# many dd loops load the host's disk at once, default 1), CHURN=1 (a loop of renames and a second fsync writer
# beside the probes: a rename holds the namespace while it waits for the mount lock, which is the chain p051
# cuts, so the stalls it causes come often enough to count).
# Prints the FSPROBE summary of each probe and the stat operations over one second.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
out=$2
fsprobe=$3
vmunix=${4:-}
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
export GUEST_RUNTIME=${GUEST_RUNTIME:-build/p9-run}
seconds=${RUN_SECONDS:-45}
load_dir=${LOAD_DIR:-$out}
guest=plan/tools/guest/guest.py
mkdir -p "$out" "$load_dir"
count=$((seconds * 100))

# Boots the guest and gives it the probe and the files it works on.
python3 $guest stop > /dev/null 2>&1
python3 $guest start --disk nvme "$image" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH"; exit 1; }
python3 $guest put "$fsprobe" /root/fsprobe > /dev/null || exit 1
python3 $guest run 'chmod 755 /root/fsprobe && mkdir -p /root/w && echo x > /root/desktop.conf && sync && rm -f /root/*.out' || exit 1

# Loads the host's disk: 1 GiB written and fdatasynced, again and again, by LOAD_JOBS loops.
load_pids=
job=0
while [ "$job" -lt "${LOAD_JOBS:-1}" ]; do
	(while :; do dd if=/dev/zero of="$load_dir/load$job.bin" bs=1M count=1000 conv=fdatasync 2> /dev/null; done) &
	load_pids="$load_pids $!"
	job=$((job + 1))
done

# Starts the probes in the guest, each writing its own record.
python3 $guest run "(nohup /root/fsprobe fsync /root/w 400 10 > /root/fsync.out 2>&1 < /dev/null &);
	(nohup /root/fsprobe replace /root/desktop.conf 300 10 > /root/replace.out 2>&1 < /dev/null &);
	(nohup /root/fsprobe stat /root/desktop.conf $count 10 > /root/stat.out 2>&1 < /dev/null &);
	(nohup /root/fsprobe nap - $count 10 > /root/nap.out 2>&1 < /dev/null &); echo started"
if [ "${CHURN:-0}" = "1" ]; then
	python3 $guest run "mkdir -p /root/w2; echo r > /root/r1; (nohup sh -c 'while :; do mv /root/r1 /root/r2; mv /root/r2 /root/r1; done' > /dev/null 2>&1 < /dev/null &);
		(nohup /root/fsprobe fsync /root/w2 400 10 > /root/fsync2.out 2>&1 < /dev/null &); echo churn started"
fi

# Samples the mount's locks through the gdbstub while the probes run, when a DWARF kernel is named.
started=$(date +%s)
if [ -n "$vmunix" ]; then
	port=$(python3 -c "import json; print(json.load(open('$GUEST_RUNTIME/session.json'))['debug_port'])")
	: > "$out/locks.txt"
	while [ $(( $(date +%s) - started )) -lt "$seconds" ]; do
		echo "== t=$(( $(date +%s) - started ))" >> "$out/locks.txt"
		timeout 20 gdb -q -batch -ex "set \$port=$port" -x plan/ws073/tests/p045-locks.py "$vmunix" >> "$out/locks.txt" 2>&1
		sleep 0.5
	done
fi

# Waits for the stat and nap probes to finish (the writers are slower under the load and are cut short).
deadline=$(( started + seconds * 3 + 60 ))
while [ "$(date +%s)" -lt "$deadline" ]; do
	done_count=$(python3 $guest run 'cat /root/stat.out /root/nap.out 2>/dev/null | grep -c "FSPROBE op="' 2>/dev/null | tr -d '\r')
	[ "${done_count:-0}" = "2" ] && break
	sleep 3
done
kill $load_pids 2> /dev/null
wait $load_pids 2> /dev/null
rm -f "$load_dir"/load*.bin

# Collects the records and stops the guest.
for name in stat nap fsync replace; do
	python3 $guest get "/root/$name.out" "$out/$name.out" > /dev/null 2>&1
done
python3 $guest stop > /dev/null

# Reports.
for name in stat nap fsync replace; do
	echo "== $name"
	grep "FSPROBE op=" "$out/$name.out" 2> /dev/null || echo "(no summary: $(wc -l < "$out/$name.out" 2>/dev/null || echo 0) lines, slow: $(grep -c 'FSPROBE slow' "$out/$name.out" 2>/dev/null))"
done
echo "== stat over 1 s"
grep "FSPROBE slow" "$out/stat.out" 2> /dev/null | awk '{ split($NF, took, "="); if (took[2] + 0 >= 1000000) print }' | head -40
