#!/bin/sh
# Starts the AX211 VFIO runner with the gdbstub waiting (-S) and attaches
# ax211-gdb-trace.py to it, so the trace stream covers the guest from its first
# instruction (BUG-158, q684).  Run on the test host; pipe the output over ssh
# so the stream survives a host hang.
#
#   ax211-vfio-trace-run.sh IMAGE VMUNIX WORK-DIR [RUNNER-ENV...]
#
# RUNNER-ENV are NAME=VALUE pairs for run-intel-ax211-vfio-qemu.sh (for example
# AX211_VFIO_HOST_DRIVER=none AX211_VFIO_SAFE_ROUTE_DEVICE=enx...).  The gdb
# port is AX211_VFIO_GDB_PORT (default 1234).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
[ $# -ge 3 ] || { echo "usage: $0 IMAGE VMUNIX WORK-DIR [NAME=VALUE...]" >&2; exit 2; }
image=$1
vmunix=$2
work_dir=$3
shift 3
here=$(cd "$(dirname -- "$0")" && pwd)
port=${AX211_VFIO_GDB_PORT:-1234}
mkdir -p "$work_dir"
date '+%F %T trace-run: starting QEMU (gdbstub waits)'
sudo env "$@" AX211_VFIO_GDB_PORT="$port" AX211_VFIO_GDB_WAIT=1 \
	sh "$here/run-intel-ax211-vfio-qemu.sh" "$image" "$work_dir/qemu" \
	> "$work_dir/runner.log" 2>&1 &
runner=$!
tries=0
while ! ss -ltn 2>/dev/null | grep -q ":$port "; do
	sleep 1
	tries=$((tries + 1))
	if [ $tries -gt 60 ] || ! kill -0 $runner 2>/dev/null; then
		echo "trace-run: gdbstub did not appear; runner log:" >&2
		cat "$work_dir/runner.log" >&2
		exit 1
	fi
done
date '+%F %T trace-run: gdbstub up, attaching gdb'
gdb -batch -ex 'set pagination off' -ex 'set confirm off' \
	-ex "target remote 127.0.0.1:$port" \
	-x "$here/ax211-gdb-trace.py" "$vmunix" 2>&1 | tee "$work_dir/trace.log"
date '+%F %T trace-run: gdb ended'
wait $runner || true
cat "$work_dir/runner.log"
